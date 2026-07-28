/**
 * @file test_protocol.c
 * @brief 串口协议测试：帧打包/解包往返、CRC 错误注入、命令分发与应答
 *
 * 协议层完全不碰硬件——字节输出通过注入的 mock 写函数捕获——
 * 所以整条"收字节 → 拆帧 → 分发 → 组应答帧"的链路都能在 Host 上闭环验证。
 */
#include <math.h>
#include <string.h>

#include "crc16.h"
#include "protocol.h"
#include "protocol_frame.h"
#include "unity.h"
#include "version.h"

/* --- mock 物理层 --- */

#define MOCK_TX_CAP     1024u

static uint8_t  s_tx[MOCK_TX_CAP];
static uint16_t s_tx_len;

static RobotVelocity s_last_velocity;
static int s_velocity_calls;
static int s_estop_calls;
static int s_ping_calls;

static void mock_write(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx;
    if ((uint32_t)s_tx_len + len > MOCK_TX_CAP) {
        return;   /* 溢出直接丢弃，由用例断言长度来暴露问题 */
    }
    memcpy(&s_tx[s_tx_len], data, len);
    s_tx_len = (uint16_t)(s_tx_len + len);
}

static void on_set_velocity(const RobotVelocity *cmd, void *ctx)
{
    (void)ctx;
    s_last_velocity = *cmd;
    s_velocity_calls++;
}

static void on_emergency_stop(void *ctx)
{
    (void)ctx;
    s_estop_calls++;
}

static void on_ping(void *ctx)
{
    (void)ctx;
    s_ping_calls++;
}

static void reset_protocol_fixture(void)
{
    ProtocolHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.on_set_velocity = on_set_velocity;
    handlers.on_emergency_stop = on_emergency_stop;
    handlers.on_ping = on_ping;

    s_tx_len = 0u;
    s_velocity_calls = 0;
    s_estop_calls = 0;
    s_ping_calls = 0;
    memset(&s_last_velocity, 0, sizeof(s_last_velocity));
    memset(s_tx, 0, sizeof(s_tx));

    protocol_init(mock_write, NULL, &handlers);
}

/** 从 mock 发送缓冲里解析出第 index 帧 (0 起) */
static bool take_tx_frame(uint16_t index, Frame *out)
{
    FrameParser parser;
    frame_parser_init(&parser);

    uint16_t seen = 0u;
    for (uint16_t i = 0; i < s_tx_len; i++) {
        if (frame_parser_push(&parser, s_tx[i], out)) {
            if (seen == index) {
                return true;
            }
            seen++;
        }
    }
    return false;
}

/** 构造一帧 SET_VELOCITY 的原始字节 */
static uint16_t build_set_velocity(uint8_t *out, uint16_t cap,
                                   float vx, float vy, float omega)
{
    uint8_t payload[PAYLOAD_LEN_SET_VELOCITY];
    frame_put_f32(&payload[0], vx);
    frame_put_f32(&payload[4], vy);
    frame_put_f32(&payload[8], omega);
    const int n = frame_encode(CMD_SET_VELOCITY, payload, PAYLOAD_LEN_SET_VELOCITY, out, cap);
    TEST_ASSERT_TRUE(n > 0);
    return (uint16_t)n;
}

/* --- 小端序读写 --- */

static void test_little_endian_helpers(void)
{
    uint8_t buf[4];

    frame_put_u16(buf, 0xBEEFu);
    TEST_ASSERT_EQUAL_HEX8(0xEFu, buf[0]);      /* LSB 在前 */
    TEST_ASSERT_EQUAL_HEX8(0xBEu, buf[1]);
    TEST_ASSERT_EQUAL_HEX16(0xBEEFu, frame_get_u16(buf));

    /* 1.0f 的 IEEE-754 binary32 小端表示为 00 00 80 3F */
    frame_put_f32(buf, 1.0f);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, buf[3]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, frame_get_f32(buf));

    frame_put_f32(buf, -123.456f);
    TEST_ASSERT_EQUAL_FLOAT(-123.456f, frame_get_f32(buf));
}

/* --- 打包 --- */

