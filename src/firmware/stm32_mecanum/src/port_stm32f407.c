/**
 * @file port_stm32f407.c
 * @brief 移植层的 STM32F407VET6 实现 —— 本固件里唯一碰寄存器的文件
 *
 * 本文件是 `common/mcu_port.h` 在 STM32F407 上的落地。重构之前，这些寄存器操作
 * 散在 bsp.c / encoder.c / motor.c / uart.c / main.c 五个文件里，
 * 与测速窗口、环形缓冲、故障判定等板级逻辑混在一起 —— 后果是那些逻辑
 * **只能上板验证**。收拢之后 encoder.c / motor.c / uart.c 变成纯 C，
 * 可以在 Host 上用假外设直接测。理由与代价见 mcu_port.h 顶部。
 *
 * 外设分配：
 *   TIM1        四路 PWM (CH1..CH4 → PE9/PE11/PE13/PE14, AF1), 20kHz
 *   TIM2/3/4/5  四路正交编码器 (4 倍频)
 *   TIM8        PC6..PC9 四路 HC-SR04 Echo 输入捕获（一次只触发一路）
 *   TIM6        1kHz 控制中断
 *   USART1      PA9/PA10 (AF7), 115200 8N1, RXNE/IDLE/TXE 中断
 *   DRV8871     每轮 TIM1 PWM→IN1、GPIOD→IN2（单 PWM + 方向控制）
 *   SysTick     1ms 时基
 *
 * @warning 时序常量按 8MHz HSE 晶振推导。若核心板换了晶振，必须同步改
 *          stm32f4xx_conf.h 的 HSE_VALUE_HZ 与下面的 PLL 分频系数，
 *          否则串口波特率、PWM 载频、控制周期会集体偏掉。
 *
 * @note 尚未在真实硬件上验证。上板前走 README §七 的检查清单。
 */
#include "board_config.h"
#include "kinematics.h"
#include "mcu_port.h"
#include "stm32f4xx_conf.h"

/* ===================== 一、系统 ===================== */

static volatile uint32_t s_tick_ms;

/** 配置一个 GPIO 引脚。收拢在本文件内，上层不再需要 bsp_gpio_config()。 */
static void gpio_config(GPIO_TypeDef *port, uint32_t pin, uint32_t mode,
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

void port_system_init(void)
{
    system_clock_config();

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN
                  | RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;

    SysTick->LOAD = (SYSCLK_HZ / 1000uL) - 1uL;
    SysTick->VAL = 0uL;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE | SysTick_CTRL_TICKINT | SysTick_CTRL_ENABLE;
}

/** SysTick 中断服务例程，由 startup 的向量表引用 */
void SysTick_Handler(void)
{
    s_tick_ms++;
}

uint32_t port_millis(void)
{
    return s_tick_ms;
}

void port_delay_ms(uint32_t ms)
{
    const uint32_t start = port_millis();
    while ((port_millis() - start) < ms) {
        /* 忙等。无符号减法天然处理回绕。 */
    }
}

void port_irq_disable(void) { irq_disable(); }
void port_irq_enable(void)  { irq_enable(); }

/* ===================== 二、控制定时器 (TIM6, 1kHz) ===================== */

/** TIM6：84MHz APB1 定时器时钟 → /84 = 1MHz → 每 1000 计数产生 1kHz 更新中断 */
void port_control_timer_init(uint32_t freq_hz)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;

    TIM6->CR1 = 0uL;
    TIM6->PSC = (TIMCLK_APB1_HZ / 1000000uL) - 1uL;
    TIM6->ARR = (1000000uL / freq_hz) - 1uL;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0uL;
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->CR1 = TIM_CR1_CEN;

    nvic_enable_irq(IRQn_TIM6_DAC);
}

void TIM6_DAC_IRQHandler(void)
{
    if ((TIM6->SR & TIM_SR_UIF) == 0u) {
        return;
    }
    TIM6->SR = ~TIM_SR_UIF;
    port_control_isr_hook();
}

/* ===================== 三、电机 PWM 与方向 ===================== */

/** TIM1 计数上限：168MHz / 20kHz - 1 */
#define PWM_ARR     ((TIMCLK_APB2_HZ / MOTOR_PWM_FREQ_HZ) - 1uL)

/** 每轮的 DRV8871 IN1 PWM 比较寄存器与 IN2 方向脚 */
typedef struct {
    __IO uint32_t *ccr;
    uint32_t dir_pin;
    float duty_abs;
    PortMotorDirection direction;
} MotorChannel;

static MotorChannel s_motors[NUM_WHEELS];

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

