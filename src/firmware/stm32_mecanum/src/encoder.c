/**
 * @file encoder.c
 * @brief 编码器测速实现 —— 窗口计数 + 一阶低通（纯逻辑，不碰寄存器）
 *
 * 计数器读取通过 mcu_port.h 的 port_encoder_read_count() 完成，
 * 因此下面三段最容易出错的逻辑可以在 Host 上用假编码器直接测（test_encoder.c）：
 *
 * 1. **计数器回绕**。定时器计数器按 16 位用，从 0xFFFF 走到 0x0000 时
 *    朴素相减会得到 -65535 而不是 +1。做法是先按 uint16 相减（自然取模），
 *    再整体转成 int16 —— 只要单周期位移不超过 ±32767 就恒正确。
 *    本工程最快 330 RPM ⟹ 每 1ms 约 7.3 个计数，余量四千倍。
 *
 * 2. **测速分辨率**。1ms 窗口下一个计数就是 45 RPM 的台阶，PID 会被
 *    量化噪声牵着走。改用 10ms 窗口（4.5 RPM/计数）+ EMA，速度环仍跑 1kHz。
 *
 * 3. **窗口未满时返回什么**。返回 0 会让 PID 每 10ms 看到一次"速度掉到 0"的
 *    假象并猛推积分；返回上一次的值才是对的。
 */
#include "encoder.h"

#include "board_config.h"
#include "mcu_port.h"

/**
 * 一个窗口计数 → RPM 的换算系数（编译期常量）。
 *
 *   RPM = counts / COUNTS_PER_REV / window_seconds × 60
 *   window_seconds = WINDOW_TICKS / CONTROL_FREQ_HZ
 *   ⟹ RPM = counts × (60 × CONTROL_FREQ_HZ) / (COUNTS_PER_REV × WINDOW_TICKS)
 *
 * 本工程：60 × 1000 / (1320 × 10) = 4.5454... RPM/计数
 */
#define RPM_PER_WINDOW_COUNT                                                   \
    ((60.0f * (float)CONTROL_FREQ_HZ) /                                        \
     (ENCODER_COUNTS_PER_REV * (float)ENCODER_SPEED_WINDOW_TICKS))

/** 各轮方向符号。若某轮方向与车体约定相反，改 board_config.h，不要改接线。 */
static const int s_dir_sign[NUM_WHEELS] = {
    ENCODER_DIR_SIGN_FL,
    ENCODER_DIR_SIGN_FR,
    ENCODER_DIR_SIGN_RL,
    ENCODER_DIR_SIGN_RR
};

static uint16_t s_last_count[NUM_WHEELS];    /**< 上一拍的计数器原值 */
static int32_t  s_window_counts[NUM_WHEELS]; /**< 当前窗口内累计位移 */
static int32_t  s_total_counts[NUM_WHEELS];  /**< 上电以来累计位移（里程计） */
static float    s_rpm[NUM_WHEELS];           /**< 滤波后转速 */
static uint32_t s_tick;                      /**< 窗口内已过的控制周期数 */

void encoder_init(void)
{
    port_encoder_init();
    encoder_reset();
}

void encoder_reset(void)
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        s_last_count[i] = port_encoder_read_count(i);
        s_window_counts[i] = 0;
        s_total_counts[i] = 0;
        s_rpm[i] = 0.0f;
    }
    s_tick = 0u;
}

void encoder_update(void)
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        const uint16_t now = port_encoder_read_count(i);
        /* 先按 uint16 取模相减，再转 int16 —— 回绕自动正确 */
        const int16_t delta = (int16_t)(uint16_t)(now - s_last_count[i]);
        s_last_count[i] = now;

        const int32_t signed_delta = (int32_t)delta * s_dir_sign[i];
        s_window_counts[i] += signed_delta;
        s_total_counts[i] += signed_delta;
    }

    s_tick++;
    if (s_tick < ENCODER_SPEED_WINDOW_TICKS) {
        return;   /* 窗口未满：保持上一次的转速估计，不要报 0 */
    }
    s_tick = 0u;

    for (int i = 0; i < NUM_WHEELS; i++) {
        const float raw = (float)s_window_counts[i] * RPM_PER_WINDOW_COUNT;
        s_window_counts[i] = 0;
        /* 一阶低通（EMA）：量化噪声 ±4.5 RPM 直接进 PID 会让占空比抖动 */
        s_rpm[i] += (raw - s_rpm[i]) * ENCODER_RPM_FILTER_ALPHA;
    }
}

float encoder_get_rpm(int wheel)
{
    if (wheel < 0 || wheel >= NUM_WHEELS) {
        return 0.0f;
    }
    return s_rpm[wheel];
}

int32_t encoder_get_total_counts(int wheel)
{
    if (wheel < 0 || wheel >= NUM_WHEELS) {
        return 0;
    }
    return s_total_counts[wheel];
}
