#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <gd32h7xx_rcu.h>
#include <gd32h7xx_exmc.h>

// 设备树节点
#define SDRAM_NODE DT_NODELABEL(sdram)
#define EXMC_NODE DT_NODELABEL(exmc)
// 设备树相关属性
#define SDRAM_BASE DT_REG_ADDR(SDRAM_NODE)
#define SDRAM_SIZE DT_REG_SIZE(SDRAM_NODE)
// 控制参数相关
#define CTL(idx) DT_PROP_BY_IDX(EXMC_NODE, exmc_sdram_control, idx) // 数组属性中按索引获取属性数值
// 时序参数相关
#define TIM(idx) DT_PROP_BY_IDX(EXMC_NODE, exmc_sdram_timing, idx) // 设备树里存的是真实周期数 库函数内部会完成(值-1)再左移的编码
// 初始化参数相关
#define POWER_UP_DELAY_US DT_PROP(EXMC_NODE, power_up_delay_us)
#define NUM_AUTO_REFRESH DT_PROP(EXMC_NODE, num_auto_refresh)
#define MODE_REGISTER DT_PROP(EXMC_NODE, mode_register)
#define REFRESH_RATE DT_PROP(EXMC_NODE, refresh_rate)

// sdram读写测试 成功返回0
int sdram_test(void)
{
    volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;
    // 第1层 冒烟
    p[0] = 0x12345678U;
    if (p[0] != 0x12345678U)
        return -1;
    // 第2层 数据总线 1/0交替
    p[0] = 0xAAAAAAAAU;
    if (p[0] != 0xAAAAAAAAU)
        return -2;
    p[0] = 0x55555555U;
    if (p[0] != 0x55555555U)
        return -3;
    // 第3层 地址线 2的幂偏移各写各的魔数
    for (uint32_t off = 4; off < SDRAM_SIZE; off <<= 1)
    {
        uint32_t *q = (uint32_t *)((uint8_t *)SDRAM_BASE + off);
        *q = 0xC0000000U + off;
    }
    for (uint32_t off = 4; off < SDRAM_SIZE; off <<= 1)
    {
        uint32_t *q = (uint32_t *)((uint8_t *)SDRAM_BASE + off);
        if (*q != 0xC0000000U + off)
            return -(int)(off);
    }

    return 0;
}

