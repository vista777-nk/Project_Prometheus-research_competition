/**
 * @file stm32f407_regs.h
 * @brief STM32F407VET6 寄存器映射 —— 本固件用到的最小子集
 *
 * 为什么不直接用 ST 官方 HAL / CMSIS：
 *   - Phase 1 的目标是 "CI 里 `make` 一把过 + Host 单元测试全绿"。引入 HAL 意味着
 *     往仓库里塞进几万行厂商代码，或者让 CI 去外网拉 STM32CubeF4 —— 两者都让
 *     "克隆下来就能编" 这件事变脆。
 *   - 本固件真正用到的外设只有 GPIO / TIM / USART / ADC / SysTick，
 *     直接写寄存器反而更短、更好读、更好审。
 *
 * 如果将来确实需要 HAL (比如上 USB 或 SDIO)，在 stm32f4xx_conf.h 里定义
 * USE_HAL_DRIVER 切换即可，本文件的常量命名刻意与 CMSIS 保持一致。
 *
 * 参考手册：RM0090 Rev 19 (STM32F405/407/415/417)
 */
#ifndef STM32_MECANUM_STM32F407_REGS_H
#define STM32_MECANUM_STM32F407_REGS_H

#include <stdint.h>

#define __IO    volatile

/* ===================== 总线基地址 ===================== */
#define PERIPH_BASE         0x40000000uL
#define APB1PERIPH_BASE     (PERIPH_BASE + 0x00000000uL)
#define APB2PERIPH_BASE     (PERIPH_BASE + 0x00010000uL)
#define AHB1PERIPH_BASE     (PERIPH_BASE + 0x00020000uL)

/* ===================== GPIO ===================== */
typedef struct {
    __IO uint32_t MODER;    /**< 端口模式          偏移 0x00 */
    __IO uint32_t OTYPER;   /**< 输出类型          0x04 */
    __IO uint32_t OSPEEDR;  /**< 输出速度          0x08 */
    __IO uint32_t PUPDR;    /**< 上下拉            0x0C */
    __IO uint32_t IDR;      /**< 输入数据          0x10 */
    __IO uint32_t ODR;      /**< 输出数据          0x14 */
    __IO uint32_t BSRR;     /**< 置位/复位 (原子)  0x18 */
    __IO uint32_t LCKR;     /**< 配置锁定          0x1C */
    __IO uint32_t AFR[2];   /**< 复用功能 低/高    0x20 */
} GPIO_TypeDef;

#define GPIOA               ((GPIO_TypeDef *)(AHB1PERIPH_BASE + 0x0000uL))
#define GPIOB               ((GPIO_TypeDef *)(AHB1PERIPH_BASE + 0x0400uL))
#define GPIOC               ((GPIO_TypeDef *)(AHB1PERIPH_BASE + 0x0800uL))
#define GPIOD               ((GPIO_TypeDef *)(AHB1PERIPH_BASE + 0x0C00uL))
#define GPIOE               ((GPIO_TypeDef *)(AHB1PERIPH_BASE + 0x1000uL))

/* GPIO MODER 取值 */
#define GPIO_MODE_INPUT     0x0uL
#define GPIO_MODE_OUTPUT    0x1uL
#define GPIO_MODE_AF        0x2uL
#define GPIO_MODE_ANALOG    0x3uL
/* GPIO PUPDR 取值 */
#define GPIO_PULL_NONE      0x0uL
#define GPIO_PULL_UP        0x1uL
#define GPIO_PULL_DOWN      0x2uL

