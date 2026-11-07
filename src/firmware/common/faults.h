/**
 * @file faults.h
 * @brief 故障位图与故障状态机 (task-10 麦轮 / task-11 差速共用)
 *
 * ─────────────────────────────────────────────────────────────────────────
 * 为什么这个模块必须存在
 * ─────────────────────────────────────────────────────────────────────────
 * 故障位图是**线上契约**：树莓派端按这些位解读下位机状态。既然两块板共用同一套
 * 位定义，位定义就该只有一份 —— 分别写在两个 protocol.h 里迟早会漂移。
 *
 * 更要紧的是第二件事。故障的置位/清除逻辑原先散在各板 main.c 的 static 函数里，
 * 结果是**一个单元测试都覆盖不到**：它依赖 1kHz 中断、依赖真实编码器、
 * 依赖 ADC 采样。而这些逻辑恰恰是 task-10 评审阶段用真实缺陷换来的：
 *
 *   · FAULT_STALL 只置不清 → 一次瞬时堵转，故障灯亮到复位
 *   · FAULT_UART_ERROR 用累计计数 → 开机一次噪声，故障位永久挂着
 *   · 停机期间 STALL 位的语义 → 电机已刹停时"轮子不转"不构成堵转证据
 *
 * 三条契约全都只能靠上板检查清单验证，这是不可接受的。把判定逻辑抽成纯函数
 * 之后，它们可以被表驱动测试逐条钉死。
 *
 * ─────────────────────────────────────────────────────────────────────────
 * 三类故障的生命周期 —— 这是本模块的核心语义
 * ─────────────────────────────────────────────────────────────────────────
 * | 类型 | 位 | 清除方式 |
 * |------|-----|----------|
 * | **锁存型** | ESTOP | 只能复位退出。安全优先于可用性 |
 * | **跟随型** | STALL / KINEMATICS_SAT / OVERCURRENT / CMD_TIMEOUT | 条件消失即清 |
 * | **增量型** | UART_ERROR | 比较"距上次评估有无新增错误"，而非累计值 |
 *
 * @warning **位与位之间的优先级也属于协议。** 只定义每一位单独的含义是不够的：
 *   ESTOP 或 OVERCURRENT 置位期间电机已刹停，此时"轮子不转"不构成堵转证据，
 *   因此 STALL 停止更新、保持旧值。上位机在看到 ESTOP/OVERCURRENT 时
 *   **必须忽略 STALL**。这条规则由 faults_evaluate_motion() 内部的早退顺序保证，
 *   并由 test_faults.c 钉死。
 */
#ifndef FIRMWARE_COMMON_FAULTS_H
#define FIRMWARE_COMMON_FAULTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 故障位图 (线上契约，两块板逐位一致) =====================
 * TELEMETRY 帧末尾的 uint16，小端。改动需同步改两块板的固件与上位机解码器。
 */
#define FAULT_NONE              0x0000u
#define FAULT_OVERCURRENT       0x0001u  /**< 任一电机电流超过阈值 */
#define FAULT_STALL             0x0002u  /**< 有目标转速但轮子不转 (堵转/断线)。
                                              见本文件顶部 @warning：ESTOP/OVERCURRENT
                                              置位期间本位保持旧值，上位机应忽略 */
#define FAULT_CMD_TIMEOUT       0x0004u  /**< 超时未收到速度指令，已自动刹停 */
#define FAULT_ESTOP             0x0008u  /**< 硬件急停被触发 (锁存，只能复位退出) */
#define FAULT_KINEMATICS_SAT    0x0010u  /**< 速度指令超出底盘能力，已等比缩放 */
#define FAULT_UART_ERROR        0x0020u  /**< 串口链路错误率异常 (CRC/溢出) */

/** 支持的最大轮数：麦轮 4 / 差速 2 */
#define FAULTS_MAX_WHEELS       4

