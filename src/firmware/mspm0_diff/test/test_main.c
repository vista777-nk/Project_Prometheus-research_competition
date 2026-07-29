/**
 * @file test_main.c
 * @brief Host 单元测试总入口
 *
 * 各 test_*.c 只导出 run_xxx_tests()，main() 与 setUp()/tearDown() 在此唯一定义，
 * 避免多个翻译单元重复定义符号。
 *
 * 编译运行：
 *   make test                 (推荐)
 *   或见 README.md §5 编译与测试
 *
 * 注意本工程**没有** test_crc16.c / test_pid.c：
 * `common/crc16.c` 与 `common/pid.c` 是与麦轮固件共用的同一份实现，
 * 其单体测试在 stm32_mecanum/test/ 下唯一存在。同一份代码测两遍不增加信息，
 * 只增加两处要同步维护的用例。本工程改测 test_control_loop.c ——
 * 逆解 + 双路 PID + 正解**串起来**的组合行为，那才是 task-11 特有的风险。
 */
#include <stdio.h>

#include "unity.h"

void run_kinematics_tests(void);
void run_protocol_tests(void);
void run_encoder_tests(void);
void run_control_loop_tests(void);

/* 本套测试的每个用例都自带初始化，不需要公共夹具 */
void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    printf("=== MSPM0G3507 Differential Firmware — Host Unit Tests ===\n");

    UNITY_BEGIN();
    run_kinematics_tests();
    run_protocol_tests();
    run_encoder_tests();
    run_control_loop_tests();
    return UNITY_END();
}
