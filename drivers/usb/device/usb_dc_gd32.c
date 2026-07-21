/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief USB device controller shim driver for GD32 USBHS (legacy USB stack)
 *
 * This driver wraps the GigaDevice GD32H73x_75x USBHS firmware library
 * (provided by the hal_gigadevice module under
 * gd32h7xx/usbhs_library/, built via CONFIG_USE_GD32_USBHS) so that the legacy
 * Zephyr USB device stack can drive a USBHS controller. The controller
 * (USBHS0/USBHS1) and the speed (full-/high-speed) are selected in the
 * devicetree: enable exactly one gd,gd32-usbhs node and set its maximum-speed.
 * This driver derives all hardware constants (base address, IRQ, clock index
 * and RCU gate) from that node. The on-chip PHY type additionally follows the
 * OC_FS_PHY/OC_HS_PHY selection in usbhs_library/config/usb_conf.h, which must
 * match the node's maximum-speed. The GD32 low
 * level interrupt handler (usbhs_library/driver/Source/drv_usbd_int.c) has
 * been adapted to route USB events to the zephyr_usbd_* callbacks defined at
 * the bottom of this file.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/reset.h>

#include "usb_conf.h"
#include "usbd_core.h"
#include "drv_usb_hw.h"
#include "drv_usbd_int.h"

#define LOG_LEVEL CONFIG_USB_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
LOG_MODULE_REGISTER(usb_dc_gd32);

#define DT_DRV_COMPAT gd_gd32_usbhs

#if DT_NUM_INST_STATUS_OKAY(gd_gd32_usbhs) != 1
#error "Enable exactly one gd,gd32-usbhs node (usbhs0 or usbhs1)"
#endif

#define GD32_USB_NODE           DT_COMPAT_GET_ANY_STATUS_OKAY(gd_gd32_usbhs)
/*
 * The GD32 library peripheral handle equals the register base address
 * (USBHS0 == 0x40040000, USBHS1 == 0x40080000), so it is taken from reg.
 */
#define GD32_USB_PERIPH         DT_REG_ADDR(GD32_USB_NODE)
#define GD32_USB_IRQN           DT_IRQN(GD32_USB_NODE)
#define GD32_USB_IRQ_PRI        DT_IRQ(GD32_USB_NODE, priority)
#define USB_NUM_BIDIR_ENDPOINTS DT_PROP(GD32_USB_NODE, num_bidir_endpoints)

#if (DT_PROP(GD32_USB_NODE, gd_usbhs_index) == 0)
#define GD32_USB_IDX IDX_USBHS0
#define GD32_USB_RCU RCU_USBHS0
#else
#define GD32_USB_IDX IDX_USBHS1
#define GD32_USB_RCU RCU_USBHS1
#endif

#if DT_ENUM_HAS_VALUE(GD32_USB_NODE, maximum_speed, high_speed)
#define GD32_USB_SPEED  USB_SPEED_HIGH
#define GD32_USB_USE_HS 1
#else
#define GD32_USB_SPEED  USB_SPEED_FULL
#define GD32_USB_USE_HS 0
#endif

#define EP0_MPS 64
#define EP_MPS  64

/* Bounded busy-wait budget (us) for clock/PLL/power readiness polling. */
#define GD32_USB_CLK_READY_TIMEOUT_US  10000U

/* Size of a USB SETUP packet */
#define SETUP_SIZE 8

/* Helper macros to make it easier to work with endpoint numbers */
#define USB_EP0_IDX 0
#define USB_EP0_IN  (USB_EP0_IDX | USB_EP_DIR_IN)
#define USB_EP0_OUT (USB_EP0_IDX | USB_EP_DIR_OUT)

