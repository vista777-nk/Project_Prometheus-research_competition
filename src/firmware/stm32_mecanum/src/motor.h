/**
 * @file motor.h
 * @brief 电机 PWM 输出、方向控制与电流采样
 *
 * 本模块**不含任何寄存器访问** —— 全部通过 mcu_port.h 完成。
 */
#ifndef STM32_MECANUM_MOTOR_H
#define STM32_MECANUM_MOTOR_H

#include "kinematics.h"

/** 配置 DRV8871 的 PWM/方向 GPIO。需在 port_system_init() 之后调用。 */
void motor_init(void);

/**
 * 设置某轮的输出占空比。
 * @param wheel 轮索引 0..3，越界忽略
 * @param duty  [-1.0, 1.0]，正=前进；超出范围会被钳位；NaN 视为 0
 */
void motor_set_duty(int wheel, float duty);

/** 立即停止全部电机：DRV8871 IN1=IN2=0（休眠/高阻） */
void motor_all_stop(void);

/**
 * 让 DRV8871 进入 IN1=IN2=1 的慢衰减/刹车状态。
 * 用于急停与过流保护。
 */
void motor_brake_all(void);

/** 读回某轮当前生效的占空比 */
float motor_get_duty(int wheel);

/**
 * 填充四路电机电流。当前 DRV8871 板没有反馈输出，全部返回 NaN；
 * 以后增加外部分流/放大电路后才能改为实测值。
 */
void motor_sample_currents(float out[NUM_WHEELS]);

#endif /* STM32_MECANUM_MOTOR_H */
