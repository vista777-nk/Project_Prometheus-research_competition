/**
 * @file test_encoder.c
 * @brief 编码器测速逻辑的 Host 单元测试（四轮）
 *
 * 这组用例是移植层分离（common/mcu_port.h）换来的直接收益。重构之前，
 * `encoder.c` 直接读 `TIM2->CNT`，导致回绕处理、测速窗口、EMA 这三段
 * **最容易出错的逻辑**只能上板验证 —— 52 个用例里没有一个能覆盖
 * "计数器从 0xFFF0 走到 0x0005 到底被算成 +21 还是 -65515"。
 *
 * 测试通过重新定义 port_encoder_* 两个原语来驱动被测代码 ——
 * 移植层接口越窄，替身越容易写，这也是当初把它收到十几个函数的原因。
 */
#include "board_config.h"
#include "encoder.h"
#include "kinematics.h"
#include "mcu_port.h"
#include "unity.h"

/* --- 假编码器：直接操纵计数器原值 --- */

static uint16_t s_fake_count[NUM_WHEELS];

void port_encoder_init(void)
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        s_fake_count[i] = 0u;
    }
}

uint16_t port_encoder_read_count(int wheel)
{
    if (wheel < 0 || wheel >= NUM_WHEELS) {
        return 0u;
    }
    return s_fake_count[wheel];
}

/** 各轮方向符号，与 board_config.h 保持一致 */
static const int s_expected_sign[NUM_WHEELS] = {
    ENCODER_DIR_SIGN_FL,
    ENCODER_DIR_SIGN_FR,
    ENCODER_DIR_SIGN_RL,
    ENCODER_DIR_SIGN_RR
};

/** 让某轮的计数器前进 delta（可为负），自然按 uint16 回绕 */
static void advance(int wheel, int delta)
{
    s_fake_count[wheel] = (uint16_t)(s_fake_count[wheel] + (uint16_t)delta);
}

/**
 * 跑满 n 个测速窗口，每个窗口开头给指定轮一次性推进 counts 个计数。
 * 一次性给满而不是逐拍均摊，是为了避免整除截断干扰断言。
 */
static void run_windows_on(int wheel, int n, int counts)
{
    for (int w = 0; w < n; w++) {
        for (uint32_t t = 0; t < ENCODER_SPEED_WINDOW_TICKS; t++) {
            if (t == 0u && wheel >= 0) {
                advance(wheel, counts);
            }
            encoder_update();
        }
    }
}

/* 一个窗口 78 个计数 = 300 RPM：
   300 RPM = 5 rev/s × 1560 counts/rev = 7800 counts/s × 0.01s = 78 */
#define COUNTS_FOR_300RPM_PER_WINDOW    78

/* ===================== 用例 ===================== */

/** 初始化后四轮转速与里程均为 0 */
static void test_starts_at_zero(void)
{
    encoder_init();
    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(i));
        TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(i));
    }
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
    run_windows_on(WHEEL_FRONT_LEFT, 1, COUNTS_FOR_300RPM_PER_WINDOW);

    const float after_first = encoder_get_rpm(WHEEL_FRONT_LEFT);
    TEST_ASSERT_TRUE_MESSAGE(after_first > 1.0f, "first window must produce an estimate");

    for (uint32_t t = 0; t < ENCODER_SPEED_WINDOW_TICKS - 1u; t++) {
        advance(WHEEL_FRONT_LEFT, 100);
        encoder_update();
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-6f, after_first,
                                         encoder_get_rpm(WHEEL_FRONT_LEFT),
                                         "rpm must not change mid-window");
    }
}

/** 恒定转速下 EMA 必须收敛到真值 300 RPM */
static void test_converges_to_true_speed(void)
{
    encoder_init();
    run_windows_on(WHEEL_FRONT_LEFT, 40, COUNTS_FOR_300RPM_PER_WINDOW);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.5f, 300.0f, encoder_get_rpm(WHEEL_FRONT_LEFT),
                                     "EMA must converge to the true speed");
}

/** EMA 第一拍的值必须恰好是 alpha × 原始值 —— 把滤波器系数钉死 */
static void test_first_window_applies_ema_alpha(void)
{
    encoder_init();
    run_windows_on(WHEEL_FRONT_LEFT, 1, COUNTS_FOR_300RPM_PER_WINDOW);
    const float expect = 300.0f * ENCODER_RPM_FILTER_ALPHA;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, expect, encoder_get_rpm(WHEEL_FRONT_LEFT));
}

