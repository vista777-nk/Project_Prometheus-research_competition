/**
 * @file pid.c
 * @brief 通用 PID 速度环实现
 */
#include "pid.h"

static float clampf(float value, float limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static float absf(float value)
{
    return (value < 0.0f) ? -value : value;
}

void pid_init(PIDController *pid, float kp, float ki, float kd,
              float integral_limit, float output_limit)
{
    if (pid == 0) {
        return;
    }
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->integral_limit = absf(integral_limit);
    pid->output_limit = absf(output_limit);
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->has_prev = false;
}

void pid_set_gains(PIDController *pid, float kp, float ki, float kd)
{
    if (pid == 0) {
        return;
    }
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void pid_reset(PIDController *pid)
{
    if (pid == 0) {
        return;
    }
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->has_prev = false;
}

float pid_update(PIDController *pid, float setpoint, float measured, float dt)
{
    if (pid == 0 || dt <= 0.0f) {
        return 0.0f;
    }

    const float error = setpoint - measured;

    /* 微分项：首个周期没有历史误差，跳过以避免微分冲击 */
    float derivative = 0.0f;
    if (pid->has_prev) {
        derivative = (error - pid->prev_error) / dt;
    }
    pid->prev_error = error;
    pid->has_prev = true;

    /* 先算不含本周期积分增量的输出，用于判断是否已经饱和 */
    const float unsaturated = pid->kp * error
                            + pid->ki * pid->integral
                            + pid->kd * derivative;

    /* 条件积分：输出已顶到限幅、且误差继续推向同一方向时不再累加，
       否则积分器会在饱和期间充满，退饱和时产生大幅超调。 */
    const bool pushing_up = (unsaturated >= pid->output_limit) && (error > 0.0f);
    const bool pushing_down = (unsaturated <= -pid->output_limit) && (error < 0.0f);
    if (!pushing_up && !pushing_down) {
        pid->integral += error * dt;
        pid->integral = clampf(pid->integral, pid->integral_limit);
    }

    const float output = pid->kp * error
                       + pid->ki * pid->integral
                       + pid->kd * derivative;

    return clampf(output, pid->output_limit);
}
