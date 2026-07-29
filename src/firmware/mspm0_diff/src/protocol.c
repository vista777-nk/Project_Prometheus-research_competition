/**
 * @file protocol.c
 * @brief 命令分发与遥测组装实现
 */
#include "protocol.h"

#include <math.h>
#include <string.h>

#include "version.h"

/** 发送缓冲：最大定长帧 = TELEMETRY 18 字节载荷 + 6 字节开销 = 24；
 *  ERROR 帧变长但上限受此约束，超出部分在 protocol_send_error() 里截断。 */
#define TX_BUFFER_SIZE      64u

static ProtocolWriteFn  s_write_fn;
static void            *s_write_ctx;
static ProtocolHandlers s_handlers;
static FrameParser      s_parser;

static void emit(const uint8_t *data, uint16_t len)
{
    if (s_write_fn != 0 && len > 0u) {
        s_write_fn(data, len, s_write_ctx);
    }
}

/** 组帧并发送。组帧失败时静默丢弃 —— 不递归回 ERROR，否则缓冲不足会无限套娃。 */
static bool emit_frame(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    uint8_t tx[TX_BUFFER_SIZE];
    const int written = frame_encode(cmd, data, data_len, tx, (uint16_t)sizeof(tx));
    if (written <= 0) {
        return false;
    }
    emit(tx, (uint16_t)written);
    return true;
}

void protocol_init(ProtocolWriteFn write_fn, void *write_ctx,
                   const ProtocolHandlers *handlers)
{
    s_write_fn = write_fn;
    s_write_ctx = write_ctx;
    if (handlers != 0) {
        s_handlers = *handlers;
    } else {
        memset(&s_handlers, 0, sizeof(s_handlers));
    }
    frame_parser_init(&s_parser);
}

const FrameParser *protocol_get_parser(void)
{
    return &s_parser;
}

void protocol_notify_line_idle(void)
{
    frame_parser_reset(&s_parser);
}

/* --- 发送侧 --- */

void protocol_send_ack(uint8_t acked_cmd)
{
    const uint8_t payload[PAYLOAD_LEN_ACK] = { acked_cmd };
    (void)emit_frame(CMD_ACK, payload, PAYLOAD_LEN_ACK);
}

void protocol_send_pong(void)
{
    const uint8_t payload[PAYLOAD_LEN_PONG] = {
        (uint8_t)FW_MAJOR,
        (uint8_t)FW_MINOR,
        (uint8_t)FW_PATCH,
        (uint8_t)FW_BOARD_TYPE,
        (uint8_t)FW_CHASSIS_TYPE
    };
    (void)emit_frame(CMD_PONG, payload, PAYLOAD_LEN_PONG);
}

void protocol_send_telemetry(const float rpm[NUM_WHEELS],
                             const float current[NUM_WHEELS],
                             uint16_t fault)
{
    uint8_t payload[PAYLOAD_LEN_TELEMETRY];
    uint8_t *cursor = payload;

    /* 布局与麦轮固件同构：先所有 RPM，再所有电流，最后故障位图。
       只是轮数从 4 变成 2，所以 34 → 18 字节。 */
    for (int i = 0; i < NUM_WHEELS; i++) {
        frame_put_f32(cursor, (rpm != 0) ? rpm[i] : 0.0f);
        cursor += 4;
    }
    for (int i = 0; i < NUM_WHEELS; i++) {
        frame_put_f32(cursor, (current != 0) ? current[i] : 0.0f);
        cursor += 4;
    }
    frame_put_u16(cursor, fault);

    (void)emit_frame(CMD_TELEMETRY, payload, PAYLOAD_LEN_TELEMETRY);
}

void protocol_send_error(uint8_t code, const uint8_t *detail, uint8_t detail_len)
{
    uint8_t payload[FRAME_MAX_DATA_LEN];

    if (detail == 0) {
        detail_len = 0u;
    }
    if (detail_len > (uint8_t)(FRAME_MAX_DATA_LEN - 1u)) {
        detail_len = (uint8_t)(FRAME_MAX_DATA_LEN - 1u);
    }

    payload[0] = code;
    if (detail_len > 0u) {
        memcpy(&payload[1], detail, detail_len);
    }
    (void)emit_frame(CMD_ERROR, payload, (uint8_t)(detail_len + 1u));
}

/* --- 接收侧 --- */

