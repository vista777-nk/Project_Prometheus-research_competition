/**
 * @file faults.c
 * @brief 故障状态机实现 —— 纯函数，无硬件依赖，可完整单元测试
 */
#include "faults.h"

#include <string.h>

static void set_bit(uint16_t *bitmap, uint16_t bit, bool on)
{
    if (on) {
        *bitmap |= bit;
    } else {
        *bitmap &= (uint16_t)~bit;
    }
}

static int clamp_wheel_count(int count)
{
    if (count < 0) {
        return 0;
    }
    if (count > FAULTS_MAX_WHEELS) {
        return FAULTS_MAX_WHEELS;
    }
    return count;
}

static float absf(float value)
{
    return (value < 0.0f) ? -value : value;
}

void faults_init(FaultMonitor *m, const FaultConfig *cfg)
{
    if (m == 0) {
        return;
    }
    memset(m, 0, sizeof(*m));
    if (cfg != 0) {
        m->cfg = *cfg;
    }
    m->bitmap = FAULT_NONE;
}

uint16_t faults_evaluate_motion(FaultMonitor *m, const FaultMotionInput *in)
{
    if (m == 0) {
        return FAULT_NONE;
    }
    if (in == 0) {
        return m->bitmap;
    }

    /* --- 1. 急停：锁存，且优先级高于一切 ---
       一旦触发就不再评估任何其他运动侧故障 —— 电机已刹停，
       此时的转速读数不携带任何关于底盘健康状况的信息。 */
    if (in->estop_asserted) {
        m->estop_latched = true;
    }
    if (m->estop_latched) {
        m->bitmap |= FAULT_ESTOP;
        return m->bitmap;
    }

    /* --- 2. 过流期间同样停止运动侧评估 ---
       OVERCURRENT 由 faults_evaluate_link() 置位。电机已刹停时
       "轮子不转"不构成堵转证据，强行清零 STALL 反而是误报，
       因此本位保持旧值。这条语义是线上契约的一部分，见 faults.h @warning。 */
    if ((m->bitmap & FAULT_OVERCURRENT) != 0u) {
        return m->bitmap;
    }

    /* --- 3. 指令看门狗 --- */
    set_bit(&m->bitmap, FAULT_CMD_TIMEOUT,
            in->command_age_ms > m->cfg.cmd_timeout_ms);

    /* --- 4. 运动学饱和 --- */
    set_bit(&m->bitmap, FAULT_KINEMATICS_SAT, in->kinematics_saturated);

    /* --- 5. 堵转：给了转速却不转，持续够久才算 --- */
    const int wheels = clamp_wheel_count(in->wheel_count);
    bool any_stalled = false;

    for (int i = 0; i < wheels; i++) {
        const float target = (in->target_rpm != 0) ? in->target_rpm[i] : 0.0f;
        const float actual = (in->actual_rpm != 0) ? in->actual_rpm[i] : 0.0f;

        if (absf(target) > m->cfg.stall_target_rpm &&
            absf(actual) < m->cfg.stall_rpm_floor) {
            m->stall_count[i]++;
        } else {
            m->stall_count[i] = 0u;
        }
        if (m->stall_count[i] >= m->cfg.stall_ticks) {
            any_stalled = true;
        }
    }
    /* 没在评估范围内的轮子不该留着陈旧计数 */
    for (int i = wheels; i < FAULTS_MAX_WHEELS; i++) {
        m->stall_count[i] = 0u;
    }

    /* 跟随实际状态：轮子转起来了就把位清掉，
       否则一次瞬时堵转会让故障灯一直亮到下次复位。 */
    set_bit(&m->bitmap, FAULT_STALL, any_stalled);

    return m->bitmap;
}

uint16_t faults_evaluate_link(FaultMonitor *m, const FaultLinkInput *in)
{
    if (m == 0) {
        return FAULT_NONE;
    }
    if (in == 0) {
        return m->bitmap;
    }

    /* --- 过流：跟随实际电流 --- */
    const int wheels = clamp_wheel_count(in->wheel_count);
    bool overcurrent = false;
    if (in->current_a != 0) {
        for (int i = 0; i < wheels; i++) {
            if (in->current_a[i] > m->cfg.current_limit_a) {
                overcurrent = true;
            }
        }
    }
    set_bit(&m->bitmap, FAULT_OVERCURRENT, overcurrent);

    /* --- 串口链路：增量判定 ---
       首次调用只记基线。否则上电前若已有历史计数 (例如热复位)，
       第一次评估就会误报一次。 */
    if (!m->link_primed) {
        m->link_primed = true;
        m->last_crc_errors = in->crc_errors;
        m->last_rx_overruns = in->rx_overruns;
        m->last_tx_drops = in->tx_drops;
        set_bit(&m->bitmap, FAULT_UART_ERROR, false);
        return m->bitmap;
    }

    const bool degraded = (in->crc_errors != m->last_crc_errors)
                       || (in->rx_overruns != m->last_rx_overruns)
                       || (in->tx_drops != m->last_tx_drops);
    m->last_crc_errors = in->crc_errors;
    m->last_rx_overruns = in->rx_overruns;
    m->last_tx_drops = in->tx_drops;

    set_bit(&m->bitmap, FAULT_UART_ERROR, degraded);

    return m->bitmap;
}

uint16_t faults_get(const FaultMonitor *m)
{
    return (m != 0) ? m->bitmap : FAULT_NONE;
}

bool faults_should_halt(const FaultMonitor *m)
{
    if (m == 0) {
        return true;   /* 拿不到状态就当作要停 —— 失效安全 */
    }
    return (m->bitmap & (uint16_t)(FAULT_ESTOP | FAULT_OVERCURRENT)) != 0u;
}

bool faults_estop_latched(const FaultMonitor *m)
{
    return (m != 0) && m->estop_latched;
}
