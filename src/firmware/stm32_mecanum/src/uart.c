/**
 * @file uart.c
 * @brief 收发环形缓冲实现（纯逻辑，不碰寄存器）
 *
 * 字节进出通过 mcu_port.h 完成：接收由移植层在中断里调 port_uart_rx_hook()，
 * 发送由移植层调 port_uart_tx_next() 取字节。因此本文件与 MSPM0 版本
 * 逻辑完全一致，两块板的发送策略差异（STM32 走 TXE 中断 / MSPM0 就地忙等）
 * 被完全挡在移植层里。
 *
 * 缓冲策略上有一条刻意的不对称：
 *
 *   接收满了 → 丢**最新到达的那个字节**，计一次溢出
 *   发送满了 → 丢**整段待发数据**，计一次丢包
 *
 * 发送侧之所以要么全写要么全不写：半帧发出去会让对端的拆帧器按错误的 LEN
 * 空等最多 257 字节（见 protocol_frame.h 的 @warning），
 * 一次半帧造成的损失远大于丢一整帧遥测。遥测是 20Hz 周期性的，丢一帧毫无影响。
 *
 * 为什么不用 DMA（task-10 §10.5 原文提到 DMA）：
 *   接收侧是变长帧，DMA 循环模式仍要靠 IDLE 中断切分，省不下多少中断；
 *   发送侧单帧最长 40 字节，@115200 约 3.5ms，TXE 中断的开销完全可接受。
 *   Phase 1 优先选更少出错面的实现，DMA 留到确实成为瓶颈时再上。
 */
#include "uart.h"

#include "board_config.h"
#include "mcu_port.h"

/** 缓冲区容量必须是 2 的幂 —— 用掩码取模 */
#define RX_BUFFER_SIZE      256u
#define TX_BUFFER_SIZE      512u
#define RX_BUFFER_MASK      (RX_BUFFER_SIZE - 1u)
#define TX_BUFFER_MASK      (TX_BUFFER_SIZE - 1u)

static volatile uint8_t  s_rx_buf[RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head;      /* 中断写 */
static volatile uint16_t s_rx_tail;      /* 主循环读 */

static volatile uint8_t  s_tx_buf[TX_BUFFER_SIZE];
static volatile uint16_t s_tx_head;      /* 主循环写 */
static volatile uint16_t s_tx_tail;      /* 中断读 */

static volatile bool     s_idle_event;
static volatile uint32_t s_rx_overruns;
static volatile uint32_t s_tx_drops;

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
        /* 缓冲满：丢掉新字节而不是覆盖旧字节 —— 保住已经收到的半帧 */
        s_rx_overruns++;
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

/**
 * 移植层取下一个待发字节。
 *
 * @note 在 STM32 上本函数运行于 TXE 中断上下文。s_tx_tail 只有本函数一个写者、
 *       s_tx_head 只有 uart_write() 一个写者，是标准的单生产者单消费者
 *       环形缓冲，不需要额外临界区。
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

/* ===================== 主循环上下文 ===================== */

void uart_write(const uint8_t *data, uint16_t len)
{
    if (data == 0 || len == 0u) {
        return;
    }

    /* 先检查空间是否足够装下整段：绝不发半帧 */
    const uint16_t used = (uint16_t)((s_tx_head - s_tx_tail) & TX_BUFFER_MASK);
    if ((uint32_t)used + len >= TX_BUFFER_SIZE) {
        s_tx_drops++;
        return;
    }

    uint16_t head = s_tx_head;
    for (uint16_t i = 0; i < len; i++) {
        s_tx_buf[head] = data[i];
        head = (uint16_t)((head + 1u) & TX_BUFFER_MASK);
    }
    s_tx_head = head;

    /* 通知移植层有数据要发。怎么发是它的事：
       STM32 打开 TXE 中断慢慢送，MSPM0 就地忙等抽干 —— 本文件不关心。 */
    port_uart_tx_start();
}

uint16_t uart_read(uint8_t *dst, uint16_t cap)
{
    if (dst == 0) {
        return 0u;
    }

    uint16_t count = 0u;
    uint16_t tail = s_rx_tail;
    while (count < cap && tail != s_rx_head) {
        dst[count++] = s_rx_buf[tail];
        tail = (uint16_t)((tail + 1u) & RX_BUFFER_MASK);
    }
    s_rx_tail = tail;
    return count;
}

bool uart_take_idle_event(void)
{
    /* 读-改-写 volatile 标志，需要短暂关中断，否则可能丢掉一次事件 */
    port_irq_disable();
    const bool event = s_idle_event;
    s_idle_event = false;
    port_irq_enable();
    return event;
}

uint32_t uart_get_rx_overrun_count(void)
{
    return s_rx_overruns;
}

uint32_t uart_get_tx_drop_count(void)
{
    return s_tx_drops;
}
