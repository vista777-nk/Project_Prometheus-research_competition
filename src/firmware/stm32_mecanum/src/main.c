/**
 * @file main.c
 * @brief STM32F407 麦轮固件入口：初始化、1kHz 速度环中断、主循环
 *
 * 任务划分 —— 这是本固件最重要的一条结构约定：
 *
 *   控制中断 (1kHz，硬实时)      主循环 (软实时)
 *   ├─ 硬件急停检测              ├─ 串口收字节 → 拆帧 → 命令分发
 *   ├─ 编码器采样                ├─ 空闲重同步
 *   ├─ 逆运动学解算              ├─ 电流采样 (ADC 轮询)
 *   ├─ 四路 PID                  ├─ 遥测上报 (20Hz)
 *   ├─ PWM 输出                  └─ 状态灯
 *   └─ 故障评估
 *
 * 速度环放在中断里，是因为 PID 的正确性依赖固定的 dt。如果和串口解析、
 * ADC 轮询挤在同一个主循环里，一次 40 字节的遥测发送就能让控制周期抖动几毫秒，
 * 积分项和微分项会跟着一起失真。task-10 §10.5 的伪代码用 HAL_Delay(1) 只是示意。
 *
 * 分层：本文件不含任何寄存器访问。硬件操作走 common/mcu_port.h，
 * 由 port_stm32f407.c 实现；故障判定走 common/faults.c。
 * 这样做的直接收益是那两部分逻辑都能在 Host 上测 —— 详见 README §代码结构。
 *
 * 铁律：本固件只做运动控制。不做感知、不做决策、不做通信路由。
 */
#include <stdbool.h>
#include <string.h>

#include "board_config.h"
#include "encoder.h"
#include "faults.h"
#include "kinematics.h"
#include "mcu_port.h"
#include "motor.h"
#include "pid.h"
#include "protocol.h"
#include "uart.h"
#include "version.h"

/* ===================== 共享状态 =====================
 * 主循环写 / 中断读的状态，一律通过 port_irq_disable() 保护整体拷贝。
 * RobotVelocity 是 12 字节，不是单条指令能原子完成的。
 */

static volatile RobotVelocity s_cmd;              /**< 最新速度指令 */
static volatile uint32_t      s_last_cmd_ms;      /**< 最近一次 SET_VELOCITY 的时刻 */
static volatile float         s_actual_rpm[NUM_WHEELS];

/** 故障状态机。判定逻辑在 common/faults.c，本文件只负责采集输入与执行处置。 */
static FaultMonitor s_faults;

static PIDController s_pid[NUM_WHEELS];

/* ===================== 协议回调 ===================== */

static void handle_set_velocity(const RobotVelocity *cmd, void *ctx)
{
    (void)ctx;
    port_irq_disable();
    s_cmd.vx = cmd->vx;
    s_cmd.vy = cmd->vy;
    s_cmd.omega = cmd->omega;
    s_last_cmd_ms = port_millis();
    port_irq_enable();
}

static void handle_emergency_stop(void *ctx)
{
    (void)ctx;
    port_irq_disable();
    s_cmd.vx = 0.0f;
    s_cmd.vy = 0.0f;
    s_cmd.omega = 0.0f;
    port_irq_enable();

    motor_brake_all();
    for (int i = 0; i < NUM_WHEELS; i++) {
        pid_reset(&s_pid[i]);
    }
}

/* ===================== 1kHz 速度环中断 ===================== */

/** 读取一份速度指令的快照，避免在中断中间被主循环改写 */
static RobotVelocity read_command_snapshot(void)
{
    RobotVelocity cmd;
    cmd.vx = s_cmd.vx;
    cmd.vy = s_cmd.vy;
    cmd.omega = s_cmd.omega;
    return cmd;
}

/**
 * 控制中断回调，由移植层在 TIM6 ISR 上下文中调用。
 * 这样 main.c 不需要知道中断向量叫什么名字。
 */
void port_control_isr_hook(void)
{
    /* --- 1. 编码器采样 --- */
    encoder_update();
    for (int i = 0; i < NUM_WHEELS; i++) {
        s_actual_rpm[i] = encoder_get_rpm(i);
    }

    /* --- 2. 指令看门狗：上位机掉线就刹停 --- */
    const uint32_t command_age_ms = port_millis() - s_last_cmd_ms;
    RobotVelocity cmd = read_command_snapshot();
    if (command_age_ms > CMD_TIMEOUT_MS) {
        cmd.vx = 0.0f;
        cmd.vy = 0.0f;
        cmd.omega = 0.0f;
    }

    /* --- 3. 逆运动学 --- */
    float target_rpm[NUM_WHEELS];
    const int kin_status = inverse_kinematics(&cmd, target_rpm);

    /* --- 4. 故障评估 ---
       急停锁存、堵转判定、位间优先级（过流/急停期间 STALL 保持旧值）
       全部由状态机负责，本文件不再自己拼位图。 */
    float actual_snapshot[NUM_WHEELS];
    for (int i = 0; i < NUM_WHEELS; i++) {
        actual_snapshot[i] = s_actual_rpm[i];
    }

    FaultMotionInput motion;
#if ESTOP_REQUIRE_HARDWARE
    motion.estop_asserted = port_estop_asserted();
#else
    motion.estop_asserted = false;
#endif
    motion.wheel_count = NUM_WHEELS;
    motion.target_rpm = target_rpm;
    motion.actual_rpm = actual_snapshot;
    motion.kinematics_saturated = (kin_status == KIN_SATURATED);
    motion.command_age_ms = command_age_ms;

    (void)faults_evaluate_motion(&s_faults, &motion);

    /* --- 5. 处置：急停或过流一律刹停并跳过本周期输出 ---
       急停是不可恢复的，只能靠复位退出 —— 安全优先于可用性。 */
    if (faults_should_halt(&s_faults)) {
        motor_brake_all();
        for (int i = 0; i < NUM_WHEELS; i++) {
            pid_reset(&s_pid[i]);
            if (faults_estop_latched(&s_faults)) {
                s_actual_rpm[i] = 0.0f;
            }
        }
        return;
    }

    /* --- 6. 四路独立 PID → PWM --- */
    for (int i = 0; i < NUM_WHEELS; i++) {
        const float duty = pid_update(&s_pid[i], target_rpm[i],
                                      actual_snapshot[i], CONTROL_DT_S);
        motor_set_duty(i, duty);
    }
}