/** 故障判定阈值。各板从自己的 board_config.h 填充。 */
typedef struct {
    float    stall_target_rpm;   /**< 目标转速高于此值才考虑堵转 */
    float    stall_rpm_floor;    /**< 实测转速低于此值算"没转" */
    uint32_t stall_ticks;        /**< 持续多少个评估周期才判定堵转 */
    float    current_limit_a;    /**< 单电机过流阈值 (A) */
    uint32_t cmd_timeout_ms;     /**< 多久未收到速度指令即判超时 */
} FaultConfig;

/** 故障状态机实例 (每块板一个)。字段为内部状态，调用方不应直接读写。 */
typedef struct {
    FaultConfig cfg;
    uint16_t    bitmap;
    uint32_t    stall_count[FAULTS_MAX_WHEELS];
    bool        estop_latched;

    /* 链路统计的上一拍快照，用于"增量型"判定 */
    uint32_t    last_crc_errors;
    uint32_t    last_rx_overruns;
    uint32_t    last_tx_drops;
    bool        link_primed;     /**< 首次评估只记基线，不报错 */
} FaultMonitor;

/** 运动侧输入 (控制中断里每周期一次) */
typedef struct {
    bool         estop_asserted;   /**< 硬件急停引脚当前电平判定结果 */
    int          wheel_count;      /**< 实际轮数，≤ FAULTS_MAX_WHEELS */
    const float *target_rpm;       /**< 各轮目标转速 */
    const float *actual_rpm;       /**< 各轮实测转速 */
    bool         kinematics_saturated;
    uint32_t     command_age_ms;   /**< 距上次收到速度指令的时间 */
} FaultMotionInput;

/** 链路侧输入 (遥测周期里每次一次) */
typedef struct {
    int          wheel_count;
    const float *current_a;        /**< 各轮电流 (A)，可为 NULL 表示无采样 */
    uint32_t     crc_errors;       /**< 累计值，本模块内部转成增量 */
    uint32_t     rx_overruns;
    uint32_t     tx_drops;
} FaultLinkInput;

/**
 * 初始化。cfg 为 NULL 时全部阈值取 0（等价于禁用所有判定）。
 */
void faults_init(FaultMonitor *m, const FaultConfig *cfg);

/**
 * 评估运动侧故障，返回最新位图。应在控制中断里每周期调用一次。
 *
 * 内部的早退顺序即是位间优先级（见本文件顶部 @warning）：
 *   1. 急停：锁存 ESTOP，其余位一律不动，立即返回
 *   2. 已过流：OVERCURRENT 已由链路侧置位，此时电机已刹停，
 *      STALL 与 KINEMATICS_SAT 不动，立即返回
 *   3. 正常：更新 CMD_TIMEOUT / KINEMATICS_SAT / STALL
 *
 * @param in 为 NULL 时不做任何更新，直接返回当前位图
 */
uint16_t faults_evaluate_motion(FaultMonitor *m, const FaultMotionInput *in);

/**
 * 评估链路侧故障 (过流 + 串口)，返回最新位图。应在遥测周期调用。
 *
 * UART_ERROR 采用**增量**判定：只看距上次调用有没有新增错误，而不是累计值。
 * 用累计值的话，开机时的一次噪声就会让故障位永远挂着。
 * 首次调用只记录基线，不报错。
 *
 * @param in 为 NULL 时不做任何更新，直接返回当前位图
 */
uint16_t faults_evaluate_link(FaultMonitor *m, const FaultLinkInput *in);

/** 当前位图 */
uint16_t faults_get(const FaultMonitor *m);

/**
 * 是否应当立即刹停并跳过本周期的 PID 输出。
 * 等价于 (ESTOP | OVERCURRENT) 中任一置位。
 */
bool faults_should_halt(const FaultMonitor *m);

/** 急停是否已锁存 (状态灯常亮判据) */
bool faults_estop_latched(const FaultMonitor *m);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_COMMON_FAULTS_H */
