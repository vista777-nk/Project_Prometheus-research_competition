/**
 * @file mcu_port.h
 * @brief MCU 移植层接口 —— 固件里唯一碰寄存器的地方 (两块板共用同一份接口)
 *
 * ─────────────────────────────────────────────────────────────────────────
 * 这个文件存在的理由
 * ─────────────────────────────────────────────────────────────────────────
 * 固件里最容易出错的从来不是寄存器操作本身 (那有数据手册可查)，而是**围绕它的
 * 板级逻辑**：编码器计数回绕、测速窗口保持、EMA 滤波、环形缓冲、占空比→方向
 * 映射。这些逻辑一旦和寄存器访问写在同一个函数里，就只能上板验证。
 *
 * 把二者切开之后：
 *
 *   encoder.c / motor.c / uart.c / main.c       ← 板级**逻辑**，纯 C，可 Host 测试
 *              ↓ 只调用下面这十几个原语
 *   common/mcu_port.h                            ← 本文件：移植层接口
 *              ↓ 每块板一份实现
 *   stm32_mecanum/port_stm32f407.c    mspm0_diff/port_driverlib.c
 *                                     mspm0_diff/port_stub.c (CI / 无硬件)
 *
 * 收益：
 *   · 换 MCU 只需要重写这十几个函数，板级逻辑一行不动 (RESEARCH_PHILOSOPHY §五)
 *   · 测速窗口与 EMA 这类算法可以在 Host 上用假编码器直接测 (见各板 test_encoder.c)
 *   · 无法核对的器件细节被收拢到一个文件里，而不是散落四处
 *
 * 代价：多一层间接调用。1kHz 控制中断里每周期约多 10 次函数调用 ——
 * @168MHz 约 0.2µs、@80MHz 约 0.5µs，相对 1000µs 的周期可忽略。刻意付的。
 *
 * ─────────────────────────────────────────────────────────────────────────
 * 调用方向约定
 * ─────────────────────────────────────────────────────────────────────────
 * `port_*` 前缀的函数分两类，看实现方就能区分：
 *
 *   **移植层实现** —— 上层调用它们操作硬件 (port_motor_set_pwm 等)
 *   **上层实现**   —— 移植层在中断上下文里回调 (port_*_hook / port_uart_tx_next)
 *
 * 回调反转是为了让两块板都能保留各自更优的实现：STM32 用 TXE 中断驱动发送，
 * MSPM0 用忙等 FIFO，二者共用同一个环形缓冲逻辑。
 */
#ifndef FIRMWARE_COMMON_MCU_PORT_H
#define FIRMWARE_COMMON_MCU_PORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 一、系统 ===================== */

/**
 * 初始化时钟树与 1ms 时基。必须在任何其他 port_* 调用之前执行。
 * STM32F407: HSE+PLL → 168MHz · MSPM0G3507: SYSOSC+SYSPLL → 80MHz
 */
void port_system_init(void);

/** 自复位以来的毫秒数 (1ms 时基累加，49.7 天回绕) */
uint32_t port_millis(void);

/** 忙等延时。仅用于上电初始化阶段，控制循环里禁止调用。 */
void port_delay_ms(uint32_t ms);

/** 全局中断开关 (保护主循环与控制中断之间的多字节共享状态) */
void port_irq_disable(void);
void port_irq_enable(void);

/* ===================== 二、控制定时器 ===================== */

/**
 * 配置周期定时器并使能其中断，中断里回调 port_control_isr_hook()。
 * @param freq_hz 期望频率，本项目固定 CONTROL_FREQ_HZ (1000)
 */
void port_control_timer_init(uint32_t freq_hz);

/**
 * 【上层实现】控制定时器中断回调 —— 移植层在 ISR 上下文中调用。
 * 这样 main.c 不必知道具体 MCU 的中断向量叫什么名字。
 */
void port_control_isr_hook(void);

/* ===================== 三、电机 PWM 与方向 ===================== */

/** 配置 PWM 定时器与方向 GPIO */
void port_motor_init(uint32_t pwm_freq_hz);

/**
 * 设置某轮 PWM 占空比的**绝对值**。方向由 port_motor_set_direction() 单独控制。
 * @param wheel    轮索引，越界忽略
 * @param duty_abs [0.0, 1.0]，调用方保证已钳位且为有限值
 */
