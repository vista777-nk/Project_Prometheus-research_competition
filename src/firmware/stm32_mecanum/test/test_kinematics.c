/**
 * @file test_kinematics.c
 * @brief 麦轮逆 / 正运动学单元测试 (task-10 §10.6 最低测试覆盖 + 扩展)
 *
 * 参照物是仿真侧 mecanum_controller.py。任何一处符号约定改动，
 * 这组用例都应该同时红掉——这正是它存在的目的。
 */
#include <math.h>

#include "kinematics.h"
#include "unity.h"

/* 测试几何：与 chassis_params.yaml : mecanum_chassis 一致
   wheel_base=0.20 → lx=0.10 ; track_width=0.18 → ly=0.09 */
#define TEST_R          0.033f
#define TEST_LX         0.10f
#define TEST_LY         0.09f
#define TEST_MAX_RPM    330.0f
#define TEST_LEVER      (TEST_LX + TEST_LY)     /* 0.19 */

#define RADPS_TO_RPM    9.549296585513720f
#define EPS             1e-3f

static void init_default_geometry(void)
{
    const MecanumGeometry geo = { TEST_R, TEST_LX, TEST_LY, TEST_MAX_RPM };
    TEST_ASSERT_EQUAL_INT(KIN_OK, kinematics_init(&geo));
}

/** 期望轮速 (RPM)：由线速度分量直接换算 */
static float expected_rpm(float linear_component)
{
    return linear_component / TEST_R * RADPS_TO_RPM;
}

/* --- §10.6 要求的 5 个用例 --- */

/** vx=0.5, vy=0, ω=0 → 四轮均正转且 RPM 相等 */
static void test_forward(void)
{
    init_default_geometry();
    const RobotVelocity cmd = { 0.5f, 0.0f, 0.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&cmd, rpm));

    const float want = expected_rpm(0.5f);   /* ≈ 144.69 RPM */
    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_TRUE_MESSAGE(rpm[i] > 0.0f, "forward: every wheel must spin forward");
        TEST_ASSERT_FLOAT_WITHIN(EPS, want, rpm[i]);
    }
}

/** vx=0, vy=0.5 (向左平移), ω=0 → 左前 + 右后 反转，右前 + 左后 正转 */
static void test_strafe_left(void)
{
    init_default_geometry();
    const RobotVelocity cmd = { 0.0f, 0.5f, 0.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&cmd, rpm));

    const float want = expected_rpm(0.5f);
    TEST_ASSERT_FLOAT_WITHIN(EPS, -want, rpm[WHEEL_FRONT_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, +want, rpm[WHEEL_FRONT_RIGHT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, +want, rpm[WHEEL_REAR_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, -want, rpm[WHEEL_REAR_RIGHT]);
}

/**
 * ω=+1.0 rad/s → 右手系下为逆时针 (CCW)：左侧两轮反转、右侧两轮正转。
 *
 * task-10 §10.6 的表格把该用例写作 "test_rotate_cw"，但同时给出 ω=+1.0；
 * 二者不自洽 —— ω 为正在 REP-103 右手系里就是逆时针。这里以数学与仿真实现为准，
 * 顺时针的情形由下一个用例 test_rotate_cw 覆盖。
 */
static void test_rotate_ccw(void)
{
    init_default_geometry();
    const RobotVelocity cmd = { 0.0f, 0.0f, 1.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&cmd, rpm));

    const float want = expected_rpm(TEST_LEVER * 1.0f);   /* ≈ 54.98 RPM */
    TEST_ASSERT_FLOAT_WITHIN(EPS, -want, rpm[WHEEL_FRONT_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, +want, rpm[WHEEL_FRONT_RIGHT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, -want, rpm[WHEEL_REAR_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, +want, rpm[WHEEL_REAR_RIGHT]);
}

/** ω=-1.0 rad/s → 顺时针：左侧正转、右侧反转，幅值与 CCW 相同 */
static void test_rotate_cw(void)
{
    init_default_geometry();
    const RobotVelocity cmd = { 0.0f, 0.0f, -1.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&cmd, rpm));

    const float want = expected_rpm(TEST_LEVER * 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(EPS, +want, rpm[WHEEL_FRONT_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, -want, rpm[WHEEL_FRONT_RIGHT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, +want, rpm[WHEEL_REAR_LEFT]);
    TEST_ASSERT_FLOAT_WITHIN(EPS, -want, rpm[WHEEL_REAR_RIGHT]);
}

/** 零指令 → 四轮 RPM 全零 */
static void test_zero_input(void)
{
    init_default_geometry();
    const RobotVelocity cmd = { 0.0f, 0.0f, 0.0f };
    float rpm[NUM_WHEELS] = { 1.0f, 2.0f, 3.0f, 4.0f };  /* 故意预置脏值 */

    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&cmd, rpm));
    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[i]);
    }
}

/** 超速指令 → 返回 KIN_SATURATED，峰值轮恰好落在 max_rpm 上 */
static void test_saturation(void)
{
    init_default_geometry();
    /* vx = 2.0 m/s 对应约 578 RPM，远超 330 RPM 上限 */
    const RobotVelocity cmd = { 2.0f, 0.0f, 0.0f };
    float rpm[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_SATURATED, inverse_kinematics(&cmd, rpm));
    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(EPS, TEST_MAX_RPM, rpm[i]);
    }
}

/* --- 扩展用例 --- */

/**
 * 限幅采用等比缩放而非逐轮硬钳位：缩放后各轮转速的比例关系必须不变，
 * 否则实际运动方向会偏离指令方向。这是本实现与"逐轮 clamp"的关键分野。
 */
static void test_saturation_preserves_direction(void)
{
    init_default_geometry();
    const RobotVelocity cmd = { 2.0f, 1.0f, 3.0f };
    float scaled[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_SATURATED, inverse_kinematics(&cmd, scaled));

    /* 同方向的 1/10 缩小指令不会触发限幅，可作为"未失真"的参照 */
    const RobotVelocity small = { 0.2f, 0.1f, 0.3f };
    float reference[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&small, reference));

    float peak = 0.0f;
    for (int i = 0; i < NUM_WHEELS; i++) {
        if (fabsf(scaled[i]) > peak) {
            peak = fabsf(scaled[i]);
        }
    }
    TEST_ASSERT_FLOAT_WITHIN(EPS, TEST_MAX_RPM, peak);

    /* scaled 与 reference 必须共线：两两比值相同 */
    const float ratio = scaled[0] / reference[0];
    for (int i = 1; i < NUM_WHEELS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-2f, ratio, scaled[i] / reference[i]);
    }
}

