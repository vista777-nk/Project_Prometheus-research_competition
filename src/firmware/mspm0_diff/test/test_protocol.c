/**
 * @file test_protocol.c
 * @brief MSPM0 差速固件协议层测试：命令表、载荷布局、错误注入、跨板兼容
 *
 * 帧结构本身 (SOF/LEN/CRC/EOF、CRC 向量、失同步恢复) 已由 task-10 的
 * test_protocol.c / test_crc16.c 在 common/ 层测透，本文件**不重复**，
 * 只测两件 task-11 特有的事：
 *   1. 本板命令表与载荷布局 (8B SET_VELOCITY / 18B TELEMETRY / 0x10 扩展)
 *   2. 跨板兼容：两块板子的帧必须能被同一个解析器吃下 (ADR-0003 的核心承诺)
 */
#include <math.h>
#include <string.h>

#include "crc16.h"
#include "protocol.h"
#include "unity.h"

/* --- 测试替身：把协议层的输出接到内存缓冲 --- */

#define CAPTURE_CAP     512

static uint8_t s_tx[CAPTURE_CAP];
static uint16_t s_tx_len;

static DiffVelocity s_last_cmd;
static int s_set_velocity_calls;
static int s_estop_calls;
static int s_ping_calls;
static uint8_t s_ext_sub;
static uint8_t s_ext_payload[16];
static uint8_t s_ext_payload_len;
static int s_ext_calls;
static bool s_ext_should_accept;

static void capture_write(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx;
    if ((uint16_t)(s_tx_len + len) > (uint16_t)CAPTURE_CAP) {
        return;
    }
    memcpy(&s_tx[s_tx_len], data, len);
    s_tx_len = (uint16_t)(s_tx_len + len);
}

static void on_set_velocity(const DiffVelocity *cmd, void *ctx)
{
    (void)ctx;
    s_last_cmd = *cmd;
    s_set_velocity_calls++;
}

static void on_estop(void *ctx) { (void)ctx; s_estop_calls++; }
static void on_ping(void *ctx)  { (void)ctx; s_ping_calls++; }

static bool on_extension(uint8_t sub, const uint8_t *payload,
                         uint8_t payload_len, void *ctx)
{
    (void)ctx;
    s_ext_calls++;
    s_ext_sub = sub;
    s_ext_payload_len = payload_len;
    if (payload != 0 && payload_len <= (uint8_t)sizeof(s_ext_payload)) {
        memcpy(s_ext_payload, payload, payload_len);
    }
    return s_ext_should_accept;
}

/** 装好全部回调 */
static void setup_protocol(void)
{
    ProtocolHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.on_set_velocity = on_set_velocity;
    handlers.on_emergency_stop = on_estop;
    handlers.on_ping = on_ping;
    handlers.on_extension = on_extension;

    protocol_init(capture_write, 0, &handlers);

    s_tx_len = 0u;
    s_set_velocity_calls = 0;
    s_estop_calls = 0;
    s_ping_calls = 0;
    s_ext_calls = 0;
    s_ext_payload_len = 0u;
    s_ext_should_accept = true;
    memset(&s_last_cmd, 0, sizeof(s_last_cmd));
}

/** 不装扩展回调 (用于 NOT_IMPLEMENTED 路径) */
static void setup_protocol_without_extension(void)
{
    setup_protocol();

    ProtocolHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.on_set_velocity = on_set_velocity;
    protocol_init(capture_write, 0, &handlers);
    s_tx_len = 0u;
}

/** 手工拼一帧喂进去 */
static void feed_frame(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    uint8_t buf[FRAME_MAX_TOTAL_LEN];
    const int n = frame_encode(cmd, data, data_len, buf, (uint16_t)sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "frame_encode must succeed in test fixture");
    (void)protocol_feed(buf, (uint16_t)n);
}

