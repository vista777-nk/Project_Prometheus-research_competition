/**
 * @file version.h
 * @brief 固件版本与构建溯源信息
 *
 * FW_COMMIT_HASH / FW_BUILD_TIME 由 CI 在编译期通过 -D 注入
 * (见 Makefile 的 VERSION_FLAGS 与 .github/workflows/ci.yml)，
 * 本地手工编译时退化为 "local" / "unknown"，不影响构建。
 *
 * PONG 帧只回传 major/minor/patch 三字节；完整的 commit/时间字符串
 * 保留在固件里，供调试探针 / 串口调试时读取。
 */
#ifndef MSPM0_DIFF_VERSION_H
#define MSPM0_DIFF_VERSION_H

#define FW_MAJOR                0u
#define FW_MINOR                2u
#define FW_PATCH                0u

#ifndef FW_COMMIT_HASH
#define FW_COMMIT_HASH          "local"
#endif

#ifndef FW_BUILD_TIME
#define FW_BUILD_TIME           "unknown"
#endif

/** 板卡类型编码 (ADR-0003)：0x01=STM32F407 · 0x02=MSPM0G3507 */
#define BOARD_TYPE_MSPM0G3507       0x02u
/** 底盘类型编码 (ADR-0003)：0x01=麦轮 · 0x02=差速 */
#define CHASSIS_TYPE_DIFFERENTIAL   0x02u

#define FW_BOARD_TYPE           BOARD_TYPE_MSPM0G3507
#define FW_CHASSIS_TYPE         CHASSIS_TYPE_DIFFERENTIAL

/**
 * 构建剖面 (build profile)，随 PONG 之外的调试通道暴露。
 *
 * 之所以要在固件里留下这个标记：本工程的默认 CI 构建**不是可烧录固件**
 * (见 mspm0_conf.h 与 README §2)，必须能在事后区分手里的 .bin 是哪一种，
 * 否则"为什么烧进去电机不转"会变成一次昂贵的排查。
 */
#ifndef FW_BUILD_PROFILE
#define FW_BUILD_PROFILE        "ci-link"
#endif

#endif /* MSPM0_DIFF_VERSION_H */
