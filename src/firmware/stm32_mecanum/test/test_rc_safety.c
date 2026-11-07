/** @file test_rc_safety.c */
#include <string.h>

#include "rc_safety.h"
#include "unity.h"

static RcSafetyConfig config(void)
{
    const RcSafetyConfig cfg = {
        1500u, 500u, 20u, 900u, 2100u, 1300u, 1700u, 100u, 3000u
    };
    return cfg;
}

static IBusChannels channels(uint16_t arm, uint16_t mode)
{
    IBusChannels result;
    for (uint8_t i = 0u; i < IBUS_CHANNEL_COUNT; i++) {
        result.channels[i] = 1500u;
    }
    result.channels[RC_CHANNEL_ARM_INDEX] = arm;
    result.channels[RC_CHANNEL_MODE_INDEX] = mode;
    return result;
}

static void arm_at(RcSafety *state, uint32_t start_ms, uint16_t mode)
{
    IBusChannels frame = channels(1000u, mode);
    (void)rc_safety_accept_frame(state, &frame, start_ms);
    frame.channels[RC_CHANNEL_ARM_INDEX] = 2000u;
    for (uint32_t elapsed = 1u; elapsed <= 3001u; elapsed += 100u) {
        (void)rc_safety_accept_frame(state, &frame, start_ms + elapsed);
    }
}

static void test_booting_with_arm_high_never_unlocks(void)
{
    RcSafety state;
    const RcSafetyConfig cfg = config();
    rc_safety_init(&state, &cfg);
    IBusChannels frame = channels(2000u, 1000u);
    (void)rc_safety_accept_frame(&state, &frame, 0u);
    (void)rc_safety_accept_frame(&state, &frame, 5000u);
    TEST_ASSERT_FALSE(rc_safety_poll(&state, 5000u).armed);
}

static void test_low_to_high_and_three_seconds_neutral_arms(void)
{
    RcSafety state;
    const RcSafetyConfig cfg = config();
    rc_safety_init(&state, &cfg);
    arm_at(&state, 10u, 1000u);
    const RcSafetyOutput output = rc_safety_poll(&state, 3011u);
    TEST_ASSERT_TRUE(output.armed);
    TEST_ASSERT_EQUAL_INT(RC_CONTROL_MANUAL, output.mode);
}

static void test_stick_motion_restarts_neutral_hold(void)
{
    RcSafety state;
    const RcSafetyConfig cfg = config();
    rc_safety_init(&state, &cfg);
    IBusChannels frame = channels(1000u, 1000u);
    (void)rc_safety_accept_frame(&state, &frame, 0u);
    frame.channels[RC_CHANNEL_ARM_INDEX] = 2000u;
    for (uint32_t now = 1u; now <= 2401u; now += 100u) {
        (void)rc_safety_accept_frame(&state, &frame, now);
    }
    frame.channels[RC_CHANNEL_X_INDEX] = 1800u;
    (void)rc_safety_accept_frame(&state, &frame, 2500u);
    frame.channels[RC_CHANNEL_X_INDEX] = 1500u;
    for (uint32_t now = 2501u; now <= 3001u; now += 100u) {
        (void)rc_safety_accept_frame(&state, &frame, now);
    }
    TEST_ASSERT_FALSE(rc_safety_poll(&state, 3001u).armed);
    for (uint32_t now = 3101u; now <= 5501u; now += 100u) {
        (void)rc_safety_accept_frame(&state, &frame, now);
    }
    TEST_ASSERT_TRUE(rc_safety_poll(&state, 5501u).armed);
}

static void test_frame_gap_cannot_count_as_continuous_neutral_hold(void)
{
    RcSafety state;
    const RcSafetyConfig cfg = config();
    rc_safety_init(&state, &cfg);
    IBusChannels frame = channels(1000u, 1000u);
    (void)rc_safety_accept_frame(&state, &frame, 0u);
    frame.channels[RC_CHANNEL_ARM_INDEX] = 2000u;
    (void)rc_safety_accept_frame(&state, &frame, 1u);
    (void)rc_safety_accept_frame(&state, &frame, 3001u);
    TEST_ASSERT_FALSE(rc_safety_poll(&state, 3001u).armed);
}

static void test_timeout_disarms_and_requires_new_low_edge(void)
{
    RcSafety state;
    const RcSafetyConfig cfg = config();
    rc_safety_init(&state, &cfg);
    arm_at(&state, 0u, 1000u);
    TEST_ASSERT_FALSE(rc_safety_poll(&state, 3200u).armed);

    IBusChannels frame = channels(2000u, 1000u);
    (void)rc_safety_accept_frame(&state, &frame, 3201u);
    (void)rc_safety_accept_frame(&state, &frame, 7000u);
    TEST_ASSERT_FALSE(rc_safety_poll(&state, 7000u).armed);
}

static void test_auto_stick_motion_causes_manual_override(void)
{
    RcSafety state;
    const RcSafetyConfig cfg = config();
    rc_safety_init(&state, &cfg);
    arm_at(&state, 0u, 2000u);
    TEST_ASSERT_EQUAL_INT(RC_CONTROL_AUTO, rc_safety_poll(&state, 3001u).mode);

    IBusChannels frame = channels(2000u, 2000u);
    frame.channels[RC_CHANNEL_YAW_INDEX] = 1750u;
    (void)rc_safety_accept_frame(&state, &frame, 3002u);
    const RcSafetyOutput output = rc_safety_poll(&state, 3002u);
    TEST_ASSERT_EQUAL_INT(RC_CONTROL_MANUAL, output.mode);
    TEST_ASSERT_TRUE(output.manual_override);
    TEST_ASSERT_TRUE(output.axis_yaw > 0.0f);
}

static void test_invalid_channel_or_switch_disarms(void)
{
    RcSafety state;
    const RcSafetyConfig cfg = config();
    rc_safety_init(&state, &cfg);
    arm_at(&state, 0u, 1000u);
    IBusChannels frame = channels(2000u, 1500u);
    TEST_ASSERT_FALSE(rc_safety_accept_frame(&state, &frame, 3002u));
    TEST_ASSERT_FALSE(rc_safety_poll(&state, 3002u).armed);
}

void run_rc_safety_tests(void)
{
    UNITY_SET_FILE();
    RUN_TEST(test_booting_with_arm_high_never_unlocks);
    RUN_TEST(test_low_to_high_and_three_seconds_neutral_arms);
    RUN_TEST(test_stick_motion_restarts_neutral_hold);
    RUN_TEST(test_frame_gap_cannot_count_as_continuous_neutral_hold);
    RUN_TEST(test_timeout_disarms_and_requires_new_low_edge);
    RUN_TEST(test_auto_stick_motion_causes_manual_override);
    RUN_TEST(test_invalid_channel_or_switch_disarms);
}
