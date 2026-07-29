/**
 * @file port_stub.c
 * @brief 移植层的 **CI 链接验证实现** —— 可交叉编译、可链接、可运行，但不碰任何外设
 *
 * ⚠ 用本文件编出来的固件**不能驱动电机**。原因见 mspm0_conf.h 顶部的长注释。
 *
 * 但它也不是一堆空函数。凡是 ARM Cortex-M 内核自带、与 TI 无关的东西，
 * 这里都是真实实现：
 *
 *   · SysTick (0xE000E010) —— ARMv6-M 内核外设，地址由 ARM 定义，与器件无关
 *   · CPSID/CPSIE 临界区   —— 内核指令
 *   · 1kHz 控制中断        —— 直接由 SysTick 驱动，不需要 TI 的 TIMG
 *
 * 也就是说：**整个控制循环在这个剖面下是真的在跑的**。时基真实、
 * 中断真实、PID 真实、协议状态机真实，只是读不到编码器、也推不动 PWM。
 * 这让它可以在没有硬件时用调试器验证时序与栈占用，而不只是"能编过"。
 *
 * 真实硬件请用 `make PROFILE=driverlib` (见 README §7)。
 */
#include "mspm0_conf.h"

#ifdef MSPM0_PORT_STUB

#include "board_config.h"
#include "mspm0_port.h"

/* --- ARMv6-M 内核外设。这些地址由 ARM 架构定义，任何 Cortex-M0+ 都一样。 --- */
#define SYST_CSR    (*(volatile uint32_t *)0xE000E010uL)
#define SYST_RVR    (*(volatile uint32_t *)0xE000E014uL)
#define SYST_CVR    (*(volatile uint32_t *)0xE000E018uL)

#define SYST_CSR_ENABLE     (1uL << 0)
#define SYST_CSR_TICKINT    (1uL << 1)
#define SYST_CSR_CLKSOURCE  (1uL << 2)   /* 1 = 处理器时钟 */

static volatile uint32_t s_millis;

/* ===================== 系统 ===================== */

void port_system_init(void)
{
    /* 真实硬件上这里要配 SYSOSC → SYSPLL → 80MHz。空实现剖面下 CPU 跑在
       复位默认频率 (SYSOSC 32MHz)，因此下面按 SYSCLK_HZ 算出的 SysTick 重载值
       会让"1ms"实际偏长。这个偏差是刻意保留的 —— 它只影响本剖面，
       而本剖面本来就不该上车。 */
    SYST_RVR = (SYSCLK_HZ / 1000uL) - 1uL;
    SYST_CVR = 0uL;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

uint32_t port_millis(void)
{
    return s_millis;
}

void port_delay_ms(uint32_t ms)
{
    const uint32_t start = s_millis;
    while ((s_millis - start) < ms) {
        /* 忙等 */
    }
}

void port_irq_disable(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

void port_irq_enable(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

/* ===================== 控制定时器 ===================== */

/**
 * 本剖面直接复用 SysTick 当控制定时器：1kHz 恰好等于 1ms 时基，
 * 一个中断同时干两件事，不需要 TI 的 TIMG。
 */
void port_control_timer_init(uint32_t freq_hz)
{
    (void)freq_hz;   /* SysTick 已在 port_system_init() 里按 1kHz 配好 */
}

/** SysTick 中断服务例程 —— 名字与 startup_mspm0g3507.c 的向量表对应 */
void SysTick_Handler(void)
{
    s_millis++;
    port_control_isr_hook();
}

/* ===================== 以下为真正的空实现 ===================== */
/* 每一个都需要 MSPM0G3507 TRM (SLAU846) + SysConfig 生成的引脚配置才能填。
   实现见 port_driverlib.c。 */

void port_motor_init(uint32_t pwm_freq_hz) { (void)pwm_freq_hz; }
void port_motor_set_pwm(int wheel, float duty_abs) { (void)wheel; (void)duty_abs; }
void port_motor_set_direction(int wheel, PortMotorDirection dir) { (void)wheel; (void)dir; }

void port_encoder_init(void) {}

uint16_t port_encoder_read_count(int wheel)
{
    (void)wheel;
    /* 恒返回 0 ⟹ 上层算出的转速恒为 0 ⟹ 一旦给了速度指令，
       堵转检测会在 FAULT_STALL_TIME_MS 后置位 FAULT_STALL。
       这是**刻意**的：把"移植层没接"这件事变成一个上位机看得见的故障，
       而不是让车安静地不动。 */
    return 0u;
}

void port_uart_init(uint32_t baudrate) { (void)baudrate; }
void port_uart_put_byte(uint8_t byte) { (void)byte; }

void port_adc_init(void) {}
uint16_t port_adc_read(uint8_t channel) { (void)channel; return 0u; }

void port_gpio_init(void) {}

bool port_estop_asserted(void)
{
    /* 空实现下**不**汇报急停：否则固件开机即锁死，连控制循环都跑不起来，
       这个剖面就失去了验证时序的价值。真实急停语义见 port_driverlib.c。 */
    return false;
}

void port_led_set(bool on) { (void)on; }

bool port_is_stub(void) { return true; }

#endif /* MSPM0_PORT_STUB */
