/**
 * @file test_pid.c
 * @brief PID 速度环单元测试，含一阶电机模型的阶跃响应验收
 *
 * 验收标准 (task-10 §验收标准)：超调 < 20%，稳态误差 < 5%。
 * 这里用一阶惯性环节近似 520 减速电机的转速响应：
 *
 *     dω/dt = (K·duty − ω) / τ ,  K = MOTOR_MAX_RPM, τ ≈ 80 ms
 *
 * 模型不追求物理精确 —— 它的作用是把"这组增益是否稳定、是否振荡、
 * 是否有稳态误差"这件事变成可回归的自动化断言，避免每次改 PID 都要上实车。
 */
#include <math.h>

#include "board_config.h"
#include "pid.h"
#include "unity.h"

/** 一阶电机模型时间常数 (s) */
#define MOTOR_TAU_S         0.08f
/** 仿真步长，与固件控制周期一致 */
#define SIM_DT_S            CONTROL_DT_S
/** 阶跃目标转速 (RPM) */
#define STEP_SETPOINT_RPM   200.0f

/** 一阶电机：输入占空比 [-1,1]，输出转速 RPM */
static float motor_step(float rpm, float duty, float dt)
{
    const float target = MOTOR_MAX_RPM * duty;
    return rpm + (target - rpm) * (dt / MOTOR_TAU_S);
}

/* --- 基础行为 --- */

static void test_pid_init_and_reset(void)
{
    PIDController pid;
    pid_init(&pid, 0.01f, 2.0f, 3.0f, -10.0f, -1.0f);   /* 限幅传负值，应被取绝对值 */

    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.01f, pid.kp);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.0f, pid.integral_limit);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, pid.output_limit);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid.integral);
    TEST_ASSERT_FALSE(pid.has_prev);

    /* kp·e = 0.1 未触发输出限幅，因此本周期应当正常累加积分 */
    (void)pid_update(&pid, 10.0f, 0.0f, SIM_DT_S);
    TEST_ASSERT_TRUE(pid.has_prev);
    TEST_ASSERT_TRUE(fabsf(pid.integral) > 0.0f);

    pid_reset(&pid);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid.integral);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid.prev_error);
    TEST_ASSERT_FALSE(pid.has_prev);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.01f, pid.kp);   /* 整定参数不受 reset 影响 */
}

/**
 * 条件积分的核心行为：输出已经饱和时不再累加积分。
 * 这条断言直接锁死抗饱和策略，改成"无条件积分"会立刻红掉。
 */
static void test_pid_does_not_integrate_while_saturated(void)
{
    PIDController pid;
    pid_init(&pid, 1.0f, 1.0f, 0.0f, 100.0f, 1.0f);

    /* kp·e = 1000 远超输出限幅 1.0，积分器必须原地不动 */
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, pid_update(&pid, 1000.0f, 0.0f, SIM_DT_S));
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid.integral);
}

/** 零误差 → 零输出；误差符号决定输出符号 */
static void test_pid_output_sign_follows_error(void)
{
    PIDController pid;
    pid_init(&pid, 0.01f, 0.0f, 0.0f, 10.0f, 1.0f);

    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid_update(&pid, 0.0f, 0.0f, SIM_DT_S));
    TEST_ASSERT_TRUE(pid_update(&pid, 50.0f, 0.0f, SIM_DT_S) > 0.0f);

    pid_reset(&pid);
    TEST_ASSERT_TRUE(pid_update(&pid, -50.0f, 0.0f, SIM_DT_S) < 0.0f);
}

/** 首个周期不产生微分冲击：纯 D 控制器的第一次输出必须为 0 */
static void test_pid_no_derivative_kick_on_first_call(void)
{
    PIDController pid;
    pid_init(&pid, 0.0f, 0.0f, 1.0f, 10.0f, 1.0f);

    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid_update(&pid, 100.0f, 0.0f, SIM_DT_S));
    /* 第二个周期误差不变，微分仍为 0 */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid_update(&pid, 100.0f, 0.0f, SIM_DT_S));
}

/** 输出限幅：大误差下不得超过 output_limit */
static void test_pid_output_is_clamped(void)
{
    PIDController pid;
    pid_init(&pid, 1.0f, 0.0f, 0.0f, 10.0f, 1.0f);

    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, pid_update(&pid, 1000.0f, 0.0f, SIM_DT_S));
    pid_reset(&pid);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, pid_update(&pid, -1000.0f, 0.0f, SIM_DT_S));
}

/**
 * 抗积分饱和：输出长期顶在限幅上时，积分器不得无限累积。
 * 否则误差反向后需要很长时间"退饱和"，表现为大幅超调。
 */
static void test_pid_anti_windup_limits_integral(void)
{
    PIDController pid;
    pid_init(&pid, 0.01f, 0.5f, 0.0f, 5.0f, 1.0f);

    for (int i = 0; i < 5000; i++) {
        (void)pid_update(&pid, 500.0f, 0.0f, SIM_DT_S);   /* 持续大误差，输出必然饱和 */
    }
    TEST_ASSERT_TRUE_MESSAGE(fabsf(pid.integral) <= 5.0f + 1e-3f,
                             "integral exceeded its configured limit");

    /* 误差瞬间反向后，输出应当很快跟着反向 (而不是被撑满的积分器拖住) */
    float output = 0.0f;
    int cycles = 0;
    while (cycles < 100 && output >= 0.0f) {
        output = pid_update(&pid, -500.0f, 0.0f, SIM_DT_S);
        cycles++;
    }
    TEST_ASSERT_TRUE_MESSAGE(output < 0.0f, "output failed to reverse after setpoint flip");
}

