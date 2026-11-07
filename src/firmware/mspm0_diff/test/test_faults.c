/**
 * @file test_faults.c
 * @brief 故障状态机测试 —— 把三条"用真实缺陷换来的"行为契约钉死
 *
 * 这组用例的存在本身就是一次修正。task-10 评审阶段发现了三个故障处理缺陷，
 * 修完之后它们只能靠上板检查清单验证 —— 因为判定逻辑写在 main.c 的 static
 * 函数里，依赖 1kHz 中断、真实编码器与 ADC。抽成 common/faults.c 之后，
 * 三条契约全部变成可以逐条断言的纯逻辑：
 *
 *   1. FAULT_STALL 必须**跟随实际状态**，堵转解除即清位
 *   2. FAULT_UART_ERROR 必须按**增量**判定，不是累计值
 *   3. 停机期间 (ESTOP / OVERCURRENT) STALL **保持旧值**，不更新也不清零
 *
 * 第 3 条尤其重要：它是位与位之间的优先级规则，属于线上契约的一部分，
 * 但在抽出本模块之前，整个仓库里没有任何一处能验证它。
 */
#include "faults.h"
#include "unity.h"

/* 测试配置。stall_ticks 取小值让用例读起来短，判定逻辑与取值无关。 */
#define TEST_STALL_TARGET_RPM   30.0f
#define TEST_STALL_RPM_FLOOR    3.0f
#define TEST_STALL_TICKS        5u
#define TEST_CURRENT_LIMIT_A    3.0f
#define TEST_CMD_TIMEOUT_MS     500u

#define TEST_WHEELS             2

static FaultMonitor s_monitor;

static void setup_monitor(void)
{
    const FaultConfig cfg = {
        TEST_STALL_TARGET_RPM,
        TEST_STALL_RPM_FLOOR,
        TEST_STALL_TICKS,
        TEST_CURRENT_LIMIT_A,
        TEST_CMD_TIMEOUT_MS
    };
    faults_init(&s_monitor, &cfg);
}

/** 跑一次运动侧评估 */
static uint16_t motion(bool estop, const float *target, const float *actual,
                       bool saturated, uint32_t age_ms)
{
    const FaultMotionInput in = {
        estop, TEST_WHEELS, target, actual, saturated, age_ms
    };
    return faults_evaluate_motion(&s_monitor, &in);
}

/** 跑 n 次相同的运动侧评估 */
static uint16_t motion_n(int n, bool estop, const float *target,
                         const float *actual, bool saturated, uint32_t age_ms)
{
    uint16_t bitmap = faults_get(&s_monitor);
    for (int i = 0; i < n; i++) {
        bitmap = motion(estop, target, actual, saturated, age_ms);
    }
    return bitmap;
}

/** 跑一次链路侧评估 */
static uint16_t link(const float *current, uint32_t crc, uint32_t ovr, uint32_t drop)
{
    const FaultLinkInput in = { TEST_WHEELS, current, crc, ovr, drop };
    return faults_evaluate_link(&s_monitor, &in);
}

/* 常用输入 */
static const float TARGET_MOVING[TEST_WHEELS] = { 100.0f, 100.0f };
static const float ACTUAL_STUCK[TEST_WHEELS]  = { 0.0f, 0.0f };
static const float ACTUAL_MOVING[TEST_WHEELS] = { 98.0f, 99.0f };
static const float TARGET_IDLE[TEST_WHEELS]   = { 0.0f, 0.0f };
static const float CURRENT_OK[TEST_WHEELS]    = { 0.8f, 0.9f };
static const float CURRENT_HIGH[TEST_WHEELS]  = { 0.8f, 5.0f };

/* ===================== 基础 ===================== */

static void test_starts_clean(void)
{
    setup_monitor();
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, faults_get(&s_monitor));
    TEST_ASSERT_FALSE(faults_should_halt(&s_monitor));
    TEST_ASSERT_FALSE(faults_estop_latched(&s_monitor));
}

