/**
 * @file protocol.h
 * @brief MSPM0G3507 差速固件的应用层协议：命令分发 + 遥测组装
 *
 * 分层关系 (与麦轮固件完全对称)：
 *   common/protocol_frame.c  ← 帧结构 (SOF/LEN/CRC/EOF)，两块板子共用，一字不改
 *   mspm0_diff/protocol.c    ← 命令表与载荷布局，本板专有   ← 本文件
 *
 * 本模块**不含任何硬件调用**：字节输出通过 protocol_init() 注入的写函数完成，
 * 因此可以在 Host 上完整测试收发往返，实机上再把写函数换成 uart_write()。
 *
 * 命令表 (ADR-0003 · ADR-0004)：
 * | CMD  | 方向      | 名称           | DATA                                          |
 * |------|-----------|----------------|-----------------------------------------------|
 * | 0x01 | Pi→MSPM0  | SET_VELOCITY   | v(f32) ω(f32) = 8 B                           |
 * | 0x02 | Pi→MSPM0  | EMERGENCY_STOP | 无                                            |
 * | 0x03 | Pi→MSPM0  | PING           | 无                                            |
 * | 0x10 | Pi→MSPM0  | EXTENSION      | 子命令(u8) + 变长载荷 —— 电赛外设预留          |
 * | 0x11 | MSPM0→Pi  | TELEMETRY      | rpm(f32×2) + 电流(f32×2) + 故障码(u16) = 18 B  |
 * | 0x12 | MSPM0→Pi  | ACK            | 被确认的 CMD(u8) = 1 B                         |
 * | 0x13 | MSPM0→Pi  | PONG           | major,minor,patch,board_type,chassis_type = 5 B |
 * | 0xFF | MSPM0→Pi  | ERROR          | 错误码(u8) + 变长详情                          |
 *
 * 与麦轮固件的差异**只有载荷长度和 board/chassis 编码**，帧结构、CRC、
 * SOF/EOF、字节序完全一致 —— 树莓派端可以用同一个帧解析器处理两块板子，
 * 靠 PONG 里的 board_type/chassis_type 区分对端是谁。
 *
 * 帧内所有多字节标量均为小端序。
 */
#ifndef MSPM0_DIFF_PROTOCOL_H
#define MSPM0_DIFF_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "kinematics.h"
#include "protocol_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- 命令字 --- */
#define CMD_SET_VELOCITY        0x01u
#define CMD_EMERGENCY_STOP      0x02u
#define CMD_PING                0x03u
#define CMD_EXTENSION           0x10u
#define CMD_TELEMETRY           0x11u
#define CMD_ACK                 0x12u
#define CMD_PONG                0x13u
#define CMD_ERROR               0xFFu

/* --- 各命令的 DATA 段长度 (EXTENSION 变长，不在此列) --- */
#define PAYLOAD_LEN_SET_VELOCITY     8u
#define PAYLOAD_LEN_EMERGENCY_STOP   0u
#define PAYLOAD_LEN_PING             0u
#define PAYLOAD_LEN_TELEMETRY       18u
#define PAYLOAD_LEN_ACK              1u
#define PAYLOAD_LEN_PONG             5u
/** EXTENSION 至少要有 1 字节子命令 */
#define PAYLOAD_MIN_LEN_EXTENSION    1u

/** ERROR 帧的错误码 */
typedef enum {
    PROTO_ERR_UNKNOWN_CMD     = 0x01u,  /**< 命令字不在本板命令表中 */
    PROTO_ERR_BAD_LENGTH      = 0x02u,  /**< DATA 长度与命令定义不符 */
    PROTO_ERR_BAD_VALUE       = 0x03u,  /**< 载荷含 NaN / Inf 等非法数值 */
    PROTO_ERR_TX_OVERFLOW     = 0x04u,  /**< 发送侧组帧失败 (缓冲不足) */
    PROTO_ERR_NOT_IMPLEMENTED = 0x05u   /**< 命令字合法但本固件未注册处理器 */
} ProtocolErrorCode;