void sdram_init(void)
{
    // 使能EXMC时钟 库内部就是操作RCU_AHB3EN寄存器的bit0(EXMCEN)
    rcu_periph_clock_enable(RCU_EXMC);

    // 复位EXMC 库内部就是操作RCU_AHB3RST寄存器的bit0(EXMCRST)
    rcu_periph_reset_enable(RCU_EXMCRST);
    k_busy_wait(10);
    rcu_periph_reset_disable(RCU_EXMCRST);

    // 选择CK_AHB(AHB3总线时钟)作为EXMC的时钟源 库内部就是清除RCU_CFG4寄存器的bit9:bit8
    rcu_exmc_clock_config(RCU_EXMCSRC_AHB);

    // 时序参数 库结构体的每个字段填真实周期数(1~16) 和原来SDTCFG0的(值-1)<<偏移 等价
    exmc_sdram_timing_parameter_struct sdram_timing = {
        .load_mode_register_delay = TIM(0), // LMRD 加载模式寄存器延迟
        .exit_selfrefresh_delay = TIM(1),   // XSRD 退出自刷新延迟
        .row_address_select_delay = TIM(2), // RASD 行地址选择延迟
        .auto_refresh_delay = TIM(3),       // ARFD 自动刷新延迟
        .write_recovery_delay = TIM(4),     // WRD 写恢复延迟
        .row_precharge_delay = TIM(5),      // RPD 行预充电延迟
        .row_to_column_delay = TIM(6),      // RCD 行到列延迟
    };

    // 控制参数 设备树里存的是SDCTL寄存器的编码值 和库的宏编码完全一致(CAW_9=0x1 RAW_13=0x8...) 大部分字段直接填
    // 只有WPEN和BRSTRD两个例外 设备树里直接写0/1 库内部会自己左移到bit9/bit12
    exmc_sdram_parameter_struct sdram_param = {
        .sdram_device = EXMC_SDRAM_DEVICE0, // 使用SDRAM设备0
        .column_address_width = CTL(0),     // GD32_EXMC_CAW_9 列地址9位
        .row_address_width = CTL(1),        // GD32_EXMC_RAW_13 行地址13位
        .data_width = CTL(2),               // GD32_EXMC_SDW_16 16位数据总线
        .internal_bank_number = CTL(3),     // GD32_EXMC_NBK_4 4个内部bank
        .cas_latency = CTL(4),              // GD32_EXMC_CL_3 CAS延迟3个时钟
        .write_protection = CTL(5),         // 设备树直接写0/1 库内部移位到bit9
        .sdclock_config = CTL(6),           // GD32_EXMC_SDCLK_DIV3 CK_EXMC的300MHz经过三分频 得到100MHz时钟频率供给SDRAM芯片
        .burst_read_switch = CTL(7),        // 设备树直接写0/1 库内部移位到bit12
        .pipeline_read_delay = CTL(8),      // GD32_EXMC_RPIPE_0 流水线读延迟0
        .timing = &sdram_timing,
    };
    exmc_sdram_init(&sdram_param); // 库内部一次性完成EXMC_SDCTL0和EXMC_SDTCFG0的写入

    // 命令参数 SDRAM的命令都是通过写EXMC_SDCMD寄存器发出的 bank_select相当于原来的BIT(4)(DS0位域)
    exmc_sdram_command_parameter_struct sdram_cmd = {
        .bank_select = EXMC_SDRAM_DEVICE0_SELECT, // 命令发给SDRAM设备0
    };

    // 1.时钟使能命令 先让SDRAM内部的时钟跑起来等它稳定
    sdram_cmd.command = EXMC_SDRAM_CLOCK_ENABLE;
    sdram_cmd.auto_refresh_number = 0;
    sdram_cmd.mode_register_content = 0;
    exmc_sdram_command_config(&sdram_cmd);
    k_busy_wait(POWER_UP_DELAY_US); // k_busy_wait函数是死等延迟 不会调度 参数微秒级别单位

    // 2.预充电命令 关闭所有已经打开的行
    sdram_cmd.command = EXMC_SDRAM_PRECHARGE_ALL;
    exmc_sdram_command_config(&sdram_cmd);

    // 3.自动刷新命令 NARF位域是连续自动刷新的个数-1 8个就是SDCMD_NARF(7) 等价于库的EXMC_SDRAM_AUTO_REFLESH_8_SDCLK
    sdram_cmd.command = EXMC_SDRAM_AUTO_REFRESH;
    sdram_cmd.auto_refresh_number = SDCMD_NARF(NUM_AUTO_REFRESH - 1U);
    exmc_sdram_command_config(&sdram_cmd);

    // 4.加载模式寄存器命令 SDRAM芯片没有专门的配置总线 库内部会把mode_register_content左移9位放到SDCMD的MRC位域
    //   发命令时MRC位域的内容会通过数据线写进SDRAM芯片的模式寄存器 0x30的含义见设备树mode-register的注释
    sdram_cmd.command = EXMC_SDRAM_LOAD_MODE_REGISTER;
    sdram_cmd.mode_register_content = MODE_REGISTER;
    exmc_sdram_command_config(&sdram_cmd);

    // 设置自动刷新间隔 库内部会把REFRESH_RATE左移1位写到EXMC_SDARI的ARINTV位域
    // 刷新错误中断REIE复位后默认是0 不使能 和设备树里的reie=0一致
    exmc_sdram_refresh_count_set(REFRESH_RATE);

    if (sdram_test() == 0)
        printk("sdram_test pass\n");
}