/** NULL 入参不得崩溃；拿不到状态时 should_halt 必须保守地返回 true */
static void test_null_arguments_are_safe(void)
{
    setup_monitor();

    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, faults_evaluate_motion(0, 0));
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, faults_evaluate_link(0, 0));
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, faults_get(0));
    TEST_ASSERT_FALSE(faults_estop_latched(0));
    TEST_ASSERT_TRUE_MESSAGE(faults_should_halt(0),
                             "unknown state must be treated as halt (fail-safe)");

    /* in == NULL：不更新，返回当前位图 */
    (void)motion_n((int)TEST_STALL_TICKS, false, TARGET_MOVING, ACTUAL_STUCK, false, 0u);
    const uint16_t before = faults_get(&s_monitor);
    TEST_ASSERT_EQUAL_HEX16(before, faults_evaluate_motion(&s_monitor, 0));
    TEST_ASSERT_EQUAL_HEX16(before, faults_evaluate_link(&s_monitor, 0));

    faults_init(0, 0);   /* 不得崩溃 */
}

/* ===================== 契约 1：STALL 跟随实际状态 ===================== */

/** 堵转必须**持续** stall_ticks 个周期才置位，早一拍都不行 */
static void test_stall_requires_sustained_condition(void)
{
    setup_monitor();

    const uint16_t before = motion_n((int)TEST_STALL_TICKS - 1, false,
                                     TARGET_MOVING, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_NONE, before & FAULT_STALL,
                                    "stall must not trip one tick early");

    const uint16_t at = motion(false, TARGET_MOVING, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_STALL, at & FAULT_STALL,
                                    "stall must trip exactly at the threshold");
}

/**
 * ★ 契约 1：堵转解除后故障位必须**自动清除**。
 * 这是 task-10 的原始缺陷 —— 一次瞬时堵转会让故障灯亮到复位。
 */
static void test_stall_clears_when_wheel_recovers(void)
{
    setup_monitor();
    uint16_t bitmap = motion_n((int)TEST_STALL_TICKS, false,
                               TARGET_MOVING, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_STALL, bitmap & FAULT_STALL);

    /* 松手，轮子转起来 —— 下一拍就该清 */
    bitmap = motion(false, TARGET_MOVING, ACTUAL_MOVING, false, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_NONE, bitmap & FAULT_STALL,
                                    "stall must clear as soon as the wheel spins");
}

/** 恢复后计数必须归零，而不是接着上次累加 */
static void test_stall_counter_resets_on_recovery(void)
{
    setup_monitor();

    (void)motion_n((int)TEST_STALL_TICKS - 1, false,
                   TARGET_MOVING, ACTUAL_STUCK, false, 0u);
    (void)motion(false, TARGET_MOVING, ACTUAL_MOVING, false, 0u);   /* 恢复一拍 */

    /* 若计数没归零，这一拍就会立刻触发 */
    const uint16_t bitmap = motion(false, TARGET_MOVING, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_NONE, bitmap & FAULT_STALL,
                                    "stall counter must restart after recovery");
}

/** 没给目标转速时不算堵转 —— 车本来就该停着 */
static void test_idle_wheel_is_not_a_stall(void)
{
    setup_monitor();
    const uint16_t bitmap = motion_n((int)TEST_STALL_TICKS * 3, false,
                                     TARGET_IDLE, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, bitmap & FAULT_STALL);
}

/** 反向堵转同样要检出 (判据取绝对值) */
static void test_reverse_stall_is_detected(void)
{
    setup_monitor();
    const float reverse_target[TEST_WHEELS] = { -100.0f, -100.0f };
    const uint16_t bitmap = motion_n((int)TEST_STALL_TICKS, false,
                                     reverse_target, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_STALL, bitmap & FAULT_STALL);
}

/** 单轮堵转即足以报警 —— 差速底盘一侧卡住比两侧都卡更危险 */
static void test_single_wheel_stall_trips(void)
{
    setup_monitor();
    const float target[TEST_WHEELS] = { 100.0f, 100.0f };
    const float actual[TEST_WHEELS] = { 0.0f, 99.0f };   /* 只有左轮卡住 */
    const uint16_t bitmap = motion_n((int)TEST_STALL_TICKS, false,
                                     target, actual, false, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_STALL, bitmap & FAULT_STALL);
}

/* ===================== 契约 2：UART_ERROR 按增量判定 ===================== */

