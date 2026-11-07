/**
 * @file test_kinematics.c
 * @brief 差速逆 / 正运动学单元测试 (task-11 §11.6 最低测试覆盖 + 扩展)
 *
 * 参照物是仿真侧的 diff_drive_controller 与 chassis_params.yaml。
 * 任何一处符号约定改动，这组用例都应该同时红掉 —— 这正是它存在的目的。
 */
#include <math.h>

#include "kinematics.h"
#include "unity.h"

/* 测试几何：与 chassis_params.yaml : diff_chassis 一致 */
#define TEST_R          0.031f
#define TEST_TRACK      0.166f
#define TEST_MAX_RPM    360.0f
#define TEST_HALF_TRACK (TEST_TRACK * 0.5f)     /* 0.083 */

#define RADPS_TO_RPM    9.549296585513720f
#define EPS             1e-3f

/** 满转速对应的直线速度 (m/s)：360 RPM → 1.1687 m/s */
#define MAX_LINEAR_MPS  (TEST_MAX_RPM / RADPS_TO_RPM * TEST_R)

static void init_default_geometry(void)
{
    const DiffGeometry geo = { TEST_R, TEST_TRACK, TEST_MAX_RPM };
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_kinematics_init(&geo));
}

/** 期望轮速 (RPM)：由轮缘线速度直接换算 */
static float expected_rpm(float wheel_linear_mps)
{
    return wheel_linear_mps / TEST_R * RADPS_TO_RPM;
}

/* ===================== §11.6 要求的 6 个用例 ===================== */

/** v=0.5, ω=0 → 双轮均正转且 RPM 相等 */
static void test_straight_forward(void)
{
    init_default_geometry();
    const DiffVelocity cmd = { 0.5f, 0.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, rpm));

    const float want = expected_rpm(0.5f);   /* ≈ 154.02 RPM */
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_LEFT] > 0.0f, "forward: left wheel must spin forward");
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_RIGHT] > 0.0f, "forward: right wheel must spin forward");
    TEST_ASSERT_FLOAT_WITHIN(EPS, want, rpm[WHEEL_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, want, rpm[WHEEL_RIGHT]);
}

/** v=-0.5, ω=0 → 双轮均反转且 RPM 相等 */
static void test_straight_backward(void)
{
    init_default_geometry();
    const DiffVelocity cmd = { -0.5f, 0.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, rpm));

    const float want = expected_rpm(-0.5f);
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_LEFT] < 0.0f, "backward: left wheel must reverse");
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_RIGHT] < 0.0f, "backward: right wheel must reverse");
    TEST_ASSERT_FLOAT_WITHIN(EPS, want, rpm[WHEEL_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, want, rpm[WHEEL_RIGHT]);
}

/**
 * v=0, ω=+1.0 → 原地**逆时针 (左转)**：左轮倒转、右轮正转，两者等幅。
 *
 * ⚠ task-11 §11.6 把这个输入命名为 `test_rotate_in_place_cw`(顺时针)，但期望输出
 *   写的是 "RPM_left < 0, RPM_right > 0" —— 那恰恰是逆时针。右手系下 ω>0 是逆时针，
 *   所以文档的**命名**错了、**期望值**是对的。这里按数学与 REP-103 命名，
 *   并额外补一个真正的顺时针用例。task-10 的 §10.6 有同一处笔误。
 */
static void test_rotate_in_place_ccw(void)
{
    init_default_geometry();
    const DiffVelocity cmd = { 0.0f, 1.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, rpm));

    const float want = expected_rpm(TEST_HALF_TRACK * 1.0f);   /* ≈ 25.57 RPM */
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_LEFT] < 0.0f, "ccw: left wheel must reverse");
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_RIGHT] > 0.0f, "ccw: right wheel must go forward");
    TEST_ASSERT_FLOAT_WITHIN(EPS, -want, rpm[WHEEL_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, want, rpm[WHEEL_RIGHT]);
    /* 等幅是"原地"旋转的定义：线速度分量必须恰好抵消 */
    TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[WHEEL_LEFT] + rpm[WHEEL_RIGHT]);
}

/** v=0, ω=-1.0 → 原地顺时针：左轮正转、右轮倒转 */
static void test_rotate_in_place_cw(void)
{
    init_default_geometry();
    const DiffVelocity cmd = { 0.0f, -1.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, rpm));

    const float want = expected_rpm(TEST_HALF_TRACK * 1.0f);
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_LEFT] > 0.0f, "cw: left wheel must go forward");
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_RIGHT] < 0.0f, "cw: right wheel must reverse");
    TEST_ASSERT_FLOAT_WITHIN(EPS, want, rpm[WHEEL_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, -want, rpm[WHEEL_RIGHT]);
}

