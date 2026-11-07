/**
 * @file test_control_loop.c
 * @brief 差速底盘**整链闭环**测试：指令 → 逆解 → 双路 PID → 电机模型 → 正解
 *
 * 为什么这里不重复 task-10 的 PID 单元测试：
 *   `common/pid.c` 的单体行为 (阶跃响应、抗饱和、条件积分、NULL 防护) 已由
 *   task-10 的 test_pid.c 测过，两块板子共用同一份实现，再抄一遍只是让
 *   同一个 bug 在两处同时红，不增加任何信息。
 *
 * 本文件测的是 task-11 特有、且 task-10 覆盖不到的东西：**组合正确性**。
 * 逆解、两路独立 PID、正解三者串起来之后，车体真的会按指令的速度和曲率走吗？
 * 单独测每一环都过、串起来跑偏，是控制固件最典型的失败方式
 * (符号约定不一致、左右轮接反、曲率被饱和策略掰弯)。
 *
 * 电机模型：一阶惯性环节，时间常数 τ≈80ms (520 减速电机 @12V 的量级)。
 *   rpm[k+1] = rpm[k] + (duty·MAX_RPM − load − rpm[k]) · dt/τ
 * 这不是高保真模型，但足以暴露上面那几类错误。
 */
#include <math.h>

#include "board_config.h"
#include "kinematics.h"
#include "pid.h"
#include "unity.h"

#define MOTOR_TAU_S     0.08f
/** 每个控制周期的一阶离散系数 = dt/τ = 0.0125 */
#define PLANT_ALPHA     (CONTROL_DT_S / MOTOR_TAU_S)

/** 双轮仿真台架 */
typedef struct {
    PIDController pid[NUM_WHEELS];
    float rpm[NUM_WHEELS];       /**< 当前实际转速 */
    float duty[NUM_WHEELS];      /**< 当前占空比 */
    float load_rpm[NUM_WHEELS];  /**< 负载等效转速损失 */
    float peak_rpm[NUM_WHEELS];  /**< 记录峰值，用于算超调 */
} Bench;

static void bench_init(Bench *b)
{
    const DiffGeometry geo = {
        CHASSIS_WHEEL_RADIUS_M, CHASSIS_TRACK_WIDTH_M, MOTOR_MAX_RPM
    };
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_kinematics_init(&geo));

    for (int i = 0; i < NUM_WHEELS; i++) {
        pid_init(&b->pid[i], PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT,
                 PID_INTEGRAL_LIMIT, PID_OUTPUT_LIMIT);
        b->rpm[i] = 0.0f;
        b->duty[i] = 0.0f;
        b->load_rpm[i] = 0.0f;
        b->peak_rpm[i] = 0.0f;
    }
}

/** 推进一个 1kHz 控制周期 */
static void bench_tick(Bench *b, const float target_rpm[NUM_WHEELS])
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        b->duty[i] = pid_update(&b->pid[i], target_rpm[i], b->rpm[i], CONTROL_DT_S);

        const float driven = b->duty[i] * MOTOR_MAX_RPM - b->load_rpm[i];
        b->rpm[i] += (driven - b->rpm[i]) * PLANT_ALPHA;

        const float magnitude = fabsf(b->rpm[i]);
        if (magnitude > b->peak_rpm[i]) {
            b->peak_rpm[i] = magnitude;
        }
    }
}

/** 跑 ms 毫秒 */
static void bench_run(Bench *b, const float target_rpm[NUM_WHEELS], int ms)
{
    for (int t = 0; t < ms; t++) {
        bench_tick(b, target_rpm);
    }
}

/* ===================== 单轮阶跃 (验收标准: 超调<20%, 稳态误差<5%) ===================== */

static void test_step_response_meets_acceptance(void)
{
    Bench b;
    bench_init(&b);

    const float target[NUM_WHEELS] = { 150.0f, 150.0f };
    bench_run(&b, target, 3000);

    for (int i = 0; i < NUM_WHEELS; i++) {
        const float steady_error = fabsf(target[i] - b.rpm[i]) / target[i];
        TEST_ASSERT_TRUE_MESSAGE(steady_error < 0.05f,
                                 "steady-state error must stay under 5%");

        const float overshoot = (b.peak_rpm[i] - target[i]) / target[i];
        TEST_ASSERT_TRUE_MESSAGE(overshoot < 0.20f, "overshoot must stay under 20%");
    }
}

