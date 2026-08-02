/**
 * @file mspm0_conf.h
 * @brief 固件的硬件抽象入口 —— 决定"这次编译面向什么目标"
 *
 * 三种编译剖面 (build profile)，源文件只 include 本文件，不直接 include 器件头：
 *
 *   1. TEST_HOST           —— Host 单元测试。不包含任何器件定义。
 *   2. MSPM0_PORT_STUB     —— **默认**。可交叉编译、可链接、可产出 .bin，
 *                             但移植层是空实现，**不是可烧录固件**。见下方说明。
 *   3. USE_TI_DRIVERLIB    —— 真实硬件。需要本机装有 TI MSPM0 SDK。
 *
 * ─────────────────────────────────────────────────────────────────────────
 * ⚠ 为什么默认剖面不是可烧录固件 —— 这一点必须说在最前面
 * ─────────────────────────────────────────────────────────────────────────
 * 麦轮固件 (task-10) 自带了一份手写的 STM32F4 寄存器层，因为 STM32F4 的寄存器
 * 映射是公开且我能逐条核对的。MSPM0G3507 不是这个情况：TI 官方支持的路径是
 * DriverLib + SysConfig 生成引脚配置，而 MSPM0 SDK 无法在 GitHub Actions 上
 * 免登录安装。
 *
 * 面对这个约束有两个选择：
 *   (a) 凭印象手写一套 MSPM0 寄存器地址。能编过、CI 会绿、看起来很完整 ——
 *       然后在某个人真的烧录时以最难排查的方式失败。
 *   (b) 把移植层收窄成十几个原语函数，默认给空实现，并在每一处
 *       (本文件 / README §2 / .bin 文件名 / 构建横幅) 标明这不是可烧录固件。
 *
 * 本工程选 (b)。代价是"make 出来的 .bin 不能直接用"，收益是没有人会
 * 因为一份看起来很完整的固件而浪费一整天。要得到可烧录固件，见 README §7。
 *
 * 需要强调的是：**被空实现掉的只有 port_stub.c 里那十几个寄存器原语**。
 * 运动学、协议、PID、测速窗口、环形缓冲、故障状态机全部是真实代码，
 * 且已被 71 个 Host 用例覆盖。
 */
#ifndef MSPM0_DIFF_MSPM0_CONF_H
#define MSPM0_DIFF_MSPM0_CONF_H

#if defined(TEST_HOST)

/* Host 测试构建：不应有任何文件走到这里来要器件定义 */
#  error "mspm0_conf.h must not be included in a TEST_HOST build"

#elif defined(USE_TI_DRIVERLIB)

/* 真实硬件路径。需要在 Makefile 里额外提供 MSPM0 SDK 的 include 路径与源文件，
   并用 SysConfig 生成 ti_msp_dl_config.h/.c。 */
#  include <ti/devices/msp/msp.h>
#  include <ti/driverlib/driverlib.h>
#  include "ti_msp_dl_config.h"
#  define FW_BUILD_PROFILE   "driverlib"

#else

/* 默认：可链接但不可烧录的 CI 验证剖面 */
#  define MSPM0_PORT_STUB    1
#  define FW_BUILD_PROFILE   "ci-link"

#endif

/* --- 全固件共用的系统时钟常量 --- */

/** MSPM0G3507 最高主频 (Hz)。SYSPLL 由内部 SYSOSC 倍频而来，无需外部晶振。 */
#define SYSCLK_HZ           80000000uL
/** 外设总线时钟。MSPM0 的 TIMG/TIMA/UART 默认挂在 BUSCLK 上。 */
#define BUSCLK_HZ           SYSCLK_HZ

#endif /* MSPM0_DIFF_MSPM0_CONF_H */
