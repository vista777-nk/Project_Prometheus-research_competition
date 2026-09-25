/**
 * @file stm32f4xx_conf.h
 * @brief 固件的硬件抽象入口 —— 决定"这次编译面向什么目标"
 *
 * 三种编译目标由宏区分，源文件只 include 本文件，不直接 include 寄存器头：
 *
 *   1. TEST_HOST        —— Host 单元测试。不包含任何寄存器定义，
 *                          HAL 相关的 .c 文件根本不参与编译。
 *   2. STM32F407xx      —— 默认交叉编译目标，使用本项目自带的裸机寄存器层。
 *   3. + USE_HAL_DRIVER —— 改用 ST 官方 HAL 库 (需自行提供 STM32CubeF4)。
 *                          Phase 1 不启用，留作将来上 USB / SDIO 时的切换点。
 */
#ifndef STM32_MECANUM_STM32F4XX_CONF_H
#define STM32_MECANUM_STM32F4XX_CONF_H

#if defined(TEST_HOST)

/* Host 测试构建：不应有任何文件走到这里来要寄存器定义 */
#  error "stm32f4xx_conf.h must not be included in a TEST_HOST build"

#elif defined(USE_HAL_DRIVER)

/* 官方 HAL 路径。需要在 Makefile 里额外提供 CubeF4 的 include 路径与源文件。 */
#  include "stm32f4xx_hal.h"

#else

/* 默认：本项目自带的最小寄存器层 */
#  include "stm32f407_regs.h"

#endif

/* --- 全固件共用的系统时钟常量 --- */

/** 外部晶振频率 (Hz)。多数 F407 核心板板载 8MHz HSE，换板子改这里。 */
#define HSE_VALUE_HZ        8000000uL
/** SYSCLK / AHB (HPRE=1) */
#define SYSCLK_HZ           168000000uL
/** APB1 = SYSCLK/4 = 42MHz；接在 APB1 上的定时器时钟为其 2 倍 */
#define PCLK1_HZ            (SYSCLK_HZ / 4uL)
#define TIMCLK_APB1_HZ      (PCLK1_HZ * 2uL)
/** APB2 = SYSCLK/2 = 84MHz；APB2 定时器时钟为其 2 倍 */
#define PCLK2_HZ            (SYSCLK_HZ / 2uL)
#define TIMCLK_APB2_HZ      (PCLK2_HZ * 2uL)

#endif /* STM32_MECANUM_STM32F4XX_CONF_H */
