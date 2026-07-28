/**
 * @file encoder.h
 * @brief AB 相编码器读取 (TIM2/3/4/5 编码器模式，4 倍频)
 *
 * 只参与交叉编译，不进入 Host 单元测试 —— 测试侧通过 Mock 转速直接驱动 PID。
 */
#ifndef STM32_MECANUM_ENCODER_H
#define STM32_MECANUM_ENCODER_H

#include <stdint.h>

#include "kinematics.h"

/** 配置 TIM2/3/4/5 为编码器模式并清零计数。需在 bsp_init() 之后调用。 */
void encoder_init(void);

/**
 * 采样一次编码器。必须在 1kHz 控制中断里调用。
 *
 * 内部按 ENCODER_SPEED_WINDOW_TICKS 个周期为一个测速窗口累积计数，
 * 窗口满时刷新转速估计并做一阶低通。窗口未满时转速保持上一次的值。
 */
void encoder_update(void);

/**
 * 读取某轮的滤波后转速 (RPM)，正 = 推动车体前进的方向。
 * @param wheel 轮索引 0..3，越界返回 0
 */
float encoder_get_rpm(int wheel);

/** 读取某轮自上电以来的累计计数 (已按车体方向修正符号)，用于里程计 */
int32_t encoder_get_total_counts(int wheel);

/** 清零累计计数与转速估计 (急停恢复、里程计重置时调用) */
void encoder_reset(void);

#endif /* STM32_MECANUM_ENCODER_H */