/** v=0.5, ω=0.5 → 左转弧线：右轮 > 左轮 > 0 */
static void test_curve_forward(void)
{
    init_default_geometry();
    const DiffVelocity cmd = { 0.5f, 0.5f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, rpm));

    const float want_left  = expected_rpm(0.5f - TEST_HALF_TRACK * 0.5f);
    const float want_right = expected_rpm(0.5f + TEST_HALF_TRACK * 0.5f);

    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_RIGHT] > rpm[WHEEL_LEFT],
                             "ccw curve: right wheel must lead");
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_LEFT] > 0.0f, "gentle curve: both wheels forward");
    TEST_ASSERT_FLOAT_WITHIN(EPS, want_left, rpm[WHEEL_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, want_right, rpm[WHEEL_RIGHT]);
}

/** (0, 0) → 双轮 RPM = 0 */
static void test_zero_input(void)
{
    init_default_geometry();
    const DiffVelocity cmd = { 0.0f, 0.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, rpm));
    TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[WHEEL_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[WHEEL_RIGHT]);
}

/** v=5.0 (远超能力) → 报 SATURATED，双轮都被压到 max_rpm */
static void test_saturation(void)
{
    init_default_geometry();
    const DiffVelocity cmd = { 5.0f, 0.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_SATURATED, diff_inverse_kinematics(&cmd, rpm));
    TEST_ASSERT_FLOAT_WITHIN(EPS, TEST_MAX_RPM, rpm[WHEEL_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, TEST_MAX_RPM, rpm[WHEEL_RIGHT]);
}

/* ===================== 扩展用例 ===================== */

/**
 * 饱和时**曲率必须保持不变** —— 这是等比缩放区别于逐轮硬钳位的唯一可观测证据。
 *
 * 直线饱和 (test_saturation) 两种策略结果完全相同，验不出区别；
 * 必须用一个 v 和 ω 都非零、且只有一侧轮超限的指令才能把它们区分开。
 *
 * v=1.0, ω=3.0 → 右轮超过 360 RPM，左轮仍在范围内。
 * 逐轮钳位只会砍右轮 → 曲率被掰弯；等比缩放两轮同砍 → 弧线不变，只是慢了。
 */
static void test_saturation_preserves_curvature(void)
{
    init_default_geometry();
    const DiffVelocity cmd = { 1.0f, 3.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_SATURATED, diff_inverse_kinematics(&cmd, rpm));

    /* 缩放后必须恰好顶到限值，不多不少 */
    TEST_ASSERT_FLOAT_WITHIN(EPS, TEST_MAX_RPM, rpm[WHEEL_RIGHT]);
    TEST_ASSERT_TRUE_MESSAGE(rpm[WHEEL_LEFT] < rpm[WHEEL_RIGHT], "left must stay slower");

    /* 正解回车体速度，检查曲率 κ = ω/v 与指令一致 */
    DiffVelocity actual;
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_forward_kinematics(rpm, &actual));

    const float commanded_curvature = cmd.omega / cmd.v;      /* 3.0 */
    const float actual_curvature = actual.omega / actual.v;
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-3f, commanded_curvature, actual_curvature,
                                     "proportional scaling must preserve path curvature");

    /* 而且确实是"降速"而不是"变向"：缩放系数 < 1 且 > 0 */
    TEST_ASSERT_TRUE_MESSAGE(actual.v > 0.0f && actual.v < cmd.v,
                             "saturated command must slow down, not reverse");
}

/**
 * 饱和阈值：恰好在 max_rpm 边界上的用例是脆的 (单精度往返误差)，
 * 因此测 0.999× 不报饱和、1.001× 报饱和。
 */
static void test_saturation_threshold(void)
{
    init_default_geometry();
    float rpm[NUM_WHEELS];

    const DiffVelocity below = { MAX_LINEAR_MPS * 0.999f, 0.0f };
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&below, rpm));

    const DiffVelocity above = { MAX_LINEAR_MPS * 1.001f, 0.0f };
    TEST_ASSERT_EQUAL_INT(KIN_SATURATED, diff_inverse_kinematics(&above, rpm));
}

