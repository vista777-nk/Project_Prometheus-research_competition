/**
 * @file uart.c
 * @brief USART1 中断收发实现
 *
 * 收发都走环形缓冲 + 中断，主循环永不阻塞在串口上：
 *   RX  : RXNE 中断逐字节入队；IDLE 中断置空闲标志 (拆帧重同步用)
 *   TX  : uart_write() 入队并打开 TXEIE，TXE 中断逐字节出队，队空关 TXEIE
 *
 * 为什么不用 DMA (task-10 §10.5 原文提到 DMA)：
 *   接收侧是变长帧，DMA 循环模式仍要靠 IDLE 中断切分，省不下多少中断；
 *   发送侧单帧最长 40 字节，@115200 约 3.5ms，TXE 中断的开销完全可接受。
 *   Phase 1 优先选更少出错面的实现，DMA 留到确实成为瓶颈时再上。
 *
 * @note 尚未在真实硬件上验证。
 */
#include "uart.h"

#include "board_config.h"
#include "bsp.h"

/* 环形缓冲容量必须是 2 的幂，索引用位与代替取模 */
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
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9 = TX, PA10 = RX, AF7。RX 上拉，防止线缆未接时悬空乱收。 */
    bsp_gpio_config(GPIOA, 9u,  GPIO_MODE_AF, 7u, GPIO_PULL_NONE);
    bsp_gpio_config(GPIOA, 10u, GPIO_MODE_AF, 7u, GPIO_PULL_UP);

    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_tx_head = 0u;
    s_tx_tail = 0u;
    s_idle_event = false;
    s_rx_overruns = 0u;
    s_tx_drops = 0u;

    USART1->CR1 = 0uL;
    /* BRR 的编码恰好就是 "fPCLK/baud" 的 Q12.4 定点数，四舍五入即可 */
    USART1->BRR = (PCLK2_HZ + (UART_BAUDRATE / 2uL)) / UART_BAUDRATE;
    USART1->CR2 = 0uL;                  /* 1 位停止位 */
    USART1->CR3 = 0uL;                  /* 无硬件流控 */
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE
                | USART_CR1_RXNEIE | USART_CR1_IDLEIE;

    nvic_enable_irq(IRQn_USART1);
}

void uart_write(const uint8_t *data, uint16_t len)
{
    if (data == 0 || len == 0u) {
        return;
    }

    /* 先检查空间是否足够装下整段：半截帧发出去只会让对端多一次 CRC 错误 */
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

    /* 打开 TXE 中断，剩下的交给中断把队列排空 */
    USART1->CR1 |= USART_CR1_TXEIE;
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
    irq_disable();
    const bool event = s_idle_event;
    s_idle_event = false;
    irq_enable();
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

/** USART1 中断服务例程，覆盖 startup 里的弱定义 */
void USART1_IRQHandler(void)
{
    const uint32_t status = USART1->SR;

    /* 溢出：必须读 SR 再读 DR 才能清标志，否则会一直重入中断 */
    if ((status & USART_SR_ORE) != 0u) {
        (void)USART1->DR;
        s_rx_overruns++;
    }

    if ((status & USART_SR_RXNE) != 0u) {
        const uint8_t byte = (uint8_t)(USART1->DR & 0xFFuL);
        const uint16_t next = (uint16_t)((s_rx_head + 1u) & RX_BUFFER_MASK);
        if (next != s_rx_tail) {
            s_rx_buf[s_rx_head] = byte;
            s_rx_head = next;
        } else {
            /* 缓冲满：丢掉新字节而不是覆盖旧字节 —— 保住已经收到的半帧 */
            s_rx_overruns++;
        }
    }

    if ((status & USART_SR_IDLE) != 0u) {
        (void)USART1->DR;               /* 读 SR 后读 DR 清 IDLE 标志 */
        s_idle_event = true;
    }

    if ((status & USART_SR_TXE) != 0u && (USART1->CR1 & USART_CR1_TXEIE) != 0u) {
        if (s_tx_tail != s_tx_head) {
            USART1->DR = s_tx_buf[s_tx_tail];
            s_tx_tail = (uint16_t)((s_tx_tail + 1u) & TX_BUFFER_MASK);
        } else {
            USART1->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}
