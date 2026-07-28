/**
 * @file main.c
 * @brief STM32F407 麦轮固件入口：初始化、1kHz 速度环中断、主循环
 *
 * 任务划分 —— 这是本固件最重要的一条结构约定：
 *
 *   TIM6 中断 (1kHz，硬实时)     主循环 (软实时)
 *   ├─ 硬件急停检测              ├─ 串口收字节 → 拆帧 → 命令分发
 *   ├─ 编码器采样                ├─ 空闲重同步
 *   ├─ 逆运动学解算              ├─ 电流采样 (ADC 轮询)
 *   ├─ 四路 PID                  ├─ 遥测上报 (20Hz)
 *   ├─ PWM 输出                  └─ 状态灯
 *   └─ 堵转/超时检测
 *
 * 速度环放在中断里，是因为 PID 的正确性依赖固定的 dt。如果和串口解析、
 * ADC 轮询挤在同一个主循环里，一次 40 字节的遥测发送就能让控制周期抖动几毫秒，
 * 积分项和微分项会跟着一起失真。task-10 §10.5 的伪代码用 HAL_Delay(1) 只是示意。
 *
 * 铁律：本固件只做运动控制。不做感知、不做决策、不做通信路由。
 */
#include <stdbool.h>
#include <string.h>

#include "board_config.h"
#include "bsp.h"
#include "encoder.h"
#include "kinematics.h"
#include "motor.h"
#include "pid.h"
#include "protocol.h"
#include "stm32f4xx_conf.h"
#include "uart.h"
#include "version.h"

/* ===================== 共享状态 =====================
 * 主循环写 / 中断读的状态，一律通过 irq_disable() 保护整体拷贝。
 * RobotVelocity 是 12 字节，不是单条指令能原子完成的。
 */

static volatile RobotVelocity s_cmd;              /**< 最新速度指令 */
static volatile uint32_t      s_last_cmd_ms;      /**< 最近一次 SET_VELOCITY 的时刻 */
static volatile uint16_t      s_fault;            /**< 故障位图 */
static volatile bool          s_estop_latched;    /**< 硬件急停已锁死 */
static volatile float         s_actual_rpm[NUM_WHEELS];

static PIDController s_pid[NUM_WHEELS];
static uint32_t s_stall_ms[NUM_WHEELS];

/* ===================== 协议回调 ===================== */

static void handle_set_velocity(const RobotVelocity *cmd, void *ctx)
{
    (void)ctx;
    irq_disable();
    s_cmd.vx = cmd->vx;
    s_cmd.vy = cmd->vy;
    s_cmd.omega = cmd->omega;
    s_last_cmd_ms = bsp_get_tick_ms();
    s_fault &= (uint16_t)~FAULT_CMD_TIMEOUT;
    irq_enable();
}