/** 在捕获缓冲里找第一个指定 CMD 的帧，返回其 DATA 起始下标；找不到返回 -1 */
static int find_frame(uint8_t cmd, uint8_t *out_data_len)
{
    uint16_t i = 0u;
    while ((uint16_t)(i + 2u) <= s_tx_len) {
        if (s_tx[i] != FRAME_SOF) {
            i++;
            continue;
        }
        const uint8_t len_field = s_tx[i + 1u];
        if (len_field < FRAME_MIN_LEN_FIELD) {
            i++;
            continue;
        }
        const uint16_t total = (uint16_t)(2u + len_field);
        if ((uint16_t)(i + total) > s_tx_len) {
            break;
        }
        if (s_tx[i + 2u] == cmd) {
            if (out_data_len != 0) {
                *out_data_len = (uint8_t)(len_field - FRAME_LEN_OVERHEAD);
            }
            return (int)(i + 3u);
        }
        i = (uint16_t)(i + total);
    }
    return -1;
}

/* ===================== 命令表 ===================== */

/** SET_VELOCITY 载荷是 8 字节，两个小端 float */
static void test_set_velocity_payload_is_eight_bytes(void)
{
    setup_protocol();

    uint8_t data[PAYLOAD_LEN_SET_VELOCITY];
    frame_put_f32(&data[0], 0.75f);
    frame_put_f32(&data[4], -1.25f);
    feed_frame(CMD_SET_VELOCITY, data, sizeof(data));

    TEST_ASSERT_EQUAL_INT(1, s_set_velocity_calls);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, s_last_cmd.v);
    TEST_ASSERT_EQUAL_FLOAT(-1.25f, s_last_cmd.omega);

    /* 必须回 ACK，且 ACK 载荷是被确认的命令字 */
    uint8_t ack_len = 0u;
    const int ack = find_frame(CMD_ACK, &ack_len);
    TEST_ASSERT_TRUE_MESSAGE(ack >= 0, "SET_VELOCITY must be acknowledged");
    TEST_ASSERT_EQUAL_UINT(PAYLOAD_LEN_ACK, ack_len);
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_VELOCITY, s_tx[ack]);
}

/** 麦轮的 12 字节载荷喂给差速板必须被拒 —— 这是接错板子的第一道防线 */
static void test_rejects_mecanum_sized_velocity(void)
{
    setup_protocol();

    uint8_t data[12];
    memset(data, 0, sizeof(data));
    feed_frame(CMD_SET_VELOCITY, data, sizeof(data));

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_set_velocity_calls,
                                  "12-byte (mecanum) payload must not reach the handler");

    uint8_t err_len = 0u;
    const int err = find_frame(CMD_ERROR, &err_len);
    TEST_ASSERT_TRUE_MESSAGE(err >= 0, "wrong length must produce an ERROR frame");
    TEST_ASSERT_EQUAL_HEX8(PROTO_ERR_BAD_LENGTH, s_tx[err]);
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_VELOCITY, s_tx[err + 1]);
    TEST_ASSERT_EQUAL_UINT(12u, s_tx[err + 2]);
}

/** NaN / Inf 必须在协议入口就被拦下，绝不能进 PID */
static void test_rejects_non_finite_velocity(void)
{
    setup_protocol();

    uint8_t data[PAYLOAD_LEN_SET_VELOCITY];
    frame_put_f32(&data[0], NAN);
    frame_put_f32(&data[4], 0.0f);
    feed_frame(CMD_SET_VELOCITY, data, sizeof(data));

    TEST_ASSERT_EQUAL_INT(0, s_set_velocity_calls);
    uint8_t err_len = 0u;
    const int err = find_frame(CMD_ERROR, &err_len);
    TEST_ASSERT_TRUE(err >= 0);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ERR_BAD_VALUE, s_tx[err]);
}

/** 急停必须"先动作后应答" */
static void test_emergency_stop_acts_then_acks(void)
{
    setup_protocol();
    feed_frame(CMD_EMERGENCY_STOP, 0, 0);

    TEST_ASSERT_EQUAL_INT(1, s_estop_calls);
    uint8_t ack_len = 0u;
    const int ack = find_frame(CMD_ACK, &ack_len);
    TEST_ASSERT_TRUE(ack >= 0);
    TEST_ASSERT_EQUAL_HEX8(CMD_EMERGENCY_STOP, s_tx[ack]);
}

