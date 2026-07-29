/**
 * @file kinematics.c
 * @brief 差速底盘逆 / 正运动学实现
 *
 * Cortex-M0+ 没有 FPU 也没有硬件除法指令，所有浮点运算都是库调用
 * (__aeabi_fmul / __aeabi_fdiv ...)。因此本文件刻意做了两件事：
 *   1. 预先算好 1/R 与 1/track，热路径上只做乘法，不做除法；
 *   2. 不调用 <math.h> 里除 isfinite 之外的任何函数。
 * 实测代价估算见 README §5.2。
 */
#include "kinematics.h"

#include <math.h>

#include "board_config.h"

/** rad/s → RPM 的换算系数 60 / (2π) */
#define RADPS_TO_RPM    9.549296585513720f
/** RPM → rad/s */
#define RPM_TO_RADPS    (1.0f / RADPS_TO_RPM)

/* 默认几何参数取自 board_config.h，未显式调用 diff_kinematics_init() 时也能工作，
   避免上电早期误用到全零几何而除零。 */
static DiffGeometry s_geo = {
    CHASSIS_WHEEL_RADIUS_M,
    CHASSIS_TRACK_WIDTH_M,
    MOTOR_MAX_RPM
};

/* 预计算的倒数。每次 diff_kinematics_init() 成功后刷新，
   逆解热路径上就不必再做 __aeabi_fdiv。 */
static float s_inv_radius = 1.0f / CHASSIS_WHEEL_RADIUS_M;
static float s_half_track = CHASSIS_TRACK_WIDTH_M * 0.5f;

static int geometry_is_valid(const DiffGeometry *geo)
{
    if (geo == 0) {
        return 0;
    }
    const float values[3] = { geo->wheel_radius, geo->track, geo->max_rpm };
    for (int i = 0; i < 3; i++) {
        if (!isfinite(values[i]) || values[i] <= 0.0f) {
            return 0;
        }
    }
    return 1;
}

int diff_kinematics_init(const DiffGeometry *geo)
{
    if (!geometry_is_valid(geo)) {
        return KIN_INVALID;
    }
    s_geo = *geo;
    s_inv_radius = 1.0f / s_geo.wheel_radius;
    s_half_track = s_geo.track * 0.5f;
    return KIN_OK;
}

const DiffGeometry *diff_kinematics_get_geometry(void)
{
    return &s_geo;
}

int diff_inverse_kinematics(const DiffVelocity *cmd, float rpm[NUM_WHEELS])
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
    if (!isfinite(cmd->v) || !isfinite(cmd->omega)) {
        return KIN_INVALID;
    }

    /* 轮缘线速度 (m/s)。ω>0 = 逆时针 = 左转 ⟹ 右轮更快。 */
    const float differential = cmd->omega * s_half_track;
    const float v_left  = cmd->v - differential;
    const float v_right = cmd->v + differential;

    /* 线速度 → 轮角速度 (rad/s) → RPM */
    rpm[WHEEL_LEFT]  = v_left  * s_inv_radius * RADPS_TO_RPM;
    rpm[WHEEL_RIGHT] = v_right * s_inv_radius * RADPS_TO_RPM;

    float peak = 0.0f;
    for (int i = 0; i < NUM_WHEELS; i++) {
        const float magnitude = (rpm[i] < 0.0f) ? -rpm[i] : rpm[i];
        if (magnitude > peak) {
            peak = magnitude;
        }
    }

    if (peak <= s_geo.max_rpm) {
        return KIN_OK;
    }

    /* 等比缩放：双轮同乘 s ⟹ v'=s·v, ω'=s·ω ⟹ 曲率 ω/v 严格不变。
       车走同一条弧线，只是慢了。详见 kinematics.h 的说明。 */
    const float scale = s_geo.max_rpm / peak;
    for (int i = 0; i < NUM_WHEELS; i++) {
        rpm[i] *= scale;
    }
    return KIN_SATURATED;
}

int diff_forward_kinematics(const float rpm[NUM_WHEELS], DiffVelocity *out)
{
    if (out == 0) {
        return KIN_INVALID;
    }

    out->v = 0.0f;
    out->omega = 0.0f;

    if (rpm == 0 || !geometry_is_valid(&s_geo)) {
        return KIN_INVALID;
    }
    for (int i = 0; i < NUM_WHEELS; i++) {
        if (!isfinite(rpm[i])) {
            return KIN_INVALID;
        }
    }

    const float r = s_geo.wheel_radius;
    const float v_left  = rpm[WHEEL_LEFT]  * RPM_TO_RADPS * r;
    const float v_right = rpm[WHEEL_RIGHT] * RPM_TO_RADPS * r;

    out->v = (v_left + v_right) * 0.5f;
    /* 这里的除法只在里程计路径 (20Hz) 上执行，不在 1kHz 控制中断里，
       所以直接用除法换取可读性。 */
    out->omega = (v_right - v_left) / s_geo.track;
    return KIN_OK;
}