/**
 * TELEMETRY 帧的故障码位图定义在 `common/faults.h`。
 *
 * 位定义是**线上契约**且两块板逐位一致，因此只存一份 —— 分别写在两个
 * protocol.h 里迟早会漂移。位间的优先级规则（ESTOP/OVERCURRENT 置位期间
 * STALL 保持旧值）同样在那里，并由 test/test_faults.c 钉死。
 */
#include "faults.h"

/**
 * 字节输出回调：把组好的整帧交给物理层 (实机=UART，测试=内存缓冲)。
 * 实现方必须一次性接收全部 len 字节，不得部分写入。
 */
typedef void (*ProtocolWriteFn)(const uint8_t *data, uint16_t len, void *ctx);

/**
 * 扩展命令处理器 (CMD 0x10)。
 *
 * @param sub     子命令字节 (DATA[0])
 * @param payload 子命令之后的载荷，可能为 NULL (payload_len==0 时)
 * @param payload_len 载荷长度
 * @param ctx     透传上下文
 * @return true = 已处理 (回 ACK)；false = 拒绝 (回 ERROR/NOT_IMPLEMENTED)
 */
typedef bool (*ProtocolExtensionFn)(uint8_t sub, const uint8_t *payload,
                                    uint8_t payload_len, void *ctx);

/** 上层业务回调。任一成员为 NULL 表示忽略该命令 (仍会回 ACK)。 */
typedef struct {
    void (*on_set_velocity)(const DiffVelocity *cmd, void *ctx);
    void (*on_emergency_stop)(void *ctx);
    void (*on_ping)(void *ctx);
    ProtocolExtensionFn on_extension;   /**< NULL = 回 ERROR/NOT_IMPLEMENTED */
    void *ctx;
} ProtocolHandlers;

/**
 * 初始化协议层。会复位拆帧状态机与链路统计。
 *
 * @param write_fn 字节输出回调，NULL 表示只收不发
 * @param write_ctx 透传给 write_fn 的上下文
 * @param handlers 业务回调，可为 NULL
 */
void protocol_init(ProtocolWriteFn write_fn, void *write_ctx,
                   const ProtocolHandlers *handlers);

/**
 * 喂入串口收到的原始字节，内部完成拆帧 + 分发 + 自动应答。
 *
 * @return 本次调用中成功分发的帧数
 */
uint16_t protocol_feed(const uint8_t *bytes, uint16_t len);

/**
 * 分发单帧 (已通过 CRC 校验)。protocol_feed() 内部调用，也可单独用于测试。
 *
 * @return true 表示命令合法并已处理；false 表示已回 ERROR 帧
 */
bool protocol_dispatch(const Frame *frame);

/**
 * 通知协议层"接收线路已空闲"，触发拆帧状态机重同步。
 *
 * 由 UART 层在空闲检测中调用。上位机总是把一帧连续发完，所以"收了半截 + 线路空闲"
 * 一定是失同步，必须立刻放弃半截帧，否则杂散字节会让解析器空等最多 257 字节。
 * 详见 protocol_frame.h 的 @warning。
 */
void protocol_notify_line_idle(void);

/* --- 主动上报 --- */

void protocol_send_ack(uint8_t acked_cmd);
void protocol_send_pong(void);

/**
 * 上报遥测。
 * @param rpm     双轮实测转速，索引同 kinematics.h (0=左, 1=右)
 * @param current 双轮电流 (A)
 * @param fault   故障位图，见上方 FAULT_* 定义
 */
void protocol_send_telemetry(const float rpm[NUM_WHEELS],
                             const float current[NUM_WHEELS],
                             uint16_t fault);

void protocol_send_error(uint8_t code, const uint8_t *detail, uint8_t detail_len);

/** 读取拆帧统计 (CRC 错误数等)，用于链路质量诊断 */
const FrameParser *protocol_get_parser(void);

#ifdef __cplusplus
}
#endif

#endif /* MSPM0_DIFF_PROTOCOL_H */
