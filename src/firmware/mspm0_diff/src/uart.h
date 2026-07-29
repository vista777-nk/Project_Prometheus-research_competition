/**
 * @file uart.h
 * @brief UART 环形缓冲收发 (对接树莓派 /dev/ttyAMA1, 115200 8N1)
 *
 * 与麦轮固件不同，本模块**不含任何寄存器访问** —— 字节进出通过 mcu_port.h 完成。
 * 协议层再通过注入写函数与本模块解耦，因此 Host 测试里两层都可以替换掉。
 */
#ifndef MSPM0_DIFF_UART_H
#define MSPM0_DIFF_UART_H

#include <stdbool.h>
#include <stdint.h>

/** 配置串口与中断。需在 port_system_init() 之后调用。 */
void uart_init(void);

/**
 * 发送一段字节：拷进发送环形缓冲，随后通知移植层开始送。
 *
 * 缓冲不足时**整段丢弃**并累加丢包计数 —— 宁可丢一整帧遥测，
 * 也不要送出半帧，半帧会让对端的拆帧器进入失同步窗口。
 *
 * 送出方式由移植层决定 (MSPM0 就地忙等抽干 / STM32 开 TXE 中断)，
 * 本模块只负责排队，因此两块板共用同一份环形缓冲逻辑。
 */
void uart_write(const uint8_t *data, uint16_t len);

/**
 * 取走接收环形缓冲里的数据。
 * @return 实际取出的字节数
 */
uint16_t uart_read(uint8_t *dst, uint16_t cap);

/**
 * 取走并清除"接收线路空闲"事件标志。
 * 主循环据此调用 protocol_notify_line_idle() 完成拆帧重同步。
 */
bool uart_take_idle_event(void);

/** 接收溢出次数 (RX 环形缓冲满 或 硬件 ORE) */
uint32_t uart_get_rx_overrun_count(void);
/** 发送丢弃次数 (TX 环形缓冲满) */
uint32_t uart_get_tx_drop_count(void);

#endif /* MSPM0_DIFF_UART_H */
