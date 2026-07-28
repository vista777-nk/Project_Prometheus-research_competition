/**
 * @file bsp.c
 * @brief 板级支持包实现 (裸机寄存器操作)
 *
 * @warning 本文件的时序常量按 8MHz HSE 晶振推导。若核心板换了晶振，
 *          必须同步改 stm32f4xx_conf.h 的 HSE_VALUE_HZ 与下面的 PLL 分频系数，
 *          否则串口波特率、PWM 载频、编码器采样周期会集体偏掉。
 *
 * @note 尚未在真实硬件上验证 (Phase 1 目标是可编译 + Host 可测)。
 *       上板前请先用 test_pin_toggle 验证时钟树，见 README §上板检查清单。
 */
#include "bsp.h"

#include "board_config.h"

static volatile uint32_t s_tick_ms;

/* --- 时钟树 --- */

/**
 * 168MHz：HSE 8MHz → /M=8 → 1MHz → ×N=336 → 336MHz → /P=2 → 168MHz
 * AHB = 168MHz, APB1 = 42MHz (上限 42), APB2 = 84MHz (上限 84)
 */
static void system_clock_config(void)
{
    /* Flash 预取 + 指令/数据缓存 + 5 个等待周期 (168MHz @ 2.7~3.6V) */
    FLASH_R->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN
                 | FLASH_ACR_LATENCY_5WS;

    /* 电压调节器切到 Scale 1，否则跑不到 168MHz */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS_SCALE1;

    /* 起 HSE。真实硬件上晶振起振约需几百微秒；这里不设超时——
       起不来就说明板子有硬件问题，与其带着错误时钟跑飞，不如停在这里等看门狗。 */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0u) {
        /* 等待 HSE 就绪 */
    }

    /* 总线分频必须在切到 PLL 之前配好，否则切换瞬间 APB 会超频 */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    /* PLLM=8, PLLN=336, PLLP=2 (编码值 0), PLLQ=7 (USB 48MHz, 本项目未用) */
    RCC->PLLCFGR = 8uL
                 | (336uL << 6)
                 | (0uL << 16)
                 | RCC_PLLCFGR_PLLSRC_HSE
                 | (7uL << 24);

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) {
        /* 等待 PLL 锁定 */
    }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) {
        /* 等待系统时钟切换完成 */
    }
}

static void systick_config(void)
{
    SysTick->LOAD = (SYSCLK_HZ / 1000uL) - 1uL;
    SysTick->VAL = 0uL;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE | SysTick_CTRL_TICKINT | SysTick_CTRL_ENABLE;
}

/** SysTick 中断服务例程，由 startup 的向量表引用 */
void SysTick_Handler(void)
{
    s_tick_ms++;
}

uint32_t bsp_get_tick_ms(void)
{
    return s_tick_ms;
}

void bsp_delay_ms(uint32_t ms)
{
    const uint32_t start = s_tick_ms;
    while ((s_tick_ms - start) < ms) {
        /* 忙等。无符号减法天然处理回绕。 */
    }
}

/* --- GPIO --- */

void bsp_gpio_config(GPIO_TypeDef *port, uint32_t pin, uint32_t mode,
                     uint32_t af, uint32_t pull)
{
    const uint32_t pin2 = pin * 2uL;

    port->MODER = (port->MODER & ~(3uL << pin2)) | (mode << pin2);
    port->PUPDR = (port->PUPDR & ~(3uL << pin2)) | (pull << pin2);
    /* 输出速度一律拉满：PWM 20kHz 与 USART 115200 都不希望边沿被压慢 */
    port->OSPEEDR |= (3uL << pin2);
    port->OTYPER &= ~(1uL << pin);   /* 推挽 */

    if (mode == GPIO_MODE_AF) {
        const uint32_t index = pin >> 3;             /* 0..7 → AFR[0], 8..15 → AFR[1] */
        const uint32_t shift = (pin & 7uL) * 4uL;
        port->AFR[index] = (port->AFR[index] & ~(0xFuL << shift)) | (af << shift);
    }
}

static void gpio_clocks_enable(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN
                  | RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
}

/* --- 急停与 LED --- */

bool bsp_estop_asserted(void)
{
    /* 常闭开关 + 内部上拉：低电平 = 回路完好且未按下 */
    return (ESTOP_PORT->IDR & (1uL << ESTOP_PIN)) != 0u;
}

void bsp_led_set(bool on)
{
    /* BSRR 低半字置位、高半字复位，单次写入即可，无需读改写 */
    LED_PORT->BSRR = on ? (1uL << LED_PIN) : (1uL << (LED_PIN + 16u));
}

void bsp_led_toggle(void)
{
    LED_PORT->ODR ^= (1uL << LED_PIN);
}

/* --- 入口 --- */

void bsp_init(void)
{
    system_clock_config();
    gpio_clocks_enable();

    bsp_gpio_config(ESTOP_PORT, ESTOP_PIN, GPIO_MODE_INPUT, 0u, GPIO_PULL_UP);
    bsp_gpio_config(LED_PORT, LED_PIN, GPIO_MODE_OUTPUT, 0u, GPIO_PULL_NONE);
    bsp_led_set(false);

    systick_config();
}