/** PONG 必须自报 board=0x02 / chassis=0x02，Pi 端据此拒绝接错板子 */
static void test_pong_identifies_mspm0_differential(void)
{
    setup_protocol();
    feed_frame(CMD_PING, 0, 0);

    TEST_ASSERT_EQUAL_INT(1, s_ping_calls);

    uint8_t pong_len = 0u;
    const int pong = find_frame(CMD_PONG, &pong_len);
    TEST_ASSERT_TRUE_MESSAGE(pong >= 0, "PING must be answered with PONG");
    TEST_ASSERT_EQUAL_UINT(PAYLOAD_LEN_PONG, pong_len);
    TEST_ASSERT_EQUAL_HEX8(0x02u, s_tx[pong + 3]);   /* board_type   = MSPM0G3507 */
    TEST_ASSERT_EQUAL_HEX8(0x02u, s_tx[pong + 4]);   /* chassis_type = differential */
}

/** TELEMETRY 布局：rpm×2 → 电流×2 → 故障码，共 18 字节小端 */
static void test_telemetry_frame_layout(void)
{
    setup_protocol();

    const float rpm[NUM_WHEELS] = { 120.5f, -98.25f };
    const float current[NUM_WHEELS] = { 0.75f, 1.5f };
    protocol_send_telemetry(rpm, current, FAULT_STALL | FAULT_CMD_TIMEOUT);

    uint8_t len = 0u;
    const int at = find_frame(CMD_TELEMETRY, &len);
    TEST_ASSERT_TRUE(at >= 0);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(PAYLOAD_LEN_TELEMETRY, len,
                                   "differential telemetry must be 18 bytes, not 34");

    TEST_ASSERT_EQUAL_FLOAT(120.5f, frame_get_f32(&s_tx[at + 0]));
    TEST_ASSERT_EQUAL_FLOAT(-98.25f, frame_get_f32(&s_tx[at + 4]));
    TEST_ASSERT_EQUAL_FLOAT(0.75f, frame_get_f32(&s_tx[at + 8]));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, frame_get_f32(&s_tx[at + 12]));
    TEST_ASSERT_EQUAL_HEX16(FAULT_STALL | FAULT_CMD_TIMEOUT, frame_get_u16(&s_tx[at + 16]));
}

/** 遥测传 NULL 不得崩溃，按 0 上报 */
static void test_telemetry_tolerates_null_arrays(void)
{
    setup_protocol();
    protocol_send_telemetry(0, 0, FAULT_NONE);

    uint8_t len = 0u;
    const int at = find_frame(CMD_TELEMETRY, &len);
    TEST_ASSERT_TRUE(at >= 0);
    TEST_ASSERT_EQUAL_UINT(PAYLOAD_LEN_TELEMETRY, len);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, frame_get_f32(&s_tx[at + 0]));
    TEST_ASSERT_EQUAL_HEX16(FAULT_NONE, frame_get_u16(&s_tx[at + 16]));
}

/** HC-SR04 快照按 front,rear,left,right 的 u16 毫米值上报。 */
static void test_ultrasonic_frame_layout(void)
{
    setup_protocol();
    const uint16_t ranges[4] = { 123u, 456u, 4000u, ULTRASONIC_UNAVAILABLE_MM };
    protocol_send_ultrasonic(ranges);

    uint8_t len = 0u;
    const int at = find_frame(CMD_ULTRASONIC, &len);
    TEST_ASSERT_TRUE(at >= 0);
    TEST_ASSERT_EQUAL_UINT(PAYLOAD_LEN_ULTRASONIC, len);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_HEX16(ranges[i], frame_get_u16(&s_tx[at + i * 2]));
    }
}