/**
 * 黄金帧：PING 的完整字节序列被写死在这里。
 * 若哪天有人"顺手优化"了 CRC 参数或帧字段顺序，这条断言会立刻红掉，
 * 提醒对方树莓派端和 MSPM0 端也得同步改 —— 这正是 ADR-0003 的意义。
 */
static void test_frame_encode_ping_golden_bytes(void)
{
    const uint8_t expected[] = { 0xA5u, 0x04u, 0x03u, 0x93u, 0xD1u, 0x5Au };
    uint8_t out[16];

    const int n = frame_encode(CMD_PING, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT((int)sizeof(expected), n);
    TEST_ASSERT_EQUAL_MEMORY(expected, out, sizeof(expected));
}

/** 黄金帧：SET_VELOCITY(vx=1.0, vy=0, ω=0) */
static void test_frame_encode_set_velocity_golden_bytes(void)
{
    const uint8_t expected[] = {
        0xA5u, 0x10u, 0x01u,
        0x00u, 0x00u, 0x80u, 0x3Fu,   /* vx = 1.0f */
        0x00u, 0x00u, 0x00u, 0x00u,   /* vy = 0.0f */
        0x00u, 0x00u, 0x00u, 0x00u,   /* ω  = 0.0f */
        0x0Du, 0xE5u, 0x5Au
    };
    uint8_t out[32];
    const uint16_t n = build_set_velocity(out, sizeof(out), 1.0f, 0.0f, 0.0f);

    TEST_ASSERT_EQUAL_INT((int)sizeof(expected), (int)n);
    TEST_ASSERT_EQUAL_MEMORY(expected, out, sizeof(expected));
}

/** LEN 字段语义：LEN = DATA 长度 + 4 */
static void test_frame_encode_len_field_semantics(void)
{
    uint8_t data[64];
    uint8_t out[128];
    memset(data, 0x11, sizeof(data));

    const int n = frame_encode(0x42u, data, 64u, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(64 + 6, n);
    TEST_ASSERT_EQUAL_HEX8(FRAME_SOF, out[0]);
    TEST_ASSERT_EQUAL_HEX8(64u + FRAME_LEN_OVERHEAD, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x42u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_EOF, out[n - 1]);

    /* CRC 只覆盖 CMD + DATA */
    const uint16_t crc = crc16_ccitt(&out[2], 65u);
    TEST_ASSERT_EQUAL_HEX16(crc, frame_get_u16(&out[3 + 64]));
}

static void test_frame_encode_rejects_bad_arguments(void)
{
    uint8_t out[16];
    uint8_t data[16] = { 0 };

    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL, frame_encode(0x01u, data, 4u, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL, frame_encode(0x01u, NULL, 4u, out, sizeof(out)));
    /* 容量不足：4 字节载荷需要 10 字节输出空间 */
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NO_SPACE, frame_encode(0x01u, data, 4u, out, 9u));
    /* 零载荷 + NULL 指针是合法组合 */
    TEST_ASSERT_EQUAL_INT(6, frame_encode(0x01u, NULL, 0u, out, sizeof(out)));
}

/* --- 拆包 --- */

/** 0~251 字节载荷全长度往返 */
static void test_frame_round_trip_all_payload_lengths(void)
{
    uint8_t raw[FRAME_MAX_TOTAL_LEN];
    uint8_t payload[FRAME_MAX_DATA_LEN];
    Frame decoded;
    FrameParser parser;

    for (uint16_t len = 0; len <= FRAME_MAX_DATA_LEN; len++) {
        for (uint16_t i = 0; i < len; i++) {
            payload[i] = (uint8_t)(i * 7u + len);
        }

        const int n = frame_encode(0x11u, (len > 0u) ? payload : NULL,
                                   (uint8_t)len, raw, sizeof(raw));
        TEST_ASSERT_EQUAL_INT((int)len + 6, n);

        frame_parser_init(&parser);
        bool got = false;
        for (int i = 0; i < n; i++) {
            got = frame_parser_push(&parser, raw[i], &decoded);
            TEST_ASSERT_TRUE_MESSAGE(!got || i == n - 1,
                                     "frame completed before its last byte");
        }
        TEST_ASSERT_TRUE_MESSAGE(got, "round trip failed to produce a frame");
        TEST_ASSERT_EQUAL_HEX8(0x11u, decoded.cmd);
        TEST_ASSERT_EQUAL_UINT(len, decoded.len);
        if (len > 0u) {
            TEST_ASSERT_EQUAL_MEMORY(payload, decoded.data, len);
        }
        TEST_ASSERT_EQUAL_UINT(1u, parser.stat_frames_ok);
    }
}

/** 载荷里出现 SOF/EOF 字节不做转义，靠 LEN 界定边界即可正确解析 */
static void test_parser_accepts_sof_eof_inside_payload(void)
{
    const uint8_t payload[6] = { FRAME_SOF, FRAME_EOF, FRAME_SOF, FRAME_SOF, 0x00u, FRAME_EOF };
    uint8_t raw[32];
    Frame decoded;
    FrameParser parser;

    const int n = frame_encode(CMD_TELEMETRY, payload, sizeof(payload), raw, sizeof(raw));
    TEST_ASSERT_TRUE(n > 0);

    frame_parser_init(&parser);
    uint16_t consumed = 0u;
    TEST_ASSERT_TRUE(frame_parser_push_buffer(&parser, raw, (uint16_t)n, &decoded, &consumed));
    TEST_ASSERT_EQUAL_UINT((uint16_t)n, consumed);
    TEST_ASSERT_EQUAL_MEMORY(payload, decoded.data, sizeof(payload));
}

/** 前导垃圾字节应被跳过，不影响后续正常帧 */
static void test_parser_recovers_from_leading_garbage(void)
{
    /* 末尾刻意不留裸 0xA5：那属于失同步场景，由下一个用例单独覆盖 */
    const uint8_t garbage[] = { 0x00u, 0xFFu, 0xA5u, 0x01u, 0x7Eu, 0x5Au, 0x33u };
    uint8_t raw[32];
    Frame decoded;
    FrameParser parser;

    const int n = frame_encode(CMD_PING, NULL, 0, raw, sizeof(raw));
    frame_parser_init(&parser);

    for (size_t i = 0; i < sizeof(garbage); i++) {
        TEST_ASSERT_FALSE(frame_parser_push(&parser, garbage[i], &decoded));
    }
    bool got = false;
    for (int i = 0; i < n; i++) {
        got = frame_parser_push(&parser, raw[i], &decoded);
    }
    TEST_ASSERT_TRUE_MESSAGE(got, "parser failed to resync after garbage");
    TEST_ASSERT_EQUAL_HEX8(CMD_PING, decoded.cmd);
    /* 垃圾里的 LEN=0x01 < 4，应被记为一次 LEN 非法 */
    TEST_ASSERT_EQUAL_UINT(1u, parser.stat_err_len);
}

/**
 * 失同步与空闲重同步。
 *
 * 场景：噪声在一帧之前插入一个杂散 0xA5，真实 SOF (0xA5 = 165) 于是被当成 LEN，
 * 解析器开始空等 165 字节，把后面的正常帧全吞掉 —— 这是所有"不转义 + 定长头"
 * 协议的固有缺陷 (见 protocol_frame.h @warning)。
 *
 * 本用例先钉死"确实会吞帧"这个事实，再验证 frame_parser_reset() 能把它救回来。
 * 实机上该调用由 USART IDLE 中断触发。
 */
static void test_parser_desyncs_on_stray_sof_and_idle_reset_recovers(void)
{
    uint8_t raw[32];
    Frame decoded;
    FrameParser parser;

    const int n = frame_encode(CMD_PING, NULL, 0, raw, sizeof(raw));
    frame_parser_init(&parser);

    /* 杂散 SOF → 后续真实帧被误读为 LEN=165 的超长帧，一帧也解不出来 */
    TEST_ASSERT_FALSE(frame_parser_push(&parser, FRAME_SOF, &decoded));
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_FALSE_MESSAGE(frame_parser_push(&parser, raw[i], &decoded),
                                  "expected the frame to be swallowed while desynced");
    }
    TEST_ASSERT_EQUAL_UINT(0u, parser.stat_frames_ok);

    /* 线路空闲 → 丢掉半截帧，重新等待 SOF */
    frame_parser_reset(&parser);
    TEST_ASSERT_EQUAL_UINT(1u, parser.stat_resyncs);

    bool got = false;
    for (int i = 0; i < n; i++) {
        got = frame_parser_push(&parser, raw[i], &decoded);
    }
    TEST_ASSERT_TRUE_MESSAGE(got, "idle reset failed to restore framing");
    TEST_ASSERT_EQUAL_HEX8(CMD_PING, decoded.cmd);

    /* 已经对齐时再调用不应记为重同步 */
    frame_parser_reset(&parser);
    TEST_ASSERT_EQUAL_UINT(1u, parser.stat_resyncs);
}