/* ===================== TIM ===================== */
typedef struct {
    __IO uint32_t CR1;      /**< 0x00 */
    __IO uint32_t CR2;      /**< 0x04 */
    __IO uint32_t SMCR;     /**< 0x08 从模式控制 (编码器模式在此配置) */
    __IO uint32_t DIER;     /**< 0x0C 中断使能 */
    __IO uint32_t SR;       /**< 0x10 状态 */
    __IO uint32_t EGR;      /**< 0x14 事件产生 */
    __IO uint32_t CCMR1;    /**< 0x18 捕获/比较模式 1 */
    __IO uint32_t CCMR2;    /**< 0x1C 捕获/比较模式 2 */
    __IO uint32_t CCER;     /**< 0x20 捕获/比较使能 */
    __IO uint32_t CNT;      /**< 0x24 计数器 */
    __IO uint32_t PSC;      /**< 0x28 预分频 */
    __IO uint32_t ARR;      /**< 0x2C 自动重装 */
    __IO uint32_t RCR;      /**< 0x30 重复计数 (仅高级定时器) */
    __IO uint32_t CCR1;     /**< 0x34 */
    __IO uint32_t CCR2;     /**< 0x38 */
    __IO uint32_t CCR3;     /**< 0x3C */
    __IO uint32_t CCR4;     /**< 0x40 */
    __IO uint32_t BDTR;     /**< 0x44 刹车与死区 (仅 TIM1/TIM8) */
    __IO uint32_t DCR;      /**< 0x48 */
    __IO uint32_t DMAR;     /**< 0x4C */
    __IO uint32_t OR;       /**< 0x50 */
} TIM_TypeDef;

#define TIM2                ((TIM_TypeDef *)(APB1PERIPH_BASE + 0x0000uL))
#define TIM3                ((TIM_TypeDef *)(APB1PERIPH_BASE + 0x0400uL))
#define TIM4                ((TIM_TypeDef *)(APB1PERIPH_BASE + 0x0800uL))
#define TIM5                ((TIM_TypeDef *)(APB1PERIPH_BASE + 0x0C00uL))
#define TIM6                ((TIM_TypeDef *)(APB1PERIPH_BASE + 0x1000uL))
#define TIM1                ((TIM_TypeDef *)(APB2PERIPH_BASE + 0x0000uL))
#define TIM8                ((TIM_TypeDef *)(APB2PERIPH_BASE + 0x0400uL))

#define TIM_CR1_CEN         (1uL << 0)
#define TIM_CR1_ARPE        (1uL << 7)
#define TIM_CR2_MMS_UPDATE  (2uL << 4)
#define TIM_DIER_UIE        (1uL << 0)
#define TIM_DIER_CC1IE      (1uL << 1)
#define TIM_DIER_CC2IE      (1uL << 2)
#define TIM_DIER_CC3IE      (1uL << 3)
#define TIM_DIER_CC4IE      (1uL << 4)
#define TIM_SR_UIF          (1uL << 0)
#define TIM_SR_CC1IF        (1uL << 1)
#define TIM_SR_CC2IF        (1uL << 2)
#define TIM_SR_CC3IF        (1uL << 3)
#define TIM_SR_CC4IF        (1uL << 4)
#define TIM_EGR_UG          (1uL << 0)
#define TIM_CCER_CC1E       (1uL << 0)
#define TIM_CCER_CC1P       (1uL << 1)
#define TIM_CCER_CC2E       (1uL << 4)
#define TIM_CCER_CC2P       (1uL << 5)
#define TIM_CCER_CC3E       (1uL << 8)
#define TIM_CCER_CC3P       (1uL << 9)
#define TIM_CCER_CC4E       (1uL << 12)
#define TIM_CCER_CC4P       (1uL << 13)
#define TIM_BDTR_MOE        (1uL << 15)
/** 从模式：编码器模式 3 —— TI1 与 TI2 双边沿计数，即 4 倍频 */
#define TIM_SMCR_SMS_ENCODER3   (3uL << 0)
/** PWM 模式 1 + 预装载，写在 CCMRx 的对应半字里 */
#define TIM_CCMR_PWM1_CH_LOW    ((6uL << 4) | (1uL << 3))    /* OCxM=110, OCxPE=1 */
#define TIM_CCMR_PWM1_CH_HIGH   ((6uL << 12) | (1uL << 11))
/** 输入捕获映射到 TIx (编码器模式需要) */
#define TIM_CCMR1_CC1S_TI1      (1uL << 0)
#define TIM_CCMR1_CC2S_TI2      (1uL << 8)
#define TIM_CCMR2_CC3S_TI3      (1uL << 0)
#define TIM_CCMR2_CC4S_TI4      (1uL << 8)

