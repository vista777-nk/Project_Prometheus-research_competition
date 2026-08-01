/**
 * @file startup_stm32f407vetx.c
 * @brief 复位向量、启动代码与中断向量表 (C 实现，不依赖汇编启动文件)
 *
 * 用 C 而不是常见的 startup_*.s：向量表本质是一张函数指针表，用 C 写同样精确，
 * 而且不必为 armasm / GNU as 的语法差异各留一份。链接时配合 -nostartfiles，
 * 由本文件的 Reset_Handler 完成 crt0 的工作。
 *
 * 启动顺序：
 *   1. 使能 FPU (必须在任何浮点指令之前 —— 编译用的是 -mfloat-abi=hard)
 *   2. 拷贝 .data 段 (Flash → SRAM)
 *   3. 清零 .bss 段
 *   4. 跳 main()
 */
#include <stdint.h>

/* 链接脚本导出的符号。取的是"地址"而不是"值"，故声明为数组。 */
extern uint32_t _sidata;    /**< .data 在 Flash 中的加载地址 */
extern uint32_t _sdata;     /**< .data 在 SRAM 中的起始地址 */
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;    /**< 主栈顶 */

extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

/* --- 异常与中断处理函数 ---------------------------------------------
 * 全部弱定义为死循环。其他翻译单元 (port_stm32f407.c) 里的同名强定义
 * 会在链接时自动覆盖，不需要修改本文件。
 */
#define WEAK_HANDLER(name) \
    __attribute__((weak)) void name(void) { Default_Handler(); }

WEAK_HANDLER(NMI_Handler)
WEAK_HANDLER(HardFault_Handler)
WEAK_HANDLER(MemManage_Handler)
WEAK_HANDLER(BusFault_Handler)
WEAK_HANDLER(UsageFault_Handler)
WEAK_HANDLER(SVC_Handler)
WEAK_HANDLER(DebugMon_Handler)
WEAK_HANDLER(PendSV_Handler)
WEAK_HANDLER(SysTick_Handler)

WEAK_HANDLER(WWDG_IRQHandler)
WEAK_HANDLER(PVD_IRQHandler)
WEAK_HANDLER(TAMP_STAMP_IRQHandler)
WEAK_HANDLER(RTC_WKUP_IRQHandler)
WEAK_HANDLER(FLASH_IRQHandler)
WEAK_HANDLER(RCC_IRQHandler)
WEAK_HANDLER(EXTI0_IRQHandler)
WEAK_HANDLER(EXTI1_IRQHandler)
WEAK_HANDLER(EXTI2_IRQHandler)
WEAK_HANDLER(EXTI3_IRQHandler)
WEAK_HANDLER(EXTI4_IRQHandler)
WEAK_HANDLER(DMA1_Stream0_IRQHandler)
WEAK_HANDLER(DMA1_Stream1_IRQHandler)
WEAK_HANDLER(DMA1_Stream2_IRQHandler)
WEAK_HANDLER(DMA1_Stream3_IRQHandler)
WEAK_HANDLER(DMA1_Stream4_IRQHandler)
WEAK_HANDLER(DMA1_Stream5_IRQHandler)
WEAK_HANDLER(DMA1_Stream6_IRQHandler)
WEAK_HANDLER(ADC_IRQHandler)
WEAK_HANDLER(CAN1_TX_IRQHandler)
WEAK_HANDLER(CAN1_RX0_IRQHandler)
WEAK_HANDLER(CAN1_RX1_IRQHandler)
WEAK_HANDLER(CAN1_SCE_IRQHandler)
WEAK_HANDLER(EXTI9_5_IRQHandler)
WEAK_HANDLER(TIM1_BRK_TIM9_IRQHandler)
WEAK_HANDLER(TIM1_UP_TIM10_IRQHandler)
WEAK_HANDLER(TIM1_TRG_COM_TIM11_IRQHandler)
WEAK_HANDLER(TIM1_CC_IRQHandler)
WEAK_HANDLER(TIM2_IRQHandler)
WEAK_HANDLER(TIM3_IRQHandler)
WEAK_HANDLER(TIM4_IRQHandler)
WEAK_HANDLER(I2C1_EV_IRQHandler)
WEAK_HANDLER(I2C1_ER_IRQHandler)
WEAK_HANDLER(I2C2_EV_IRQHandler)
WEAK_HANDLER(I2C2_ER_IRQHandler)
WEAK_HANDLER(SPI1_IRQHandler)
WEAK_HANDLER(SPI2_IRQHandler)
WEAK_HANDLER(USART1_IRQHandler)
WEAK_HANDLER(USART2_IRQHandler)
WEAK_HANDLER(USART3_IRQHandler)
WEAK_HANDLER(EXTI15_10_IRQHandler)
WEAK_HANDLER(RTC_Alarm_IRQHandler)
WEAK_HANDLER(OTG_FS_WKUP_IRQHandler)
WEAK_HANDLER(TIM8_BRK_TIM12_IRQHandler)
WEAK_HANDLER(TIM8_UP_TIM13_IRQHandler)
WEAK_HANDLER(TIM8_TRG_COM_TIM14_IRQHandler)
WEAK_HANDLER(TIM8_CC_IRQHandler)
WEAK_HANDLER(DMA1_Stream7_IRQHandler)
WEAK_HANDLER(FSMC_IRQHandler)
WEAK_HANDLER(SDIO_IRQHandler)
WEAK_HANDLER(TIM5_IRQHandler)
WEAK_HANDLER(SPI3_IRQHandler)
WEAK_HANDLER(UART4_IRQHandler)
WEAK_HANDLER(UART5_IRQHandler)
WEAK_HANDLER(TIM6_DAC_IRQHandler)
WEAK_HANDLER(TIM7_IRQHandler)