/** CRC 错误注入：载荷任一比特被翻转，帧必须被拒绝 */
static void test_parser_rejects_crc_error(void)
{
    uint8_t raw[32];
    Frame decoded;
    FrameParser parser;

    const uint16_t n = build_set_velocity(raw, sizeof(raw), 0.5f, 0.0f, 0.0f);

    for (uint16_t pos = 2u; pos < n - 1u; pos++) {
        raw[pos] ^= 0x01u;

        frame_parser_init(&parser);
        bool got = false;
        for (uint16_t i = 0; i < n; i++) {
            got = got || frame_parser_push(&parser, raw[i], &decoded);
        }
        TEST_ASSERT_TRUE_MESSAGE(!got, "corrupted frame was accepted");
        TEST_ASSERT_EQUAL_UINT(0u, parser.stat_frames_ok);
        TEST_ASSERT_EQUAL_UINT(1u, parser.stat_err_crc);

        raw[pos] ^= 0x01u;
    }

    /* 恢复原样后应当正常解析 */
    frame_parser_init(&parser);
    bool got = false;
    for (uint16_t i = 0; i < n; i++) {
        got = got || frame_parser_push(&parser, raw[i], &decoded);
    }
    TEST_ASSERT_TRUE(got);
}

/** EOF 字节被破坏 → 计入 stat_err_eof 并丢帧 */
static void test_parser_rejects_bad_eof(void)
{
    uint8_t raw[32];
    Frame decoded;
    FrameParser parser;

    const uint16_t n = build_set_velocity(raw, sizeof(raw), 0.5f, 0.0f, 0.0f);
    raw[n - 1u] = 0x00u;

    frame_parser_init(&parser);
    bool got = false;
    for (uint16_t i = 0; i < n; i++) {
        got = got || frame_parser_push(&parser, raw[i], &decoded);
    }
    TEST_ASSERT_FALSE(got);
    TEST_ASSERT_EQUAL_UINT(1u, parser.stat_err_eof);
    TEST_ASSERT_EQUAL_UINT(0u, parser.stat_err_crc);
}

