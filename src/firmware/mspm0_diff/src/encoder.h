/**
 * @file encoder.h
 * @brief AB 相编码器测速 (正交解码 4 倍频 + 窗口测速 + 一阶低通)
 *
 * 与麦轮固件不同，本模块**不含任何寄存器访问** —— 计数器读取通过
 * mcu_port.h 的 port_encoder_read_count() 完成。因此测速窗口与滤波这两段
 * 真正容易出错的逻辑可以在 Host 上直接测 (test_encoder.c)。
 */
#ifndef MSPM0_DIFF_ENCODER_H
#define MSPM0_DIFF_ENCODER_H

#include <stdint.h>

#include "kinematics.h"

/** 配置两路正交编码器并清零内部状态 */
void encoder_init(void);

/**
 * 采样一次编码器。必须在 1kHz 控制中断里调用。
 *
 * 内部按 ENCODER_SPEED_WINDOW_TICKS 个周期为一个测速窗口累积计数，
 * 窗口满时刷新转速估计并做一阶低通。窗口未满时转速保持上一次的值。
 * 理由见 board_config.h §二 的量化分辨率推导。
 */
void encoder_update(void);

/**
 * 读取某轮的滤波后转速 (RPM)，正 = 推动车体前进的方向。
 * @param wheel 轮索引 0..1，越界返回 0
 */
float encoder_get_rpm(int wheel);

/** 读取某轮自上电以来的累计计数 (已按车体方向修正符号)，用于里程计 */
int32_t encoder_get_total_counts(int wheel);

/** 清零累计计数与转速估计 (急停恢复、里程计重置时调用) */
void encoder_reset(void);

#endif /* MSPM0_DIFF_ENCODER_H */
