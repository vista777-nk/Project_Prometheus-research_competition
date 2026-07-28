/**
 * @file encoder.c
 * @brief AB 相编码器读取实现
 *
 * 四个轮子各占一个定时器的编码器模式：
 *   轮 0 (左前) TIM2 : PA15 / PB3  AF1
 *   轮 1 (右前) TIM3 : PA6  / PA7  AF2
 *   轮 2 (左后) TIM4 : PB6  / PB7  AF2
 *   轮 3 (右后) TIM5 : PA0  / PA1  AF2
 *
 * TIM2/TIM5 是 32 位计数器、TIM3/TIM4 是 16 位。这里统一把 ARR 设成 0xFFFF
 * 当作 16 位用，两次采样之间的差值用 int16_t 强制回绕 —— 只要采样间隔内
 * 计数变化不超过 ±32767 就永远正确。最坏情况 330RPM × 1320 计数/转 ÷ 60
 * ≈ 7260 计数/秒，10ms 窗口才 73 个计数，余量充足。
 *
 * @note 尚未在真实硬件上验证。上板后需先确认每个轮子的旋转方向符号
 *       (board_config.h 的 ENCODER_DIR_SIGN_*)，见 README §上板检查清单。
 */
#include "encoder.h"

#include "board_config.h"
#include "bsp.h"

/** 每个轮子的编码器定时器与方向符号 */
typedef struct {
    TIM_TypeDef *tim;
    int32_t sign;
} EncoderChannel;

static EncoderChannel s_channels[NUM_WHEELS];

static uint16_t s_last_count[NUM_WHEELS];
static int32_t  s_window_delta[NUM_WHEELS];
static int32_t  s_total_counts[NUM_WHEELS];
static float    s_rpm[NUM_WHEELS];
static uint32_t s_window_ticks;

/** 一个测速窗口对应的秒数 */
#define WINDOW_SECONDS  ((float)ENCODER_SPEED_WINDOW_TICKS / (float)CONTROL_FREQ_HZ)

/** 把一路定时器配置成编码器模式 3 (TI1+TI2 双边沿 = 4 倍频) */
static void encoder_timer_config(TIM_TypeDef *tim)
{
    tim->CR1 = 0uL;
    tim->PSC = 0uL;
    tim->ARR = 0xFFFFuL;

    /* CC1 映射到 TI1、CC2 映射到 TI2，均为输入捕获 */
    tim->CCMR1 = TIM_CCMR1_CC1S_TI1 | TIM_CCMR1_CC2S_TI2;
    /* 不反相；若某轮方向反了，优先改 board_config.h 的符号常量而不是这里 */
    tim->CCER = 0uL;
    tim->SMCR = TIM_SMCR_SMS_ENCODER3;

    tim->CNT = 0uL;
    tim->EGR = TIM_EGR_UG;      /* 让 PSC/ARR 立即生效 */
    tim->CR1 = TIM_CR1_CEN;
}

void encoder_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM3EN
                  | RCC_APB1ENR_TIM4EN | RCC_APB1ENR_TIM5EN;

    /* 轮 0 (左前) — TIM2_CH1/CH2 = PA15 / PB3, AF1 */
    bsp_gpio_config(GPIOA, 15u, GPIO_MODE_AF, 1u, GPIO_PULL_UP);
    bsp_gpio_config(GPIOB, 3u,  GPIO_MODE_AF, 1u, GPIO_PULL_UP);
    /* 轮 1 (右前) — TIM3_CH1/CH2 = PA6 / PA7, AF2 */
    bsp_gpio_config(GPIOA, 6u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    bsp_gpio_config(GPIOA, 7u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    /* 轮 2 (左后) — TIM4_CH1/CH2 = PB6 / PB7, AF2 */
    bsp_gpio_config(GPIOB, 6u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    bsp_gpio_config(GPIOB, 7u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    /* 轮 3 (右后) — TIM5_CH1/CH2 = PA0 / PA1, AF2 */
    bsp_gpio_config(GPIOA, 0u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    bsp_gpio_config(GPIOA, 1u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);

    s_channels[WHEEL_FRONT_LEFT].tim   = TIM2;
    s_channels[WHEEL_FRONT_LEFT].sign  = ENCODER_DIR_SIGN_FL;
    s_channels[WHEEL_FRONT_RIGHT].tim  = TIM3;
    s_channels[WHEEL_FRONT_RIGHT].sign = ENCODER_DIR_SIGN_FR;
    s_channels[WHEEL_REAR_LEFT].tim    = TIM4;
    s_channels[WHEEL_REAR_LEFT].sign   = ENCODER_DIR_SIGN_RL;
    s_channels[WHEEL_REAR_RIGHT].tim   = TIM5;
    s_channels[WHEEL_REAR_RIGHT].sign  = ENCODER_DIR_SIGN_RR;

    for (int i = 0; i < NUM_WHEELS; i++) {
        encoder_timer_config(s_channels[i].tim);
    }
    encoder_reset();
}

void encoder_reset(void)
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        s_last_count[i] = (s_channels[i].tim != 0)
                        ? (uint16_t)s_channels[i].tim->CNT
                        : 0u;
        s_window_delta[i] = 0;
        s_total_counts[i] = 0;
        s_rpm[i] = 0.0f;
    }
    s_window_ticks = 0u;
}

void encoder_update(void)
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        if (s_channels[i].tim == 0) {
            continue;
        }
        const uint16_t now = (uint16_t)s_channels[i].tim->CNT;
        /* 用 int16_t 承接差值，硬件计数溢出/下溢会自然折回正确的增量 */
        const int32_t delta = (int32_t)(int16_t)(now - s_last_count[i])
                            * s_channels[i].sign;
        s_last_count[i] = now;
        s_window_delta[i] += delta;
        s_total_counts[i] += delta;
    }

    s_window_ticks++;
    if (s_window_ticks < ENCODER_SPEED_WINDOW_TICKS) {
        return;
    }
    s_window_ticks = 0u;

    for (int i = 0; i < NUM_WHEELS; i++) {
        const float revolutions = (float)s_window_delta[i] / ENCODER_COUNTS_PER_REV;
        const float measured = revolutions / WINDOW_SECONDS * 60.0f;
        /* 一阶低通：量化噪声 ±4.5 RPM 直接进 PID 会让占空比抖动 */
        s_rpm[i] += ENCODER_RPM_FILTER_ALPHA * (measured - s_rpm[i]);
        s_window_delta[i] = 0;
    }
}

float encoder_get_rpm(int wheel)
{
    if (wheel < 0 || wheel >= NUM_WHEELS) {
        return 0.0f;
    }
    return s_rpm[wheel];
}

int32_t encoder_get_total_counts(int wheel)
{
    if (wheel < 0 || wheel >= NUM_WHEELS) {
        return 0;
    }
    return s_total_counts[wheel];
}