void port_motor_init(uint32_t pwm_freq_hz)
{
    (void)pwm_freq_hz;   /* 载频由 PWM_ARR 在编译期定死，见 board_config.h */

    /* PWM 引脚 PE9/PE11/PE13/PE14 → TIM1_CH1..CH4, AF1 */
    gpio_config(MOTOR_PWM_PORT, MOTOR_PWM_PIN_FL, GPIO_MODE_AF, 1u, GPIO_PULL_NONE);
    gpio_config(MOTOR_PWM_PORT, MOTOR_PWM_PIN_FR, GPIO_MODE_AF, 1u, GPIO_PULL_NONE);
    gpio_config(MOTOR_PWM_PORT, MOTOR_PWM_PIN_RL, GPIO_MODE_AF, 1u, GPIO_PULL_NONE);
    gpio_config(MOTOR_PWM_PORT, MOTOR_PWM_PIN_RR, GPIO_MODE_AF, 1u, GPIO_PULL_NONE);

    /* DRV8871 IN2：PD0/2/4/6 推挽输出 */
    static const uint32_t dir_pins[NUM_WHEELS] = {
        MOTOR_DIR_PIN_FL, MOTOR_DIR_PIN_FR,
        MOTOR_DIR_PIN_RL, MOTOR_DIR_PIN_RR
    };
    for (int i = 0; i < NUM_WHEELS; i++) {
        gpio_config(MOTOR_DIR_PORT, dir_pins[i], GPIO_MODE_OUTPUT, 0u, GPIO_PULL_NONE);
    }

    pwm_timer_config();

    s_motors[WHEEL_FRONT_LEFT].ccr    = &TIM1->CCR1;
    s_motors[WHEEL_FRONT_LEFT].dir_pin = MOTOR_DIR_PIN_FL;
    s_motors[WHEEL_FRONT_RIGHT].ccr   = &TIM1->CCR2;
    s_motors[WHEEL_FRONT_RIGHT].dir_pin = MOTOR_DIR_PIN_FR;
    s_motors[WHEEL_REAR_LEFT].ccr     = &TIM1->CCR3;
    s_motors[WHEEL_REAR_LEFT].dir_pin = MOTOR_DIR_PIN_RL;
    s_motors[WHEEL_REAR_RIGHT].ccr    = &TIM1->CCR4;
    s_motors[WHEEL_REAR_RIGHT].dir_pin = MOTOR_DIR_PIN_RR;

    for (int i = 0; i < NUM_WHEELS; i++) {
        s_motors[i].duty_abs = 0.0f;
        s_motors[i].direction = PORT_MOTOR_COAST;
        port_motor_set_direction(i, PORT_MOTOR_COAST);
    }
}

/**
 * 用单路 PWM + 单路方向 GPIO 驱动 DRV8871：
 *   forward: IN1=PWM,     IN2=0
 *   reverse: IN1=1-PWM,   IN2=1（PWM 低电平期间反转，高电平期间慢衰减）
 *   coast:   IN1=0,       IN2=0
 *   brake:   IN1=1,       IN2=1
 */
static void motor_apply(int wheel)
{
    MotorChannel *motor = &s_motors[wheel];
    float in1_duty = 0.0f;
    bool in2 = false;
    switch (motor->direction) {
    case PORT_MOTOR_FORWARD:
        in1_duty = motor->duty_abs;
        break;
    case PORT_MOTOR_REVERSE:
        in1_duty = 1.0f - motor->duty_abs;
        in2 = true;
        break;
    case PORT_MOTOR_BRAKE:
        in1_duty = 1.0f;
        in2 = true;
        break;
    case PORT_MOTOR_COAST:
    default:
        break;
    }

    *motor->ccr = (uint32_t)(in1_duty * (float)PWM_ARR);
    MOTOR_DIR_PORT->BSRR = in2
        ? (1uL << motor->dir_pin)
        : (1uL << (motor->dir_pin + 16u));
}

void port_motor_set_pwm(int wheel, float duty_abs)
{
    if (wheel < 0 || wheel >= NUM_WHEELS || s_motors[wheel].ccr == 0) {
        return;
    }
    s_motors[wheel].duty_abs = duty_abs;
    motor_apply(wheel);
}

void port_motor_set_direction(int wheel, PortMotorDirection dir)
{
    if (wheel < 0 || wheel >= NUM_WHEELS || s_motors[wheel].ccr == 0) {
        return;
    }

    s_motors[wheel].direction = dir;
    motor_apply(wheel);
}

/* ===================== 四、编码器 (TIM2/3/4/5) ===================== */