/* ===================== 整链：直线 ===================== */

/**
 * 给定 v=0.4 / ω=0，闭环收敛后用正解还原车体速度，必须回到 0.4。
 * 这条链路上任何一处符号或系数错误都会在这里暴露。
 */
static void test_closed_loop_tracks_straight_line(void)
{
    Bench b;
    bench_init(&b);

    const DiffVelocity cmd = { 0.4f, 0.0f };
    float target[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, target));

    bench_run(&b, target, 3000);

    DiffVelocity measured;
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_forward_kinematics(b.rpm, &measured));

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.02f, cmd.v, measured.v,
                                     "closed loop must track commanded linear speed");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.05f, 0.0f, measured.omega,
                                     "straight line must not drift into a turn");
}

/* ===================== 整链：弧线 (曲率跟踪) ===================== */

/**
 * 给定 v=0.4 / ω=0.8 (未饱和)，闭环收敛后 v 与 ω 都必须还原。
 * 只测 v 不测 ω 抓不到"左右轮接反"这类错误 —— 那时 v 仍然对，ω 会翻符号。
 */
static void test_closed_loop_tracks_curvature(void)
{
    Bench b;
    bench_init(&b);

    const DiffVelocity cmd = { 0.4f, 0.8f };
    float target[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, target));
    TEST_ASSERT_TRUE_MESSAGE(target[WHEEL_RIGHT] > target[WHEEL_LEFT],
                             "ccw command must ask more of the right wheel");

    bench_run(&b, target, 3000);

    DiffVelocity measured;
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_forward_kinematics(b.rpm, &measured));

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.02f, cmd.v, measured.v,
                                     "closed loop must track linear speed on an arc");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.08f, cmd.omega, measured.omega,
                                     "closed loop must track angular speed on an arc");
}

/* ===================== 整链：原地旋转 ===================== */

/** v=0 / ω=1.5：双轮反向等速，车体线速度必须保持 0 */
static void test_closed_loop_spins_in_place(void)
{
    Bench b;
    bench_init(&b);

    const DiffVelocity cmd = { 0.0f, 1.5f };
    float target[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, target));

    bench_run(&b, target, 3000);

    TEST_ASSERT_TRUE_MESSAGE(b.rpm[WHEEL_LEFT] < 0.0f, "spin ccw: left wheel reverses");
    TEST_ASSERT_TRUE_MESSAGE(b.rpm[WHEEL_RIGHT] > 0.0f, "spin ccw: right wheel advances");

    DiffVelocity measured;
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_forward_kinematics(b.rpm, &measured));
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.02f, 0.0f, measured.v,
                                     "in-place spin must not creep forward");
    TEST_ASSERT_FLOAT_WITHIN(0.10f, cmd.omega, measured.omega);
}

/* ===================== 负载扰动 ===================== */

/**
 * 单侧加载 —— 差速底盘最常见的工况 (一侧轮压上斜坡 / 地毯)。
 * 积分器必须把这一侧的占空比顶上去，否则车会不受控地拐弯。
 *
 * 这条用例也在守 task-10 踩过的坑：ki × integral_limit ≥ output_limit。
 * 若积分限幅配小了，带载轮永远差一截，这里会直接红。
 */
