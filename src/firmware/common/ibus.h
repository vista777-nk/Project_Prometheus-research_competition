/**
 * @file ibus.h
 * @brief Flysky AFHDS 2A iBUS 接收机帧解析（两块底盘 MCU 共用）
 *
 * FS-iA6B 的 iBUS-SERVO 口输出 115200 8N1、32 字节定长帧：
 * 0x20, 0x40, 14×uint16 little-endian, checksum little-endian。
 * checksum = 0xFFFF - 前 30 字节逐字节之和。
 */
#ifndef FIRMWARE_COMMON_IBUS_H
#define FIRMWARE_COMMON_IBUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IBUS_FRAME_LENGTH       32u
#define IBUS_CHANNEL_COUNT      14u
#define IBUS_FRAME_LENGTH_BYTE  0x20u
#define IBUS_SERVO_COMMAND      0x40u

typedef struct {
    uint16_t channels[IBUS_CHANNEL_COUNT];
} IBusChannels;

typedef struct {
    uint8_t buffer[IBUS_FRAME_LENGTH];
    uint8_t index;
    uint32_t frames_ok;
    uint32_t checksum_errors;
    uint32_t header_errors;
} IBusParser;

void ibus_parser_init(IBusParser *parser);
bool ibus_parser_push(IBusParser *parser, uint8_t byte, IBusChannels *out);

/**
 * 把一个通道映射到 [-1, 1]，中心死区内返回 0。
 * span 与 deadband 都使用接收机原始单位（典型范围约 1000..2000）。
 */
float ibus_channel_unit(uint16_t value, uint16_t center,
                        uint16_t span, uint16_t deadband);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_COMMON_IBUS_H */
