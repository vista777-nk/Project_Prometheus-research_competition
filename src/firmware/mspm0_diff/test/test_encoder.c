/**
 * @file test_encoder.c
 * @brief 编码器测速逻辑的 Host 单元测试
 *
 * 这组用例是 mspm0_port.h 那层间接调用换来的直接收益：麦轮固件把
 * 计数器读取写死在 encoder.c 里，导致回绕处理、测速窗口、EMA 这三段
 * **最容易出错的逻辑**只能上板验证；这里用一个假编码器就能全部测掉。
 *
 * 测试通过重新定义 port_encoder_* 两个原语来驱动被测代码 ——
 * 移植层接口越窄，替身越容易写，这也是当初把它收到十几个函数的原因。
 */
#include "board_config.h"
#include "encoder.h"
#include "kinematics.h"
#include "mspm0_port.h"
#include "unity.h"

/* --- 假编码器：直接操纵计数器原值 --- */

static uint16_t s_fake_count[NUM_WHEELS];

void port_encoder_init(void)
{
    s_fake_count[0] = 0u;
    s_fake_count[1] = 0u;
}

uint16_t port_encoder_read_count(int wheel)
{
    if (wheel < 0 || wheel >= NUM_WHEELS) {
        return 0u;
    }
    return s_fake_count[wheel];
}

/** 让某轮的计数器前进 delta (可为负)，自然按 uint16 回绕 */
static void advance(int wheel, int delta)
{
    s_fake_count[wheel] = (uint16_t)(s_fake_count[wheel] + (uint16_t)delta);
}

/** 走满 n 个测速窗口，每个控制周期给每轮推进 per_tick 个计数 */
static void run_windows(int n, int per_tick_left, int per_tick_right)
{
    for (int w = 0; w < n; w++) {
        for (uint32_t t = 0; t < ENCODER_SPEED_WINDOW_TICKS; t++) {
            advance(0, per_tick_left);
            advance(1, per_tick_right);
            encoder_update();
        }
    }
}

/* 一个窗口 66 个计数 = 300 RPM：
   300 RPM = 5 rev/s × 1320 counts/rev = 6600 counts/s × 0.01s = 66 */
#define COUNTS_FOR_300RPM_PER_WINDOW    66

/* ===================== 用例 ===================== */

/** 初始化后转速为 0，累计计数为 0 */
static void test_starts_at_zero(void)
{
    encoder_init();
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(1));
    TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(0));
    TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(1));
}

/**
 * 窗口未满时必须**保持上一次的转速**，而不是报 0。
 *
 * 报 0 的后果很具体：PID 每 10ms 会看到一次"速度突然掉到 0"的假象，
 * 积分项被猛推一下，输出出现 100Hz 的周期性抖动。
 */
static void test_holds_previous_rpm_until_window_completes(void)
{
    encoder_init();

    /* 先跑满一个窗口，拿到一个非零估计 */
    run_windows(1, COUNTS_FOR_300RPM_PER_WINDOW / 10, 0);
    const float after_first = encoder_get_rpm(0);
    TEST_ASSERT_TRUE_MESSAGE(after_first > 1.0f, "first window must produce an estimate");

    /* 再走窗口长度 - 1 个周期：估计值不得变化 */
    for (uint32_t t = 0; t < ENCODER_SPEED_WINDOW_TICKS - 1u; t++) {
        advance(0, 100);
        encoder_update();
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-6f, after_first, encoder_get_rpm(0),
                                         "rpm must not change mid-window");
    }
}

/** 恒定转速下 EMA 必须收敛到真值 300 RPM */
static void test_converges_to_true_speed(void)
{
    encoder_init();
    run_windows(40, COUNTS_FOR_300RPM_PER_WINDOW / 10, 0);

    /* 每周期 6.6 个计数取整会有误差，所以用窗口整数量重新驱动一次：
       直接每个窗口末尾一次性给满 66 个计数，避免整除截断干扰断言。 */
    encoder_init();
    for (int w = 0; w < 40; w++) {
        for (uint32_t t = 0; t < ENCODER_SPEED_WINDOW_TICKS; t++) {
            if (t == 0u) {
                advance(0, COUNTS_FOR_300RPM_PER_WINDOW);
            }
            encoder_update();
        }
    }
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.5f, 300.0f, encoder_get_rpm(0),
                                     "EMA must converge to the true speed");
}