static void handle_emergency_stop(void *ctx)
{
    (void)ctx;
    irq_disable();
    s_cmd.vx = 0.0f;
    s_cmd.vy = 0.0f;
    s_cmd.omega = 0.0f;
    irq_enable();

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

/** 堵转判据：给了转速却不转，持续够久就报故障。返回该轮当前是否处于堵转。 */
static bool update_stall_detection(int wheel, float target, float actual)
{
    const float abs_target = (target < 0.0f) ? -target : target;
    const float abs_actual = (actual < 0.0f) ? -actual : actual;

    if (abs_target > FAULT_STALL_TARGET_RPM && abs_actual < FAULT_STALL_RPM_FLOOR) {
        s_stall_ms[wheel]++;
    } else {
        s_stall_ms[wheel] = 0u;
    }
    return s_stall_ms[wheel] >= FAULT_STALL_TIME_MS;
}

void TIM6_DAC_IRQHandler(void)
{
    if ((TIM6->SR & TIM_SR_UIF) == 0u) {
        return;
    }
    TIM6->SR = ~TIM_SR_UIF;

    /* --- 1. 硬件急停：优先级高于一切，且一旦触发就锁死 --- */
#if ESTOP_REQUIRE_HARDWARE
    if (bsp_estop_asserted()) {
        s_estop_latched = true;
    }
#endif
    if (s_estop_latched) {
        motor_brake_all();
        s_fault |= FAULT_ESTOP;
        for (int i = 0; i < NUM_WHEELS; i++) {
            pid_reset(&s_pid[i]);
            s_actual_rpm[i] = 0.0f;
        }
        return;   /* 不可恢复，只能靠复位退出 —— 安全优先于可用性 */
    }

    /* --- 2. 编码器采样 --- */
    encoder_update();
    for (int i = 0; i < NUM_WHEELS; i++) {
        s_actual_rpm[i] = encoder_get_rpm(i);
    }

    /* --- 3. 指令看门狗：上位机掉线就刹停 --- */
    RobotVelocity cmd = read_command_snapshot();
    if ((bsp_get_tick_ms() - s_last_cmd_ms) > CMD_TIMEOUT_MS) {
        cmd.vx = 0.0f;
        cmd.vy = 0.0f;
        cmd.omega = 0.0f;
        s_fault |= FAULT_CMD_TIMEOUT;
    }

    /* --- 4. 逆运动学 --- */
    float target_rpm[NUM_WHEELS];
    const int kin_status = inverse_kinematics(&cmd, target_rpm);
    if (kin_status == KIN_SATURATED) {
        s_fault |= FAULT_KINEMATICS_SAT;
    } else {
        s_fault &= (uint16_t)~FAULT_KINEMATICS_SAT;
    }

    /* --- 5. 过流已由主循环置位：带电流故障时不再输出 --- */
    if ((s_fault & FAULT_OVERCURRENT) != 0u) {
        motor_brake_all();
        return;
    }

    /* --- 6. 四路独立 PID → PWM --- */
    bool any_stalled = false;
    for (int i = 0; i < NUM_WHEELS; i++) {
        const float duty = pid_update(&s_pid[i], target_rpm[i],
                                      s_actual_rpm[i], CONTROL_DT_S);
        motor_set_duty(i, duty);
        if (update_stall_detection(i, target_rpm[i], s_actual_rpm[i])) {
            any_stalled = true;
        }
    }
    /* 故障位跟着实际状态走：轮子转起来了就把位清掉，
       否则一次瞬时堵转会让故障灯一直亮到下次复位。 */
    if (any_stalled) {
        s_fault |= FAULT_STALL;
    } else {
        s_fault &= (uint16_t)~FAULT_STALL;
    }
}

/** TIM6：84MHz APB1 定时器时钟 → /84 = 1MHz → 每 1000 计数产生 1kHz 更新中断 */
static void control_timer_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;

    TIM6->CR1 = 0uL;
    TIM6->PSC = (TIMCLK_APB1_HZ / 1000000uL) - 1uL;
    TIM6->ARR = (1000000uL / CONTROL_FREQ_HZ) - 1uL;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0uL;
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->CR1 = TIM_CR1_CEN;

    nvic_enable_irq(IRQn_TIM6_DAC);
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

    irq_disable();
    for (int i = 0; i < NUM_WHEELS; i++) {
        rpm[i] = s_actual_rpm[i];
    }
    irq_enable();

    motor_sample_currents(current);

    /* 过流判定放在主循环 (ADC 在这里采)，但处置动作由控制中断执行 */
    bool overcurrent = false;
    for (int i = 0; i < NUM_WHEELS; i++) {
        if (current[i] > FAULT_CURRENT_LIMIT_A) {
            overcurrent = true;
        }
    }
    irq_disable();
    if (overcurrent) {
        s_fault |= FAULT_OVERCURRENT;
    } else {
        s_fault &= (uint16_t)~FAULT_OVERCURRENT;
    }
    irq_enable();

    /* 链路质量：只看"距上次遥测有没有新增错误"，而不是累计值。
       用累计值的话，开机时的一次噪声就会让故障位永远挂着。
       具体错了多少次，上位机可以另行查询统计计数器。 */
    static uint32_t last_crc_errors;
    static uint32_t last_rx_overruns;
    static uint32_t last_tx_drops;

    const uint32_t crc_errors = protocol_get_parser()->stat_err_crc;
    const uint32_t rx_overruns = uart_get_rx_overrun_count();
    const uint32_t tx_drops = uart_get_tx_drop_count();
    const bool link_degraded = (crc_errors != last_crc_errors)
                            || (rx_overruns != last_rx_overruns)
                            || (tx_drops != last_tx_drops);
    last_crc_errors = crc_errors;
    last_rx_overruns = rx_overruns;
    last_tx_drops = tx_drops;

    irq_disable();
    if (link_degraded) {
        s_fault |= FAULT_UART_ERROR;
    } else {
        s_fault &= (uint16_t)~FAULT_UART_ERROR;
    }
    const uint16_t fault = s_fault;
    irq_enable();

    protocol_send_telemetry(rpm, current, fault);
}

/** 状态灯：正常 1Hz 慢闪，有故障 5Hz 快闪，急停锁死时常亮 */
static void update_status_led(uint32_t now_ms)
{
    static uint32_t last_toggle_ms;

    if (s_estop_latched) {
        bsp_led_set(true);
        return;
    }
    const uint32_t period = (s_fault != FAULT_NONE) ? 100u : 500u;
    if ((now_ms - last_toggle_ms) >= period) {
        last_toggle_ms = now_ms;
        bsp_led_toggle();
    }
}

int main(void)
{
    bsp_init();

    const MecanumGeometry geometry = {
        CHASSIS_WHEEL_RADIUS_M,
        CHASSIS_LX_M,
        CHASSIS_LY_M,
        MOTOR_MAX_RPM
    };
    (void)kinematics_init(&geometry);

    for (int i = 0; i < NUM_WHEELS; i++) {
        pid_init(&s_pid[i], PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT,
                 PID_INTEGRAL_LIMIT, PID_OUTPUT_LIMIT);
        s_stall_ms[i] = 0u;
        s_actual_rpm[i] = 0.0f;
    }

    s_cmd.vx = 0.0f;
    s_cmd.vy = 0.0f;
    s_cmd.omega = 0.0f;
    s_fault = FAULT_NONE;
    s_estop_latched = false;
    /* 上电即视为"刚收到指令"，避免开机第一秒就报超时故障 */
    s_last_cmd_ms = bsp_get_tick_ms();

    encoder_init();
    motor_init();
    uart_init();

    ProtocolHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.on_set_velocity = handle_set_velocity;
    handlers.on_emergency_stop = handle_emergency_stop;
    protocol_init(uart_writer, 0, &handlers);

    /* 所有外设就绪后才开控制中断，否则中断会用到未初始化的外设 */
    control_timer_init();

    uint32_t last_telemetry_ms = bsp_get_tick_ms();
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
        const uint32_t now_ms = bsp_get_tick_ms();
        if ((now_ms - last_telemetry_ms) >= TELEMETRY_PERIOD_MS) {
            last_telemetry_ms = now_ms;
            publish_telemetry();
        }

        /* 4. 状态灯 */
        update_status_led(now_ms);
    }
}