/* ===================== USART ===================== */
typedef struct {
    __IO uint32_t SR;       /**< 0x00 状态 */
    __IO uint32_t DR;       /**< 0x04 数据 */
    __IO uint32_t BRR;      /**< 0x08 波特率 */
    __IO uint32_t CR1;      /**< 0x0C */
    __IO uint32_t CR2;      /**< 0x10 */
    __IO uint32_t CR3;      /**< 0x14 */
    __IO uint32_t GTPR;     /**< 0x18 */
} USART_TypeDef;

#define USART1              ((USART_TypeDef *)(APB2PERIPH_BASE + 0x1000uL))

#define USART_SR_RXNE       (1uL << 5)
#define USART_SR_TC         (1uL << 6)
#define USART_SR_TXE        (1uL << 7)
#define USART_SR_IDLE       (1uL << 4)
#define USART_SR_ORE        (1uL << 3)
#define USART_CR1_RE        (1uL << 2)
#define USART_CR1_TE        (1uL << 3)
#define USART_CR1_IDLEIE    (1uL << 4)
#define USART_CR1_RXNEIE    (1uL << 5)
#define USART_CR1_TXEIE     (1uL << 7)
#define USART_CR1_UE        (1uL << 13)

/* ===================== ADC ===================== */
typedef struct {
    __IO uint32_t SR;
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t SMPR1;
    __IO uint32_t SMPR2;
    __IO uint32_t JOFR[4];
    __IO uint32_t HTR;
    __IO uint32_t LTR;
    __IO uint32_t SQR1;
    __IO uint32_t SQR2;
    __IO uint32_t SQR3;
    __IO uint32_t JSQR;
    __IO uint32_t JDR[4];
    __IO uint32_t DR;
} ADC_TypeDef;

#define ADC1                ((ADC_TypeDef *)(APB2PERIPH_BASE + 0x2000uL))
/** ADC 公共寄存器 CCR (预分频在此) */
#define ADC_CCR             (*(__IO uint32_t *)(APB2PERIPH_BASE + 0x2304uL))

#define ADC_CR2_ADON        (1uL << 0)
#define ADC_CR2_SWSTART     (1uL << 30)
#define ADC_SR_EOC          (1uL << 1)

/* ===================== RCC ===================== */
typedef struct {
    __IO uint32_t CR;           /**< 0x00 */
    __IO uint32_t PLLCFGR;      /**< 0x04 */
    __IO uint32_t CFGR;         /**< 0x08 */
    __IO uint32_t CIR;          /**< 0x0C */
    __IO uint32_t AHB1RSTR;     /**< 0x10 */
    __IO uint32_t AHB2RSTR;     /**< 0x14 */
    __IO uint32_t AHB3RSTR;     /**< 0x18 */
    uint32_t      RESERVED0;    /**< 0x1C */
    __IO uint32_t APB1RSTR;     /**< 0x20 */
    __IO uint32_t APB2RSTR;     /**< 0x24 */
    uint32_t      RESERVED1[2]; /**< 0x28 */
    __IO uint32_t AHB1ENR;      /**< 0x30 */
    __IO uint32_t AHB2ENR;      /**< 0x34 */
    __IO uint32_t AHB3ENR;      /**< 0x38 */
    uint32_t      RESERVED2;    /**< 0x3C */
    __IO uint32_t APB1ENR;      /**< 0x40 */
    __IO uint32_t APB2ENR;      /**< 0x44 */
} RCC_TypeDef;

#define RCC                 ((RCC_TypeDef *)(AHB1PERIPH_BASE + 0x3800uL))

#define RCC_CR_HSEON        (1uL << 16)
#define RCC_CR_HSERDY       (1uL << 17)
#define RCC_CR_PLLON        (1uL << 24)
#define RCC_CR_PLLRDY       (1uL << 25)
#define RCC_CFGR_SW_PLL     (2uL << 0)
#define RCC_CFGR_SWS_MASK   (3uL << 2)
#define RCC_CFGR_SWS_PLL    (2uL << 2)
#define RCC_CFGR_HPRE_DIV1  (0uL << 4)
#define RCC_CFGR_PPRE1_DIV4 (5uL << 10)
#define RCC_CFGR_PPRE2_DIV2 (4uL << 13)
#define RCC_PLLCFGR_PLLSRC_HSE  (1uL << 22)

