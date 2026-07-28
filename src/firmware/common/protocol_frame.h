/**
 * @file protocol_frame.h
 * @brief 串口二进制帧的打包 / 拆包 (ADR-0003 统一帧结构)
 *
 * task-10 (STM32F407 麦轮) 与 task-11 (MSPM0G3507 差速) 共用本模块：
 * 帧结构、CRC、SOF/EOF 完全一致，只有命令集与 DATA 长度不同，
 * 因此树莓派端可以用同一个解析器处理两块板子。
 *
 * 帧格式：
 * ┌────────┬────────┬──────────┬──────────────┬──────────┬────────┐
 * │  SOF   │  LEN   │   CMD    │    DATA      │  CRC16   │  EOF   │
 * │ 1 byte │ 1 byte │  1 byte  │ 0~251 bytes  │ 2 bytes  │ 1 byte │
 * │  0xA5  │  n+4   │          │              │ LSB→MSB  │  0x5A  │
 * └────────┴────────┴──────────┴──────────────┴──────────┴────────┘
 *
 *   LEN = CMD(1) + DATA(n) + CRC(2) + EOF(1) = n + 4,  LEN ∈ [4, 255]
 *   CRC 覆盖范围 = CMD + DATA，SOF / LEN / CRC / EOF 本身不参与计算
 *   帧内所有多字节标量 (float32 / uint16) 一律小端序
 *
 * @note 本实现不做字节填充 (byte stuffing)。载荷中出现 0xA5 / 0x5A 是允许的：
 *       LEN 字段已经界定了帧边界，CRC + EOF 双重校验足以拒绝错位帧，
 *       解析器通过重新搜索 SOF 完成重同步。理由详见 ADR-0003 §决策-3。
 *
 * @warning 基于长度的拆帧有一个固有失同步窗口：若线路噪声在一帧之前插入了
 *       一个杂散 0xA5，紧随其后的真实 SOF (0xA5 = 165) 会被当成 LEN 字段，
 *       解析器随即空等 165 字节 (@115200 约 14ms) 才会因 CRC/EOF 不符而重同步，
 *       期间到达的正常帧全部被吞掉。任何不带转义的定长头协议都有这个问题。
 *       解药是**空闲重同步**：物理层检测到 RX 线空闲若干字符时间后调用
 *       frame_parser_reset()，把状态机拉回等待 SOF。见 uart.c 的 IDLE 中断处理。
 */
#ifndef FIRMWARE_COMMON_PROTOCOL_FRAME_H
#define FIRMWARE_COMMON_PROTOCOL_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_SOF               0xA5u
#define FRAME_EOF               0x5Au

/** LEN 字段之外的固定开销 (CMD + CRC16 + EOF) */
#define FRAME_LEN_OVERHEAD      4u
/** DATA 段最大字节数 */
#define FRAME_MAX_DATA_LEN      251u
/** LEN 字段合法区间 */
#define FRAME_MIN_LEN_FIELD     FRAME_LEN_OVERHEAD
#define FRAME_MAX_LEN_FIELD     255u
/** 一整帧 (含 SOF + LEN) 的最大字节数 */
#define FRAME_MAX_TOTAL_LEN     (2u + FRAME_MAX_LEN_FIELD)

/** 解析出的一帧 */
typedef struct {
    uint8_t cmd;                          /**< 命令字 */
    uint8_t len;                          /**< DATA 段长度 (字节)，0~251 */
    uint8_t data[FRAME_MAX_DATA_LEN];     /**< DATA 段内容 */
} Frame;

/** frame_encode() 的错误码 (均为负值，非负返回值表示实际写入的字节数) */
typedef enum {
    FRAME_ERR_NULL        = -1,  /**< 输出缓冲为 NULL，或 data_len>0 但 data 为 NULL */
    FRAME_ERR_TOO_LONG    = -2,  /**< data_len 超过 FRAME_MAX_DATA_LEN */
    FRAME_ERR_NO_SPACE    = -3   /**< 输出缓冲容量不足 */
} FrameEncodeError;

