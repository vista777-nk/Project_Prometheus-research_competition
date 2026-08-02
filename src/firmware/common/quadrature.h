/**
 * @file quadrature.h
 * @brief GPIO 双边沿正交编码器解码器（MSPM0 两轮共用算法）
 */
#ifndef FIRMWARE_COMMON_QUADRATURE_H
#define FIRMWARE_COMMON_QUADRATURE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t count;
    uint32_t invalid_transitions;
    uint8_t previous_state;
    bool initialized;
} QuadratureDecoder;

/** 用当前 A/B 电平建立初始状态；初始化本身不产生计数。 */
void quadrature_init(QuadratureDecoder *decoder, bool phase_a, bool phase_b);

/**
 * 输入任一相双边沿中断后的 A/B 电平。
 * 正向 Gray 序列 00→01→11→10→00 每边沿 +1；反向每边沿 -1。
 */
void quadrature_update(QuadratureDecoder *decoder, bool phase_a, bool phase_b);

/** 返回低 16 位原始计数，供现有 encoder.c 的回绕差分逻辑使用。 */
uint16_t quadrature_count16(const QuadratureDecoder *decoder);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_COMMON_QUADRATURE_H */