/**
 * 限幅门限的两侧行为：略低于 max_rpm 不报超限，略高于就必须报。
 *
 * 不去测"恰好等于 max_rpm"这一点 —— 单精度浮点在 vx → rpm 的往返换算里
 * 会有末位误差，落在门限哪一侧属于实现细节，测它只会得到一个脆弱的用例。
 */
static void test_saturation_threshold(void)
{
    init_default_geometry();
    const float vx_at_limit = TEST_MAX_RPM / RADPS_TO_RPM * TEST_R;
    float rpm[NUM_WHEELS];

    const RobotVelocity below = { vx_at_limit * 0.999f, 0.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&below, rpm));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, TEST_MAX_RPM, rpm[0]);

    const RobotVelocity above = { vx_at_limit * 1.001f, 0.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(KIN_SATURATED, inverse_kinematics(&above, rpm));
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, TEST_MAX_RPM, rpm[0]);
}

/** 逆解 → 正解往返：未限幅时应还原出原始速度指令 */
static void test_inverse_forward_round_trip(void)
{
    init_default_geometry();
    const RobotVelocity cmd = { 0.30f, -0.20f, 0.80f };
    float rpm[NUM_WHEELS];
    RobotVelocity recovered;

    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&cmd, rpm));
    TEST_ASSERT_EQUAL_INT(KIN_OK, forward_kinematics(rpm, &recovered));

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, cmd.vx, recovered.vx);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, cmd.vy, recovered.vy);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, cmd.omega, recovered.omega);
}

/** 复合运动：前进 + 左移 应等于两者单独解算之和 (线性叠加) */
static void test_superposition(void)
{
    init_default_geometry();
    const RobotVelocity forward = { 0.30f, 0.0f, 0.0f };
    const RobotVelocity strafe  = { 0.0f, 0.25f, 0.0f };
    const RobotVelocity both    = { 0.30f, 0.25f, 0.0f };
    float a[NUM_WHEELS], b[NUM_WHEELS], c[NUM_WHEELS];

    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&forward, a));
    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&strafe, b));
    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&both, c));

    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(EPS, a[i] + b[i], c[i]);
    }
}

/** 非法入参：NULL / NaN / Inf 一律返回 KIN_INVALID，且输出被清零 */
static void test_invalid_inputs_are_rejected(void)
{
    init_default_geometry();
    float rpm[NUM_WHEELS] = { 9.0f, 9.0f, 9.0f, 9.0f };

    TEST_ASSERT_EQUAL_INT(KIN_INVALID, inverse_kinematics(NULL, rpm));
    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[i]);
    }

    const RobotVelocity nan_cmd = { NAN, 0.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, inverse_kinematics(&nan_cmd, rpm));

    const RobotVelocity inf_cmd = { 0.0f, INFINITY, 0.0f };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, inverse_kinematics(&inf_cmd, rpm));
    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rpm[i]);
    }

    const RobotVelocity ok_cmd = { 0.1f, 0.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, inverse_kinematics(&ok_cmd, NULL));
}

/** 非法几何 (零轮径 / 负轮距) 必须被拒，并保留上一次的有效配置 */
static void test_invalid_geometry_is_rejected(void)
{
    init_default_geometry();

    const MecanumGeometry zero_radius = { 0.0f, TEST_LX, TEST_LY, TEST_MAX_RPM };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, kinematics_init(&zero_radius));

    const MecanumGeometry negative_lever = { TEST_R, -0.1f, TEST_LY, TEST_MAX_RPM };
    TEST_ASSERT_EQUAL_INT(KIN_INVALID, kinematics_init(&negative_lever));

    TEST_ASSERT_EQUAL_INT(KIN_INVALID, kinematics_init(NULL));

    /* 上一次的有效几何仍在生效 */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, TEST_R, kinematics_get_geometry()->wheel_radius);

    const RobotVelocity cmd = { 0.5f, 0.0f, 0.0f };
    float rpm[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_OK, inverse_kinematics(&cmd, rpm));
    TEST_ASSERT_FLOAT_WITHIN(EPS, expected_rpm(0.5f), rpm[0]);
}

void run_kinematics_tests(void)
{
    UNITY_SET_FILE();
    RUN_TEST(test_forward);
    RUN_TEST(test_strafe_left);
    RUN_TEST(test_rotate_ccw);
    RUN_TEST(test_rotate_cw);
    RUN_TEST(test_zero_input);
    RUN_TEST(test_saturation);
    RUN_TEST(test_saturation_preserves_direction);
    RUN_TEST(test_saturation_threshold);
    RUN_TEST(test_inverse_forward_round_trip);
    RUN_TEST(test_superposition);
    RUN_TEST(test_invalid_inputs_are_rejected);
    RUN_TEST(test_invalid_geometry_is_rejected);
}
