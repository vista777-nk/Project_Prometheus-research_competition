/**
 * @file test_crc16.c
 * @brief CRC-16/CCITT-FALSE 测试向量验证 (ADR-0003)
 *
 * 这组用例是整个串口链路的"锚点"：STM32 固件、MSPM0 固件、树莓派 Python
 * 三方各自实现的 CRC 都必须通过同一批向量，否则跨端解析必然出错。
 */
#include <string.h>

#include "crc16.h"
#include "unity.h"

/** 标准测试向量：CRC-16/CCITT-FALSE("123456789") == 0x29B1 */
static void test_crc16_standard_check_value(void)
{
    const uint8_t data[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(CRC16_CCITT_CHECK_VALUE, crc16_ccitt(data, 9));
}

/** 空数据的 CRC 即初始值 0xFFFF (对应 EMERGENCY_STOP / PING 这类零载荷帧) */
static void test_crc16_empty_input(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, crc16_ccitt(NULL, 0));
    const uint8_t data[1] = { 0x00 };
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, crc16_ccitt(data, 0));
}

/** 单字节向量 —— 与 §协议黄金帧中的 PING/ESTOP 帧对得上 */
static void test_crc16_single_byte_vectors(void)
{
    const uint8_t ping[1]  = { 0x03 };
    const uint8_t estop[1] = { 0x02 };
    TEST_ASSERT_EQUAL_HEX16(0xD193u, crc16_ccitt(ping, 1));
    TEST_ASSERT_EQUAL_HEX16(0xC1B2u, crc16_ccitt(estop, 1));
}

/** 增量式接口与一次性接口必须给出相同结果 */
static void test_crc16_incremental_matches_bulk(void)
{
    const uint8_t data[] = { 0x11, 0xA5, 0x5A, 0x00, 0xFF, 0x7E };
    uint16_t incremental = 0xFFFFu;
    for (size_t i = 0; i < sizeof(data); i++) {
        incremental = crc16_ccitt_update(incremental, data[i]);
    }
    TEST_ASSERT_EQUAL_HEX16(crc16_ccitt(data, (uint16_t)sizeof(data)), incremental);
}

/** 单比特翻转必须改变 CRC —— 这是校验码存在的意义 */
static void test_crc16_detects_single_bit_flip(void)
{
    uint8_t data[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    const uint16_t original = crc16_ccitt(data, sizeof(data));

    for (size_t byte = 0; byte < sizeof(data); byte++) {
        for (int bit = 0; bit < 8; bit++) {
            data[byte] ^= (uint8_t)(1u << bit);
            TEST_ASSERT_TRUE_MESSAGE(crc16_ccitt(data, sizeof(data)) != original,
                                     "single bit flip went undetected");
            data[byte] ^= (uint8_t)(1u << bit);
        }
    }
}

/** 字节顺序敏感性：交换两字节应改变 CRC */
static void test_crc16_is_order_sensitive(void)
{
    const uint8_t a[2] = { 0xAB, 0xCD };
    const uint8_t b[2] = { 0xCD, 0xAB };
    TEST_ASSERT_TRUE(crc16_ccitt(a, 2) != crc16_ccitt(b, 2));
}

void run_crc16_tests(void)
{
    UNITY_SET_FILE();
    RUN_TEST(test_crc16_standard_check_value);
    RUN_TEST(test_crc16_empty_input);
    RUN_TEST(test_crc16_single_byte_vectors);
    RUN_TEST(test_crc16_incremental_matches_bulk);
    RUN_TEST(test_crc16_detects_single_bit_flip);
    RUN_TEST(test_crc16_is_order_sensitive);
}