/**
 * 计数器 16 位回绕必须被正确处理。
 *
 * 从 0xFFF0 走到 0x0005 是 +21，朴素相减会得到 -65515 ——
 * 那会让 PID 瞬间看到一个荒谬的反向转速并猛打方向。
 * TIM2/TIM5 虽是 32 位计数器，但 ARR 被设成 0xFFFF 当 16 位用，
 * 因此四个轮子的回绕语义一致。
 */
static void test_handles_counter_wraparound(void)
{
    encoder_init();
    s_fake_count[WHEEL_FRONT_LEFT] = 0xFFF0u;
    encoder_reset();               /* 让 s_last_count 对齐到 0xFFF0 */

    advance(WHEEL_FRONT_LEFT, 21); /* 0xFFF0 + 21 = 0x0005，跨越回绕点 */
    encoder_update();

    const int32_t expect = 21 * s_expected_sign[WHEEL_FRONT_LEFT];
    TEST_ASSERT_EQUAL_INT_MESSAGE(expect, encoder_get_total_counts(WHEEL_FRONT_LEFT),
                                  "wraparound must be read as +21, not -65515");
}

/** 反向回绕：0x0005 → 0xFFF0 应当是 -21 */
static void test_handles_reverse_wraparound(void)
{
    encoder_init();
    s_fake_count[WHEEL_FRONT_LEFT] = 0x0005u;
    encoder_reset();

    advance(WHEEL_FRONT_LEFT, -21);
    encoder_update();

    const int32_t expect = -21 * s_expected_sign[WHEEL_FRONT_LEFT];
    TEST_ASSERT_EQUAL_INT(expect, encoder_get_total_counts(WHEEL_FRONT_LEFT));
}

/**
 * 四个轮子的方向符号必须各自生效。
 * 右侧两轮在车上是镜像安装的，同向前进时编码器计数方向与左侧相反。
 */
static void test_direction_signs_are_applied_per_wheel(void)
{
    encoder_init();
    for (int i = 0; i < NUM_WHEELS; i++) {
        advance(i, 100);
    }
    encoder_update();

    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(100 * s_expected_sign[i],
                                      encoder_get_total_counts(i),
                                      "each wheel must honour its own direction sign");
    }
}

/** 倒转时转速为负（按各轮符号折算后） */
static void test_reverse_rotation_yields_negative_rpm(void)
{
    encoder_init();
    run_windows_on(WHEEL_FRONT_LEFT, 40, -COUNTS_FOR_300RPM_PER_WINDOW);
    const float expect = -300.0f * (float)s_expected_sign[WHEEL_FRONT_LEFT];
    TEST_ASSERT_FLOAT_WITHIN(0.5f, expect, encoder_get_rpm(WHEEL_FRONT_LEFT));
}

/** 四轮相互独立，不得串扰 —— 麦轮底盘四轮转速各不相同，串扰会毁掉整个解算 */
static void test_wheels_are_independent(void)
{
    encoder_init();
    run_windows_on(WHEEL_REAR_RIGHT, 40, COUNTS_FOR_300RPM_PER_WINDOW);

    const float expect = 300.0f * (float)s_expected_sign[WHEEL_REAR_RIGHT];
    TEST_ASSERT_FLOAT_WITHIN(0.5f, expect, encoder_get_rpm(WHEEL_REAR_RIGHT));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(WHEEL_FRONT_LEFT));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(WHEEL_FRONT_RIGHT));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(WHEEL_REAR_LEFT));
}

/** reset 必须清掉转速与里程，且不把复位前后的计数差算成一次巨大位移 */
static void test_reset_clears_state(void)
{
    encoder_init();
    run_windows_on(WHEEL_FRONT_LEFT, 5, COUNTS_FOR_300RPM_PER_WINDOW);
    TEST_ASSERT_TRUE(encoder_get_total_counts(WHEEL_FRONT_LEFT) != 0);

    encoder_reset();
    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(i));
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, encoder_get_rpm(i));
    }

    encoder_update();
    TEST_ASSERT_EQUAL_INT(0, encoder_get_total_counts(WHEEL_FRONT_LEFT));
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
    RUN_TEST(test_direction_signs_are_applied_per_wheel);
    RUN_TEST(test_reverse_rotation_yields_negative_rpm);
    RUN_TEST(test_wheels_are_independent);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_out_of_range_wheel_is_safe);
}