/** 未知命令回 ERROR/UNKNOWN_CMD，并把冒犯的命令字带回去 */
static void test_unknown_command_is_rejected(void)
{
    setup_protocol();
    feed_frame(0x7Bu, 0, 0);

    uint8_t err_len = 0u;
    const int err = find_frame(CMD_ERROR, &err_len);
    TEST_ASSERT_TRUE(err >= 0);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ERR_UNKNOWN_CMD, s_tx[err]);
    TEST_ASSERT_EQUAL_HEX8(0x7Bu, s_tx[err + 1]);
}

/* ===================== 电赛扩展命令 0x10 ===================== */

/** 扩展命令把子命令与剩余载荷完整路由给回调 */
static void test_extension_routes_subcommand_and_payload(void)
{
    setup_protocol();

    const uint8_t data[5] = { 0x42u, 0xDEu, 0xADu, 0xBEu, 0xEFu };
    feed_frame(CMD_EXTENSION, data, sizeof(data));

    TEST_ASSERT_EQUAL_INT(1, s_ext_calls);
    TEST_ASSERT_EQUAL_HEX8(0x42u, s_ext_sub);
    TEST_ASSERT_EQUAL_UINT(4u, s_ext_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(&data[1], s_ext_payload, 4);

    uint8_t ack_len = 0u;
    const int ack = find_frame(CMD_ACK, &ack_len);
    TEST_ASSERT_TRUE(ack >= 0);
    TEST_ASSERT_EQUAL_HEX8(CMD_EXTENSION, s_tx[ack]);
}

/** 只有子命令、没有载荷也是合法的 */
static void test_extension_accepts_bare_subcommand(void)
{
    setup_protocol();

    const uint8_t data[1] = { 0x07u };
    feed_frame(CMD_EXTENSION, data, sizeof(data));

    TEST_ASSERT_EQUAL_INT(1, s_ext_calls);
    TEST_ASSERT_EQUAL_HEX8(0x07u, s_ext_sub);
    TEST_ASSERT_EQUAL_UINT(0u, s_ext_payload_len);
}

/** 空载荷 (连子命令都没有) 必须被拒 */
static void test_extension_rejects_empty_payload(void)
{
    setup_protocol();
    feed_frame(CMD_EXTENSION, 0, 0);

    TEST_ASSERT_EQUAL_INT(0, s_ext_calls);
    uint8_t err_len = 0u;
    const int err = find_frame(CMD_ERROR, &err_len);
    TEST_ASSERT_TRUE(err >= 0);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ERR_BAD_LENGTH, s_tx[err]);
}

/**
 * 没注册扩展回调时必须明确回 NOT_IMPLEMENTED，而不是假装 ACK。
 * 赛场上"上位机以为固件支持、固件其实没实现"是最难查的一类问题。
 */
static void test_extension_without_handler_reports_not_implemented(void)
{
    setup_protocol_without_extension();

    const uint8_t data[2] = { 0x01u, 0x02u };
    feed_frame(CMD_EXTENSION, data, sizeof(data));

    uint8_t err_len = 0u;
    const int err = find_frame(CMD_ERROR, &err_len);
    TEST_ASSERT_TRUE(err >= 0);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ERR_NOT_IMPLEMENTED, s_tx[err]);
    TEST_ASSERT_EQUAL_HEX8(CMD_EXTENSION, s_tx[err + 1]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, s_tx[err + 2]);

    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, find_frame(CMD_ACK, 0),
                                  "unimplemented extension must not be ACKed");
}

/** 回调主动拒绝时也回 NOT_IMPLEMENTED，不回 ACK */
static void test_extension_handler_rejection_is_reported(void)
{
    setup_protocol();
    s_ext_should_accept = false;

    const uint8_t data[2] = { 0x09u, 0x00u };
    feed_frame(CMD_EXTENSION, data, sizeof(data));

    TEST_ASSERT_EQUAL_INT(1, s_ext_calls);
    uint8_t err_len = 0u;
    const int err = find_frame(CMD_ERROR, &err_len);
    TEST_ASSERT_TRUE(err >= 0);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ERR_NOT_IMPLEMENTED, s_tx[err]);
    TEST_ASSERT_EQUAL_INT(-1, find_frame(CMD_ACK, 0));
}