/** 逆解 → 正解 往返应还原原指令 (未饱和区) */
static void test_inverse_forward_roundtrip(void)
{
    init_default_geometry();

    const DiffVelocity cases[4] = {
        { 0.30f,  0.0f },
        { 0.0f,   1.20f },
        { 0.45f, -0.80f },
        { -0.25f, 0.60f }
    };

    for (int i = 0; i < 4; i++) {
        float rpm[NUM_WHEELS];
        TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cases[i], rpm));

        DiffVelocity back;
        TEST_ASSERT_EQUAL_INT(KIN_OK, diff_forward_kinematics(rpm, &back));
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, cases[i].v, back.v);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, cases[i].omega, back.omega);
    }
}

/** 非有限输入必须被拒绝，且输出已清零 (不能留上一次的值给控制中断用) */
static void test_rejects_non_finite_input(void)
{
    init_default_geometry();
    float rpm[NUM_WHEELS] = { 123.0f, 456.0f };

    const DiffVelocity nan_cmd = { NAN, 0.0f };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_inverse_kinematics(&nan_cmd, rpm));
    TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[WHEEL_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[WHEEL_RIGHT]);

    rpm[0] = 123.0f;
    rpm[1] = 456.0f;
    const DiffVelocity inf_cmd = { 0.0f, INFINITY };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_inverse_kinematics(&inf_cmd, rpm));
    TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[WHEEL_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[WHEEL_RIGHT]);
}

/** NULL 入参不得崩溃 */
static void test_rejects_null_arguments(void)
{
    init_default_geometry();
    float rpm[NUM_WHEELS] = { 0.0f, 0.0f };
    const DiffVelocity cmd = { 0.5f, 0.0f };
    DiffVelocity out;

    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_inverse_kinematics(&cmd, 0));
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_inverse_kinematics(0, rpm));
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_forward_kinematics(rpm, 0));
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_forward_kinematics(0, &out));
}

/** 非法几何必须被拒绝，且**保留上一次的有效配置**而不是把自己搞成除零状态 */
static void test_rejects_invalid_geometry(void)
{
    init_default_geometry();

    const DiffGeometry zero_radius = { 0.0f, TEST_TRACK, TEST_MAX_RPM };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_kinematics_init(&zero_radius));

    const DiffGeometry negative_track = { TEST_R, -0.166f, TEST_MAX_RPM };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_kinematics_init(&negative_track));

    const DiffGeometry nan_max = { TEST_R, TEST_TRACK, NAN };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_kinematics_init(&nan_max));

    TEST_ASSERT_EQUAL_INT(KIN_INVALID, diff_kinematics_init(0));

    /* 上面四次失败都不应破坏已生效的几何 */
    const DiffGeometry *geo = diff_kinematics_get_geometry();
    TEST_ASSERT_NOT_NULL(geo);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, TEST_R, geo->wheel_radius);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, TEST_TRACK, geo->track);

    const DiffVelocity cmd = { 0.5f, 0.0f };
    float rpm[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, rpm));
}

/**
 * 换几何参数后解算必须跟着变 —— 防止有人把 s_geo 缓存成编译期常量。
 * 轮距加倍 ⟹ 同样的 ω 需要加倍的轮速差。
 */
static void test_geometry_change_takes_effect(void)
{
    init_default_geometry();
    const DiffVelocity cmd = { 0.0f, 1.0f };
    float narrow[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, narrow));

    const DiffGeometry wide = { TEST_R, TEST_TRACK * 2.0f, TEST_MAX_RPM };
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_kinematics_init(&wide));

    float spread[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, spread));
    TEST_ASSERT_FLOAT_WITHIN(EPS, narrow[WHEEL_RIGHT] * 2.0f, spread[WHEEL_RIGHT]);

    init_default_geometry();   /* 复原，避免污染后续用例 */
}

void run_kinematics_tests(void)
{
    UNITY_SET_FILE();

    RUN_TEST(test_straight_forward);
    RUN_TEST(test_straight_backward);
    RUN_TEST(test_rotate_in_place_ccw);
    RUN_TEST(test_rotate_in_place_cw);
    RUN_TEST(test_curve_forward);
    RUN_TEST(test_zero_input);
    RUN_TEST(test_saturation);

    RUN_TEST(test_saturation_preserves_curvature);
    RUN_TEST(test_saturation_threshold);
    RUN_TEST(test_inverse_forward_roundtrip);
    RUN_TEST(test_rejects_non_finite_input);
    RUN_TEST(test_rejects_null_arguments);
    RUN_TEST(test_rejects_invalid_geometry);
    RUN_TEST(test_geometry_change_takes_effect);
}
