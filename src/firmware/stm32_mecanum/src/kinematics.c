/**
 * @file kinematics.c
 * @brief 麦轮底盘逆 / 正运动学实现
 */
#include "kinematics.h"

#include <math.h>

#include "board_config.h"

/** rad/s → RPM 的换算系数 60 / (2π) */
#define RADPS_TO_RPM    9.549296585513720f
/** RPM → rad/s */
#define RPM_TO_RADPS    (1.0f / RADPS_TO_RPM)

/* 默认几何参数取自 board_config.h，未显式调用 kinematics_init() 时也能工作，
   避免上电早期误用到全零几何而除零。 */
static MecanumGeometry s_geo = {
    CHASSIS_WHEEL_RADIUS_M,
    CHASSIS_LX_M,
    CHASSIS_LY_M,
    MOTOR_MAX_RPM
};

static int geometry_is_valid(const MecanumGeometry *geo)
{
    if (geo == 0) {
        return 0;
    }
    const float values[4] = { geo->wheel_radius, geo->lx, geo->ly, geo->max_rpm };
    for (int i = 0; i < 4; i++) {
        if (!isfinite(values[i]) || values[i] <= 0.0f) {
            return 0;
        }
    }
    return 1;
}

int kinematics_init(const MecanumGeometry *geo)
{
    if (!geometry_is_valid(geo)) {
        return KIN_INVALID;
    }
    s_geo = *geo;
    return KIN_OK;
}

const MecanumGeometry *kinematics_get_geometry(void)
{
    return &s_geo;
}

int inverse_kinematics(const RobotVelocity *cmd, float rpm[NUM_WHEELS])
{
    if (rpm == 0) {
        return KIN_INVALID;
    }

    /* 任何异常路径都必须先把输出清零：调用方 (控制中断) 在拿到错误码时
       仍可能直接使用 rpm[]，留着上一次的值比停车危险得多。 */
    for (int i = 0; i < NUM_WHEELS; i++) {
        rpm[i] = 0.0f;
    }

    if (cmd == 0 || !geometry_is_valid(&s_geo)) {
        return KIN_INVALID;
    }
    if (!isfinite(cmd->vx) || !isfinite(cmd->vy) || !isfinite(cmd->omega)) {
        return KIN_INVALID;
    }

    const float lever = s_geo.lx + s_geo.ly;
    const float inv_r = 1.0f / s_geo.wheel_radius;
    const float rotation = cmd->omega * lever;

    /* 轮角速度 (rad/s) */
    const float omega_wheel[NUM_WHEELS] = {
        (cmd->vx - cmd->vy - rotation) * inv_r,   /* 0 左前 */
        (cmd->vx + cmd->vy + rotation) * inv_r,   /* 1 右前 */
        (cmd->vx + cmd->vy - rotation) * inv_r,   /* 2 左后 */
        (cmd->vx - cmd->vy + rotation) * inv_r    /* 3 右后 */
    };

    float peak = 0.0f;
    for (int i = 0; i < NUM_WHEELS; i++) {
        rpm[i] = omega_wheel[i] * RADPS_TO_RPM;
        const float magnitude = fabsf(rpm[i]);
        if (magnitude > peak) {
            peak = magnitude;
        }
    }

    if (peak <= s_geo.max_rpm) {
        return KIN_OK;
    }

    /* 等比缩放：只降速，不改变运动方向 */
    const float scale = s_geo.max_rpm / peak;
    for (int i = 0; i < NUM_WHEELS; i++) {
        rpm[i] *= scale;
    }
    return KIN_SATURATED;
}

int forward_kinematics(const float rpm[NUM_WHEELS], RobotVelocity *out)
{
    if (out == 0) {
        return KIN_INVALID;
    }

    out->vx = 0.0f;
    out->vy = 0.0f;
    out->omega = 0.0f;

    if (rpm == 0 || !geometry_is_valid(&s_geo)) {
        return KIN_INVALID;
    }

    float omega_wheel[NUM_WHEELS];
    for (int i = 0; i < NUM_WHEELS; i++) {
        if (!isfinite(rpm[i])) {
            return KIN_INVALID;
        }
        omega_wheel[i] = rpm[i] * RPM_TO_RADPS;
    }

    const float lever = s_geo.lx + s_geo.ly;
    const float r = s_geo.wheel_radius;

    out->vx = r * (omega_wheel[0] + omega_wheel[1] + omega_wheel[2] + omega_wheel[3]) / 4.0f;
    out->vy = r * (-omega_wheel[0] + omega_wheel[1] + omega_wheel[2] - omega_wheel[3]) / 4.0f;
    out->omega = r * (-omega_wheel[0] + omega_wheel[1] - omega_wheel[2] + omega_wheel[3])
               / (4.0f * lever);
    return KIN_OK;
}