/* Endpoint state */
struct usb_dc_gd32_ep_state {
	uint16_t ep_mps;         /** Endpoint max packet size */
	uint16_t ep_pma_buf_len; /** Previously allocated buffer size */
	uint8_t ep_type;         /** Endpoint type (GD32 HAL enum) */
	uint8_t ep_stalled;      /** Endpoint stall flag */
	usb_dc_ep_callback cb;   /** Endpoint callback function */
	uint32_t read_count;     /** Number of bytes in read buffer  */
	uint32_t read_offset;    /** Current offset in read buffer */
	struct k_sem write_sem;  /** Write boolean semaphore */
};

/* Driver state */
struct usb_dc_gd32_state {
	usb_core_driver udev;
	usb_dc_status_callback status_cb; /* Status callback */
	struct usb_dc_gd32_ep_state out_ep_state[USB_NUM_BIDIR_ENDPOINTS];
	struct usb_dc_gd32_ep_state in_ep_state[USB_NUM_BIDIR_ENDPOINTS];
	uint8_t ep_buf[USB_NUM_BIDIR_ENDPOINTS][EP_MPS];
};

static struct usb_dc_gd32_state usb_dc_state_s;

/*
 * The vendored GD32 USB library uses usb_udelay()/usb_mdelay() for timing.
 * On bare-metal these come from drv_usb_hw.c (a SysTick/timer helper); here we
 * provide Zephyr implementations based on the kernel busy-wait.
 */
void usb_udelay(const uint32_t usec)
{
	k_busy_wait(usec);
}

void usb_mdelay(const uint32_t msec)
{
	k_busy_wait(msec * 1000U);
}

/* Internal functions */

static struct usb_dc_gd32_ep_state *usb_dc_gd32_get_ep_state(uint8_t ep)
{
	struct usb_dc_gd32_ep_state *ep_state_base;

	if (USB_EP_GET_IDX(ep) >= USB_NUM_BIDIR_ENDPOINTS) {
		return NULL;
	}

	if (USB_EP_DIR_IS_OUT(ep)) {
		ep_state_base = usb_dc_state_s.out_ep_state;
	} else {
		ep_state_base = usb_dc_state_s.in_ep_state;
	}

	return ep_state_base + USB_EP_GET_IDX(ep);
}

static void usb_dc_gd32_isr(const void *arg)
{
	ARG_UNUSED(arg);
	usbd_isr(&usb_dc_state_s.udev);
}

#if GD32_USB_USE_HS
static int pllusb_gd32_rcu_config(uint32_t usb_periph)
{
	uint32_t rem;

	if (USBHS0 == usb_periph) {
		rcu_pllusb0_config(RCU_PLLUSBHSPRE_HXTAL, RCU_PLLUSBHSPRE_DIV5, RCU_PLLUSBHS_MUL96,
				   RCU_USBHS_DIV8);
		RCU_ADDCTL1 |= RCU_ADDCTL1_PLLUSBHS0EN;
		rem = GD32_USB_CLK_READY_TIMEOUT_US / 10U;
		while (0U == (RCU_ADDCTL1 & RCU_ADDCTL1_PLLUSBHS0STB) && rem) {
			rem--;
			k_busy_wait(10);
		}
		if (0U == (RCU_ADDCTL1 & RCU_ADDCTL1_PLLUSBHS0STB)) {
			LOG_ERR("PLLUSBHS0 not stable within %u us",
				GD32_USB_CLK_READY_TIMEOUT_US);
			return -ETIMEDOUT;
		}
		rcu_usb48m_clock_config(IDX_USBHS0, RCU_USB48MSRC_PLLUSBHS);
	} else {
		rcu_pllusb1_config(RCU_PLLUSBHSPRE_HXTAL, RCU_PLLUSBHSPRE_DIV5, RCU_PLLUSBHS_MUL96,
				   RCU_USBHS_DIV8);
		RCU_ADDCTL1 |= RCU_ADDCTL1_PLLUSBHS1EN;
		rem = GD32_USB_CLK_READY_TIMEOUT_US / 10U;
		while (0U == (RCU_ADDCTL1 & RCU_ADDCTL1_PLLUSBHS1STB) && rem) {
			rem--;
			k_busy_wait(10);
		}
		if (0U == (RCU_ADDCTL1 & RCU_ADDCTL1_PLLUSBHS1STB)) {
			LOG_ERR("PLLUSBHS1 not stable within %u us",
				GD32_USB_CLK_READY_TIMEOUT_US);
			return -ETIMEDOUT;
		}
		rcu_usb48m_clock_config(IDX_USBHS1, RCU_USB48MSRC_PLLUSBHS);
	}

	return 0;
}
#endif /* GD32_USB_USE_HS */

