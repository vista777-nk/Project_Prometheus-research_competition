/**
 * @file port_driverlib.c
 * @brief 移植层的 **真实硬件实现** (TI MSPM0 DriverLib)
 *
 * ⚠ 本文件只在 `make PROFILE=driverlib` 时参与编译，需要本机安装 TI MSPM0 SDK。
 *   CI 不编译它 —— MSPM0 SDK 无法在 GitHub Actions 上免登录安装。
 *
 * ─────────────────────────────────────────────────────────────────────────
 * 上板前必须做的事 (README §7 有完整清单)
 * ─────────────────────────────────────────────────────────────────────────
 * 本文件里所有 `MOTOR_PWM_INST` / `ENCODER_L_INST` 之类的符号，都应当来自
 * **SysConfig 生成的 ti_msp_dl_config.h**，而不是手写。流程：
 *
 *   1. 用 CCS / SysConfig 打开 ccs/mspm0_diff.syscfg，按 board_config.h §六
 *      的引脚表配置 TIMA0(PWM) / TIMG8/TIMG7(QEI) / UART0 / ADC0 / GPIO
 *   2. 生成 ti_msp_dl_config.h + ti_msp_dl_config.c
 *   3. 核对生成的实例名与本文件使用的宏名一致 (不一致就改本文件，别改生成物)
 *   4. 逐项走 README §7 的上电检查清单
 *
 * 之所以把它写成"引用生成物"而不是"手写寄存器"：SysConfig 会同时生成
 * 引脚复用、时钟分频与中断优先级，手写这三者中的任何一处出错，
 * 症状都是"某个外设静默不工作"，排查成本极高。
 */
#include "mspm0_conf.h"

#ifdef USE_TI_DRIVERLIB

#include "board_config.h"
#include "mcu_port.h"

/* SysConfig 生成物提供以下符号，若名字对不上请以生成物为准修改这里：
     MOTOR_PWM_INST, MOTOR_PWM_C0_IDX, MOTOR_PWM_C1_IDX
     ENCODER_L_INST, ENCODER_R_INST
     UART_COMM_INST, UART_COMM_INST_IRQHandler
     ADC_CURRENT_INST
     GPIO_MOTOR_PORT / GPIO_MOTOR_*_PIN
     GPIO_ESTOP_PORT / GPIO_ESTOP_PIN
     GPIO_LED_PORT   / GPIO_LED_PIN                                        */

static volatile uint32_t s_millis;

/* ===================== 系统 ===================== */

void port_system_init(void)
{
    /* SYSCFG_DL_init() 由 SysConfig 生成：配好时钟树 (80MHz)、
       全部外设实例与引脚复用。 */
    SYSCFG_DL_init();

    /* 1ms 时基。控制中断另用 TIMG，不复用 SysTick ——
       真实硬件上让时基与控制周期彼此独立，调试时更容易分辨是谁在抖。 */
    DL_SYSTICK_config(SYSCLK_HZ / 1000uL);
    DL_SYSTICK_enableInterrupt();
}

void SysTick_Handler(void)
{
    s_millis++;
}

uint32_t port_millis(void)
{
    return s_millis;
}

void port_delay_ms(uint32_t ms)
{
    const uint32_t start = port_millis();
    while ((port_millis() - start) < ms) {
        __WFI();
    }
}

void port_irq_disable(void) { __disable_irq(); }
void port_irq_enable(void)  { __enable_irq(); }

/* ===================== 控制定时器 (1kHz) ===================== */

void port_control_timer_init(uint32_t freq_hz)
{
    (void)freq_hz;   /* 周期由 SysConfig 在 CONTROL_TIMER_INST 上配好 */
    DL_TimerG_startCounter(CONTROL_TIMER_INST);
    NVIC_EnableIRQ(CONTROL_TIMER_INST_INT_IRQN);
}

