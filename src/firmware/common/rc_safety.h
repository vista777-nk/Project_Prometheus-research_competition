/**
 * @file rc_safety.h
 * @brief IA6B 地面车解锁、失联与 MANUAL/AUTO 仲裁状态机
 */
#ifndef FIRMWARE_COMMON_RC_SAFETY_H
#define FIRMWARE_COMMON_RC_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#include "ibus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* iBUS 数组为零基索引；用户可见通道号分别为 CH1/CH2/CH4/CH5/CH6。 */
#define RC_CHANNEL_X_INDEX       0u
#define RC_CHANNEL_Y_INDEX       1u
#define RC_CHANNEL_YAW_INDEX     3u
#define RC_CHANNEL_ARM_INDEX     4u
#define RC_CHANNEL_MODE_INDEX    5u

typedef enum {
    RC_CONTROL_DISABLED = 0,
    RC_CONTROL_MANUAL,
    RC_CONTROL_AUTO
} RcControlMode;

typedef struct {
    uint16_t center;
    uint16_t span;
    uint16_t deadband;
    uint16_t valid_min;
    uint16_t valid_max;
    uint16_t switch_low_max;
    uint16_t switch_high_min;
    uint32_t frame_timeout_ms;
    uint32_t neutral_hold_ms;
} RcSafetyConfig;

typedef struct {
    RcSafetyConfig cfg;
    uint32_t last_frame_ms;
    uint32_t neutral_since_ms;
    float axis_x;
    float axis_y;
    float axis_yaw;
    bool valid_frame_seen;
    bool arm_low_seen;
    bool arming;
    bool armed;
    bool auto_selected;
    bool sticks_neutral;
} RcSafety;

typedef struct {
    RcControlMode mode;
    bool armed;
    bool frame_fresh;
    bool manual_override;
    float axis_x;
    float axis_y;
    float axis_yaw;
} RcSafetyOutput;

void rc_safety_init(RcSafety *state, const RcSafetyConfig *config);

/**
 * 接收一帧校验已通过的 iBUS 通道。通道越界或开关停在中间区会立即解除解锁。
 * @return true 表示通道范围与开关位置有效；false 表示该帧被安全拒绝。
 */
bool rc_safety_accept_frame(RcSafety *state, const IBusChannels *channels,
                            uint32_t now_ms);

/**
 * 读取当前安全输出；超过 frame_timeout_ms 会立即解除解锁，且必须再次经历
 * CH5 低→高和连续中位保持才能恢复。
 */
RcSafetyOutput rc_safety_poll(RcSafety *state, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_COMMON_RC_SAFETY_H */
