/**
 * @file test_main.c
 * @brief Host 单元测试总入口
 *
 * 各 test_*.c 只导出 run_xxx_tests()，main() 与 setUp()/tearDown() 在此唯一定义，
 * 避免多个翻译单元重复定义符号。
 *
 * 编译运行：
 *   make test                 (推荐)
 *   或见 README.md §编译与测试
 */
#include <stdio.h>

#include "unity.h"

void run_crc16_tests(void);
void run_kinematics_tests(void);
void run_pid_tests(void);
void run_protocol_tests(void);

/* 本套测试的每个用例都自带初始化，不需要公共夹具 */
void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    printf("=== STM32F407 Mecanum Firmware — Host Unit Tests ===\n");

    UNITY_BEGIN();
    run_crc16_tests();
    run_kinematics_tests();
    run_pid_tests();
    run_protocol_tests();
    return UNITY_END();
}