/** LEN < 4 非法 (最小帧只有 CMD+CRC+EOF) */
static void test_parser_rejects_undersized_len_field(void)
{
    Frame decoded;
    FrameParser parser;
    frame_parser_init(&parser);

    for (uint8_t len = 0u; len < FRAME_MIN_LEN_FIELD; len++) {
        TEST_ASSERT_FALSE(frame_parser_push(&parser, FRAME_SOF, &decoded));
        TEST_ASSERT_FALSE(frame_parser_push(&parser, len, &decoded));
    }
    TEST_ASSERT_EQUAL_UINT(FRAME_MIN_LEN_FIELD, parser.stat_err_len);
}

/** 两帧背靠背连发，必须都能解析出来 */
static void test_parser_handles_back_to_back_frames(void)
{
    uint8_t raw[64];
    Frame decoded;
    FrameParser parser;

    int n1 = frame_encode(CMD_PING, NULL, 0, raw, sizeof(raw));
    int n2 = frame_encode(CMD_EMERGENCY_STOP, NULL, 0, &raw[n1], (uint16_t)(sizeof(raw) - n1));
    const uint16_t total = (uint16_t)(n1 + n2);

    frame_parser_init(&parser);
    uint16_t offset = 0u;
    uint16_t consumed = 0u;

    TEST_ASSERT_TRUE(frame_parser_push_buffer(&parser, raw, total, &decoded, &consumed));
    TEST_ASSERT_EQUAL_HEX8(CMD_PING, decoded.cmd);
    offset = consumed;

    TEST_ASSERT_TRUE(frame_parser_push_buffer(&parser, &raw[offset],
                                              (uint16_t)(total - offset), &decoded, &consumed));
    TEST_ASSERT_EQUAL_HEX8(CMD_EMERGENCY_STOP, decoded.cmd);
    TEST_ASSERT_EQUAL_UINT(2u, parser.stat_frames_ok);
}

