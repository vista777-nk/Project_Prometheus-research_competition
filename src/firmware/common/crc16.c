/**
 * @file crc16.c
 * @brief CRC-16/CCITT-FALSE 实现 (ADR-0003)
 *
 * 采用逐位移位实现而非查表法：
 *   - 帧长上限 255 字节、控制周期 1kHz，逐位实现的开销可忽略
 *   - 省下 512 字节查表空间 (MSPM0G3507 只有 128KB Flash)
 *   - 与 ADR-0003 中给出的参考实现逐行对应，便于三方比对
 */
#include "crc16.h"

uint16_t crc16_ccitt_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    for (uint8_t bit = 0; bit < 8u; bit++) {
        if (crc & 0x8000u) {
            crc = (uint16_t)((crc << 1) ^ 0x1021u);
        } else {
            crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;

    if (data == 0) {
        /* 空载荷帧 (如 EMERGENCY_STOP / PING) 的 DATA 段长度为 0，
           此时只对 CMD 字节做校验，data 可以是 NULL。 */
        return crc;
    }

    for (uint16_t i = 0; i < len; i++) {
        crc = crc16_ccitt_update(crc, data[i]);
    }
    return crc;
}
