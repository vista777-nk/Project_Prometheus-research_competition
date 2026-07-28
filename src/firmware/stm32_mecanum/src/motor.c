/**
 * @file motor.c
 * @brief 电机 PWM 输出与电流采样实现
 *
 * 功率级假定为 TB6612FNG 双 H 桥 ×2：每轮一路 PWM + 两根方向脚 (IN1/IN2)。
 *   前进 = IN1 高 / IN2 低      后退 = IN1 低 / IN2 高
 *   滑行 = 双低                 刹车 = 双高
 *
 * PWM 由 TIM1 的四个通道产生，载频 20kHz —— 高于人耳上限，电机不啸叫；
 * 又远低于 MOSFET 开关损耗变显著的频段。
 *
 * @note 尚未在真实硬件上验证。换功率级 (如 DRV8833 / BTS7960) 时，
 *       只需改本文件的方向脚真值表，上层不受影响。
 */
#include "motor.h"

#include <math.h>
#include <stdbool.h>

#include "board_config.h"
#include "bsp.h"

/** TIM1 计数上限：168MHz / 20kHz - 1 */
#define PWM_ARR     ((TIMCLK_APB2_HZ / MOTOR_PWM_FREQ_HZ) - 1uL)

/** 每轮的 PWM 比较寄存器与两根方向脚 */
typedef struct {
    __IO uint32_t *ccr;
    uint32_t pin_a;
    uint32_t pin_b;
} MotorChannel;

static MotorChannel s_motors[NUM_WHEELS];
static float s_duty[NUM_WHEELS];

static void pwm_timer_config(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;

    TIM1->CR1 = 0uL;
    TIM1->PSC = 0uL;
    TIM1->ARR = PWM_ARR;

    /* 四个通道全部配成 PWM 模式 1 + 输出比较预装载 */
    TIM1->CCMR1 = TIM_CCMR_PWM1_CH_LOW | TIM_CCMR_PWM1_CH_HIGH;
    TIM1->CCMR2 = TIM_CCMR_PWM1_CH_LOW | TIM_CCMR_PWM1_CH_HIGH;
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;

    TIM1->CCR1 = 0uL;
    TIM1->CCR2 = 0uL;
    TIM1->CCR3 = 0uL;
    TIM1->CCR4 = 0uL;

    /* TIM1 是高级定时器，不置 MOE 输出脚不会动 —— 这是最容易踩的坑 */
    TIM1->BDTR = TIM_BDTR_MOE;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

static void adc_config(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    /* ADC 时钟 = PCLK2 / 4 = 21MHz，低于 36MHz 上限 */
    ADC_CCR = (1uL << 16);

    /* 通道 10~13 采样时间 84 周期：分流电阻 + 运放的输出阻抗不算低，
       采样保持电容需要足够时间充满，采太快读数会偏小。 */
    ADC1->SMPR1 = (4uL << 0) | (4uL << 3) | (4uL << 6) | (4uL << 9);
    ADC1->SQR1 = 0uL;                       /* L = 0，即每次只转换 1 个通道 */
    ADC1->CR1 = 0uL;
    ADC1->CR2 = ADC_CR2_ADON;
}

void motor_init(void)
{
    /* PWM 引脚 PE9/PE11/PE13/PE14 → TIM1_CH1..CH4, AF1 */
    bsp_gpio_config(MOTOR_PWM_PORT, MOTOR_PWM_PIN_FL, GPIO_MODE_AF, 1u, GPIO_PULL_NONE);
    bsp_gpio_config(MOTOR_PWM_PORT, MOTOR_PWM_PIN_FR, GPIO_MODE_AF, 1u, GPIO_PULL_NONE);
    bsp_gpio_config(MOTOR_PWM_PORT, MOTOR_PWM_PIN_RL, GPIO_MODE_AF, 1u, GPIO_PULL_NONE);
    bsp_gpio_config(MOTOR_PWM_PORT, MOTOR_PWM_PIN_RR, GPIO_MODE_AF, 1u, GPIO_PULL_NONE);

    /* 方向脚 PD0..PD7 推挽输出 */
    static const uint32_t dir_pins[NUM_WHEELS * 2] = {
        MOTOR_DIR_PIN_FL_A, MOTOR_DIR_PIN_FL_B,
        MOTOR_DIR_PIN_FR_A, MOTOR_DIR_PIN_FR_B,
        MOTOR_DIR_PIN_RL_A, MOTOR_DIR_PIN_RL_B,
        MOTOR_DIR_PIN_RR_A, MOTOR_DIR_PIN_RR_B
    };
    for (int i = 0; i < NUM_WHEELS * 2; i++) {
        bsp_gpio_config(MOTOR_DIR_PORT, dir_pins[i], GPIO_MODE_OUTPUT, 0u, GPIO_PULL_NONE);
    }

    /* 电流采样脚 PC0..PC3 模拟输入 */
    for (uint32_t pin = 0u; pin < 4u; pin++) {
        bsp_gpio_config(GPIOC, pin, GPIO_MODE_ANALOG, 0u, GPIO_PULL_NONE);
    }

    pwm_timer_config();
    adc_config();

    s_motors[WHEEL_FRONT_LEFT].ccr    = &TIM1->CCR1;
    s_motors[WHEEL_FRONT_LEFT].pin_a  = MOTOR_DIR_PIN_FL_A;
    s_motors[WHEEL_FRONT_LEFT].pin_b  = MOTOR_DIR_PIN_FL_B;
    s_motors[WHEEL_FRONT_RIGHT].ccr   = &TIM1->CCR2;
    s_motors[WHEEL_FRONT_RIGHT].pin_a = MOTOR_DIR_PIN_FR_A;
    s_motors[WHEEL_FRONT_RIGHT].pin_b = MOTOR_DIR_PIN_FR_B;
    s_motors[WHEEL_REAR_LEFT].ccr     = &TIM1->CCR3;
    s_motors[WHEEL_REAR_LEFT].pin_a   = MOTOR_DIR_PIN_RL_A;
    s_motors[WHEEL_REAR_LEFT].pin_b   = MOTOR_DIR_PIN_RL_B;
    s_motors[WHEEL_REAR_RIGHT].ccr    = &TIM1->CCR4;
    s_motors[WHEEL_REAR_RIGHT].pin_a  = MOTOR_DIR_PIN_RR_A;
    s_motors[WHEEL_REAR_RIGHT].pin_b  = MOTOR_DIR_PIN_RR_B;

    motor_all_stop();
}

/** 写方向脚。BSRR 低半字置位、高半字复位，一次写入完成两个动作。 */
static void set_direction(const MotorChannel *motor, bool level_a, bool level_b)
{
    uint32_t bsrr = 0uL;
    bsrr |= level_a ? (1uL << motor->pin_a) : (1uL << (motor->pin_a + 16u));
    bsrr |= level_b ? (1uL << motor->pin_b) : (1uL << (motor->pin_b + 16u));
    MOTOR_DIR_PORT->BSRR = bsrr;
}

void motor_set_duty(int wheel, float duty)
{
    if (wheel < 0 || wheel >= NUM_WHEELS || s_motors[wheel].ccr == 0) {
        return;
    }

    /* NaN 比较永远为假，因此这里用"不是有限值就当 0"，而不是逐个判断 */
    if (!isfinite(duty)) {
        duty = 0.0f;
    }
    if (duty > 1.0f) {
        duty = 1.0f;
    } else if (duty < -1.0f) {
        duty = -1.0f;
    }
    s_duty[wheel] = duty;

    const MotorChannel *motor = &s_motors[wheel];
    if (duty > 0.0f) {
        set_direction(motor, true, false);
    } else if (duty < 0.0f) {
        set_direction(motor, false, true);
        duty = -duty;
    } else {
        set_direction(motor, false, false);
    }

    *motor->ccr = (uint32_t)(duty * (float)PWM_ARR);
}

void motor_all_stop(void)
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        if (s_motors[i].ccr != 0) {
            *s_motors[i].ccr = 0uL;
            set_direction(&s_motors[i], false, false);
        }
        s_duty[i] = 0.0f;
    }
}