/* --- 命令分发 --- */

/** SET_VELOCITY：回调收到正确速度，并回一帧 ACK */
static void test_dispatch_set_velocity(void)
{
    reset_protocol_fixture();

    uint8_t raw[32];
    const uint16_t n = build_set_velocity(raw, sizeof(raw), 0.5f, -0.25f, 1.5f);
    TEST_ASSERT_EQUAL_UINT(1u, protocol_feed(raw, n));

    TEST_ASSERT_EQUAL_INT(1, s_velocity_calls);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, s_last_velocity.vx);
    TEST_ASSERT_EQUAL_FLOAT(-0.25f, s_last_velocity.vy);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, s_last_velocity.omega);

    Frame ack;
    TEST_ASSERT_TRUE(take_tx_frame(0u, &ack));
    TEST_ASSERT_EQUAL_HEX8(CMD_ACK, ack.cmd);
    TEST_ASSERT_EQUAL_UINT(PAYLOAD_LEN_ACK, ack.len);
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_VELOCITY, ack.data[0]);
}

/** 逐字节喂入 (模拟串口中断) 结果必须与整块喂入一致 */
static void test_dispatch_survives_byte_by_byte_feed(void)
{
    reset_protocol_fixture();

    uint8_t raw[32];
    const uint16_t n = build_set_velocity(raw, sizeof(raw), 0.1f, 0.2f, 0.3f);
    for (uint16_t i = 0; i < n; i++) {
        protocol_feed(&raw[i], 1u);
    }

    TEST_ASSERT_EQUAL_INT(1, s_velocity_calls);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, s_last_velocity.vy);
}

/** DATA 长度不符 → 回 ERROR(BAD_LENGTH)，业务回调不得被触发 */
static void test_dispatch_rejects_wrong_payload_length(void)
{
    reset_protocol_fixture();

    uint8_t payload[8] = { 0 };
    uint8_t raw[32];
    const int n = frame_encode(CMD_SET_VELOCITY, payload, sizeof(payload), raw, sizeof(raw));
    TEST_ASSERT_EQUAL_UINT(0u, protocol_feed(raw, (uint16_t)n));
    TEST_ASSERT_EQUAL_INT(0, s_velocity_calls);

    Frame err;
    TEST_ASSERT_TRUE(take_tx_frame(0u, &err));
    TEST_ASSERT_EQUAL_HEX8(CMD_ERROR, err.cmd);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)PROTO_ERR_BAD_LENGTH, err.data[0]);
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_VELOCITY, err.data[1]);
    TEST_ASSERT_EQUAL_HEX8(8u, err.data[2]);
}

/** NaN / Inf 速度必须在协议入口被拦下，绝不能进到 PID 积分器里 */
static void test_dispatch_rejects_non_finite_velocity(void)
{
    reset_protocol_fixture();

    uint8_t raw[32];
    uint16_t n = build_set_velocity(raw, sizeof(raw), NAN, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT(0u, protocol_feed(raw, n));

    n = build_set_velocity(raw, sizeof(raw), 0.0f, INFINITY, 0.0f);
    TEST_ASSERT_EQUAL_UINT(0u, protocol_feed(raw, n));

    TEST_ASSERT_EQUAL_INT(0, s_velocity_calls);

    Frame err;
    TEST_ASSERT_TRUE(take_tx_frame(0u, &err));
    TEST_ASSERT_EQUAL_HEX8(CMD_ERROR, err.cmd);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)PROTO_ERR_BAD_VALUE, err.data[0]);
}

