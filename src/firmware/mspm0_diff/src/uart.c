/**
 * @file uart.c
 * @brief 收发环形缓冲实现
 *
 * 缓冲策略上有一条刻意的不对称：
 *
 *   接收满了 → 丢**最新到达的那个字节**，计一次溢出
 *   发送满了 → 丢**整段待发数据**，计一次丢包
 *
 * 发送侧之所以要么全写要么全不写：半帧发出去会让对端的拆帧器按错误的 LEN
 * 空等最多 257 字节 (见 protocol_frame.h 的 @warning)，
 * 一次半帧造成的损失远大于丢一整帧遥测。遥测是 20Hz 周期性的，丢一帧毫无影响。
 */
#include "uart.h"

#include "board_config.h"
#include "mcu_port.h"

/** 缓冲区容量必须是 2 的幂 —— 用掩码取模，Cortex-M0+ 没有硬件除法 */
#define RX_BUFFER_SIZE      256u
#define TX_BUFFER_SIZE      256u
#define RX_BUFFER_MASK      (RX_BUFFER_SIZE - 1u)
#define TX_BUFFER_MASK      (TX_BUFFER_SIZE - 1u)

static volatile uint8_t  s_rx_buf[RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head;   /**< 中断写入位置 */
static volatile uint16_t s_rx_tail;   /**< 主循环读取位置 */

static uint8_t  s_tx_buf[TX_BUFFER_SIZE];
static uint16_t s_tx_head;
static uint16_t s_tx_tail;

static volatile bool     s_idle_event;
static volatile uint32_t s_rx_overruns;
static uint32_t          s_tx_drops;

void uart_init(void)
{
    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_tx_head = 0u;
    s_tx_tail = 0u;
    s_idle_event = false;
    s_rx_overruns = 0u;
    s_tx_drops = 0u;

    port_uart_init(UART_BAUDRATE);
}

/* ===================== 中断上下文回调 ===================== */

void port_uart_rx_hook(uint8_t byte)
{
    const uint16_t next = (uint16_t)((s_rx_head + 1u) & RX_BUFFER_MASK);
    if (next == s_rx_tail) {
        s_rx_overruns++;   /* 缓冲满：丢弃新字节，保住已收到的完整帧 */
        return;
    }
    s_rx_buf[s_rx_head] = byte;
    s_rx_head = next;
}

void port_uart_idle_hook(void)
{
    s_idle_event = true;
}

void port_uart_overrun_hook(void)
{
    s_rx_overruns++;
}

/* ===================== 主循环上下文 ===================== */

uint16_t uart_read(uint8_t *dst, uint16_t cap)
{
    if (dst == 0) {
        return 0u;
    }

    uint16_t count = 0u;
    while (count < cap) {
        /* 只读 volatile 头指针一次，避免中断在循环中途改变判定 */
        const uint16_t head = s_rx_head;
        if (s_rx_tail == head) {
            break;
        }
        dst[count++] = s_rx_buf[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) & RX_BUFFER_MASK);
    }
    return count;
}

bool uart_take_idle_event(void)
{
    port_irq_disable();
    const bool event = s_idle_event;
    s_idle_event = false;
    port_irq_enable();
    return event;
}

/** 发送缓冲当前可用空间 */
static uint16_t tx_free_space(void)
{
    const uint16_t used = (uint16_t)((s_tx_head - s_tx_tail) & TX_BUFFER_MASK);
    return (uint16_t)(TX_BUFFER_SIZE - 1u - used);
}

void uart_write(const uint8_t *data, uint16_t len)
{
    if (data == 0 || len == 0u) {
        return;
    }
    /* 要么整段写入，要么整段丢弃 —— 绝不发半帧 */
    if (len > tx_free_space()) {
        s_tx_drops++;
        return;
    }
    for (uint16_t i = 0; i < len; i++) {
        s_tx_buf[s_tx_head] = data[i];
        s_tx_head = (uint16_t)((s_tx_head + 1u) & TX_BUFFER_MASK);
    }
    /* 通知移植层有数据要发。怎么发是各板自己的事：
       MSPM0 就地忙等抽干，STM32 打开 TXE 中断慢慢送 —— 本文件不关心。 */
    port_uart_tx_start();
}

/**
 * 移植层取下一个待发字节。
 *
 * @note 在 STM32 上本函数运行于 TXE 中断上下文，在 MSPM0 上运行于主循环 ——
 *       两种情形下 s_tx_tail 都只有一个写者，s_tx_head 只有一个写者 (uart_write)，
 *       是标准的单生产者单消费者环形缓冲，不需要额外临界区。
 */
bool port_uart_tx_next(uint8_t *byte)
{
    if (byte == 0 || s_tx_tail == s_tx_head) {
        return false;
    }
    *byte = s_tx_buf[s_tx_tail];
    s_tx_tail = (uint16_t)((s_tx_tail + 1u) & TX_BUFFER_MASK);
    return true;
}

uint32_t uart_get_rx_overrun_count(void)
{
    return s_rx_overruns;
}

uint32_t uart_get_tx_drop_count(void)
{
    return s_tx_drops;
}