static int usb_dc_gd32_clock_enable(void)
{
	uint32_t rem;

	pmu_usb_regulator_enable();
	pmu_usb_voltage_detector_enable();
	rem = GD32_USB_CLK_READY_TIMEOUT_US / 10U;
	while (SET != pmu_flag_get(PMU_FLAG_USB33RF) && rem) {
		rem--;
		k_busy_wait(10);
	}
	if (SET != pmu_flag_get(PMU_FLAG_USB33RF)) {
		LOG_ERR("USB 3.3V regulator not ready within %u us",
			GD32_USB_CLK_READY_TIMEOUT_US);
		return -ETIMEDOUT;
	}

#ifndef USE_IRC48M
	/* configure the pll1 input and output clock range */
	rcu_pll_input_output_clock_range_config(IDX_PLL1, RCU_PLL1RNG_4M_8M, RCU_PLL1VCO_192M_836M);

	rcu_pll1_config(5, 96, 2, 10, 2);

	/* enable PLL1Q clock output */
	rcu_pll_clock_output_enable(RCU_PLL1Q);
	rcu_osci_on(RCU_PLL1_CK);
	rcu_usbhs_pll1qpsc_config(GD32_USB_IDX, RCU_USBHSPSC_DIV1);
	rcu_usb48m_clock_config(GD32_USB_IDX, RCU_USB48MSRC_PLL1Q);
#else
	/* enable IRC48M clock */
	rcu_osci_on(RCU_IRC48M);

	/* wait till IRC48M is ready */
	rem = GD32_USB_CLK_READY_TIMEOUT_US / 10U;
	while (SUCCESS != rcu_osci_stab_wait(RCU_IRC48M) && rem) {
		rem--;
		k_busy_wait(10);
	}
	if (SUCCESS != rcu_osci_stab_wait(RCU_IRC48M)) {
		LOG_ERR("IRC48M not stable within %u us", GD32_USB_CLK_READY_TIMEOUT_US);
		return -ETIMEDOUT;
	}
	rcu_usb48m_clock_config(GD32_USB_IDX, RCU_USB48MSRC_IRC48M);
#endif /* USE_IRC48M */

	/* Enable the USBHS AHB peripheral clock (register access). */
	rcu_periph_clock_enable(GD32_USB_RCU);

	return 0;
}

static int usb_dc_gd32_clock_disable(void)
{
	rcu_periph_clock_disable(GD32_USB_RCU);

	return 0;
}

