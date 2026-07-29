/**
 * @file mspm0_port.h
 * @brief 硬件移植层 —— 固件里唯一碰寄存器的地方
 *
 * ─────────────────────────────────────────────────────────────────────────
 * 这个文件存在的理由
 * ─────────────────────────────────────────────────────────────────────────
 * 麦轮固件 (task-10) 把寄存器操作直接写在 encoder.c / motor.c / uart.c 里。
 * 那样做的代价是：测速窗口、EMA 滤波、环形缓冲、占空比→方向映射这些
 * **真正容易出错的逻辑**，被寄存器访问绑死在交叉编译目标上，Host 测不到。
 *
 * 本工程把二者切开：
 *
 *   encoder.c / motor.c / uart.c / bsp.c   ← 板级**逻辑**，纯 C，可 Host 测试
 *              ↓ 只调用下面这十几个原语
 *   mspm0_port.h                            ← 本文件：移植层接口
 *              ↓ 两份实现，编译期二选一
 *   port_stub.c          port_driverlib.c
 *   (CI / Host 测试)      (真实硬件, 需 TI SDK)
 *
 * 收益：
 *   · 换 MCU 只需要重写这十几个函数，板级逻辑一行不动 (符合 RESEARCH_PHILOSOPHY §五)
 *   · 测速窗口与 EMA 这类算法可以在 Host 上用假编码器直接测 (见 test_encoder.c)
 *   · 我无法核对的 MSPM0 寄存器细节被收拢到一个文件里，而不是散落四处
 *
 * 代价：多一层间接调用。在 1kHz 控制中断里每周期约多 10 次函数调用，
 * @80MHz 约 0.5µs，相对 1000µs 的周期可以忽略。这个代价是刻意付的。
 *
 * ⚠ port_driverlib.c 中的引脚与外设实例名必须与 SysConfig 生成的
 *   ti_msp_dl_config.h 保持一致。核对流程见 README §7。
 */
#ifndef MSPM0_DIFF_MSPM0_PORT_H
#define MSPM0_DIFF_MSPM0_PORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 一、系统 ===================== */

/**
 * 初始化时钟树 (SYSOSC → SYSPLL → 80MHz MCLK) 与 SysTick 1ms。
 * 必须在任何其他 port_* 调用之前执行。
 */
void port_system_init(void);

/** 自复位以来的毫秒数 (SysTick 累加，49.7 天回绕) */
uint32_t port_millis(void);

/** 忙等延时。仅用于上电初始化阶段，控制循环里禁止调用。 */
void port_delay_ms(uint32_t ms);

/** 全局中断开关 (用于保护主循环与控制中断之间的多字节共享状态) */
void port_irq_disable(void);
void port_irq_enable(void);

/* ===================== 二、控制定时器 ===================== */

/**
 * 配置周期定时器并使能其中断，中断里回调 port_control_isr_hook()。
 * @param freq_hz 期望频率，本工程固定 CONTROL_FREQ_HZ (1000)
 */
void port_control_timer_init(uint32_t freq_hz);

/**
 * 控制定时器中断回调 —— 由移植层在 ISR 上下文中调用，
 * 实现在 main.c。这样 main.c 不必知道 MSPM0 的中断向量叫什么名字。
 */
void port_control_isr_hook(void);

/* ===================== 三、电机 PWM 与方向 ===================== */

/** 配置 PWM 定时器 (两路互补通道) 与四根方向 GPIO */
void port_motor_init(uint32_t pwm_freq_hz);

/**
 * 设置某轮 PWM 占空比的**绝对值**。方向由 port_motor_set_direction() 单独控制。
 * @param wheel 0=左, 1=右
 * @param duty_abs [0.0, 1.0]，调用方保证已钳位
 */
void port_motor_set_pwm(int wheel, float duty_abs);

/** TB6612 方向真值表 */
typedef enum {
    PORT_MOTOR_COAST = 0,   /**< IN1=0 IN2=0 滑行 (高阻) */
    PORT_MOTOR_FORWARD,     /**< IN1=1 IN2=0 */
    PORT_MOTOR_REVERSE,     /**< IN1=0 IN2=1 */
    PORT_MOTOR_BRAKE        /**< IN1=1 IN2=1 短接刹车 */
} PortMotorDirection;

void port_motor_set_direction(int wheel, PortMotorDirection dir);

/* ===================== 四、编码器 ===================== */

/** 配置两路定时器为正交编码器模式并清零计数 */
void port_encoder_init(void);

/**
 * 读取某轮编码器计数器的**原始当前值**。
 *
 * 返回 16 位无符号原值，回绕由上层用差值 + 有符号截断处理 ——
 * 这样移植层不必关心回绕语义，上层也不依赖某款 MCU 的计数器位宽。
 */
uint16_t port_encoder_read_count(int wheel);

/* ===================== 五、串口 ===================== */

/**
 * 配置 UART (8N1) 与收发中断。
 * 收到字节时移植层调用 port_uart_rx_hook()，
 * 检测到线路空闲时调用 port_uart_idle_hook()。
 */
void port_uart_init(uint32_t baudrate);

/** 发送一个字节 (忙等到发送寄存器可写)。上层已用环形缓冲削峰，这里不必再排队。 */
void port_uart_put_byte(uint8_t byte);

/** 接收中断回调，实现在 uart.c */
void port_uart_rx_hook(uint8_t byte);
/** 接收线路空闲回调，实现在 uart.c */
void port_uart_idle_hook(void);
/** 硬件接收溢出回调 (ORE)，实现在 uart.c */
void port_uart_overrun_hook(void);

/* ===================== 六、ADC (电流采样 + 电赛扩展) ===================== */

void port_adc_init(void);

/**
 * 单通道阻塞采样。
 * @return 12 位原始值 [0, 4095]；通道非法或未实现时返回 0
 */
uint16_t port_adc_read(uint8_t channel);

/* ===================== 七、GPIO (急停 / LED / 电赛扩展) ===================== */

void port_gpio_init(void);

/** 急停引脚电平。常闭接法：true = 高电平 = 已触发 (按下或断线) */
bool port_estop_asserted(void);

void port_led_set(bool on);

/**
 * 移植层是否为空实现。
 *
 * 用于在 PONG / 调试输出里如实汇报"这块固件根本碰不到硬件"。
 * 空实现下返回 true —— 让"烧进去电机不转"这件事在第一次 PING 就能被发现，
 * 而不是靠万用表排查半天。
 */
bool port_is_stub(void);

#ifdef __cplusplus
}
#endif

#endif /* MSPM0_DIFF_MSPM0_PORT_H */
