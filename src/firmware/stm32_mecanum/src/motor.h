/**
 * @file motor.h
 * @brief 电机 PWM 输出与电流采样 (TIM1 四路 PWM + TB6612 方向控制 + ADC1)
 *
 * 只参与交叉编译，不进入 Host 单元测试。
 */
#ifndef STM32_MECANUM_MOTOR_H
#define STM32_MECANUM_MOTOR_H

#include "kinematics.h"

/** 配置 TIM1 PWM、方向 GPIO 与 ADC1。需在 bsp_init() 之后调用。 */
void motor_init(void);

/**
 * 设置某轮的输出占空比。
 * @param wheel 轮索引 0..3，越界忽略
 * @param duty  [-1.0, 1.0]，正=前进；超出范围会被钳位；NaN 视为 0
 */
void motor_set_duty(int wheel, float duty);

/** 立即停止全部电机：占空比清零 + 方向脚双低 (TB6612 滑行) */
void motor_all_stop(void);

/**
 * 让 H 桥进入刹车状态 (方向脚双高)，比滑行停得更快。
 * 用于急停与过流保护。
 */
void motor_brake_all(void);

/** 读回某轮当前生效的占空比 */
float motor_get_duty(int wheel);

/**
 * 采样四路电机电流 (A)。ADC 单次转换轮询，约 4×2µs，
 * 只在遥测周期 (20Hz) 于主循环里调用，不在控制中断中调用。
 */
void motor_sample_currents(float out[NUM_WHEELS]);

#endif /* STM32_MECANUM_MOTOR_H */