/* --- 中断向量表 ---
 * 放在 .isr_vector 段，链接脚本把它摆在 Flash 起始处 (0x08000000)。
 * 表项 0 是初始主栈指针，表项 1 是复位入口，其后按 RM0090 表 62 的顺序排列。
 * 表在 TIM7 (IRQ 55) 之后截断 —— 本固件不会使能更靠后的中断源，
 * 未列出的向量取不到也不会被触发。
 */
typedef void (*isr_handler_t)(void);

__attribute__((section(".isr_vector"), used))
const isr_handler_t g_pfnVectors[] = {
    (isr_handler_t)(&_estack),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,                     /* 保留 */
    SVC_Handler,
    DebugMon_Handler,
    0,                              /* 保留 */
    PendSV_Handler,
    SysTick_Handler,

    WWDG_IRQHandler,                /* IRQ  0 */
    PVD_IRQHandler,
    TAMP_STAMP_IRQHandler,
    RTC_WKUP_IRQHandler,
    FLASH_IRQHandler,
    RCC_IRQHandler,
    EXTI0_IRQHandler,
    EXTI1_IRQHandler,
    EXTI2_IRQHandler,
    EXTI3_IRQHandler,
    EXTI4_IRQHandler,               /* IRQ 10 */
    DMA1_Stream0_IRQHandler,
    DMA1_Stream1_IRQHandler,
    DMA1_Stream2_IRQHandler,
    DMA1_Stream3_IRQHandler,
    DMA1_Stream4_IRQHandler,
    DMA1_Stream5_IRQHandler,
    DMA1_Stream6_IRQHandler,
    ADC_IRQHandler,
    CAN1_TX_IRQHandler,
    CAN1_RX0_IRQHandler,            /* IRQ 20 */
    CAN1_RX1_IRQHandler,
    CAN1_SCE_IRQHandler,
    EXTI9_5_IRQHandler,
    TIM1_BRK_TIM9_IRQHandler,
    TIM1_UP_TIM10_IRQHandler,
    TIM1_TRG_COM_TIM11_IRQHandler,
    TIM1_CC_IRQHandler,
    TIM2_IRQHandler,
    TIM3_IRQHandler,
    TIM4_IRQHandler,                /* IRQ 30 */
    I2C1_EV_IRQHandler,
    I2C1_ER_IRQHandler,
    I2C2_EV_IRQHandler,
    I2C2_ER_IRQHandler,
    SPI1_IRQHandler,
    SPI2_IRQHandler,
    USART1_IRQHandler,              /* IRQ 37 — 上位机串口 */
    USART2_IRQHandler,
    USART3_IRQHandler,
    EXTI15_10_IRQHandler,           /* IRQ 40 */
    RTC_Alarm_IRQHandler,
    OTG_FS_WKUP_IRQHandler,
    TIM8_BRK_TIM12_IRQHandler,
    TIM8_UP_TIM13_IRQHandler,
    TIM8_TRG_COM_TIM14_IRQHandler,
    TIM8_CC_IRQHandler,
    DMA1_Stream7_IRQHandler,
    FSMC_IRQHandler,
    SDIO_IRQHandler,
    TIM5_IRQHandler,                /* IRQ 50 */
    SPI3_IRQHandler,
    UART4_IRQHandler,
    UART5_IRQHandler,
    TIM6_DAC_IRQHandler,            /* IRQ 54 — 1kHz 速度环 */
    TIM7_IRQHandler                 /* IRQ 55 */
};

/** 未安装处理函数的中断落到这里。停在死循环里，方便调试器抓现场。 */
void Default_Handler(void)
{
    while (1) {
        /* 有意死循环：静默返回只会让问题变成"偶发怪现象" */
    }
}

void Reset_Handler(void)
{
    /* 1. 使能 FPU (CP10/CP11 全访问)。编译用的是硬浮点 ABI，
          在此之前执行任何浮点指令都会触发 UsageFault。 */
    volatile uint32_t *const cpacr = (volatile uint32_t *)0xE000ED88uL;
    *cpacr |= (0xFuL << 20);
    __asm volatile ("dsb");
    __asm volatile ("isb");

    /* 2. 把已初始化的全局变量从 Flash 搬到 SRAM */
    uintptr_t src_addr = (uintptr_t)&_sidata;
    uintptr_t dst_addr = (uintptr_t)&_sdata;
    const uintptr_t data_end = (uintptr_t)&_edata;
    while (dst_addr < data_end) {
        *(uint32_t *)dst_addr = *(const uint32_t *)src_addr;
        dst_addr += sizeof(uint32_t);
        src_addr += sizeof(uint32_t);
    }

    /* 3. 未初始化的全局变量清零 */
    dst_addr = (uintptr_t)&_sbss;
    const uintptr_t bss_end = (uintptr_t)&_ebss;
    while (dst_addr < bss_end) {
        *(uint32_t *)dst_addr = 0uL;
        dst_addr += sizeof(uint32_t);
    }

    (void)main();

    /* main() 不应返回。真返回了就停住，不要让 PC 跑到未知区域。 */
    while (1) {
    }
}