static int usb_dc_gd32_init(void)
{
	unsigned int i;
	usb_core_driver *udev;
	int ret;

	udev = &usb_dc_state_s.udev;

	/* USBHS0/1 with the on-chip full- or high-speed PHY */
	usb_para_init(udev, GD32_USB_PERIPH, GD32_USB_SPEED);

	/* configure USB capabilities */
	(void)usb_basic_init(&udev->bp, &udev->regs);

	/* disable global interrupts */
	usb_globalint_disable(&udev->regs);

	/* initializes the USB core */
	(void)usb_core_init(udev->bp, &udev->regs);

	/* set device disconnect */
	usbd_disconnect(udev);

#ifndef USE_OTG_MODE
	usb_curmode_set(&udev->regs, DEVICE_MODE);
#endif /* USE_OTG_MODE */

	/* initializes device mode */
	(void)usb_devcore_init(udev);

	/* enable global interrupts */
	usb_globalint_enable(&udev->regs);

	/* set device connect */
	usbd_connect(udev);

	usb_dc_state_s.out_ep_state[USB_EP0_IDX].ep_mps = EP0_MPS;
	usb_dc_state_s.out_ep_state[USB_EP0_IDX].ep_type = USB_DC_EP_CONTROL;
	usb_dc_state_s.in_ep_state[USB_EP0_IDX].ep_mps = EP0_MPS;
	usb_dc_state_s.in_ep_state[USB_EP0_IDX].ep_type = USB_DC_EP_CONTROL;

	for (i = 0U; i < USB_NUM_BIDIR_ENDPOINTS; i++) {
		k_sem_init(&usb_dc_state_s.in_ep_state[i].write_sem, 1, 1);
	}

#if GD32_USB_USE_HS
	ret = pllusb_gd32_rcu_config(GD32_USB_PERIPH);
	if (ret) {
		return ret;
	}
#endif /* GD32_USB_USE_HS */

	IRQ_CONNECT(GD32_USB_IRQN, GD32_USB_IRQ_PRI, usb_dc_gd32_isr, 0, 0);
	irq_enable(GD32_USB_IRQN);

	return 0;
}

/* Zephyr USB device controller API implementation */

int usb_dc_attach(void)
{
	int ret;

	ret = usb_dc_gd32_clock_enable();
	if (ret) {
		return ret;
	}

	ret = usb_dc_gd32_init();
	if (ret) {
		return ret;
	}

	return 0;
}

int usb_dc_ep_set_callback(const uint8_t ep, const usb_dc_ep_callback cb)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	LOG_DBG("ep 0x%02x", ep);

	if (!ep_state) {
		return -EINVAL;
	}

	ep_state->cb = cb;

	return 0;
}

void usb_dc_set_status_callback(const usb_dc_status_callback cb)
{
	LOG_DBG("");

	usb_dc_state_s.status_cb = cb;
}

int usb_dc_set_address(const uint8_t addr)
{
	LOG_DBG("addr %u (0x%02x)", addr, addr);

	usbd_addr_set(&usb_dc_state_s.udev, addr);

	return 0;
}

int usb_dc_ep_start_read(uint8_t ep, uint8_t *data, uint32_t max_data_len)
{
	LOG_DBG("ep 0x%02x, len %u", ep, max_data_len);

	if (!USB_EP_DIR_IS_OUT(ep) && (ep != USB_EP0_IN || max_data_len)) {
		LOG_ERR("invalid ep 0x%02x", ep);
		return -EINVAL;
	}

	if (max_data_len > EP_MPS) {
		max_data_len = EP_MPS;
	}

	usbd_ep_recev(&usb_dc_state_s.udev, ep, usb_dc_state_s.ep_buf[USB_EP_GET_IDX(ep)],
		      max_data_len);

	return 0;
}

int usb_dc_ep_get_read_count(uint8_t ep, uint32_t *read_bytes)
{
	if (!USB_EP_DIR_IS_OUT(ep) || !read_bytes) {
		LOG_ERR("invalid ep 0x%02x", ep);
		return -EINVAL;
	}

	*read_bytes = usbd_rxcount_get(&usb_dc_state_s.udev, USB_EP_GET_IDX(ep));

	return 0;
}

int usb_dc_ep_check_cap(const struct usb_dc_ep_cfg_data *const cfg)
{
	uint8_t ep_idx = USB_EP_GET_IDX(cfg->ep_addr);

	LOG_DBG("ep %x, mps %d, type %d", cfg->ep_addr, cfg->ep_mps, cfg->ep_type);

	if ((cfg->ep_type == USB_DC_EP_CONTROL) && ep_idx) {
		LOG_ERR("invalid endpoint configuration");
		return -1;
	}

	if (ep_idx > (USB_NUM_BIDIR_ENDPOINTS - 1)) {
		LOG_ERR("endpoint index/address out of range");
		return -1;
	}

	return 0;
}