/** 拆帧状态机内部状态 */
typedef enum {
    FRAME_STATE_SOF = 0,   /**< 等待 SOF */
    FRAME_STATE_LEN,       /**< 等待 LEN */
    FRAME_STATE_PAYLOAD    /**< 收集 CMD+DATA+CRC+EOF */
} FrameParseState;

/** 拆帧状态机 (每条物理链路一个实例) */
typedef struct {
    FrameParseState state;
    uint8_t  len_field;                    /**< 本帧 LEN 字段 */
    uint16_t index;                        /**< 已收集的 payload 字节数 */
    uint8_t  buf[FRAME_MAX_LEN_FIELD];     /**< CMD+DATA+CRC+EOF */

    /* 链路质量统计 —— 供遥测 / 故障诊断使用 */
    uint32_t stat_frames_ok;   /**< 成功解析的帧数 */
    uint32_t stat_err_len;     /**< LEN 字段非法的次数 */
    uint32_t stat_err_crc;     /**< CRC 校验失败的次数 */
    uint32_t stat_err_eof;     /**< EOF 字节不匹配的次数 */
    uint32_t stat_resyncs;     /**< 空闲重同步丢弃半截帧的次数 */
} FrameParser;

/**
 * 打包一帧到输出缓冲。
 *
 * @param cmd      命令字
 * @param data     DATA 段，data_len 为 0 时可传 NULL
 * @param data_len DATA 段长度，0~251
 * @param out      输出缓冲
 * @param out_cap  输出缓冲容量 (字节)
 * @return 成功时返回写入的总字节数 (data_len + 6)；失败返回 FrameEncodeError
 */
int frame_encode(uint8_t cmd, const uint8_t *data, uint8_t data_len,
                 uint8_t *out, uint16_t out_cap);

/** 初始化 / 复位拆帧状态机 (统计量一并清零) */
void frame_parser_init(FrameParser *parser);

/**
 * 空闲重同步：丢弃当前收了一半的帧，回到等待 SOF 状态，但**保留统计量**。
 *
 * 由物理层在检测到接收线路空闲 (USART IDLE 中断，或软件判定超过若干字符时间
 * 没有新字节) 时调用。上位机总是把一帧连续发完，因此"半截帧 + 线路空闲"
 * 必然意味着已经失同步，此时立刻放弃比空等 LEN 字节更安全。
 *
 * 若调用时状态机正停在等待 SOF (无半截帧)，本函数无副作用。
 */
void frame_parser_reset(FrameParser *parser);

/**
 * 向状态机喂入一个字节。
 *
 * @param parser 状态机实例
 * @param byte   新到达的字节
 * @param out    解析成功时写出的帧
 * @return true 表示本字节使一帧完成解析，out 有效
 */
bool frame_parser_push(FrameParser *parser, uint8_t byte, Frame *out);

/**
 * 向状态机喂入一段缓冲，返回本段中解析出的第一帧。
 *
 * @param parser   状态机实例
 * @param buf      输入缓冲
 * @param len      输入长度
 * @param out      解析成功时写出的帧
 * @param consumed 可为 NULL；否则写出实际消费的字节数
 *                 (解析出帧时会提前返回，剩余字节留待下次调用)
 * @return true 表示解析出一帧
 */
bool frame_parser_push_buffer(FrameParser *parser, const uint8_t *buf, uint16_t len,
                              Frame *out, uint16_t *consumed);

/* --- 小端序标量读写 (逐字节实现，与宿主机字节序无关) --- */

void     frame_put_u16(uint8_t *dst, uint16_t value);
void     frame_put_f32(uint8_t *dst, float value);
uint16_t frame_get_u16(const uint8_t *src);
float    frame_get_f32(const uint8_t *src);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_COMMON_PROTOCOL_FRAME_H */