/** 首次评估只记基线，不得报错 —— 否则热复位后开机就亮故障 */
static void test_link_first_evaluation_primes_baseline(void)
{
    setup_monitor();
    const uint16_t bitmap = link(CURRENT_OK, 7u, 3u, 1u);   /* 已有历史计数 */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_NONE, bitmap & FAULT_UART_ERROR,
                                    "pre-existing counters must not raise a fault");
}

/**
 * ★ 契约 2：新增错误置位，下一周期无新增即清除。
 * task-10 的原始缺陷是用累计值判定 —— 开机一次噪声，故障位永久挂着。
 */
static void test_link_error_is_incremental_not_cumulative(void)
{
    setup_monitor();
    (void)link(CURRENT_OK, 0u, 0u, 0u);   /* 基线 */

    uint16_t bitmap = link(CURRENT_OK, 1u, 0u, 0u);   /* 新增一次 CRC 错 */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_UART_ERROR, bitmap & FAULT_UART_ERROR,
                                    "a new CRC error must raise the flag");

    /* 累计值仍是 1，但没有**新增** —— 必须清除 */
    bitmap = link(CURRENT_OK, 1u, 0u, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_NONE, bitmap & FAULT_UART_ERROR,
                                    "a stale cumulative count must not keep the flag set");
}

/** 三类链路计数都要参与判定 */
static void test_link_error_covers_all_counters(void)
{
    setup_monitor();
    (void)link(CURRENT_OK, 0u, 0u, 0u);

    TEST_ASSERT_EQUAL_HEX16(FAULT_UART_ERROR, link(CURRENT_OK, 0u, 1u, 0u) & FAULT_UART_ERROR);
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE,       link(CURRENT_OK, 0u, 1u, 0u) & FAULT_UART_ERROR);
    TEST_ASSERT_EQUAL_HEX16(FAULT_UART_ERROR, link(CURRENT_OK, 0u, 1u, 1u) & FAULT_UART_ERROR);
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE,       link(CURRENT_OK, 0u, 1u, 1u) & FAULT_UART_ERROR);
}

/* ===================== 契约 3：停机期间 STALL 保持旧值 ===================== */

/**
 * ★ 契约 3-a：过流期间 STALL 保持旧值。
 *
 * 电机已刹停，此时"轮子不转"不构成堵转证据 —— 清零是误报，
 * 继续累加也是误报。唯一正确的行为是**不更新**。
 * 这条规则写在 protocol.h 的注释里，但在抽出 faults.c 之前无人能验证。
 */
static void test_overcurrent_freezes_stall_bit(void)
{
    setup_monitor();

    /* 先把 STALL 置起来 */
    (void)motion_n((int)TEST_STALL_TICKS, false, TARGET_MOVING, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_STALL, faults_get(&s_monitor) & FAULT_STALL);

    /* 过流 */
    uint16_t bitmap = link(CURRENT_HIGH, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_OVERCURRENT, bitmap & FAULT_OVERCURRENT);

    /* 现在即使轮子"转起来了"，STALL 也必须保持 —— 因为电机已刹停，
       这个读数不携带任何信息 */
    bitmap = motion(false, TARGET_MOVING, ACTUAL_MOVING, false, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_STALL, bitmap & FAULT_STALL,
                                    "stall must hold its value while overcurrent is latched");
    TEST_ASSERT_TRUE(faults_should_halt(&s_monitor));
}

/** 反过来：过流期间也不得**新**置位 STALL */
static void test_overcurrent_does_not_accumulate_stall(void)
{
    setup_monitor();

    (void)link(CURRENT_OK, 0u, 0u, 0u);
    (void)link(CURRENT_HIGH, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_OVERCURRENT, faults_get(&s_monitor) & FAULT_OVERCURRENT);

    /* 刹停状态下轮子当然不转，但这不该被算成堵转 */
    const uint16_t bitmap = motion_n((int)TEST_STALL_TICKS * 3, false,
                                     TARGET_MOVING, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_NONE, bitmap & FAULT_STALL,
                                    "braked motors must not be reported as stalled");
}