static void test_rejects_asymmetric_load(void)
{
    Bench b;
    bench_init(&b);

    const DiffVelocity cmd = { 0.4f, 0.0f };
    float target[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_inverse_kinematics(&cmd, target));

    bench_run(&b, target, 1500);
    /* 左轮突然吃掉 30% 满量程的负载 */
    b.load_rpm[WHEEL_LEFT] = MOTOR_MAX_RPM * 0.30f;
    bench_run(&b, target, 3000);

    for (int i = 0; i < NUM_WHEELS; i++) {
        const float error = fabsf(target[i] - b.rpm[i]) / target[i];
        TEST_ASSERT_TRUE_MESSAGE(error < 0.05f,
                                 "integrator must absorb an asymmetric load");
    }

    /* 带载轮的占空比必须显著更高 —— 否则说明它根本没在补偿 */
    TEST_ASSERT_TRUE_MESSAGE(b.duty[WHEEL_LEFT] > b.duty[WHEEL_RIGHT] * 1.2f,
                             "loaded wheel must draw more duty cycle");

    DiffVelocity measured;
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_forward_kinematics(b.rpm, &measured));
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.05f, 0.0f, measured.omega,
                                     "asymmetric load must not bend the path");
}

/* ===================== 急停 ===================== */

/** 急停后 PID 必须彻底复位，不能留着积分项在下次使能时猛冲 */
static void test_estop_reset_clears_integrator(void)
{
    Bench b;
    bench_init(&b);

    const float target[NUM_WHEELS] = { 200.0f, 200.0f };
    bench_run(&b, target, 2000);

    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_TRUE_MESSAGE(fabsf(b.pid[i].integral) > 1.0f,
                                 "integrator should have wound up by now");
        pid_reset(&b.pid[i]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, b.pid[i].integral);
        TEST_ASSERT_FALSE(b.pid[i].has_prev);
    }

    /* 复位后第一拍的输出必须只由"当拍误差"决定，不含任何历史。
       注意要拿**进入这一拍之前**的转速来算期望值：bench_tick() 先算 PID
       再推进电机模型，用推进后的转速去对账会差一个积分步长。

       第一拍的精确构成：微分被跳过 (has_prev=false 刚被 pid_reset 清掉)，
       积分只累加了一个周期 error·dt。把这两项都写出来，
       就把"复位到底清了什么"钉死了 —— 比一个宽容差的近似断言更有价值。 */
    float before[NUM_WHEELS];
    for (int i = 0; i < NUM_WHEELS; i++) {
        before[i] = b.rpm[i];
    }

    const float zero_target[NUM_WHEELS] = { 0.0f, 0.0f };
    bench_tick(&b, zero_target);

    for (int i = 0; i < NUM_WHEELS; i++) {
        const float error = 0.0f - before[i];
        const float expect = PID_KP_DEFAULT * error
                           + PID_KI_DEFAULT * (error * CONTROL_DT_S);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, expect, b.duty[i],
                                         "first tick after reset must carry no history");
    }
}

/* ===================== 饱和指令下的整链行为 ===================== */

/**
 * 指令超出底盘能力时，闭环仍必须走在**同一条弧线**上，只是慢。
 * 这是 diff_inverse_kinematics() 等比缩放策略在闭环里的最终体现。
 */
static void test_saturated_command_keeps_path(void)
{
    Bench b;
    bench_init(&b);

    const DiffVelocity cmd = { 1.0f, 3.0f };   /* 右轮需要 367 RPM > 330 */
    float target[NUM_WHEELS];
    TEST_ASSERT_EQUAL_INT(KIN_SATURATED, diff_inverse_kinematics(&cmd, target));

    bench_run(&b, target, 4000);

    DiffVelocity measured;
    TEST_ASSERT_EQUAL_INT(KIN_OK, diff_forward_kinematics(b.rpm, &measured));

    const float commanded_curvature = cmd.omega / cmd.v;
    const float measured_curvature = measured.omega / measured.v;
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.15f, commanded_curvature, measured_curvature,
                                     "saturated command must still follow the same arc");
    TEST_ASSERT_TRUE_MESSAGE(measured.v < cmd.v, "saturated command must run slower");
}

void run_control_loop_tests(void)
{
    UNITY_SET_FILE();

    RUN_TEST(test_step_response_meets_acceptance);
    RUN_TEST(test_closed_loop_tracks_straight_line);
    RUN_TEST(test_closed_loop_tracks_curvature);
    RUN_TEST(test_closed_loop_spins_in_place);
    RUN_TEST(test_rejects_asymmetric_load);
    RUN_TEST(test_estop_reset_clears_integrator);
    RUN_TEST(test_saturated_command_keeps_path);
}