/** DATA 长度校验失败时回 ERROR，详情带上实际收到的长度便于上位机定位 */
static bool reject_length(uint8_t cmd, uint8_t actual_len)
{
    const uint8_t detail[2] = { cmd, actual_len };
    protocol_send_error((uint8_t)PROTO_ERR_BAD_LENGTH, detail, sizeof(detail));
    return false;
}

static bool handle_set_velocity(const Frame *frame)
{
    if (frame->len != PAYLOAD_LEN_SET_VELOCITY) {
        return reject_length(frame->cmd, frame->len);
    }

    DiffVelocity cmd;
    cmd.v = frame_get_f32(&frame->data[0]);
    cmd.omega = frame_get_f32(&frame->data[4]);

    /* NaN / Inf 会一路污染 PID 积分器，必须在入口拦下 */
    if (!isfinite(cmd.v) || !isfinite(cmd.omega)) {
        const uint8_t detail[1] = { frame->cmd };
        protocol_send_error((uint8_t)PROTO_ERR_BAD_VALUE, detail, sizeof(detail));
        return false;
    }

    if (s_handlers.on_set_velocity != 0) {
        s_handlers.on_set_velocity(&cmd, s_handlers.ctx);
    }
    protocol_send_ack(CMD_SET_VELOCITY);
    return true;
}

/**
 * 电赛扩展命令 (0x10)。
 *
 * 存在的意义是"赛场上加一个循迹模块不用动固件结构"：
 * 子命令字节由参赛队现场约定，固件只负责把它路由给 on_extension 回调。
 * 未注册回调时明确回 NOT_IMPLEMENTED，而不是假装成功 —— 上位机能立刻发现
 * 自己在跟一个不支持该扩展的固件说话。
 */
static bool handle_extension(const Frame *frame)
{
    if (frame->len < PAYLOAD_MIN_LEN_EXTENSION) {
        return reject_length(frame->cmd, frame->len);
    }

    const uint8_t sub = frame->data[0];
    const uint8_t payload_len = (uint8_t)(frame->len - 1u);
    const uint8_t *payload = (payload_len > 0u) ? &frame->data[1] : 0;

    if (s_handlers.on_extension == 0) {
        const uint8_t detail[2] = { frame->cmd, sub };
        protocol_send_error((uint8_t)PROTO_ERR_NOT_IMPLEMENTED, detail, sizeof(detail));
        return false;
    }
    if (!s_handlers.on_extension(sub, payload, payload_len, s_handlers.ctx)) {
        const uint8_t detail[2] = { frame->cmd, sub };
        protocol_send_error((uint8_t)PROTO_ERR_NOT_IMPLEMENTED, detail, sizeof(detail));
        return false;
    }

    protocol_send_ack(CMD_EXTENSION);
    return true;
}

bool protocol_dispatch(const Frame *frame)
{
    if (frame == 0) {
        return false;
    }

    switch (frame->cmd) {
    case CMD_SET_VELOCITY:
        return handle_set_velocity(frame);

    case CMD_EMERGENCY_STOP:
        if (frame->len != PAYLOAD_LEN_EMERGENCY_STOP) {
            return reject_length(frame->cmd, frame->len);
        }
        /* 急停先执行再应答：应答丢了可以重发，动作晚了会撞车 */
        if (s_handlers.on_emergency_stop != 0) {
            s_handlers.on_emergency_stop(s_handlers.ctx);
        }
        protocol_send_ack(CMD_EMERGENCY_STOP);
        return true;

    case CMD_PING:
        if (frame->len != PAYLOAD_LEN_PING) {
            return reject_length(frame->cmd, frame->len);
        }
        if (s_handlers.on_ping != 0) {
            s_handlers.on_ping(s_handlers.ctx);
        }
        protocol_send_pong();
        return true;

    case CMD_EXTENSION:
        return handle_extension(frame);

    default: {
        const uint8_t detail[1] = { frame->cmd };
        protocol_send_error((uint8_t)PROTO_ERR_UNKNOWN_CMD, detail, sizeof(detail));
        return false;
    }
    }
}

uint16_t protocol_feed(const uint8_t *bytes, uint16_t len)
{
    if (bytes == 0) {
        return 0u;
    }

    Frame frame;
    uint16_t dispatched = 0u;

    for (uint16_t i = 0; i < len; i++) {
        if (frame_parser_push(&s_parser, bytes[i], &frame)) {
            if (protocol_dispatch(&frame)) {
                dispatched++;
            }
        }
    }
    return dispatched;
}
