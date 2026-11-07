/**
 * @file motor.h
 * @brief 电机 PWM 输出、方向控制与电流采样
 *
 * 与麦轮固件不同，本模块**不含任何寄存器访问** —— 全部通过 mcu_port.h 完成。
 */
#ifndef MSPM0_DIFF_MOTOR_H
#define MSPM0_DIFF_MOTOR_H

#include "kinematics.h"

/** 配置 DRV8871 的 PWM/方向 GPIO */
void motor_init(void);

/**
 * 设置某轮的输出占空比。
 * @param wheel 轮索引 0..1，越界忽略
 * @param duty  [-1.0, 1.0]，正=前进；超出范围会被钳位；NaN 视为 0
 */
void motor_set_duty(int wheel, float duty);

/** 立即停止全部电机：占空比清零 + 方向脚双低 (H 桥滑行) */
void motor_all_stop(void);

/**
 * 让 H 桥进入刹车状态 (方向脚双高)，比滑行停得更快。
 * 用于急停与过流保护。
 */
void motor_brake_all(void);

/** 读回某轮当前生效的占空比 */
float motor_get_duty(int wheel);

/**
 * 填充双路电机电流。当前 DRV8871 板没有反馈输出，全部返回 NaN。
 */
void motor_sample_currents(float out[NUM_WHEELS]);

#endif /* MSPM0_DIFF_MOTOR_H */