void motor_brake_all(void)
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        if (s_motors[i].ccr != 0) {
            *s_motors[i].ccr = 0uL;
            set_direction(&s_motors[i], true, true);
        }
        s_duty[i] = 0.0f;
    }
}

float motor_get_duty(int wheel)
{
    if (wheel < 0 || wheel >= NUM_WHEELS) {
        return 0.0f;
    }
    return s_duty[wheel];
}

/** 单通道 ADC 转换 (轮询)。转换约 15 个 ADC 周期 @21MHz ≈ 5µs。 */
static uint16_t adc_read_channel(uint32_t channel)
{
    ADC1->SQR3 = channel;
    ADC1->SR = 0uL;
    ADC1->CR2 |= ADC_CR2_SWSTART;

    /* 有限次轮询而不是死等：ADC 没配好时不该把整个遥测线程卡死 */
    for (uint32_t guard = 0u; guard < 10000u; guard++) {
        if ((ADC1->SR & ADC_SR_EOC) != 0u) {
            return (uint16_t)(ADC1->DR & 0xFFFFuL);
        }
    }
    return 0u;
}

void motor_sample_currents(float out[NUM_WHEELS])
{
    if (out == 0) {
        return;
    }
    static const uint32_t channels[NUM_WHEELS] = {
        CURRENT_ADC_CHANNEL_FL, CURRENT_ADC_CHANNEL_FR,
        CURRENT_ADC_CHANNEL_RL, CURRENT_ADC_CHANNEL_RR
    };
    for (int i = 0; i < NUM_WHEELS; i++) {
        /* 分流电阻采样得到的是电流幅值，不含方向；方向可由占空比符号推断 */
        out[i] = (float)adc_read_channel(channels[i]) * CURRENT_ADC_SCALE_A_PER_LSB;
    }
}