void CONTROL_TIMER_INST_IRQHandler(void)
{
    switch (DL_TimerG_getPendingInterrupt(CONTROL_TIMER_INST)) {
    case DL_TIMER_IIDX_ZERO:
        port_control_isr_hook();
        break;
    default:
        break;
    }
}

/* ===================== 电机 ===================== */

void port_motor_init(uint32_t pwm_freq_hz)
{
    (void)pwm_freq_hz;   /* 载频由 SysConfig 在 MOTOR_PWM_INST 上配好 */
    DL_TimerA_startCounter(MOTOR_PWM_INST);
    port_motor_set_pwm(0, 0.0f);
    port_motor_set_pwm(1, 0.0f);
    port_motor_set_direction(0, PORT_MOTOR_COAST);
    port_motor_set_direction(1, PORT_MOTOR_COAST);
}

void port_motor_set_pwm(int wheel, float duty_abs)
{
    if (wheel < 0 || wheel > 1) {
        return;
    }
    const uint32_t period = DL_TimerA_getLoadValue(MOTOR_PWM_INST);
    const uint32_t compare = (uint32_t)(duty_abs * (float)period);
    const DL_TIMER_CC_INDEX idx = (wheel == 0) ? MOTOR_PWM_C0_IDX : MOTOR_PWM_C1_IDX;
    DL_TimerA_setCaptureCompareValue(MOTOR_PWM_INST, compare, idx);
}

void port_motor_set_direction(int wheel, PortMotorDirection dir)
{
    if (wheel < 0 || wheel > 1) {
        return;
    }
    const uint32_t pin_a = (wheel == 0) ? GPIO_MOTOR_LEFT_A_PIN : GPIO_MOTOR_RIGHT_A_PIN;
    const uint32_t pin_b = (wheel == 0) ? GPIO_MOTOR_LEFT_B_PIN : GPIO_MOTOR_RIGHT_B_PIN;

    /* TB6612 真值表：IN1/IN2 = 00 滑行 · 10 正转 · 01 反转 · 11 刹车 */
    bool a = false;
    bool b = false;
    switch (dir) {
    case PORT_MOTOR_FORWARD: a = true;  b = false; break;
    case PORT_MOTOR_REVERSE: a = false; b = true;  break;
    case PORT_MOTOR_BRAKE:   a = true;  b = true;  break;
    case PORT_MOTOR_COAST:
    default:                 a = false; b = false; break;
    }

    if (a) { DL_GPIO_setPins(GPIO_MOTOR_PORT, pin_a); }
    else   { DL_GPIO_clearPins(GPIO_MOTOR_PORT, pin_a); }
    if (b) { DL_GPIO_setPins(GPIO_MOTOR_PORT, pin_b); }
    else   { DL_GPIO_clearPins(GPIO_MOTOR_PORT, pin_b); }
}

/* ===================== 编码器 ===================== */

void port_encoder_init(void)
{
    DL_TimerG_startCounter(ENCODER_L_INST);
    DL_TimerG_startCounter(ENCODER_R_INST);
}

uint16_t port_encoder_read_count(int wheel)
{
    if (wheel == 0) {
        return (uint16_t)DL_TimerG_getTimerCount(ENCODER_L_INST);
    }
    if (wheel == 1) {
        return (uint16_t)DL_TimerG_getTimerCount(ENCODER_R_INST);
    }
    return 0u;
}

/* ===================== 串口 ===================== */

void port_uart_init(uint32_t baudrate)
{
    (void)baudrate;   /* 波特率由 SysConfig 在 UART_COMM_INST 上配好 */
    NVIC_EnableIRQ(UART_COMM_INST_INT_IRQN);
}

/**
 * MSPM0 侧选择就地忙等把队列抽干，而不是像 STM32 那样开 TXE 中断。
 *
 * 理由：一帧遥测 24 字节 @115200 约 2ms 忙等，而速度环跑在独立的 TIMG 中断里，
 * 不受主循环阻塞影响。少一个中断源就少一处竞态。
 * 两块板共用同一份 uart.c 环形缓冲逻辑，差异只在这个函数里。
 */