/* AHB1ENR 位 */
#define RCC_AHB1ENR_GPIOAEN (1uL << 0)
#define RCC_AHB1ENR_GPIOBEN (1uL << 1)
#define RCC_AHB1ENR_GPIOCEN (1uL << 2)
#define RCC_AHB1ENR_GPIODEN (1uL << 3)
#define RCC_AHB1ENR_GPIOEEN (1uL << 4)
/* APB1ENR 位 */
#define RCC_APB1ENR_TIM2EN  (1uL << 0)
#define RCC_APB1ENR_TIM3EN  (1uL << 1)
#define RCC_APB1ENR_TIM4EN  (1uL << 2)
#define RCC_APB1ENR_TIM5EN  (1uL << 3)
#define RCC_APB1ENR_TIM6EN  (1uL << 4)
#define RCC_APB1ENR_PWREN   (1uL << 28)
/* APB2ENR 位 */
#define RCC_APB2ENR_TIM1EN   (1uL << 0)
#define RCC_APB2ENR_TIM8EN   (1uL << 1)
#define RCC_APB2ENR_USART1EN (1uL << 4)
#define RCC_APB2ENR_ADC1EN   (1uL << 8)

/* ===================== FLASH / PWR ===================== */
typedef struct {
    __IO uint32_t ACR;
    __IO uint32_t KEYR;
    __IO uint32_t OPTKEYR;
    __IO uint32_t SR;
    __IO uint32_t CR;
    __IO uint32_t OPTCR;
} FLASH_TypeDef;

#define FLASH_R             ((FLASH_TypeDef *)(AHB1PERIPH_BASE + 0x3C00uL))
#define FLASH_ACR_LATENCY_5WS   (5uL << 0)
#define FLASH_ACR_PRFTEN    (1uL << 8)
#define FLASH_ACR_ICEN      (1uL << 9)
#define FLASH_ACR_DCEN      (1uL << 10)

typedef struct {
    __IO uint32_t CR;
    __IO uint32_t CSR;
} PWR_TypeDef;

#define PWR                 ((PWR_TypeDef *)(APB1PERIPH_BASE + 0x7000uL))
#define PWR_CR_VOS_SCALE1   (3uL << 14)

/* ===================== Cortex-M4 内核外设 ===================== */
typedef struct {
    __IO uint32_t CTRL;
    __IO uint32_t LOAD;
    __IO uint32_t VAL;
    __IO uint32_t CALIB;
} SysTick_TypeDef;

#define SysTick             ((SysTick_TypeDef *)0xE000E010uL)
#define SysTick_CTRL_ENABLE     (1uL << 0)
#define SysTick_CTRL_TICKINT    (1uL << 1)
#define SysTick_CTRL_CLKSOURCE  (1uL << 2)

/** NVIC 中断使能寄存器组 (ISER0..7) */
#define NVIC_ISER           ((__IO uint32_t *)0xE000E100uL)
/** 协处理器访问控制寄存器 —— 使能 FPU 用 */
#define SCB_CPACR           (*(__IO uint32_t *)0xE000ED88uL)
/** 向量表偏移寄存器 */
#define SCB_VTOR            (*(__IO uint32_t *)0xE000ED08uL)

/** 中断号 (RM0090 表 62)，只列本固件用到的 */
#define IRQn_TIM6_DAC       54
#define IRQn_USART1         37
#define IRQn_TIM8_CC        46

/** 使能一个外设中断 */
static inline void nvic_enable_irq(uint32_t irqn)
{
    NVIC_ISER[irqn >> 5] = (1uL << (irqn & 0x1FuL));
}

/** 关中断 / 开中断。用于保护 ISR 与主循环共享的多字节状态。 */
static inline void irq_disable(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

static inline void irq_enable(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

#endif /* STM32_MECANUM_STM32F407_REGS_H */