/** 非法入参保护：dt<=0 与 NULL 均返回 0，不得崩溃或产生 NaN */
static void test_pid_rejects_invalid_arguments(void)
{
    PIDController pid;
    pid_init(&pid, 1.0f, 1.0f, 1.0f, 10.0f, 1.0f);

    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid_update(&pid, 100.0f, 0.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid_update(&pid, 100.0f, 0.0f, -0.001f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid_update(NULL, 100.0f, 0.0f, SIM_DT_S));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pid.integral);   /* 非法调用不应污染状态 */

    pid_init(NULL, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);          /* 不得崩溃 */
    pid_reset(NULL);
}

/* --- 阶跃响应验收 --- */

/** 用固件默认增益跑闭环阶跃，检查超调与稳态误差 */
static void test_pid_step_response_meets_acceptance(void)
{
    PIDController pid;
    pid_init(&pid, PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT,
             PID_INTEGRAL_LIMIT, PID_OUTPUT_LIMIT);

    float rpm = 0.0f;
    float peak = 0.0f;
    int rise_step = -1;
    const int total_steps = 3000;             /* 3 秒 */

    for (int step = 0; step < total_steps; step++) {
        const float duty = pid_update(&pid, STEP_SETPOINT_RPM, rpm, SIM_DT_S);
        TEST_ASSERT_TRUE_MESSAGE(fabsf(duty) <= PID_OUTPUT_LIMIT + 1e-6f,
                                 "duty escaped the output limit");
        rpm = motor_step(rpm, duty, SIM_DT_S);
        TEST_ASSERT_TRUE_MESSAGE(isfinite(rpm), "plant state diverged to NaN/Inf");

        if (rpm > peak) {
            peak = rpm;
        }
        if (rise_step < 0 && rpm >= 0.9f * STEP_SETPOINT_RPM) {
            rise_step = step;
        }
    }

    const float overshoot = (peak - STEP_SETPOINT_RPM) / STEP_SETPOINT_RPM * 100.0f;
    const float steady_error = fabsf(STEP_SETPOINT_RPM - rpm) / STEP_SETPOINT_RPM * 100.0f;

    TEST_ASSERT_TRUE_MESSAGE(overshoot < 20.0f, "overshoot exceeded 20%");
    TEST_ASSERT_TRUE_MESSAGE(steady_error < 5.0f, "steady-state error exceeded 5%");
    TEST_ASSERT_TRUE_MESSAGE(rise_step >= 0, "never reached 90% of setpoint");
    /* 上升时间上界：0.5 s。过慢的响应虽然"不超调"，但实车上会跟不上轨迹 */
    TEST_ASSERT_TRUE_MESSAGE(rise_step < 500, "rise to 90% took longer than 0.5 s");
}

/** 反向阶跃应当对称：负目标同样收敛且无过量超调 */
static void test_pid_step_response_reverse(void)
{
    PIDController pid;
    pid_init(&pid, PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT,
             PID_INTEGRAL_LIMIT, PID_OUTPUT_LIMIT);

    float rpm = 0.0f;
    float peak = 0.0f;
    for (int step = 0; step < 3000; step++) {
        const float duty = pid_update(&pid, -STEP_SETPOINT_RPM, rpm, SIM_DT_S);
        rpm = motor_step(rpm, duty, SIM_DT_S);
        if (rpm < peak) {
            peak = rpm;
        }
    }

    const float overshoot = (-peak - STEP_SETPOINT_RPM) / STEP_SETPOINT_RPM * 100.0f;
    const float steady_error = fabsf(-STEP_SETPOINT_RPM - rpm) / STEP_SETPOINT_RPM * 100.0f;

    TEST_ASSERT_TRUE_MESSAGE(overshoot < 20.0f, "reverse overshoot exceeded 20%");
    TEST_ASSERT_TRUE_MESSAGE(steady_error < 5.0f, "reverse steady-state error exceeded 5%");
}

/**
 * 负载扰动抑制：稳态后突然给电机加 30% 负载 (等效降低 DC 增益)，
 * 积分项应把转速拉回目标。
 */
static void test_pid_rejects_load_disturbance(void)
{
    PIDController pid;
    pid_init(&pid, PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT,
             PID_INTEGRAL_LIMIT, PID_OUTPUT_LIMIT);

    float rpm = 0.0f;
    for (int step = 0; step < 2000; step++) {
        rpm = motor_step(rpm, pid_update(&pid, STEP_SETPOINT_RPM, rpm, SIM_DT_S), SIM_DT_S);
    }

    /* 加载：同样占空比只能产生 70% 的转速 */
    for (int step = 0; step < 3000; step++) {
        const float duty = pid_update(&pid, STEP_SETPOINT_RPM, rpm, SIM_DT_S);
        const float target = MOTOR_MAX_RPM * duty * 0.7f;
        rpm += (target - rpm) * (SIM_DT_S / MOTOR_TAU_S);
    }

    const float error = fabsf(STEP_SETPOINT_RPM - rpm) / STEP_SETPOINT_RPM * 100.0f;
    TEST_ASSERT_TRUE_MESSAGE(error < 5.0f, "failed to reject 30% load disturbance");
}

void run_pid_tests(void)
{
    UNITY_SET_FILE();
    RUN_TEST(test_pid_init_and_reset);
    RUN_TEST(test_pid_does_not_integrate_while_saturated);
    RUN_TEST(test_pid_output_sign_follows_error);
    RUN_TEST(test_pid_no_derivative_kick_on_first_call);
    RUN_TEST(test_pid_output_is_clamped);
    RUN_TEST(test_pid_anti_windup_limits_integral);
    RUN_TEST(test_pid_rejects_invalid_arguments);
    RUN_TEST(test_pid_step_response_meets_acceptance);
    RUN_TEST(test_pid_step_response_reverse);
    RUN_TEST(test_pid_rejects_load_disturbance);
}