void port_uart_tx_start(void)
{
    uint8_t byte;
    while (port_uart_tx_next(&byte)) {
        while (!DL_UART_isTXFIFOEmpty(UART_COMM_INST)) {
            /* 等发送 FIFO 排空 */
        }
        DL_UART_transmitData(UART_COMM_INST, byte);
    }
}

void UART_COMM_INST_IRQHandler(void)
{
    switch (DL_UART_getPendingInterrupt(UART_COMM_INST)) {
    case DL_UART_IIDX_RX:
        port_uart_rx_hook((uint8_t)DL_UART_receiveData(UART_COMM_INST));
        break;
    case DL_UART_IIDX_OVERRUN_ERROR:
        port_uart_overrun_hook();
        break;
    /* 空闲检测是拆帧重同步的解药，见 protocol_frame.h 的 @warning。
       MSPM0 的 UART 提供接收超时中断，SysConfig 里需要把它使能。 */
    case DL_UART_IIDX_RX_TIMEOUT_ERROR:
        port_uart_idle_hook();
        break;
    default:
        break;
    }
}

/* ===================== ADC ===================== */

void port_adc_init(void)
{
    DL_ADC12_enableConversions(ADC_CURRENT_INST);
}

uint16_t port_adc_read(uint8_t channel)
{
    DL_ADC12_configConversionMem(ADC_CURRENT_INST, DL_ADC12_MEM_IDX_0,
                                 (DL_ADC12_INPUT_CHAN)channel,
                                 DL_ADC12_REFERENCE_VOLTAGE_VDDA,
                                 DL_ADC12_SAMPLE_TIMER_SOURCE_SCOMP0,
                                 DL_ADC12_AVERAGING_MODE_DISABLED,
                                 DL_ADC12_BURN_OUT_SOURCE_DISABLED,
                                 DL_ADC12_TRIGGER_MODE_AUTO_NEXT,
                                 DL_ADC12_WINDOWS_COMP_MODE_DISABLED);
    DL_ADC12_startConversion(ADC_CURRENT_INST);
    while (DL_ADC12_getStatus(ADC_CURRENT_INST) == DL_ADC12_STATUS_CONVERSION_ACTIVE) {
        /* 单次转换约 2µs，只在 20Hz 遥测周期的主循环里调用 */
    }
    const uint16_t value = DL_ADC12_getMemResult(ADC_CURRENT_INST, DL_ADC12_MEM_IDX_0);
    DL_ADC12_stopConversion(ADC_CURRENT_INST);
    return value;
}

/* ===================== GPIO ===================== */

void port_gpio_init(void)
{
    /* 引脚方向与上下拉由 SysConfig 配置。这里只把电机脚拉到安全状态。 */
    DL_GPIO_clearPins(GPIO_MOTOR_PORT,
                      GPIO_MOTOR_LEFT_A_PIN | GPIO_MOTOR_LEFT_B_PIN |
                      GPIO_MOTOR_RIGHT_A_PIN | GPIO_MOTOR_RIGHT_B_PIN);
    DL_GPIO_clearPins(GPIO_LED_PORT, GPIO_LED_PIN);
}

bool port_estop_asserted(void)
{
    /* 常闭 (NC) 接法：回路完好且未按下 → 引脚被外部拉低；
       按下或线缆断开 → 内部上拉把它拉高 → 触发急停。断线即停，失效安全。 */
    return DL_GPIO_readPins(GPIO_ESTOP_PORT, GPIO_ESTOP_PIN) != 0u;
}

void port_led_set(bool on)
{
    if (on) { DL_GPIO_setPins(GPIO_LED_PORT, GPIO_LED_PIN); }
    else    { DL_GPIO_clearPins(GPIO_LED_PORT, GPIO_LED_PIN); }
}

bool port_is_stub(void) { return false; }

#endif /* USE_TI_DRIVERLIB */
