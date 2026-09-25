/**
 * @file crc16.h
 * @brief CRC-16/CCITT-FALSE 校验 (ADR-0003 统一标准)
 *
 * 本项目所有串口二进制帧的校验算法。STM32 固件、MSPM0 固件、
 * 树莓派端 Python 解析器三方必须使用完全相同的参数与测试向量：
 *
 *   多项式 : 0x1021  (x^16 + x^12 + x^5 + 1)
 *   初始值 : 0xFFFF
 *   输入反射: 否   输出反射: 否   输出异或: 0x0000
 *   测试向量: crc16_ccitt("123456789", 9) == 0x29B1
 *
 * Python 等价实现见 ADR-0003 附录。
 */
#ifndef FIRMWARE_COMMON_CRC16_H
#define FIRMWARE_COMMON_CRC16_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CRC-16/CCITT-FALSE 标准测试向量的期望结果 */
#define CRC16_CCITT_CHECK_VALUE 0x29B1u

/**
 * 计算 CRC-16/CCITT-FALSE。
 *
 * @param data 待校验数据，len 为 0 时允许为 NULL
 * @param len  数据长度 (字节)
 * @return 16 位校验值；上串口时按小端 (LSB 在前) 发送
 *
 * @note 长度参数用 uint16_t 而非 ADR-0003 草案中的 uint8_t —— 算法完全一致，
 *       仅放宽上限，避免未来帧长扩展时的静默截断。
 */
uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);

/**
 * 增量式 CRC 更新，用于流式校验 (逐字节喂入，无需缓冲整帧)。
 *
 * @param crc  当前 CRC 累加值，首次调用传 0xFFFF
 * @param byte 新字节
 * @return 更新后的 CRC 累加值
 */
uint16_t crc16_ccitt_update(uint16_t crc, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_COMMON_CRC16_H */
