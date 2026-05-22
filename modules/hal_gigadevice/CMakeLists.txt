# Copyright (c) 2021 ATL-Electronics
# Copyright (c) 2026 GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_HAS_GD32_HAL)

string(TOUPPER ${CONFIG_SOC} gd32_soc_uc)

# GD32M531 uses gd32m53x HAL directory naming (series name differs from HAL name)
if(CONFIG_SOC_SERIES_GD32M53X)
  set(gd32_hal_series gd32m53x)
  set(gd32_soc_dir    ${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/gd32m53x)
else()
  set(gd32_hal_series ${CONFIG_SOC_SERIES})
  set(gd32_soc_dir    ${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/${CONFIG_SOC_SERIES})
endif()

if(CONFIG_ARM)
  set(gd32_soc_sys_dir   ${gd32_soc_dir}/cmsis/gd/${gd32_hal_series})
elseif(CONFIG_RISCV)
  set(gd32_soc_sys_dir   ${gd32_soc_dir}/riscv)
  zephyr_include_directories(${gd32_soc_dir}/riscv/drivers)
endif()

set(gd32_std_dir     ${gd32_soc_dir}/standard_peripheral)
set(gd32_std_src_dir ${gd32_std_dir}/source)

zephyr_library_named(hal_gigadevice)

zephyr_compile_definitions(${gd32_soc_uc})

# The header files of GigaDevice firmware are reference HXTAL_VALUE.
# The HXTAL_VALUE has the possibility of being referenced from any files
# via that header files.
# Therefore, we need to define HXTAL_VALUE for all compilations.
if(${CONFIG_GD32_HXTAL_8MHZ})
  zephyr_compile_definitions(HXTAL_VALUE=8000000 HXTAL_VALUE_25M=8000000)
elseif(${CONFIG_GD32_HXTAL_25MHZ})
  zephyr_compile_definitions(HXTAL_VALUE=25000000 HXTAL_VALUE_8M=25000000)
endif()

if(CONFIG_GD32_HAS_IRC_32K)
  zephyr_compile_definitions(IRC32K_VALUE=${CONFIG_GD32_LOW_SPEED_IRC_FREQUENCY})
endif()

if(CONFIG_GD32_HAS_IRC_40K)
  zephyr_compile_definitions(IRC40K_VALUE=${CONFIG_GD32_LOW_SPEED_IRC_FREQUENCY})
endif()

# GD32E50X series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32E50X})
  zephyr_compile_definitions(GD32E50X)
  if(${CONFIG_SOC_GD32E507})
    zephyr_compile_definitions(GD32E50X_CL)
  endif()
endif()

# GD32A50X series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32A50X})
  zephyr_compile_definitions(GD32A50X)
endif()

# GD32C2X1 series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32C2X1})
  zephyr_compile_definitions(GD32C2X1)
endif()

# GD32F50X series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32F50X})
  zephyr_compile_definitions(GD32F50X)
endif()

# GD32E51X series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32E51X})
  zephyr_compile_definitions(GD32E51X)
  if(${CONFIG_SOC_GD32E517})
    zephyr_compile_definitions(GD32E51X_CL)
  endif()
endif()

# GD32H7XX series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32H7XX})
  zephyr_compile_definitions(GD32H7XX)
endif()

# GD32F30X series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32F30X})
  if(${CONFIG_SOC_GD32F307})
    zephyr_compile_definitions(GD32F30X_CL)
  elseif(${CONFIG_SOC_GD32F303})
    zephyr_compile_definitions(GD32F30X_HD)
  endif()
endif()

# GD32H75E series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32H75E})
  zephyr_compile_definitions(GD32H75E)
endif()

# GD32C11X series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32C11X})
  zephyr_compile_definitions(GD32C11X)
endif()

# GD32M531 series HAL public headers require extra definitions
if(${CONFIG_SOC_SERIES_GD32M53X})
  zephyr_compile_definitions(GD32M53X)
endif()

# Global includes to be used outside hal_gigadevice
zephyr_include_directories(${gd32_soc_sys_dir}/include)
zephyr_include_directories(${gd32_std_dir}/include)
zephyr_include_directories(${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/include)
zephyr_include_directories(${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/common_include)

zephyr_library_sources(${gd32_soc_sys_dir}/source/system_${gd32_hal_series}.c)

zephyr_library_sources_ifdef(CONFIG_USE_GD32_ADC      ${gd32_std_src_dir}/${gd32_hal_series}_adc.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_BKP      ${gd32_std_src_dir}/${gd32_hal_series}_bkp.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_CAN      ${gd32_std_src_dir}/${gd32_hal_series}_can.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_CMP      ${gd32_std_src_dir}/${gd32_hal_series}_cmp.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_CEC      ${gd32_std_src_dir}/${gd32_hal_series}_cec.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_CRC      ${gd32_std_src_dir}/${gd32_hal_series}_crc.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_CTC      ${gd32_std_src_dir}/${gd32_hal_series}_ctc.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_DAC      ${gd32_std_src_dir}/${gd32_hal_series}_dac.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_DBG      ${gd32_std_src_dir}/${gd32_hal_series}_dbg.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_DCI      ${gd32_std_src_dir}/${gd32_hal_series}_dci.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_DMA      ${gd32_std_src_dir}/${gd32_hal_series}_dma.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_ENET     ${gd32_std_src_dir}/${gd32_hal_series}_enet.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_EXMC     ${gd32_std_src_dir}/${gd32_hal_series}_exmc.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_EXTI     ${gd32_std_src_dir}/${gd32_hal_series}_exti.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_FMC      ${gd32_std_src_dir}/${gd32_hal_series}_fmc.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_FWDGT    ${gd32_std_src_dir}/${gd32_hal_series}_fwdgt.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_GPIO     ${gd32_std_src_dir}/${gd32_hal_series}_gpio.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_I2C_V1   ${gd32_std_src_dir}/${gd32_hal_series}_i2c.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_I2C_V2   ${gd32_std_src_dir}/${gd32_hal_series}_i2c.c)
# GD32E51X has ADD IP functions in i2c.c, no separate i2c_add.c
if(NOT CONFIG_SOC_SERIES_GD32E51X AND NOT CONFIG_SOC_SERIES_GD32M53X)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_I2C_V3   ${gd32_std_src_dir}/${gd32_hal_series}_i2c_add.c)
endif()
zephyr_library_sources_ifdef(CONFIG_USE_GD32_I2C_V3   ${gd32_std_src_dir}/${gd32_hal_series}_i2c.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_IPA      ${gd32_std_src_dir}/${gd32_hal_series}_ipa.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_IREF     ${gd32_std_src_dir}/${gd32_hal_series}_iref.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_MISC     ${gd32_std_src_dir}/${gd32_hal_series}_misc.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_PMU      ${gd32_std_src_dir}/${gd32_hal_series}_pmu.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_RCU      ${gd32_std_src_dir}/${gd32_hal_series}_rcu.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_RSPDIF   ${gd32_std_src_dir}/${gd32_hal_series}_rspdif.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_RTC      ${gd32_std_src_dir}/${gd32_hal_series}_rtc.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_RTDEC    ${gd32_std_src_dir}/${gd32_hal_series}_rtdec.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_SDIO     ${gd32_std_src_dir}/${gd32_hal_series}_sdio.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_SHRTIMER ${gd32_std_src_dir}/${gd32_hal_series}_shrtimer.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_SPI      ${gd32_std_src_dir}/${gd32_hal_series}_spi.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_SYSCFG   ${gd32_std_src_dir}/${gd32_hal_series}_syscfg.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_TIMER    ${gd32_std_src_dir}/${gd32_hal_series}_timer.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_TLI      ${gd32_std_src_dir}/${gd32_hal_series}_tli.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_TMU      ${gd32_std_src_dir}/${gd32_hal_series}_tmu.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_TRIGSEL  ${gd32_std_src_dir}/${gd32_hal_series}_trigsel.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_TRNG     ${gd32_std_src_dir}/${gd32_hal_series}_trng.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_TSI      ${gd32_std_src_dir}/${gd32_hal_series}_tsi.c)
# GD32M531 uses UART instead of USART
if(NOT CONFIG_SOC_SERIES_GD32M53X)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_USART    ${gd32_std_src_dir}/${gd32_hal_series}_usart.c)
endif()
zephyr_library_sources_ifdef(CONFIG_USE_GD32_WWDGT    ${gd32_std_src_dir}/${gd32_hal_series}_wwdgt.c)

# For GD32F527
zephyr_library_sources_ifdef(CONFIG_USE_GD32_HAU       ${gd32_std_src_dir}/${gd32_hal_series}_hau.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_PKCAU     ${gd32_std_src_dir}/${gd32_hal_series}_pkcau.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_SAI       ${gd32_std_src_dir}/${gd32_hal_series}_sai.c)

zephyr_library_sources_ifdef(CONFIG_USE_GD32_CAU       ${gd32_std_src_dir}/${gd32_hal_series}_cau.c)
zephyr_library_sources_ifdef(CONFIG_USE_GD32_HAU       ${gd32_std_src_dir}/${gd32_hal_series}_hau.c)

# For GD32M531 - UART (not USART) and motor control peripherals
if(CONFIG_SOC_SERIES_GD32M53X)
  zephyr_library_sources_ifdef(CONFIG_USE_GD32_USART    ${gd32_std_src_dir}/${gd32_hal_series}_uart.c)
  zephyr_library_sources_ifdef(CONFIG_USE_GD32_GPTIMER  ${gd32_std_src_dir}/${gd32_hal_series}_gptimer.c)
  zephyr_library_sources_ifdef(CONFIG_USE_GD32_CPTIMER  ${gd32_std_src_dir}/${gd32_hal_series}_cptimer.c)
  zephyr_library_sources_ifdef(CONFIG_USE_GD32_CPTIMERW ${gd32_std_src_dir}/${gd32_hal_series}_cptimerw.c)
  zephyr_library_sources_ifdef(CONFIG_USE_GD32_SVPWM    ${gd32_std_src_dir}/${gd32_hal_series}_svpwm.c)
  zephyr_library_sources_ifdef(CONFIG_USE_GD32_POC      ${gd32_std_src_dir}/${gd32_hal_series}_poc.c)
  zephyr_library_sources_ifdef(CONFIG_USE_GD32_GTOC     ${gd32_std_src_dir}/${gd32_hal_series}_gtoc.c)
  zephyr_library_sources_ifdef(CONFIG_USE_GD32_EVIC     ${gd32_std_src_dir}/${gd32_hal_series}_evic.c)
  zephyr_library_sources_ifdef(CONFIG_USE_GD32_CFMU     ${gd32_std_src_dir}/${gd32_hal_series}_cfmu.c)
endif()

endif()