/*
 * TIM2/TIM5 是 32 位计数器、TIM3/TIM4 是 16 位。这里统一把 ARR 设成 0xFFFF
 * 当作 16 位用 —— 上层按 uint16 差值处理回绕，因此不依赖具体位宽。
 */
static TIM_TypeDef *s_encoder_tim[NUM_WHEELS];

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

void port_encoder_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM3EN
                  | RCC_APB1ENR_TIM4EN | RCC_APB1ENR_TIM5EN;

    /* 轮 0 (左前) — TIM2_CH1/CH2 = PA15 / PB3, AF1 */
    gpio_config(GPIOA, 15u, GPIO_MODE_AF, 1u, GPIO_PULL_UP);
    gpio_config(GPIOB, 3u,  GPIO_MODE_AF, 1u, GPIO_PULL_UP);
    /* 轮 1 (右前) — TIM3_CH1/CH2 = PA6 / PA7, AF2 */
    gpio_config(GPIOA, 6u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    gpio_config(GPIOA, 7u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    /* 轮 2 (左后) — TIM4_CH1/CH2 = PB6 / PB7, AF2 */
    gpio_config(GPIOB, 6u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    gpio_config(GPIOB, 7u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    /* 轮 3 (右后) — TIM5_CH1/CH2 = PA0 / PA1, AF2 */
    gpio_config(GPIOA, 0u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);
    gpio_config(GPIOA, 1u,  GPIO_MODE_AF, 2u, GPIO_PULL_UP);

    s_encoder_tim[WHEEL_FRONT_LEFT]  = TIM2;
    s_encoder_tim[WHEEL_FRONT_RIGHT] = TIM3;
    s_encoder_tim[WHEEL_REAR_LEFT]   = TIM4;
    s_encoder_tim[WHEEL_REAR_RIGHT]  = TIM5;

    for (int i = 0; i < NUM_WHEELS; i++) {
        encoder_timer_config(s_encoder_tim[i]);
    }
}

uint16_t port_encoder_read_count(int wheel)
{
    if (wheel < 0 || wheel >= NUM_WHEELS || s_encoder_tim[wheel] == 0) {
        return 0u;
    }
    return (uint16_t)s_encoder_tim[wheel]->CNT;
}

/* ===================== 五、HC-SR04 (TIM8 输入捕获) ===================== */

static const uint32_t s_ultrasonic_trig_pins[4] = {
    ULTRASONIC_TRIG_PIN_FRONT, ULTRASONIC_TRIG_PIN_REAR,
    ULTRASONIC_TRIG_PIN_LEFT, ULTRASONIC_TRIG_PIN_RIGHT
};
static __IO uint32_t *const s_ultrasonic_ccr[4] = {
    &TIM8->CCR1, &TIM8->CCR2, &TIM8->CCR3, &TIM8->CCR4
};
static const uint32_t s_ultrasonic_flags[4] = {
    TIM_SR_CC1IF, TIM_SR_CC2IF, TIM_SR_CC3IF, TIM_SR_CC4IF
};
static const uint32_t s_ultrasonic_polarity[4] = {
    TIM_CCER_CC1P, TIM_CCER_CC2P, TIM_CCER_CC3P, TIM_CCER_CC4P
};

static volatile int8_t s_ultrasonic_active = -1;
static volatile bool s_ultrasonic_waiting_fall;
static volatile uint16_t s_ultrasonic_rise_tick;
static volatile uint16_t s_ultrasonic_ranges_mm[4];
static volatile uint8_t s_ultrasonic_completed_mask;
static volatile uint32_t s_ultrasonic_started_ms;
static bool s_ultrasonic_initialized;
static uint8_t s_ultrasonic_next;

static void ultrasonic_set_rising_edge(uint8_t sensor)
{
    TIM8->CCER &= ~s_ultrasonic_polarity[sensor];
}

static void ultrasonic_set_falling_edge(uint8_t sensor)
{
    TIM8->CCER |= s_ultrasonic_polarity[sensor];
}

static void ultrasonic_init(void)
{
    static const uint32_t echo_pins[4] = {
        ULTRASONIC_ECHO_PIN_FRONT, ULTRASONIC_ECHO_PIN_REAR,
        ULTRASONIC_ECHO_PIN_LEFT, ULTRASONIC_ECHO_PIN_RIGHT
    };

    for (int sensor = 0; sensor < 4; sensor++) {
        gpio_config(ULTRASONIC_TRIG_PORT, s_ultrasonic_trig_pins[sensor],
                    GPIO_MODE_OUTPUT, 0u, GPIO_PULL_DOWN);
        ULTRASONIC_TRIG_PORT->BSRR =
            1uL << (s_ultrasonic_trig_pins[sensor] + 16u);
        gpio_config(ULTRASONIC_ECHO_PORT, echo_pins[sensor],
                    GPIO_MODE_AF, 3u, GPIO_PULL_DOWN);
        s_ultrasonic_ranges_mm[sensor] = ULTRASONIC_UNAVAILABLE_MM;
    }

    RCC->APB2ENR |= RCC_APB2ENR_TIM8EN;
    TIM8->CR1 = 0uL;
    TIM8->PSC = (TIMCLK_APB2_HZ / 1000000uL) - 1uL;
    TIM8->ARR = 0xFFFFuL;
    TIM8->CCMR1 = TIM_CCMR1_CC1S_TI1 | TIM_CCMR1_CC2S_TI2;
    TIM8->CCMR2 = TIM_CCMR2_CC3S_TI3 | TIM_CCMR2_CC4S_TI4;
    TIM8->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM8->DIER = TIM_DIER_CC1IE | TIM_DIER_CC2IE | TIM_DIER_CC3IE | TIM_DIER_CC4IE;
    TIM8->EGR = TIM_EGR_UG;
    TIM8->SR = 0uL;
    TIM8->CR1 = TIM_CR1_CEN;
    nvic_enable_irq(IRQn_TIM8_CC);
    s_ultrasonic_initialized = true;
}

/** TIM8 捕获中断：先记上升沿，再切到下降沿并换算脉宽。 */
void TIM8_CC_IRQHandler(void)
{
    const uint32_t pending = TIM8->SR &
        (TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF);
    TIM8->SR = ~pending;

    const int sensor = s_ultrasonic_active;
    if (sensor < 0 || sensor >= 4 ||
        (pending & s_ultrasonic_flags[sensor]) == 0u) {
        return;
    }

    const uint16_t captured = (uint16_t)*s_ultrasonic_ccr[sensor];
    if (!s_ultrasonic_waiting_fall) {
        s_ultrasonic_rise_tick = captured;
        s_ultrasonic_waiting_fall = true;
        ultrasonic_set_falling_edge((uint8_t)sensor);
        return;
    }

    const uint16_t pulse_us = (uint16_t)(captured - s_ultrasonic_rise_tick);
    /* HC-SR04 经验公式 distance_cm = pulse_us / 58。范围外按 unavailable。 */
    uint16_t distance_mm = ULTRASONIC_UNAVAILABLE_MM;
    if (pulse_us >= 116u && pulse_us <= 23200u) {
        distance_mm = (uint16_t)(((uint32_t)pulse_us * 10uL + 29uL) / 58uL);
    }
    s_ultrasonic_ranges_mm[sensor] = distance_mm;
    s_ultrasonic_completed_mask |= (uint8_t)(1uL << sensor);
    ultrasonic_set_rising_edge((uint8_t)sensor);
    s_ultrasonic_waiting_fall = false;
    s_ultrasonic_active = -1;
}

static void ultrasonic_trigger(uint8_t sensor, uint32_t now_ms)
{
    s_ultrasonic_active = (int8_t)sensor;
    s_ultrasonic_waiting_fall = false;
    s_ultrasonic_started_ms = now_ms;
    ultrasonic_set_rising_edge(sensor);
    TIM8->SR = ~s_ultrasonic_flags[sensor];

    ULTRASONIC_TRIG_PORT->BSRR = 1uL << s_ultrasonic_trig_pins[sensor];
    const uint16_t start = (uint16_t)TIM8->CNT;
    while ((uint16_t)((uint16_t)TIM8->CNT - start) < 10u) {
        /* 10us 脉冲只在主循环生成；1kHz 控制 ISR 仍可抢占。 */
    }
    ULTRASONIC_TRIG_PORT->BSRR =
        1uL << (s_ultrasonic_trig_pins[sensor] + 16u);
}

bool port_ultrasonic_snapshot_mm(uint16_t ranges_mm[4])
{
    if (!s_ultrasonic_initialized) {
        ultrasonic_init();
    }

    const uint32_t now_ms = port_millis();
    port_irq_disable();
    if (s_ultrasonic_active >= 0 &&
        (now_ms - s_ultrasonic_started_ms) >= ULTRASONIC_ECHO_TIMEOUT_MS) {
        const uint8_t sensor = (uint8_t)s_ultrasonic_active;
        s_ultrasonic_ranges_mm[sensor] = ULTRASONIC_UNAVAILABLE_MM;
        s_ultrasonic_completed_mask |= (uint8_t)(1uL << sensor);
        ultrasonic_set_rising_edge(sensor);
        s_ultrasonic_waiting_fall = false;
        s_ultrasonic_active = -1;
    }

    bool snapshot_ready = (s_ultrasonic_completed_mask == 0x0Fu);
    if (snapshot_ready) {
        for (int sensor = 0; sensor < 4; sensor++) {
            ranges_mm[sensor] = s_ultrasonic_ranges_mm[sensor];
        }
        s_ultrasonic_completed_mask = 0u;
    }
    port_irq_enable();

    if (s_ultrasonic_active < 0) {
        ultrasonic_trigger(s_ultrasonic_next, now_ms);
        s_ultrasonic_next = (uint8_t)((s_ultrasonic_next + 1u) & 3u);
    }
    return snapshot_ready;
}

/* ===================== 六、串口 (USART1) ===================== */

void port_uart_init(uint32_t baudrate)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9 = TX, PA10 = RX, AF7。RX 上拉，防止线缆未接时悬空乱收。 */
    gpio_config(GPIOA, 9u,  GPIO_MODE_AF, 7u, GPIO_PULL_NONE);
    gpio_config(GPIOA, 10u, GPIO_MODE_AF, 7u, GPIO_PULL_UP);

    USART1->CR1 = 0uL;
    /* BRR 的编码恰好就是 "fPCLK/baud" 的 Q12.4 定点数，四舍五入即可 */
    USART1->BRR = (PCLK2_HZ + (baudrate / 2uL)) / baudrate;
    USART1->CR2 = 0uL;                  /* 1 位停止位 */
    USART1->CR3 = 0uL;                  /* 无硬件流控 */
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE
                | USART_CR1_RXNEIE | USART_CR1_IDLEIE;

    nvic_enable_irq(IRQn_USART1);
}

/**
 * STM32 侧选择中断驱动发送，而不是像 MSPM0 那样就地忙等。
 *
 * 理由：F407 有独立的 TXE 中断且主循环还要跑遥测与状态灯，
 * 让发送在后台进行可以省下满负荷时约 3.5ms 的忙等。
 * 两块板共用同一份 uart.c 环形缓冲逻辑，差异只在这个函数和下面的 ISR 里。
 */
void port_uart_tx_start(void)
{
    USART1->CR1 |= USART_CR1_TXEIE;
}

/** USART1 中断服务例程，覆盖 startup 里的弱定义 */
void USART1_IRQHandler(void)
{
    const uint32_t status = USART1->SR;

    /* 溢出：必须读 SR 再读 DR 才能清标志，否则会一直重入中断 */
    if ((status & USART_SR_ORE) != 0u) {
        (void)USART1->DR;
        port_uart_overrun_hook();
    }

    if ((status & USART_SR_RXNE) != 0u) {
        port_uart_rx_hook((uint8_t)(USART1->DR & 0xFFuL));
    }

    if ((status & USART_SR_IDLE) != 0u) {
        (void)USART1->DR;               /* 读 SR 后读 DR 清 IDLE 标志 */
        port_uart_idle_hook();
    }

    if ((status & USART_SR_TXE) != 0u && (USART1->CR1 & USART_CR1_TXEIE) != 0u) {
        uint8_t byte;
        if (port_uart_tx_next(&byte)) {
            USART1->DR = byte;
        } else {
            USART1->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

/* ===================== 七、ADC（当前 BOM 未使用） ===================== */

void port_adc_init(void)
{
    /* DRV8871 的内部检测只服务 ILIM，没有模拟反馈输出。 */
}

uint16_t port_adc_read(uint8_t channel)
{
    (void)channel;
    return 0u;
}

/* ===================== 八、GPIO (急停 / LED) ===================== */

void port_gpio_init(void)
{
    gpio_config(ESTOP_PORT, ESTOP_PIN, GPIO_MODE_INPUT, 0u, GPIO_PULL_UP);
    gpio_config(LED_PORT, LED_PIN, GPIO_MODE_OUTPUT, 0u, GPIO_PULL_NONE);
    port_led_set(false);
}

bool port_estop_asserted(void)
{
    /* 常闭开关 + 内部上拉：低电平 = 回路完好且未按下；
       按下或断线 → 上拉把引脚拉高 → 触发急停。断线即停，失效安全。 */
    return (ESTOP_PORT->IDR & (1uL << ESTOP_PIN)) != 0u;
}

void port_led_set(bool on)
{
    /* BSRR 低半字置位、高半字复位，单次写入即可，无需读改写 */
    LED_PORT->BSRR = on ? (1uL << LED_PIN) : (1uL << (LED_PIN + 16u));
}

bool port_is_stub(void) { return false; }