int usb_dc_ep_configure(const struct usb_dc_ep_cfg_data *const ep_cfg)
{
	uint8_t ep = ep_cfg->ep_addr;
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	if (!ep_state) {
		return -EINVAL;
	}

	LOG_DBG("ep 0x%02x, previous ep_mps %u, ep_mps %u, ep_type %u", ep_cfg->ep_addr,
		ep_state->ep_mps, ep_cfg->ep_mps, ep_cfg->ep_type);

	ep_state->ep_mps = ep_cfg->ep_mps;

	switch (ep_cfg->ep_type) {
	case USB_DC_EP_CONTROL:
		ep_state->ep_type = USB_DC_EP_CONTROL;
		break;
	case USB_DC_EP_ISOCHRONOUS:
		ep_state->ep_type = USB_DC_EP_ISOCHRONOUS;
		break;
	case USB_DC_EP_BULK:
		ep_state->ep_type = USB_DC_EP_BULK;
		break;
	case USB_DC_EP_INTERRUPT:
		ep_state->ep_type = USB_DC_EP_INTERRUPT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int usb_dc_ep_set_stall(const uint8_t ep)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	LOG_DBG("ep 0x%02x", ep);

	if (!ep_state) {
		return -EINVAL;
	}

	usbd_ep_stall(&usb_dc_state_s.udev, ep);

	ep_state->ep_stalled = 1U;

	return 0;
}

int usb_dc_ep_clear_stall(const uint8_t ep)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	LOG_DBG("ep 0x%02x", ep);

	if (!ep_state) {
		return -EINVAL;
	}

	usbd_ep_stall_clear(&usb_dc_state_s.udev, ep);

	ep_state->ep_stalled = 0U;
	ep_state->read_count = 0U;

	return 0;
}

int usb_dc_ep_is_stalled(const uint8_t ep, uint8_t *const stalled)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	LOG_DBG("ep 0x%02x", ep);

	if (!ep_state || !stalled) {
		return -EINVAL;
	}

	*stalled = ep_state->ep_stalled;

	return 0;
}

int usb_dc_ep_enable(const uint8_t ep)
{
	usb_desc_ep ep_desc;
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	LOG_DBG("ep 0x%02x", ep);

	if (!ep_state) {
		return -EINVAL;
	}

	ep_desc.bEndpointAddress = ep;
	ep_desc.wMaxPacketSize = ep_state->ep_mps;
	ep_desc.bmAttributes = ep_state->ep_type;

	usbd_ep_setup(&usb_dc_state_s.udev, &ep_desc);

	if (USB_EP_DIR_IS_OUT(ep) && ep != USB_EP0_OUT) {
		return usb_dc_ep_start_read(ep, usb_dc_state_s.ep_buf[USB_EP_GET_IDX(ep)], EP_MPS);
	}

	return 0;
}

int usb_dc_ep_disable(const uint8_t ep)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	LOG_DBG("ep 0x%02x", ep);

	if (!ep_state) {
		return -EINVAL;
	}

	usbd_ep_clear(&usb_dc_state_s.udev, ep);

	return 0;
}