/* ===================== 跨板兼容 (ADR-0003 的核心承诺) ===================== */

/**
 * 黄金帧：MSPM0 的 PONG 字节序列必须逐字节符合 ADR-0003。
 *
 * 这是跨端契约的锚点。协议有三份独立实现 (STM32 / MSPM0 / Pi 端 Python)，
 * 任何一端"顺手优化"了字段顺序、字节序或 CRC 参数，都会在这里红掉。
 *
 * 帧内容：
 *   SOF=A5, LEN=9 (=5+4), CMD=13, DATA = 00 02 00 02 02  (v0.2.0, board/chassis=0x02)
 *   CRC-16/CCITT-FALSE over {13 00 01 00 02 02}, 小端存放, EOF=5A
 * CRC 期望值由 crc16_ccitt() 独立算出而非硬编码 —— 硬编码一个我没验算过的
 * 常数，只会把"测试通过"变成"测试和实现一起错"。CRC 算法本身的正确性
 * 由 task-10 的标准向量 (b"123456789" → 0x29B1) 保证。
 */
static void test_pong_golden_frame(void)
{
    setup_protocol();
    feed_frame(CMD_PING, 0, 0);

    uint8_t pong_len = 0u;
    const int at = find_frame(CMD_PONG, &pong_len);
    TEST_ASSERT_TRUE(at >= 0);

    /* 帧起点 = DATA 起点 - 3 (SOF/LEN/CMD) */
    const uint8_t *frame = &s_tx[at - 3];

    TEST_ASSERT_EQUAL_HEX8(FRAME_SOF, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(PAYLOAD_LEN_PONG + FRAME_LEN_OVERHEAD, frame[1]);   /* LEN = 9 */
    TEST_ASSERT_EQUAL_HEX8(CMD_PONG, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[3]);   /* major */
    TEST_ASSERT_EQUAL_HEX8(0x02u, frame[4]);   /* minor */
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[5]);   /* patch */
    TEST_ASSERT_EQUAL_HEX8(0x02u, frame[6]);   /* board   = MSPM0G3507 */
    TEST_ASSERT_EQUAL_HEX8(0x02u, frame[7]);   /* chassis = differential */

    /* CRC 覆盖 CMD+DATA，小端存放；用独立算的值复核，不信任被测代码 */
    const uint8_t crc_input[6] = { CMD_PONG, 0x00u, 0x02u, 0x00u, 0x02u, 0x02u };
    const uint16_t expect_crc = crc16_ccitt(crc_input, sizeof(crc_input));
    TEST_ASSERT_EQUAL_HEX16(expect_crc, frame_get_u16(&frame[8]));
    TEST_ASSERT_EQUAL_HEX8(FRAME_EOF, frame[10]);
}

/**
 * 跨板：麦轮固件格式的帧 (LEN/CRC/EOF 规则相同、只是载荷更长)
 * 必须能被本板的帧层正确拆出来，只是在命令表层因长度不符被拒。
 *
 * 这条区分了两种失败：**帧层不认识** (灾难，说明协议分叉了)
 * 与 **命令层拒绝** (正常，说明接错板子了)。后者才是我们要的行为。
 */
