/**
 * @file kinematics.h
 * @brief 差速底盘运动学解算 (纯 C99，无硬件依赖，可在 Host 上单元测试)
 *
 * 与麦轮固件 (task-10 stm32_mecanum/kinematics.h) 是**平行实现**而非共享代码：
 * 帧协议、PID、CRC 可以共享，运动学不能 —— 两种底盘的自由度根本不同
 * (麦轮 3 DoF: vx/vy/ω；差速 2 DoF: v/ω，无法横移)。
 * 强行抽象成一个"通用运动学"只会产出一个到处是 if 的四不像。
 * 参见 task-11 §与 task-10 的代码复用。
 *
 * 算法与仿真侧 ros_control 的 diff_drive_controller 一致，几何参数来源：
 *   src/air_ground_car_bringup/config/chassis_params.yaml : diff_chassis
 *
 * 轮序号约定 (俯视图，车头朝上)：
 *
 *          车头 (+x)
 *      [0]────────[1]        0 = 左轮 LEFT   1 = 右轮 RIGHT
 *       │          │
 *       └──────────┘
 *
 * 坐标系：右手系，x=前，ω=逆时针为正 (与 ROS REP-103 一致)。
 * 注意 ω>0 是**逆时针 / 左转**，此时右轮快于左轮。
 *
 * 逆运动学 (track = 轮间距)：
 *   v_left  = v - ω · track/2
 *   v_right = v + ω · track/2
 *   RPM     = v_wheel / R · 60/(2π)
 *
 * 正运动学：
 *   v = (v_left + v_right) / 2
 *   ω = (v_right - v_left) / track
 */
#ifndef MSPM0_DIFF_KINEMATICS_H
#define MSPM0_DIFF_KINEMATICS_H

#ifdef __cplusplus
extern "C" {
#endif

/** 驱动轮数量 */
#define NUM_WHEELS      2

/** 轮索引 */
enum {
    WHEEL_LEFT  = 0,
    WHEEL_RIGHT = 1
};

/** 运动学函数返回码 (取值与麦轮固件一致，便于上位机统一处理) */
typedef enum {
    KIN_OK        = 0,   /**< 解算成功，双轮均未超限 */
    KIN_SATURATED = -1,  /**< 有轮超过 max_rpm，双轮已按同一比例缩放 */
    KIN_INVALID   = -2   /**< 入参非法 (NULL / 非有限值 / 几何参数非正)，输出已置零 */
} KinematicsStatus;

/** 差速底盘几何参数 */
typedef struct {
    float wheel_radius;   /**< 轮半径 R (m) */
    float track;          /**< 轮间距 (m)，两驱动轮接地点中心距 */
    float max_rpm;        /**< 单轮最高转速 (RPM) */
} DiffGeometry;

/** 机器人速度指令 (车体系，右手坐标系，x=前，ω=逆时针正) */
typedef struct {
    float v;       /**< 线速度 (m/s)，正=前进 */
    float omega;   /**< 角速度 (rad/s)，正=逆时针/左转 */
} DiffVelocity;

/**
 * 初始化运动学参数。
 *
 * @param geo 几何参数，各项必须为有限正数
 * @return KIN_OK 或 KIN_INVALID (参数非法时保留上一次的有效配置)
 */
int diff_kinematics_init(const DiffGeometry *geo);

/** 读取当前生效的几何参数 (只读)，恒不为 NULL */
const DiffGeometry *diff_kinematics_get_geometry(void);

/**
 * 逆运动学：机器人速度 → 双轮目标转速。
 *
 * 超限处理采用**等比缩放**而非逐轮硬钳位。对差速底盘而言这不只是"方向更准"，
 * 而是一条可以精确证明的性质：
 *
 *   双轮同乘系数 s  ⟺  v' = s·v 且 ω' = s·ω  ⟹  曲率 κ = ω/v 严格不变。
 *
 * 也就是说超速时车走的是**同一条弧线，只是慢了**。逐轮硬钳位会改变
 * v_right/v_left 的比值，直接把弧线掰弯 —— 上位机以为在走 R=0.5m 的圆，
 * 实车走的是另一个半径，路径跟踪必然发散。代价见 ADR-0004 §决策-2。
 *
 * @param cmd 输入速度指令
 * @param rpm 输出双轮目标转速 (RPM)，正=该轮正转 (推动车体前进)
 * @return KIN_OK / KIN_SATURATED / KIN_INVALID
 */
int diff_inverse_kinematics(const DiffVelocity *cmd, float rpm[NUM_WHEELS]);

/**
 * 正运动学：双轮实测转速 → 机器人速度 (用于里程计与闭环校验)。
 *
 * @param rpm 双轮实测转速 (RPM)
 * @param out 输出车体速度
 * @return KIN_OK / KIN_INVALID
 */
int diff_forward_kinematics(const float rpm[NUM_WHEELS], DiffVelocity *out);

#ifdef __cplusplus
}
#endif

#endif /* MSPM0_DIFF_KINEMATICS_H */