/* ===================== 主循环 ===================== */

/** 把协议层的字节输出接到串口 */
static void uart_writer(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx;
    uart_write(data, len);
}

static void publish_telemetry(void)
{
    float rpm[NUM_WHEELS];
    float current[NUM_WHEELS];

    port_irq_disable();
    for (int i = 0; i < NUM_WHEELS; i++) {
        rpm[i] = s_actual_rpm[i];
    }
    port_irq_enable();

    /* 电流在这里采（ADC 轮询，不进控制中断），但过流的处置由控制中断执行 */
    motor_sample_currents(current);

    FaultLinkInput link;
    link.wheel_count = NUM_WHEELS;
    link.current_a = current;
    link.crc_errors = protocol_get_parser()->stat_err_crc;
    link.rx_overruns = uart_get_rx_overrun_count();
    link.tx_drops = uart_get_tx_drop_count();

    port_irq_disable();
    const uint16_t fault = faults_evaluate_link(&s_faults, &link);
    port_irq_enable();

    protocol_send_telemetry(rpm, current, fault);
}

/** 状态灯：正常 1Hz 慢闪，有故障 5Hz 快闪，急停锁死时常亮 */
static void update_status_led(uint32_t now_ms)
{
    static uint32_t last_toggle_ms;
    static bool led_on;

    if (faults_estop_latched(&s_faults)) {
        port_led_set(true);
        led_on = true;
        return;
    }
    const uint32_t period = (faults_get(&s_faults) != FAULT_NONE) ? 100u : 500u;
    if ((now_ms - last_toggle_ms) >= period) {
        last_toggle_ms = now_ms;
        led_on = !led_on;
        port_led_set(led_on);
    }
}

int main(void)
{
    port_system_init();
    port_gpio_init();

    const MecanumGeometry geometry = {
        CHASSIS_WHEEL_RADIUS_M,
        CHASSIS_LX_M,
        CHASSIS_LY_M,
        MOTOR_MAX_RPM
    };
    (void)kinematics_init(&geometry);

    /* 故障判定阈值全部来自 board_config.h。堵转以控制周期计数，
       因此 FAULT_STALL_TIME_MS 恰好等于所需的评估次数（1kHz ⟹ 1 次/ms）。 */
    const FaultConfig fault_cfg = {
        FAULT_STALL_TARGET_RPM,
        FAULT_STALL_RPM_FLOOR,
        FAULT_STALL_TIME_MS,
        FAULT_CURRENT_LIMIT_A,
        CMD_TIMEOUT_MS
    };
    faults_init(&s_faults, &fault_cfg);

    for (int i = 0; i < NUM_WHEELS; i++) {
        pid_init(&s_pid[i], PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT,
                 PID_INTEGRAL_LIMIT, PID_OUTPUT_LIMIT);
        s_actual_rpm[i] = 0.0f;
    }

    s_cmd.vx = 0.0f;
    s_cmd.vy = 0.0f;
    s_cmd.omega = 0.0f;
    /* 上电即视为"刚收到指令"，避免开机第一秒就报超时故障 */
    s_last_cmd_ms = port_millis();

    encoder_init();
    motor_init();
    uart_init();

    ProtocolHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.on_set_velocity = handle_set_velocity;
    handlers.on_emergency_stop = handle_emergency_stop;
    protocol_init(uart_writer, 0, &handlers);

    /* 所有外设就绪后才开控制中断，否则中断会用到未初始化的外设 */
    port_control_timer_init(CONTROL_FREQ_HZ);

    uint32_t last_telemetry_ms = port_millis();
    uint32_t last_ultrasonic_ms = last_telemetry_ms;
    uint8_t rx_chunk[64];

    while (1) {
        /* 1. 串口接收 → 拆帧 → 命令分发（应答在 protocol.c 内部完成） */
        const uint16_t received = uart_read(rx_chunk, (uint16_t)sizeof(rx_chunk));
        if (received > 0u) {
            (void)protocol_feed(rx_chunk, received);
        }

        /* 2. 线路空闲 → 丢弃半截帧重新对齐 */
        if (uart_take_idle_event()) {
            protocol_notify_line_idle();
        }

        /* 3. 遥测 20Hz */
        const uint32_t now_ms = port_millis();
        if ((now_ms - last_telemetry_ms) >= TELEMETRY_PERIOD_MS) {
            last_telemetry_ms = now_ms;
            publish_telemetry();
        }

        /* 4. 底盘模块的 HC-SR04 快照。移植层没有完整快照就不发帧，
           让 Pi 的实机启动检查明确失败，而不是发布全零假数据。 */
        if ((now_ms - last_ultrasonic_ms) >= ULTRASONIC_PERIOD_MS) {
            uint16_t ranges_mm[4];
            last_ultrasonic_ms = now_ms;
            if (port_ultrasonic_snapshot_mm(ranges_mm)) {
                protocol_send_ultrasonic(ranges_mm);
            }
        }

        /* 5. 状态灯 */
        update_status_led(now_ms);
    }
}
