/** @file test_quadrature.c */
#include "quadrature.h"
#include "unity.h"

static void step(QuadratureDecoder *decoder, uint8_t state)
{
    quadrature_update(decoder, (state & 2u) != 0u, (state & 1u) != 0u);
}

static void test_positive_cycle_counts_four_edges(void)
{
    QuadratureDecoder decoder;
    quadrature_init(&decoder, false, false);
    step(&decoder, 1u); step(&decoder, 3u); step(&decoder, 2u); step(&decoder, 0u);
    TEST_ASSERT_EQUAL_INT(4, decoder.count);
    TEST_ASSERT_EQUAL_UINT(0u, decoder.invalid_transitions);
}

static void test_reverse_cycle_counts_negative_four(void)
{
    QuadratureDecoder decoder;
    quadrature_init(&decoder, false, false);
    step(&decoder, 2u); step(&decoder, 3u); step(&decoder, 1u); step(&decoder, 0u);
    TEST_ASSERT_EQUAL_INT(-4, decoder.count);
}

static void test_two_bit_jump_is_rejected_and_counted(void)
{
    QuadratureDecoder decoder;
    quadrature_init(&decoder, false, false);
    step(&decoder, 3u);
    TEST_ASSERT_EQUAL_INT(0, decoder.count);
    TEST_ASSERT_EQUAL_UINT(1u, decoder.invalid_transitions);
}

static void test_first_update_only_initializes(void)
{
    QuadratureDecoder decoder = {0};
    step(&decoder, 1u);
    TEST_ASSERT_EQUAL_INT(0, decoder.count);
    TEST_ASSERT_TRUE(decoder.initialized);
}

static void test_count16_preserves_wrap_semantics(void)
{
    QuadratureDecoder decoder = {0};
    decoder.count = -1;
    TEST_ASSERT_EQUAL_UINT(0xFFFFu, quadrature_count16(&decoder));
    TEST_ASSERT_EQUAL_UINT(0u, quadrature_count16(0));
}

void run_quadrature_tests(void)
{
    UNITY_SET_FILE();
    RUN_TEST(test_positive_cycle_counts_four_edges);
    RUN_TEST(test_reverse_cycle_counts_negative_four);
    RUN_TEST(test_two_bit_jump_is_rejected_and_counted);
    RUN_TEST(test_first_update_only_initializes);
    RUN_TEST(test_count16_preserves_wrap_semantics);
}
