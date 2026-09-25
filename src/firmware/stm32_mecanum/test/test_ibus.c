/** @file test_ibus.c */
#include <string.h>

#include "ibus.h"
#include "unity.h"

static void make_frame(uint8_t frame[IBUS_FRAME_LENGTH], uint16_t base)
{
    memset(frame, 0, IBUS_FRAME_LENGTH);
    frame[0] = IBUS_FRAME_LENGTH_BYTE;
    frame[1] = IBUS_SERVO_COMMAND;
    for (uint8_t channel = 0u; channel < IBUS_CHANNEL_COUNT; channel++) {
        const uint16_t value = (uint16_t)(base + channel);
        frame[2u + channel * 2u] = (uint8_t)(value & 0xFFu);
        frame[3u + channel * 2u] = (uint8_t)(value >> 8);
    }
    uint16_t checksum = 0xFFFFu;
    for (uint8_t i = 0u; i < 30u; i++) {
        checksum = (uint16_t)(checksum - frame[i]);
    }
    frame[30] = (uint8_t)(checksum & 0xFFu);
    frame[31] = (uint8_t)(checksum >> 8);
}

static bool feed(IBusParser *parser, const uint8_t *frame, IBusChannels *channels)
{
    bool completed = false;
    for (uint8_t i = 0u; i < IBUS_FRAME_LENGTH; i++) {
        completed = ibus_parser_push(parser, frame[i], channels) || completed;
    }
    return completed;
}

static void test_parses_all_channels_and_checksum(void)
{
    uint8_t frame[IBUS_FRAME_LENGTH];
    make_frame(frame, 1000u);
    IBusParser parser;
    IBusChannels channels;
    ibus_parser_init(&parser);

    TEST_ASSERT_TRUE(feed(&parser, frame, &channels));
    TEST_ASSERT_EQUAL_UINT(1000u, channels.channels[0]);
    TEST_ASSERT_EQUAL_UINT(1013u, channels.channels[13]);
    TEST_ASSERT_EQUAL_UINT(1u, parser.frames_ok);
}

static void test_rejects_corrupt_checksum(void)
{
    uint8_t frame[IBUS_FRAME_LENGTH];
    make_frame(frame, 1500u);
    frame[8] ^= 0x01u;
    IBusParser parser;
    IBusChannels channels;
    ibus_parser_init(&parser);

    TEST_ASSERT_FALSE(feed(&parser, frame, &channels));
    TEST_ASSERT_EQUAL_UINT(1u, parser.checksum_errors);
}

static void test_resynchronizes_after_noise_and_bad_command(void)
{
    uint8_t frame[IBUS_FRAME_LENGTH];
    make_frame(frame, 1200u);
    IBusParser parser;
    IBusChannels channels;
    ibus_parser_init(&parser);

    TEST_ASSERT_FALSE(ibus_parser_push(&parser, 0xAAu, &channels));
    TEST_ASSERT_FALSE(ibus_parser_push(&parser, 0x20u, &channels));
    TEST_ASSERT_FALSE(ibus_parser_push(&parser, 0x20u, &channels));
    TEST_ASSERT_EQUAL_UINT(1u, parser.header_errors);
    TEST_ASSERT_FALSE(ibus_parser_push(&parser, 0x40u, &channels));
    for (uint8_t i = 2u; i < IBUS_FRAME_LENGTH; i++) {
        (void)ibus_parser_push(&parser, frame[i], &channels);
    }
    TEST_ASSERT_EQUAL_UINT(1u, parser.frames_ok);
}

static void test_normalizes_with_deadband_and_clamp(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f,
                             ibus_channel_unit(1510u, 1500u, 500u, 20u));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f,
                             ibus_channel_unit(2100u, 1500u, 500u, 20u));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f,
                             ibus_channel_unit(900u, 1500u, 500u, 20u));
}

void run_ibus_tests(void)
{
    UNITY_SET_FILE();
    RUN_TEST(test_parses_all_channels_and_checksum);
    RUN_TEST(test_rejects_corrupt_checksum);
    RUN_TEST(test_resynchronizes_after_noise_and_bad_command);
    RUN_TEST(test_normalizes_with_deadband_and_clamp);
}
