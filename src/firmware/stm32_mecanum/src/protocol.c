/**
 * @file protocol.c
 * @brief 命令分发与遥测组装实现
 */
#include "protocol.h"

#include <math.h>
#include <string.h>

#include "version.h"

/** 发送缓冲：最大帧 = TELEMETRY 34 字节载荷 + 6 字节开销，留够余量 */
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

/** 组帧并发送。组帧失败时回 ERROR 帧 (但不递归：ERROR 自身失败就静默丢弃)。 */
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

    RobotVelocity cmd;
    cmd.vx = frame_get_f32(&frame->data[0]);
    cmd.vy = frame_get_f32(&frame->data[4]);
    cmd.omega = frame_get_f32(&frame->data[8]);

    /* NaN / Inf 会一路污染 PID 积分器，必须在入口拦下 */
    if (!isfinite(cmd.vx) || !isfinite(cmd.vy) || !isfinite(cmd.omega)) {
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