void port_motor_set_pwm(int wheel, float duty_abs);

/** DRV8871 输入真值表的抽象状态（每块驱动板只驱动一台电机）。 */
typedef enum {
    PORT_MOTOR_COAST = 0,   /**< IN1=0 IN2=0 休眠/高阻 */
    PORT_MOTOR_FORWARD,     /**< IN1=1 IN2=0 */
    PORT_MOTOR_REVERSE,     /**< IN1=0 IN2=1 */
    PORT_MOTOR_BRAKE        /**< IN1=1 IN2=1 慢衰减/刹车 */
} PortMotorDirection;

void port_motor_set_direction(int wheel, PortMotorDirection dir);

/* ===================== 四、编码器 ===================== */

/** 配置正交编码器 (4 倍频) 并清零计数 */
void port_encoder_init(void);

/**
 * 读取某轮编码器计数器的**原始当前值**。
 *
 * 统一返回 16 位无符号原值：回绕由上层用差值 + 有符号截断处理。
 * 这样移植层不必关心回绕语义，上层也不依赖某款 MCU 的计数器位宽
 * (STM32 的 TIM2/TIM5 是 32 位、TIM3/TIM4 是 16 位，统一按 16 位用)。
 */
uint16_t port_encoder_read_count(int wheel);

/* ===================== 五、底盘超声波 ===================== */

/**
 * 读取 MCU 已完成的一组四路 HC-SR04 快照，顺序固定为前、后、左、右。
 *
 * 移植层必须用定时器输出比较/输入捕获实现微秒时序，并轮流触发四路以避免串扰；
 * 不得在控制 ISR 内忙等。未完成电子引脚表或本周期没有完整快照时返回 false，
 * 上层不会发送假读数。
 */
bool port_ultrasonic_snapshot_mm(uint16_t ranges_mm[4]);

/* ===================== 六、串口 ===================== */

/**
 * 配置 UART (8N1) 与收发中断。
 * 收到字节时移植层调用 port_uart_rx_hook()；
 * 检测到线路空闲时调用 port_uart_idle_hook()；
 * 硬件接收溢出时调用 port_uart_overrun_hook()。
 */
void port_uart_init(uint32_t baudrate);

/**
 * 通知移植层"发送队列里有新数据"。
 *
 * 移植层据此取字节发送，取法由各板自选：
 *   STM32  — 打开 TXE 中断，在 ISR 里逐字节调 port_uart_tx_next()
 *   MSPM0  — 就地忙等 FIFO，循环调 port_uart_tx_next() 直到队空
 * 上层只管入队，不关心哪种。
 */
void port_uart_tx_start(void);

/**
 * 【上层实现】取出下一个待发字节。
 * @return false 表示发送队列已空，移植层应停止取字节 (如关闭 TXE 中断)
 */
bool port_uart_tx_next(uint8_t *byte);

/** 【上层实现】收到一个字节 */
void port_uart_rx_hook(uint8_t byte);
/** 【上层实现】接收线路空闲 —— 拆帧重同步的触发点 */
void port_uart_idle_hook(void);
/** 【上层实现】硬件接收溢出 */
void port_uart_overrun_hook(void);

/* ===================== 七、ADC (扩展模拟量) ===================== */

void port_adc_init(void);

/**
 * 单通道阻塞采样。
 * @return 12 位原始值 [0, 4095]；通道非法或未实现时返回 0
 */
uint16_t port_adc_read(uint8_t channel);

/* ===================== 八、GPIO (急停 / LED) ===================== */

void port_gpio_init(void);

/**
 * 急停引脚是否已触发。
 *
 * 全项目统一按**常闭 (NC)** 接法：回路完好且未按下时引脚被拉低；
 * 按下**或线缆断开**都会因内部上拉变高 → 返回 true。断线即停，失效安全。
 */
bool port_estop_asserted(void);

void port_led_set(bool on);

/**
 * 移植层是否为空实现 (碰不到真实硬件)。
 *
 * 用于在诊断输出里如实汇报。空实现构建下返回 true —— 让"烧进去电机不转"
 * 这件事能被立刻发现，而不是靠万用表排查半天。
 * 真实硬件实现一律返回 false。
 */
bool port_is_stub(void);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_COMMON_MCU_PORT_H */
