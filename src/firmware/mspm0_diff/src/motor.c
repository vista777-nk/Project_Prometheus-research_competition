/**
 * @file motor.c
 * @brief 占空比 → PWM + 方向 的映射，以及电流采样折算
 *
 * 这一层唯一的职责是把"带符号的占空比"翻译成"PWM 幅值 + H 桥方向"。
 * 看起来平凡，但它是最容易埋雷的地方之一：
 *
 *   · NaN 占空比若直接乘进 PWM 比较寄存器，会写进一个不可预测的值 ——
 *     必须在这里拦下，而不是指望上游永远干净。
 *   · 占空比过零时若不先把方向脚切好再给 PWM，H 桥会短暂直通。
 *     这里的顺序是：先设方向，后设幅值。
 */
#include "motor.h"

#include <math.h>

#include "board_config.h"
#include "mcu_port.h"

static float s_duty[NUM_WHEELS];

void motor_init(void)
{
    port_motor_init(MOTOR_PWM_FREQ_HZ);
    for (int i = 0; i < NUM_WHEELS; i++) {
        s_duty[i] = 0.0f;
    }
    motor_all_stop();
}

void motor_set_duty(int wheel, float duty)
{
    if (wheel < 0 || wheel >= NUM_WHEELS) {
        return;
    }

    /* NaN 比较恒为 false，所以用 isfinite 显式判定，不能靠 clamp 兜住 */
    if (!isfinite(duty)) {
        duty = 0.0f;
    }
    if (duty > 1.0f) {
        duty = 1.0f;
    } else if (duty < -1.0f) {
        duty = -1.0f;
    }

    s_duty[wheel] = duty;

    /* 换向前先撤去有效驱动力：正向落到 00，反向落到 11 慢衰减；
       随后切 IN2，再恢复目标占空比。 */
    port_motor_set_pwm(wheel, 0.0f);
    if (duty > 0.0f) {
        port_motor_set_direction(wheel, PORT_MOTOR_FORWARD);
        port_motor_set_pwm(wheel, duty);
    } else if (duty < 0.0f) {
        port_motor_set_direction(wheel, PORT_MOTOR_REVERSE);
        port_motor_set_pwm(wheel, -duty);
    } else {
        port_motor_set_direction(wheel, PORT_MOTOR_COAST);
        port_motor_set_pwm(wheel, 0.0f);
    }
}

void motor_all_stop(void)
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        s_duty[i] = 0.0f;
        port_motor_set_pwm(i, 0.0f);
        port_motor_set_direction(i, PORT_MOTOR_COAST);
    }
}

void motor_brake_all(void)
{
    for (int i = 0; i < NUM_WHEELS; i++) {
        s_duty[i] = 0.0f;
        port_motor_set_pwm(i, 0.0f);
        port_motor_set_direction(i, PORT_MOTOR_BRAKE);
    }
}

float motor_get_duty(int wheel)
{
    if (wheel < 0 || wheel >= NUM_WHEELS) {
        return 0.0f;
    }
    return s_duty[wheel];
}

void motor_sample_currents(float out[NUM_WHEELS])
{
    if (out == 0) {
        return;
    }
    for (int i = 0; i < NUM_WHEELS; i++) {
        out[i] = NAN;
    }
}
