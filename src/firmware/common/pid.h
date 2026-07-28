/**
 * @file pid.h
 * @brief 通用增量限幅 PID 速度环控制器 (task-10 麦轮 4 路 / task-11 差速 2 路共用)
 *
 * 纯 C99，无 HAL 依赖、无动态内存、无浮点库调用 (只用四则运算)，
 * 可在 Cortex-M4F (硬浮点) 与 Cortex-M0+ (软浮点) 上同时使用。
 *
 * 控制量约定：
 *   setpoint / measured 单位 RPM，输出为 PWM 占空比 [-1, 1]，正=前进。
 */
#ifndef FIRMWARE_COMMON_PID_H
#define FIRMWARE_COMMON_PID_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 单路 PID 控制器状态 */
typedef struct {
    /* 整定参数 */
    float kp;
    float ki;
    float kd;
    float integral_limit;   /**< 积分项绝对值上限 (抗饱和) */
    float output_limit;     /**< 输出绝对值上限，速度环取 1.0 (占空比) */

    /* 内部状态 —— 调用方不应直接读写 */
    float integral;
    float prev_error;
    bool  has_prev;         /**< 首次调用时跳过微分，避免微分冲击 */
} PIDController;

/**
 * 初始化 PID 控制器。负的限幅值会被取绝对值。
 *
 * @param pid            控制器实例
 * @param kp,ki,kd       整定参数
 * @param integral_limit 积分限幅 (绝对值)
 * @param output_limit   输出限幅 (绝对值)，速度环传 1.0
 */
void pid_init(PIDController *pid, float kp, float ki, float kd,
              float integral_limit, float output_limit);

/**
 * 在线修改整定参数，不影响积分器与微分历史。
 */
void pid_set_gains(PIDController *pid, float kp, float ki, float kd);

/**
 * 计算一次 PID 输出。
 *
 * 抗积分饱和策略：条件积分 (conditional integration) + 积分限幅。
 * 当输出已经饱和、且误差还会把输出推向同一饱和方向时，本周期不累加积分。
 *
 * @param pid      控制器实例
 * @param setpoint 目标值 (RPM)
 * @param measured 实测值 (RPM)
 * @param dt       控制周期 (秒)，典型 0.001 (1kHz)；dt<=0 时直接返回 0
 * @return PWM 占空比 [-output_limit, +output_limit]，正=前进，负=后退
 */
float pid_update(PIDController *pid, float setpoint, float measured, float dt);

/**
 * 清空积分器与微分历史 (急停、底盘使能切换时调用)。整定参数保持不变。
 */
void pid_reset(PIDController *pid);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_COMMON_PID_H */
