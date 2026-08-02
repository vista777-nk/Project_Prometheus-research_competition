/**
 * @file main.c
 * @brief MSPM0G3507 差速固件入口：初始化、1kHz 速度环中断、主循环
 *
 * 任务划分 —— 与麦轮固件 (task-10) 保持同一套结构，便于对照阅读：
 *
 *   控制中断 (1kHz，硬实时)      主循环 (软实时)
 *   ├─ 硬件急停检测              ├─ 串口收字节 → 拆帧 → 命令分发
 *   ├─ 编码器采样                ├─ 空闲重同步
 *   ├─ 差速逆解                  ├─ 发送缓冲下发
 *   ├─ 两路 PID                  ├─ 电流采样 (ADC 轮询)
 *   ├─ PWM 输出                  ├─ 遥测上报 (20Hz)
 *   └─ 堵转/超时检测             └─ 状态灯
 *
 * 速度环放在中断里，是因为 PID 的正确性依赖固定的 dt。如果和串口解析、
 * ADC 轮询挤在同一个主循环里，一次遥测发送 (24 字节 @115200 ≈ 2ms) 就能让
 * 控制周期抖动，积分项和微分项会跟着一起失真。
 *
 * Cortex-M0+ 无 FPU 也无硬件除法，浮点全是库调用。控制中断里的开销估算见 README §5.2：
 * 约 2000–3000 个周期 @80MHz ≈ 25–38µs，占 1ms 周期的 3–4%，余量充足。
 * (这是数量级估计，不是实测值 —— 真实数字要等上板后用 GPIO 翻转 + 示波器量。)
 *
 * 铁律：本固件只做运动控制。不做感知、不做决策、不做通信路由。
 * 电赛合规：主控为 TI MSPM0G3507，运动闭环全部在本芯片内完成 (ADR-0004)。
 */
#include <stdbool.h>
#include <string.h>

#include "board_config.h"
#include "encoder.h"
#include "kinematics.h"
#include "motor.h"
#include "mcu_port.h"
#include "pid.h"
#include "protocol.h"
#include "uart.h"
#include "version.h"

/* ===================== 共享状态 =====================
 * 主循环写 / 中断读的状态，一律通过 port_irq_disable() 保护整体拷贝。
 * DiffVelocity 是 8 字节，不是单条指令能原子完成的 —— 尤其在 Cortex-M0+ 上。
 */

static volatile DiffVelocity s_cmd;               /**< 最新速度指令 */
static volatile uint32_t     s_last_cmd_ms;       /**< 最近一次 SET_VELOCITY 的时刻 */
static volatile float        s_actual_rpm[NUM_WHEELS];

/** 故障状态机。判定逻辑在 common/faults.c，本文件只负责采集输入与执行处置。
 *  这样做的直接收益：全部故障状态迁移可在 Host 上表驱动测试 (test_faults.c)，
 *  而不必依赖 1kHz 中断 + 真实编码器 + ADC。 */
static FaultMonitor s_faults;

static PIDController s_pid[NUM_WHEELS];

/* ===================== 协议回调 ===================== */

static void handle_set_velocity(const DiffVelocity *cmd, void *ctx)
{
    (void)ctx;
    port_irq_disable();
    s_cmd.v = cmd->v;
    s_cmd.omega = cmd->omega;
    s_last_cmd_ms = port_millis();
    port_irq_enable();
}

static void handle_emergency_stop(void *ctx)
{
    (void)ctx;
    port_irq_disable();
    s_cmd.v = 0.0f;
    s_cmd.omega = 0.0f;
    port_irq_enable();

    motor_brake_all();
    for (int i = 0; i < NUM_WHEELS; i++) {
        pid_reset(&s_pid[i]);
    }
}

/* 刻意**不**注册 on_extension：Phase 1 还没有任何竞赛外设接上来。
   不注册时协议层会明确回 ERROR/NOT_IMPLEMENTED，比注册一个假装成功的
   空回调诚实得多 —— 上位机能立刻知道这块固件不支持扩展。
   赛场上接入外设时，在这里补一个回调即可，协议不用改。 */

/* ===================== 1kHz 速度环中断 ===================== */

/** 读取一份速度指令的快照，避免在中断中间被主循环改写 */
static DiffVelocity read_command_snapshot(void)
{
    DiffVelocity cmd;
    cmd.v = s_cmd.v;
    cmd.omega = s_cmd.omega;
    return cmd;
}

/**
 * 控制中断回调，由移植层在定时器 ISR 上下文中调用。
 * 这样 main.c 不需要知道 MSPM0 的中断向量叫什么名字。
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
    DiffVelocity cmd = read_command_snapshot();
    if (command_age_ms > CMD_TIMEOUT_MS) {
        cmd.v = 0.0f;
        cmd.omega = 0.0f;
    }

    /* --- 3. 差速逆解 --- */
    float target_rpm[NUM_WHEELS];
    const int kin_status = diff_inverse_kinematics(&cmd, target_rpm);

    /* --- 4. 故障评估 ---
       急停锁存、堵转判定、位间优先级 (过流/急停期间 STALL 保持旧值)
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

    /* --- 6. 两路独立 PID → PWM --- */
    for (int i = 0; i < NUM_WHEELS; i++) {
        const float duty = pid_update(&s_pid[i], target_rpm[i],
                                      actual_snapshot[i], CONTROL_DT_S);
        motor_set_duty(i, duty);
    }
}

/* ===================== 主循环 ===================== */

/** 把协议层的字节输出接到串口发送缓冲 */
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

    /* 电流在这里采 (ADC 轮询，不进控制中断)，但过流的处置动作由控制中断执行 */
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

    const DiffGeometry geometry = {
        CHASSIS_WHEEL_RADIUS_M,
        CHASSIS_TRACK_WIDTH_M,
        MOTOR_MAX_RPM
    };
    (void)diff_kinematics_init(&geometry);

    /* 故障判定阈值全部来自 board_config.h。堵转以控制周期计数，
       因此 FAULT_STALL_TIME_MS 恰好等于所需的评估次数 (1kHz ⟹ 1 次/ms)。 */
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

    s_cmd.v = 0.0f;
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
        /* 1. 串口接收 → 拆帧 → 命令分发 (应答在 protocol.c 内部完成) */
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

        /* 4. 底盘模块的 HC-SR04 快照；没有完整快照时不发送假帧。 */
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