int usb_dc_ep_write(const uint8_t ep, const uint8_t *const data, const uint32_t data_len,
		    uint32_t *const ret_bytes)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);
	uint32_t len = data_len;
	int ret = 0;

	LOG_DBG("ep 0x%02x, len %u", ep, data_len);

	if (!ep_state || !USB_EP_DIR_IS_IN(ep)) {
		LOG_ERR("invalid ep 0x%02x", ep);
		return -EINVAL;
	}

	ret = k_sem_take(&ep_state->write_sem, K_NO_WAIT);
	if (ret) {
		LOG_ERR("Unable to get write lock (%d)", ret);
		return -EAGAIN;
	}

	if (!k_is_in_isr()) {
		irq_disable(GD32_USB_IRQN);
	}

	if (ep == USB_EP0_IN && len > USB_MAX_CTRL_MPS) {
		len = USB_MAX_CTRL_MPS;
	}

	usbd_ep_send(&usb_dc_state_s.udev, ep, (void *)data, len);

	if (ep == USB_EP0_IN && len > 0) {
		/* Wait for an empty package from the host. This also flushes
		 * the TX FIFO to the host.
		 */
		usb_dc_ep_start_read(ep, NULL, 0);
	}

	if (!k_is_in_isr()) {
		irq_enable(GD32_USB_IRQN);
	}

	if (!ret && ret_bytes) {
		*ret_bytes = len;
	}

	return ret;
}

int usb_dc_ep_read_wait(uint8_t ep, uint8_t *data, uint32_t max_data_len, uint32_t *read_bytes)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);
	uint32_t read_count;

	if (!ep_state) {
		LOG_ERR("Invalid Endpoint %x", ep);
		return -EINVAL;
	}

	read_count = ep_state->read_count;

	LOG_DBG("ep 0x%02x, %u bytes, %u+%u, %p", ep, max_data_len, ep_state->read_offset,
		read_count, data);

	if (!USB_EP_DIR_IS_OUT(ep)) {
		LOG_ERR("Wrong endpoint direction: 0x%02x", ep);
		return -EINVAL;
	}

	if (data) {
		read_count = MIN(read_count, max_data_len);
		memcpy(data, usb_dc_state_s.ep_buf[USB_EP_GET_IDX(ep)] + ep_state->read_offset,
		       read_count);
		ep_state->read_count -= read_count;
		ep_state->read_offset += read_count;
	} else if (max_data_len) {
		LOG_ERR("Wrong arguments");
	}

	if (read_bytes) {
		*read_bytes = read_count;
	}

	return 0;
}

int usb_dc_ep_read_continue(uint8_t ep)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	if (!ep_state || !USB_EP_DIR_IS_OUT(ep)) {
		LOG_ERR("Not valid endpoint: %02x", ep);
		return -EINVAL;
	}

	if (!ep_state->read_count) {
		usb_dc_ep_start_read(ep, usb_dc_state_s.ep_buf[USB_EP_GET_IDX(ep)], EP_MPS);
	}

	return 0;
}

int usb_dc_ep_read(const uint8_t ep, uint8_t *const data, const uint32_t max_data_len,
		   uint32_t *const read_bytes)
{
	if (usb_dc_ep_read_wait(ep, data, max_data_len, read_bytes) != 0) {
		return -EINVAL;
	}

	if (usb_dc_ep_read_continue(ep) != 0) {
		return -EINVAL;
	}

	return 0;
}

int usb_dc_ep_halt(const uint8_t ep)
{
	return usb_dc_ep_set_stall(ep);
}

int usb_dc_ep_flush(const uint8_t ep)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	if (!ep_state) {
		return -EINVAL;
	}

	LOG_ERR("Not implemented");

	return 0;
}

int usb_dc_ep_mps(const uint8_t ep)
{
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	if (!ep_state) {
		return -EINVAL;
	}

	return ep_state->ep_mps;
}

int usb_dc_wakeup_request(void)
{
	usb_rwkup_set(&usb_dc_state_s.udev);
	k_sleep(K_MSEC(2));
	usb_rwkup_reset(&usb_dc_state_s.udev);

	return 0;
}

int usb_dc_detach(void)
{
	int ret;

	LOG_DBG("USB DeInit");

	usb_globalint_disable(&usb_dc_state_s.udev.regs);
	usb_dev_stop(&usb_dc_state_s.udev);
	usbd_disconnect(&usb_dc_state_s.udev);

	ret = usb_dc_gd32_clock_disable();
	if (ret) {
		return ret;
	}

	if (irq_is_enabled(GD32_USB_IRQN)) {
		irq_disable(GD32_USB_IRQN);
	}

	return 0;
}

