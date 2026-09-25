/**
 * @file protocol_frame.c
 * @brief 串口二进制帧的打包 / 拆包实现 (ADR-0003)
 *
 * 纯 C99，无 HAL 依赖，可在 arm-none-eabi-gcc 与 Host gcc 下同时编译。
 */
#include "protocol_frame.h"

#include <string.h>

#include "crc16.h"

/* --- 小端序标量读写 --------------------------------------------------- */

void frame_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

uint16_t frame_get_u16(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

/* float 走 memcpy 转 uint32 再逐字节写出：
   既避免了严格别名 (strict aliasing) 未定义行为，也与宿主机字节序无关。
   前提是双方均为 IEEE-754 binary32 —— Cortex-M4F 与树莓派 ARM64 都满足。 */
void frame_put_f32(uint8_t *dst, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    dst[0] = (uint8_t)(bits & 0xFFu);
    dst[1] = (uint8_t)((bits >> 8) & 0xFFu);
    dst[2] = (uint8_t)((bits >> 16) & 0xFFu);
    dst[3] = (uint8_t)((bits >> 24) & 0xFFu);
}

float frame_get_f32(const uint8_t *src)
{
    uint32_t bits = (uint32_t)src[0]
                  | ((uint32_t)src[1] << 8)
                  | ((uint32_t)src[2] << 16)
                  | ((uint32_t)src[3] << 24);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* --- 打包 ------------------------------------------------------------- */

int frame_encode(uint8_t cmd, const uint8_t *data, uint8_t data_len,
                 uint8_t *out, uint16_t out_cap)
{
    if (out == 0 || (data_len > 0u && data == 0)) {
        return FRAME_ERR_NULL;
    }
    if (data_len > FRAME_MAX_DATA_LEN) {
        return FRAME_ERR_TOO_LONG;
    }

    const uint16_t total = (uint16_t)data_len + 6u;  /* SOF+LEN+CMD+DATA+CRC16+EOF */
    if (out_cap < total) {
        return FRAME_ERR_NO_SPACE;
    }

    out[0] = FRAME_SOF;
    out[1] = (uint8_t)(data_len + FRAME_LEN_OVERHEAD);
    out[2] = cmd;
    if (data_len > 0u) {
        memcpy(&out[3], data, data_len);
    }

    /* CRC 覆盖 CMD + DATA */
    const uint16_t crc = crc16_ccitt(&out[2], (uint16_t)(data_len + 1u));
    frame_put_u16(&out[3 + data_len], crc);
    out[5 + data_len] = FRAME_EOF;

    return (int)total;
}

/* --- 拆包 ------------------------------------------------------------- */

void frame_parser_init(FrameParser *parser)
{
    if (parser == 0) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser->state = FRAME_STATE_SOF;
}

void frame_parser_reset(FrameParser *parser)
{
    if (parser == 0) {
        return;
    }
    if (parser->state != FRAME_STATE_SOF) {
        parser->stat_resyncs++;
    }
    parser->state = FRAME_STATE_SOF;
    parser->len_field = 0u;
    parser->index = 0u;
}

bool frame_parser_push(FrameParser *parser, uint8_t byte, Frame *out)
{
    if (parser == 0 || out == 0) {
        return false;
    }

    switch (parser->state) {
    case FRAME_STATE_SOF:
        if (byte == FRAME_SOF) {
            parser->state = FRAME_STATE_LEN;
        }
        break;

    case FRAME_STATE_LEN:
        if (byte >= FRAME_MIN_LEN_FIELD) {
            /* LEN 上界即 uint8_t 上界，无需再判上限 */
            parser->len_field = byte;
            parser->index = 0u;
            parser->state = FRAME_STATE_PAYLOAD;
        } else {
            parser->stat_err_len++;
            /* 非法 LEN。若该字节本身是 SOF，说明上一个 SOF 是噪声，
               留在本状态用新的 SOF 继续；否则彻底重新搜索 SOF。 */
            parser->state = (byte == FRAME_SOF) ? FRAME_STATE_LEN : FRAME_STATE_SOF;
        }
        break;

    case FRAME_STATE_PAYLOAD:
    default: {
        parser->buf[parser->index++] = byte;
        if (parser->index < parser->len_field) {
            break;
        }

        /* payload 收齐：buf = CMD(1) + DATA(n) + CRC(2) + EOF(1) */
        parser->state = FRAME_STATE_SOF;

        const uint8_t data_len = (uint8_t)(parser->len_field - FRAME_LEN_OVERHEAD);
        if (parser->buf[parser->len_field - 1u] != FRAME_EOF) {
            parser->stat_err_eof++;
            break;
        }

        const uint16_t crc_rx = frame_get_u16(&parser->buf[parser->len_field - 3u]);
        const uint16_t crc_calc = crc16_ccitt(parser->buf, (uint16_t)(data_len + 1u));
        if (crc_rx != crc_calc) {
            parser->stat_err_crc++;
            break;
        }

        out->cmd = parser->buf[0];
        out->len = data_len;
        if (data_len > 0u) {
            memcpy(out->data, &parser->buf[1], data_len);
        }
        parser->stat_frames_ok++;
        return true;
    }
    }

    return false;
}

bool frame_parser_push_buffer(FrameParser *parser, const uint8_t *buf, uint16_t len,
                              Frame *out, uint16_t *consumed)
{
    if (parser == 0 || buf == 0 || out == 0) {
        if (consumed != 0) {
            *consumed = 0u;
        }
        return false;
    }

    for (uint16_t i = 0; i < len; i++) {
        if (frame_parser_push(parser, buf[i], out)) {
            if (consumed != 0) {
                *consumed = (uint16_t)(i + 1u);
            }
            return true;
        }
    }

    if (consumed != 0) {
        *consumed = len;
    }
    return false;
}