/** EMERGENCY_STOP：先执行动作再回 ACK */
static void test_dispatch_emergency_stop(void)
{
    reset_protocol_fixture();

    uint8_t raw[16];
    const int n = frame_encode(CMD_EMERGENCY_STOP, NULL, 0, raw, sizeof(raw));
    TEST_ASSERT_EQUAL_UINT(1u, protocol_feed(raw, (uint16_t)n));
    TEST_ASSERT_EQUAL_INT(1, s_estop_calls);

    Frame ack;
    TEST_ASSERT_TRUE(take_tx_frame(0u, &ack));
    TEST_ASSERT_EQUAL_HEX8(CMD_ACK, ack.cmd);
    TEST_ASSERT_EQUAL_HEX8(CMD_EMERGENCY_STOP, ack.data[0]);
}

/**
 * PING → PONG，且 board_type / chassis_type 必须是 STM32+麦轮。
 * 树莓派端靠这两个字节校验"插的是不是我以为的那块板"，接错线时拒绝启动。
 */
static void test_dispatch_ping_returns_pong(void)
{
    reset_protocol_fixture();

    uint8_t raw[16];
    const int n = frame_encode(CMD_PING, NULL, 0, raw, sizeof(raw));
    TEST_ASSERT_EQUAL_UINT(1u, protocol_feed(raw, (uint16_t)n));
    TEST_ASSERT_EQUAL_INT(1, s_ping_calls);

    Frame pong;
    TEST_ASSERT_TRUE(take_tx_frame(0u, &pong));
    TEST_ASSERT_EQUAL_HEX8(CMD_PONG, pong.cmd);
    TEST_ASSERT_EQUAL_UINT(PAYLOAD_LEN_PONG, pong.len);
    TEST_ASSERT_EQUAL_HEX8(FW_MAJOR, pong.data[0]);
    TEST_ASSERT_EQUAL_HEX8(FW_MINOR, pong.data[1]);
    TEST_ASSERT_EQUAL_HEX8(FW_PATCH, pong.data[2]);
    TEST_ASSERT_EQUAL_HEX8(BOARD_TYPE_STM32F407, pong.data[3]);
    TEST_ASSERT_EQUAL_HEX8(CHASSIS_TYPE_MECANUM, pong.data[4]);
}

/** 未知命令 → ERROR(UNKNOWN_CMD)，详情带上原命令字 */
static void test_dispatch_unknown_command(void)
{
    reset_protocol_fixture();

    uint8_t raw[16];
    const int n = frame_encode(0x7Fu, NULL, 0, raw, sizeof(raw));
    TEST_ASSERT_EQUAL_UINT(0u, protocol_feed(raw, (uint16_t)n));

    Frame err;
    TEST_ASSERT_TRUE(take_tx_frame(0u, &err));
    TEST_ASSERT_EQUAL_HEX8(CMD_ERROR, err.cmd);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)PROTO_ERR_UNKNOWN_CMD, err.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, err.data[1]);
}

/** CRC 损坏的帧不会被分发，也不会产生任何应答 (静默丢弃，靠统计量暴露) */
static void test_corrupted_frame_produces_no_response(void)
{
    reset_protocol_fixture();

    uint8_t raw[32];
    const uint16_t n = build_set_velocity(raw, sizeof(raw), 0.5f, 0.0f, 0.0f);
    raw[5] ^= 0xFFu;

    TEST_ASSERT_EQUAL_UINT(0u, protocol_feed(raw, n));
    TEST_ASSERT_EQUAL_INT(0, s_velocity_calls);
    TEST_ASSERT_EQUAL_UINT(0u, s_tx_len);
    TEST_ASSERT_EQUAL_UINT(1u, protocol_get_parser()->stat_err_crc);
}