static void test_frame_layer_accepts_other_board_frames(void)
{
    setup_protocol();

    /* 造一个 34 字节载荷的 TELEMETRY —— 麦轮板才会发的东西 */
    uint8_t payload[34];
    for (int i = 0; i < 34; i++) {
        payload[i] = (uint8_t)i;
    }

    uint8_t buf[FRAME_MAX_TOTAL_LEN];
    const int n = frame_encode(CMD_TELEMETRY, payload, sizeof(payload),
                               buf, (uint16_t)sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    Frame parsed;
    FrameParser parser;
    frame_parser_init(&parser);

    bool got = false;
    for (int i = 0; i < n; i++) {
        if (frame_parser_push(&parser, buf[i], &parsed)) {
            got = true;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(got, "shared frame layer must parse the other board's frames");
    TEST_ASSERT_EQUAL_HEX8(CMD_TELEMETRY, parsed.cmd);
    TEST_ASSERT_EQUAL_UINT(34u, parsed.len);
    TEST_ASSERT_EQUAL_MEMORY(payload, parsed.data, 34);
    TEST_ASSERT_EQUAL_UINT(1u, parser.stat_frames_ok);
    TEST_ASSERT_EQUAL_UINT(0u, parser.stat_err_crc);
}

/** 空闲重同步必须清掉半截帧，且保留统计量 */
static void test_line_idle_resyncs_parser(void)
{
    setup_protocol();

    /* 只喂半截 SET_VELOCITY */
    uint8_t data[PAYLOAD_LEN_SET_VELOCITY];
    memset(data, 0, sizeof(data));
    uint8_t buf[FRAME_MAX_TOTAL_LEN];
    const int n = frame_encode(CMD_SET_VELOCITY, data, sizeof(data),
                               buf, (uint16_t)sizeof(buf));
    TEST_ASSERT_TRUE(n > 4);
    (void)protocol_feed(buf, 4u);

    TEST_ASSERT_EQUAL_INT(0, s_set_velocity_calls);
    protocol_notify_line_idle();
    TEST_ASSERT_EQUAL_UINT(1u, protocol_get_parser()->stat_resyncs);

    /* 重同步后完整帧必须正常解析 */
    s_tx_len = 0u;
    (void)protocol_feed(buf, (uint16_t)n);
    TEST_ASSERT_EQUAL_INT(1, s_set_velocity_calls);
}

/** 没装写函数、也没装回调时协议层不得崩溃 (只收不发的诊断模式) */
static void test_protocol_without_writer_is_safe(void)
{
    protocol_init(0, 0, 0);
    protocol_send_telemetry(0, 0, FAULT_NONE);
    protocol_send_pong();
    protocol_send_ack(CMD_PING);
    protocol_send_error(PROTO_ERR_UNKNOWN_CMD, 0, 0);
    protocol_send_ultrasonic(0);

    uint8_t data[PAYLOAD_LEN_SET_VELOCITY];
    memset(data, 0, sizeof(data));
    uint8_t buf[FRAME_MAX_TOTAL_LEN];
    const int n = frame_encode(CMD_SET_VELOCITY, data, sizeof(data),
                               buf, (uint16_t)sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    /* 回调缺席不等于命令非法：帧仍算成功分发，只是没人处理。
       这个区分很重要 —— 上位机看到 ACK 就知道"板子收到了"，
       至于板子内部有没有接业务逻辑，不是协议层该回答的问题。 */
    TEST_ASSERT_EQUAL_UINT(1u, protocol_feed(buf, (uint16_t)n));
}

void run_protocol_tests(void)
{
    UNITY_SET_FILE();

    RUN_TEST(test_set_velocity_payload_is_eight_bytes);
    RUN_TEST(test_rejects_mecanum_sized_velocity);
    RUN_TEST(test_rejects_non_finite_velocity);
    RUN_TEST(test_emergency_stop_acts_then_acks);
    RUN_TEST(test_pong_identifies_mspm0_differential);
    RUN_TEST(test_telemetry_frame_layout);
    RUN_TEST(test_telemetry_tolerates_null_arrays);
    RUN_TEST(test_ultrasonic_frame_layout);
    RUN_TEST(test_unknown_command_is_rejected);

    RUN_TEST(test_extension_routes_subcommand_and_payload);
    RUN_TEST(test_extension_accepts_bare_subcommand);
    RUN_TEST(test_extension_rejects_empty_payload);
    RUN_TEST(test_extension_without_handler_reports_not_implemented);
    RUN_TEST(test_extension_handler_rejection_is_reported);

    RUN_TEST(test_pong_golden_frame);
    RUN_TEST(test_frame_layer_accepts_other_board_frames);
    RUN_TEST(test_line_idle_resyncs_parser);
    RUN_TEST(test_protocol_without_writer_is_safe);
}