int usb_dc_reset(void)
{
	LOG_ERR("Not implemented");

	return 0;
}

/* Callbacks from the (adapted) GD32 HAL interrupt handler */

void zephyr_usbd_int_reset(usb_core_driver *udev)
{
	int i;

	ARG_UNUSED(udev);

	/* Reset the IN semaphores to prevent perpetual locked state. */
	for (i = 0; i < USB_NUM_BIDIR_ENDPOINTS; i++) {
		k_sem_give(&usb_dc_state_s.in_ep_state[i].write_sem);
	}

	if (usb_dc_state_s.status_cb) {
		usb_dc_state_s.status_cb(USB_DC_RESET, NULL);
	}
}

void zephyr_usbd_int_suspend(usb_core_driver *udev)
{
	ARG_UNUSED(udev);

	if (usb_dc_state_s.status_cb) {
		usb_dc_state_s.status_cb(USB_DC_SUSPEND, NULL);
	}
}

void zephyr_usbd_int_wakeup(usb_core_driver *udev)
{
	ARG_UNUSED(udev);

	if (usb_dc_state_s.status_cb) {
		usb_dc_state_s.status_cb(USB_DC_RESUME, NULL);
	}
}

void zephyr_usbd_int_sof(usb_core_driver *udev)
{
	ARG_UNUSED(udev);

	if (usb_dc_state_s.status_cb) {
		usb_dc_state_s.status_cb(USB_DC_SOF, NULL);
	}
}

void zephyr_usbd_setup_transc(usb_core_driver *udev)
{
	usb_req *gd_req = &udev->dev.control.req;

	struct usb_setup_packet setup = {
		.bmRequestType = gd_req->bmRequestType,
		.bRequest = gd_req->bRequest,
		.wIndex = gd_req->wIndex,
		.wLength = gd_req->wLength,
		.wValue = gd_req->wValue,
	};

	struct usb_dc_gd32_ep_state *ep_state;

	ep_state = usb_dc_gd32_get_ep_state(USB_EP0_OUT); /* can't fail for ep0 */
	__ASSERT(ep_state, "No corresponding ep_state for EP0");

	ep_state->read_count = SETUP_SIZE;
	ep_state->read_offset = 0U;
	memcpy(&usb_dc_state_s.ep_buf[USB_EP0_IDX], &udev->dev.control.req, ep_state->read_count);

	if (ep_state->cb) {
		ep_state->cb(USB_EP0_OUT, USB_DC_EP_SETUP);

		if (!(setup.wLength == 0U) && usb_reqtype_is_to_device(&setup)) {
			usb_dc_ep_start_read(USB_EP0_OUT, usb_dc_state_s.ep_buf[USB_EP0_IDX],
					     setup.wLength);
		}
	}
}

void zephyr_usbd_out_transc(usb_core_driver *udev, uint8_t epnum)
{
	uint8_t ep_idx = USB_EP_GET_IDX(epnum);
	uint8_t ep = ep_idx | USB_EP_DIR_OUT;
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	usb_dc_ep_get_read_count(ep, &ep_state->read_count);
	ep_state->read_offset = 0U;

	if (ep_state->cb) {
		ep_state->cb(ep, USB_DC_EP_DATA_OUT);
	}
}

void zephyr_usbd_in_transc(usb_core_driver *udev, uint8_t epnum)
{
	uint8_t ep_idx = USB_EP_GET_IDX(epnum);
	uint8_t ep = ep_idx | USB_EP_DIR_IN;
	struct usb_dc_gd32_ep_state *ep_state = usb_dc_gd32_get_ep_state(ep);

	__ASSERT(ep_state, "No corresponding ep_state for ep");

	k_sem_give(&ep_state->write_sem);

	if (ep_state->cb) {
		ep_state->cb(ep, USB_DC_EP_DATA_IN);
	}
}
