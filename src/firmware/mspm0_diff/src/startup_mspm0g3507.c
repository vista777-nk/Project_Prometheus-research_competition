/**
 * @file startup_mspm0g3507.c
 * @brief Cortex-M0+ 向量表与复位入口 (C 实现，不用汇编)
 *
 * 用 C 而不是汇编写启动文件的理由与麦轮固件相同：向量表是一个函数指针数组，
 * 段拷贝是两个 memcpy —— 用汇编写既不会更快也不会更清楚，只会多一门语言。
 *
 * ⚠ 两处与器件强相关、必须核对的地方：
 *
 *   1. **中断向量名**。本文件只给出 ARMv6-M 内核异常 (Reset/NMI/HardFault/
 *      SVC/PendSV/SysTick) 的真实名字，外设中断一律叫 IRQ0..IRQ31_Handler。
 *      MSPM0 的外设→IRQ 编号映射在 TI 器件头 (ti/devices/msp/m0p/mspm0g350x.h)
 *      里，`make PROFILE=driverlib` 时应改用 SDK 自带的启动文件，本文件不参与编译。
 *      我不去猜这张表 —— 猜错的后果是中断进错向量，症状极其难查。
 *
 *   2. **栈顶地址**由链接脚本给出，见 MSPM0G3507.ld 的 _estack。
 *
 * Cortex-M0+ 没有 FPU，因此不需要麦轮固件里的 CPACR 使能步骤。
 */
#include <stdint.h>

/* --- 链接脚本导出的符号 --- */
extern uint32_t _estack;     /**< 栈顶 (SRAM 末端) */
extern uint32_t _sidata;     /**< .data 段在 Flash 中的加载地址 */
extern uint32_t _sdata;      /**< .data 段在 SRAM 中的起始 */
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

int main(void);

void Reset_Handler(void);
void Default_Handler(void);

/* ARMv6-M 内核异常 */
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));

/* 外设中断。名字刻意保持通用 —— 真实映射见本文件顶部说明。 */
#define DECLARE_IRQ(n) \
    void IRQ##n##_Handler(void) __attribute__((weak, alias("Default_Handler")))

DECLARE_IRQ(0);  DECLARE_IRQ(1);  DECLARE_IRQ(2);  DECLARE_IRQ(3);
DECLARE_IRQ(4);  DECLARE_IRQ(5);  DECLARE_IRQ(6);  DECLARE_IRQ(7);
DECLARE_IRQ(8);  DECLARE_IRQ(9);  DECLARE_IRQ(10); DECLARE_IRQ(11);
DECLARE_IRQ(12); DECLARE_IRQ(13); DECLARE_IRQ(14); DECLARE_IRQ(15);
DECLARE_IRQ(16); DECLARE_IRQ(17); DECLARE_IRQ(18); DECLARE_IRQ(19);
DECLARE_IRQ(20); DECLARE_IRQ(21); DECLARE_IRQ(22); DECLARE_IRQ(23);
DECLARE_IRQ(24); DECLARE_IRQ(25); DECLARE_IRQ(26); DECLARE_IRQ(27);
DECLARE_IRQ(28); DECLARE_IRQ(29); DECLARE_IRQ(30); DECLARE_IRQ(31);

/** 向量表。链接脚本把 .isr_vector 放在 Flash 最前端。 */
__attribute__((section(".isr_vector"), used))
void (* const g_vector_table[])(void) = {
    (void (*)(void))(&_estack),   /*  0: 初始栈顶 */
    Reset_Handler,                /*  1 */
    NMI_Handler,                  /*  2 */
    HardFault_Handler,            /*  3 */
    0, 0, 0, 0, 0, 0, 0,          /*  4..10: ARMv6-M 保留 */
    SVC_Handler,                  /* 11 */
    0, 0,                         /* 12..13: 保留 */
    PendSV_Handler,               /* 14 */
    SysTick_Handler,              /* 15 */

    IRQ0_Handler,  IRQ1_Handler,  IRQ2_Handler,  IRQ3_Handler,
    IRQ4_Handler,  IRQ5_Handler,  IRQ6_Handler,  IRQ7_Handler,
    IRQ8_Handler,  IRQ9_Handler,  IRQ10_Handler, IRQ11_Handler,
    IRQ12_Handler, IRQ13_Handler, IRQ14_Handler, IRQ15_Handler,
    IRQ16_Handler, IRQ17_Handler, IRQ18_Handler, IRQ19_Handler,
    IRQ20_Handler, IRQ21_Handler, IRQ22_Handler, IRQ23_Handler,
    IRQ24_Handler, IRQ25_Handler, IRQ26_Handler, IRQ27_Handler,
    IRQ28_Handler, IRQ29_Handler, IRQ30_Handler, IRQ31_Handler
};

void Reset_Handler(void)
{
    /* 1. 把 .data 的初值从 Flash 搬到 SRAM */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* 2. .bss 清零 */
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0uL;
    }

    /* 3. 进 main。正常情况下不返回。 */
    (void)main();

    /* main 若真的返回了，停在这里而不是跑飞 —— 让调试器能抓到现场 */
    for (;;) {
    }
}

/**
 * 未注册中断的兜底处理。
 *
 * 刻意死循环而不是"忽略后返回"：一个没人处理的中断如果被静默清掉，
 * 故障会以某个外设偶尔不工作的形式散布出去；停在这里至少能被调试器一眼抓到。
 */
void Default_Handler(void)
{
    for (;;) {
    }
}