/** 过流解除后恢复正常评估 */
static void test_overcurrent_clears_and_resumes_evaluation(void)
{
    setup_monitor();
    (void)link(CURRENT_OK, 0u, 0u, 0u);
    (void)link(CURRENT_HIGH, 0u, 0u, 0u);

    uint16_t bitmap = link(CURRENT_OK, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, bitmap & FAULT_OVERCURRENT);
    TEST_ASSERT_FALSE(faults_should_halt(&s_monitor));

    bitmap = motion_n((int)TEST_STALL_TICKS, false, TARGET_MOVING, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_STALL, bitmap & FAULT_STALL,
                                    "stall detection must resume once current is normal");
}

/**
 * ★ 契约 3-b：急停锁存，且冻结其余运动侧位。
 */
static void test_estop_latches_and_freezes_other_bits(void)
{
    setup_monitor();

    /* 先制造一个饱和状态 */
    uint16_t bitmap = motion(false, TARGET_MOVING, ACTUAL_MOVING, true, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_KINEMATICS_SAT, bitmap & FAULT_KINEMATICS_SAT);

    /* 急停 */
    bitmap = motion(true, TARGET_MOVING, ACTUAL_MOVING, true, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_ESTOP, bitmap & FAULT_ESTOP);
    TEST_ASSERT_TRUE(faults_estop_latched(&s_monitor));
    TEST_ASSERT_TRUE(faults_should_halt(&s_monitor));

    /* 松开急停按钮：位必须**保持** —— 只能靠复位退出 */
    bitmap = motion(false, TARGET_MOVING, ACTUAL_MOVING, false, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_ESTOP, bitmap & FAULT_ESTOP,
                                    "estop must latch until reset");
    /* 且饱和位不再被更新 (输入已改为 false，但位仍在) */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_KINEMATICS_SAT, bitmap & FAULT_KINEMATICS_SAT,
                                    "estop must freeze motion-side evaluation");
}

/** 急停优先于过流：两者同时存在时仍然锁死 */
static void test_estop_outranks_overcurrent(void)
{
    setup_monitor();
    (void)link(CURRENT_OK, 0u, 0u, 0u);
    (void)link(CURRENT_HIGH, 0u, 0u, 0u);
    (void)motion(true, TARGET_MOVING, ACTUAL_STUCK, false, 0u);

    /* 电流恢复正常，OVERCURRENT 可以清，但 ESTOP 不行 */
    const uint16_t bitmap = link(CURRENT_OK, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, bitmap & FAULT_OVERCURRENT);
    TEST_ASSERT_EQUAL_HEX16(FAULT_ESTOP, bitmap & FAULT_ESTOP);
    TEST_ASSERT_TRUE(faults_should_halt(&s_monitor));
}

/* ===================== 其余跟随型故障 ===================== */

static void test_command_timeout_follows_age(void)
{
    setup_monitor();

    uint16_t bitmap = motion(false, TARGET_IDLE, ACTUAL_STUCK, false, TEST_CMD_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FAULT_NONE, bitmap & FAULT_CMD_TIMEOUT,
                                    "exactly at the timeout is not yet a timeout");

    bitmap = motion(false, TARGET_IDLE, ACTUAL_STUCK, false, TEST_CMD_TIMEOUT_MS + 1u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_CMD_TIMEOUT, bitmap & FAULT_CMD_TIMEOUT);

    /* 收到新指令 → age 归零 → 位自动清除 */
    bitmap = motion(false, TARGET_IDLE, ACTUAL_STUCK, false, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, bitmap & FAULT_CMD_TIMEOUT);
}

static void test_kinematics_saturation_follows_input(void)
{
    setup_monitor();

    uint16_t bitmap = motion(false, TARGET_MOVING, ACTUAL_MOVING, true, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_KINEMATICS_SAT, bitmap & FAULT_KINEMATICS_SAT);

    bitmap = motion(false, TARGET_MOVING, ACTUAL_MOVING, false, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, bitmap & FAULT_KINEMATICS_SAT);
}

