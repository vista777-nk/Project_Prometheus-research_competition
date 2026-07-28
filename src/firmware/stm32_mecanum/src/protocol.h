/**
 * @file protocol.h
 * @brief STM32F407 麦轮固件的应用层协议：命令分发 + 遥测组装
 *
 * 分层关系：
 *   common/protocol_frame.c  ← 帧结构 (SOF/LEN/CRC/EOF)，两块板子共用
 *   stm32_mecanum/protocol.c ← 命令表与载荷布局，本板专有   ← 本文件
 *
 * 本模块**不含任何硬件调用**：字节输出通过 protocol_init() 注入的写函数完成，
 * 因此可以在 Host 上完整测试收发往返，实机上再把写函数换成 uart_write()。
 *
 * 命令表 (ADR-0003)：
 * | CMD  | 方向      | 名称           | DATA                                        |
 * |------|-----------|----------------|---------------------------------------------|
 * | 0x01 | Pi→STM32  | SET_VELOCITY   | vx(f32) vy(f32) ω(f32) = 12 B               |
 * | 0x02 | Pi→STM32  | EMERGENCY_STOP | 无                                          |
 * | 0x03 | Pi→STM32  | PING           | 无                                          |
 * | 0x11 | STM32→Pi  | TELEMETRY      | rpm(f32×4) + 电流(f32×4) + 故障码(u16) = 34 B |
 * | 0x12 | STM32→Pi  | ACK            | 被确认的 CMD(u8) = 1 B                       |
 * | 0x13 | STM32→Pi  | PONG           | major,minor,patch,board_type,chassis_type = 5 B |
 * | 0xFF | STM32→Pi  | ERROR          | 错误码(u8) + 变长详情                        |
 *
 * 帧内所有多字节标量均为小端序。
 */
#ifndef STM32_MECANUM_PROTOCOL_H
#define STM32_MECANUM_PROTOCOL_H

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
#define CMD_TELEMETRY           0x11u
#define CMD_ACK                 0x12u
#define CMD_PONG                0x13u
#define CMD_ERROR               0xFFu

/* --- 各命令的 DATA 段长度 --- */
#define PAYLOAD_LEN_SET_VELOCITY    12u
#define PAYLOAD_LEN_EMERGENCY_STOP   0u
#define PAYLOAD_LEN_PING             0u
#define PAYLOAD_LEN_TELEMETRY       34u
#define PAYLOAD_LEN_ACK              1u
#define PAYLOAD_LEN_PONG             5u

/** ERROR 帧的错误码 */
typedef enum {
    PROTO_ERR_UNKNOWN_CMD  = 0x01u,  /**< 命令字不在本板命令表中 */
    PROTO_ERR_BAD_LENGTH   = 0x02u,  /**< DATA 长度与命令定义不符 */
    PROTO_ERR_BAD_VALUE    = 0x03u,  /**< 载荷含 NaN / Inf 等非法数值 */
    PROTO_ERR_TX_OVERFLOW  = 0x04u   /**< 发送侧组帧失败 (缓冲不足) */
} ProtocolErrorCode;

/**
 * TELEMETRY 帧的故障码位图 (uint16, 小端)。
 * 属于线上契约的一部分：树莓派端按这些位解读故障，改动需同步改上位机。
 */
#define FAULT_NONE              0x0000u
#define FAULT_OVERCURRENT       0x0001u  /**< 任一电机电流超过阈值 */
#define FAULT_STALL             0x0002u  /**< 有目标转速但轮子不转 (堵转/断线)。
                                              注意：FAULT_OVERCURRENT 或 FAULT_ESTOP 置位期间电机已刹停，
                                              堵转检测停止更新，本位保持旧值。上位机此时应忽略本位。 */
#define FAULT_CMD_TIMEOUT       0x0004u  /**< 超时未收到 SET_VELOCITY，已自动刹停 */
#define FAULT_ESTOP             0x0008u  /**< 硬件急停被触发 */
#define FAULT_KINEMATICS_SAT    0x0010u  /**< 速度指令超出底盘能力，已等比缩放 */
#define FAULT_UART_ERROR        0x0020u  /**< 串口链路错误率异常 (CRC/溢出) */

/**
 * 字节输出回调：把组好的整帧交给物理层 (实机=UART DMA，测试=内存缓冲)。
 * 实现方必须一次性接收全部 len 字节，不得部分写入。
 */
typedef void (*ProtocolWriteFn)(const uint8_t *data, uint16_t len, void *ctx);

/** 上层业务回调。任一成员为 NULL 表示忽略该命令 (仍会回 ACK)。 */
typedef struct {
    void (*on_set_velocity)(const RobotVelocity *cmd, void *ctx);
    void (*on_emergency_stop)(void *ctx);
    void (*on_ping)(void *ctx);
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
 * @param bytes 原始字节
 * @param len   字节数
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
 * 由 UART 层在 IDLE 中断中调用。上位机总是把一帧连续发完，所以"收了半截 + 线路空闲"
 * 一定是失同步，必须立刻放弃半截帧，否则杂散字节会让解析器空等最多 257 字节。
 * 详见 protocol_frame.h 的 @warning。
 */
void protocol_notify_line_idle(void);

/* --- 主动上报 --- */

void protocol_send_ack(uint8_t acked_cmd);
void protocol_send_pong(void);

/**
 * 上报遥测。
 * @param rpm     四轮实测转速，索引同 kinematics.h
 * @param current 四轮电流 (A)
 * @param fault   故障位图，见 main.c 的 FAULT_* 定义
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

#endif /* STM32_MECANUM_PROTOCOL_H */
