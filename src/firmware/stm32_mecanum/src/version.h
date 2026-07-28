/**
 * @file version.h
 * @brief 固件版本与构建溯源信息
 *
 * FW_COMMIT_HASH / FW_BUILD_TIME 由 CI 在编译期通过 -D 注入
 * (见 Makefile 的 VERSION_FLAGS 与 .github/workflows/ci.yml)，
 * 本地手工编译时退化为 "local" / "unknown"，不影响构建。
 *
 * PONG 帧只回传 major/minor/patch 三字节；完整的 commit/时间字符串
 * 保留在固件里，供 JTAG / 串口调试时读取。
 */
#ifndef STM32_MECANUM_VERSION_H
#define STM32_MECANUM_VERSION_H

#define FW_MAJOR                0u
#define FW_MINOR                1u
#define FW_PATCH                0u

#ifndef FW_COMMIT_HASH
#define FW_COMMIT_HASH          "local"
#endif

#ifndef FW_BUILD_TIME
#define FW_BUILD_TIME           "unknown"
#endif

/** 板卡类型编码 (ADR-0003)：0x01=STM32F407 · 0x02=MSPM0G3507 */
#define BOARD_TYPE_STM32F407    0x01u
/** 底盘类型编码 (ADR-0003)：0x01=麦轮 · 0x02=差速 */
#define CHASSIS_TYPE_MECANUM    0x01u

#define FW_BOARD_TYPE           BOARD_TYPE_STM32F407
#define FW_CHASSIS_TYPE         CHASSIS_TYPE_MECANUM

#endif /* STM32_MECANUM_VERSION_H */