/** should_halt 只看 ESTOP / OVERCURRENT，其余故障不该让车停下 */
static void test_should_halt_only_covers_hard_faults(void)
{
    setup_monitor();

    (void)motion_n((int)TEST_STALL_TICKS, false, TARGET_MOVING, ACTUAL_STUCK,
                   true, TEST_CMD_TIMEOUT_MS + 1u);
    const uint16_t bitmap = faults_get(&s_monitor);
    TEST_ASSERT_EQUAL_HEX16(FAULT_STALL, bitmap & FAULT_STALL);
    TEST_ASSERT_EQUAL_HEX16(FAULT_CMD_TIMEOUT, bitmap & FAULT_CMD_TIMEOUT);
    TEST_ASSERT_EQUAL_HEX16(FAULT_KINEMATICS_SAT, bitmap & FAULT_KINEMATICS_SAT);

    TEST_ASSERT_FALSE_MESSAGE(faults_should_halt(&s_monitor),
                              "soft faults must not force a halt");
}

/* ===================== 轮数边界 ===================== */

/** 轮数超界必须被夹紧，不得越界读写 */
static void test_wheel_count_is_clamped(void)
{
    setup_monitor();

    const float target[FAULTS_MAX_WHEELS] = { 100.0f, 100.0f, 100.0f, 100.0f };
    const float actual[FAULTS_MAX_WHEELS] = { 0.0f, 0.0f, 0.0f, 0.0f };

    for (uint32_t t = 0; t < TEST_STALL_TICKS; t++) {
        const FaultMotionInput in = {
            false, FAULTS_MAX_WHEELS + 99, target, actual, false, 0u
        };
        (void)faults_evaluate_motion(&s_monitor, &in);
    }
    TEST_ASSERT_EQUAL_HEX16(FAULT_STALL, faults_get(&s_monitor) & FAULT_STALL);

    /* 负数轮数 = 不评估任何轮 */
    setup_monitor();
    for (uint32_t t = 0; t < TEST_STALL_TICKS * 3u; t++) {
        const FaultMotionInput in = { false, -3, target, actual, false, 0u };
        (void)faults_evaluate_motion(&s_monitor, &in);
    }
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, faults_get(&s_monitor) & FAULT_STALL);
}

/** target/actual 为 NULL 时按 0 处理，不得崩溃也不得误报堵转 */
static void test_null_rpm_arrays_are_safe(void)
{
    setup_monitor();
    for (uint32_t t = 0; t < TEST_STALL_TICKS * 3u; t++) {
        const FaultMotionInput in = { false, TEST_WHEELS, 0, 0, false, 0u };
        (void)faults_evaluate_motion(&s_monitor, &in);
    }
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, faults_get(&s_monitor) & FAULT_STALL);

    /* 电流数组为 NULL = 无采样，不该报过流 */
    const uint16_t bitmap = link(0, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, bitmap & FAULT_OVERCURRENT);
}

void run_faults_tests(void)
{
    UNITY_SET_FILE();

    RUN_TEST(test_starts_clean);
    RUN_TEST(test_null_arguments_are_safe);

    RUN_TEST(test_stall_requires_sustained_condition);
    RUN_TEST(test_stall_clears_when_wheel_recovers);
    RUN_TEST(test_stall_counter_resets_on_recovery);
    RUN_TEST(test_idle_wheel_is_not_a_stall);
    RUN_TEST(test_reverse_stall_is_detected);
    RUN_TEST(test_single_wheel_stall_trips);

    RUN_TEST(test_link_first_evaluation_primes_baseline);
    RUN_TEST(test_link_error_is_incremental_not_cumulative);
    RUN_TEST(test_link_error_covers_all_counters);

    RUN_TEST(test_overcurrent_freezes_stall_bit);
    RUN_TEST(test_overcurrent_does_not_accumulate_stall);
    RUN_TEST(test_overcurrent_clears_and_resumes_evaluation);
    RUN_TEST(test_estop_latches_and_freezes_other_bits);
    RUN_TEST(test_estop_outranks_overcurrent);

    RUN_TEST(test_command_timeout_follows_age);
    RUN_TEST(test_kinematics_saturation_follows_input);
    RUN_TEST(test_should_halt_only_covers_hard_faults);

    RUN_TEST(test_wheel_count_is_clamped);
    RUN_TEST(test_null_rpm_arrays_are_safe);
}