/** TELEMETRY 帧的字节布局：4×RPM + 4×电流 + 故障码，共 34 字节 */
static void test_telemetry_frame_layout(void)
{
    reset_protocol_fixture();

    const float rpm[NUM_WHEELS] = { 100.5f, -100.5f, 200.25f, -0.75f };
    const float current[NUM_WHEELS] = { 0.5f, 0.6f, 0.7f, 0.8f };
    protocol_send_telemetry(rpm, current, 0x0005u);

    Frame telemetry;
    TEST_ASSERT_TRUE(take_tx_frame(0u, &telemetry));
    TEST_ASSERT_EQUAL_HEX8(CMD_TELEMETRY, telemetry.cmd);
    TEST_ASSERT_EQUAL_UINT(PAYLOAD_LEN_TELEMETRY, telemetry.len);

    for (int i = 0; i < NUM_WHEELS; i++) {
        TEST_ASSERT_EQUAL_FLOAT(rpm[i], frame_get_f32(&telemetry.data[i * 4]));
        TEST_ASSERT_EQUAL_FLOAT(current[i], frame_get_f32(&telemetry.data[16 + i * 4]));
    }
    TEST_ASSERT_EQUAL_HEX16(0x0005u, frame_get_u16(&telemetry.data[32]));

    /* 整帧上线字节数 = 34 + 6 */
    TEST_ASSERT_EQUAL_UINT(PAYLOAD_LEN_TELEMETRY + 6u, s_tx_len);
}

/** NULL 数组按全零上报，避免遥测路径上再多一个崩溃点 */
static void test_telemetry_tolerates_null_arrays(void)
{
    reset_protocol_fixture();
    protocol_send_telemetry(NULL, NULL, 0x00FFu);

    Frame telemetry;
    TEST_ASSERT_TRUE(take_tx_frame(0u, &telemetry));
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, frame_get_f32(&telemetry.data[i * 4]));
    }
    TEST_ASSERT_EQUAL_HEX16(0x00FFu, frame_get_u16(&telemetry.data[32]));
}

/** 未注册写函数时只收不发，不得崩溃 */
static void test_protocol_without_writer_is_safe(void)
{
    protocol_init(NULL, NULL, NULL);

    uint8_t raw[32];
    const uint16_t n = build_set_velocity(raw, sizeof(raw), 0.1f, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT(1u, protocol_feed(raw, n));
    protocol_send_pong();
    protocol_send_telemetry(NULL, NULL, 0u);

    TEST_ASSERT_EQUAL_UINT(0u, protocol_feed(NULL, 10u));
    TEST_ASSERT_FALSE(protocol_dispatch(NULL));
}

void run_protocol_tests(void)
{
    UNITY_SET_FILE();
    RUN_TEST(test_little_endian_helpers);
    RUN_TEST(test_frame_encode_ping_golden_bytes);
    RUN_TEST(test_frame_encode_set_velocity_golden_bytes);
    RUN_TEST(test_frame_encode_len_field_semantics);
    RUN_TEST(test_frame_encode_rejects_bad_arguments);
    RUN_TEST(test_frame_round_trip_all_payload_lengths);
    RUN_TEST(test_parser_accepts_sof_eof_inside_payload);
    RUN_TEST(test_parser_recovers_from_leading_garbage);
    RUN_TEST(test_parser_desyncs_on_stray_sof_and_idle_reset_recovers);
    RUN_TEST(test_parser_rejects_crc_error);
    RUN_TEST(test_parser_rejects_bad_eof);
    RUN_TEST(test_parser_rejects_undersized_len_field);
    RUN_TEST(test_parser_handles_back_to_back_frames);
    RUN_TEST(test_dispatch_set_velocity);
    RUN_TEST(test_dispatch_survives_byte_by_byte_feed);
    RUN_TEST(test_dispatch_rejects_wrong_payload_length);
    RUN_TEST(test_dispatch_rejects_non_finite_velocity);
    RUN_TEST(test_dispatch_emergency_stop);
    RUN_TEST(test_dispatch_ping_returns_pong);
    RUN_TEST(test_dispatch_unknown_command);
    RUN_TEST(test_corrupted_frame_produces_no_response);
    RUN_TEST(test_telemetry_frame_layout);
    RUN_TEST(test_telemetry_tolerates_null_arrays);
    RUN_TEST(test_protocol_without_writer_is_safe);
}