/** EMA 第一拍的值必须恰好是 alpha × 原始值 —— 把滤波器系数钉死 */
static void test_first_window_applies_ema_alpha(void)
{
    encoder_init();
    for (uint32_t t = 0; t < ENCODER_SPEED_WINDOW_TICKS; t++) {
        if (t == 0u) {
            advance(0, COUNTS_FOR_300RPM_PER_WINDOW);
        }
        encoder_update();
    }
    const float expect = 300.0f * ENCODER_RPM_FILTER_ALPHA;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, expect, encoder_get_rpm(0));
}

/**
 * 计数器 16 位回绕必须被正确处理。
 *
 * 从 0xFFF0 走到 0x0005 是 +21，朴素相减会得到 -65515 ——
 * 那会让 PID 瞬间看到一个荒谬的反向转速并猛打方向。
 */
static void test_handles_counter_wraparound(void)
{
    encoder_init();
    s_fake_count[0] = 0xFFF0u;
    encoder_reset();               /* 让 s_last_count 对齐到 0xFFF0 */

    advance(0, 21);                /* 0xFFF0 + 21 = 0x0005，跨越回绕点 */
    encoder_update();

    TEST_ASSERT_EQUAL_INT_MESSAGE(21, encoder_get_total_counts(0),
                                  "wraparound must be read as +21, not -65515");
}

/** 反向回绕：0x0005 → 0xFFF0 应当是 -21 */
static void test_handles_reverse_wraparound(void)
{
    encoder_init();
    s_fake_count[0] = 0x0005u;
    encoder_reset();

    advance(0, -21);
    encoder_update();

    TEST_ASSERT_EQUAL_INT(-21, encoder_get_total_counts(0));
}

/**
 * 方向符号必须生效。右轮的 ENCODER_DIR_SIGN_RIGHT 是 -1
 * (两侧电机在车上是镜像安装的，同向前进时编码器计数方向相反)。
 */
static void test_direction_sign_is_applied(void)
{
    encoder_init();
    advance(1, 100);
    encoder_update();

    const int32_t total = encoder_get_total_counts(1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(100 * ENCODER_DIR_SIGN_RIGHT, total,
                                  "right wheel must honour its direction sign");
}

/** 倒转时转速为负 */
static void test_reverse_rotation_yields_negative_rpm(void)
{
    encoder_init();
    for (int w = 0; w < 40; w++) {
        for (uint32_t t = 0; t < ENCODER_SPEED_WINDOW_TICKS; t++) {
            if (t == 0u) {
                advance(0, -COUNTS_FOR_300RPM_PER_WINDOW);
            }
            encoder_update();
        }
    }
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -300.0f, encoder_get_rpm(0));
}

/** 两轮相互独立，不得串扰 */
static void test_wheels_are_independent(void)
{
    encoder_init();
    for (int w = 0; w < 40; w++) {
        for (uint32_t t = 0; t < ENCODER_SPEED_WINDOW_TICKS; t++) {
            if (t == 0u) {
                advance(0, COUNTS_FOR_300RPM_PER_WINDOW);
            }
            encoder_update();
        }
    }
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 300.0f, encoder_get_rpm(0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(1));
}

/** reset 必须清掉转速与里程，且不把回绕基准弄错 */
static void test_reset_clears_state(void)
{
    encoder_init();
    run_windows(5, 5, 5);
    TEST_ASSERT_TRUE(encoder_get_total_counts(0) != 0);

    encoder_reset();
    TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(0));
    TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(1));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(0));

    /* reset 之后不应把"复位前后的计数差"算成一次巨大位移 */
    encoder_update();
    TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(0));
}

/** 越界索引不得崩溃 */
static void test_out_of_range_wheel_is_safe(void)
{
    encoder_init();
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(-1));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(NUM_WHEELS));
    TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(-1));
    TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(NUM_WHEELS));
}

void run_encoder_tests(void)
{
    UNITY_SET_FILE();

    RUN_TEST(test_starts_at_zero);
    RUN_TEST(test_holds_previous_rpm_until_window_completes);
    RUN_TEST(test_converges_to_true_speed);
    RUN_TEST(test_first_window_applies_ema_alpha);
    RUN_TEST(test_handles_counter_wraparound);
    RUN_TEST(test_handles_reverse_wraparound);
    RUN_TEST(test_direction_sign_is_applied);
    RUN_TEST(test_reverse_rotation_yields_negative_rpm);
    RUN_TEST(test_wheels_are_independent);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_out_of_range_wheel_is_safe);
}
