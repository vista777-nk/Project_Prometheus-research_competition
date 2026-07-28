/**
 * @file kinematics.h
 * @brief 麦轮底盘运动学解算 (纯 C99，无硬件依赖，可在 Host 上单元测试)
 *
 * 算法与仿真侧 src/air_ground_car_bringup/scripts/mecanum_controller.py
 * 的 inverse_kinematics() / forward_kinematics() 逐项对应。两处公式必须保持一致，
 * 否则"仿真调好的策略搬到实车上会跑偏"。
 *
 * 轮序号约定 (俯视图，车头朝上)：
 *
 *          车头 (+x)
 *      [0]────────[1]        0 = 左前 FL   1 = 右前 FR
 *       │          │         2 = 左后 RL   3 = 右后 RR
 *      [2]────────[3]
 *
 * 坐标系：右手系，x=前，y=左，ω=逆时针为正 (与 ROS REP-103 一致)。
 *
 * 逆运动学 (lever = Lx + Ly)：
 *   ω0 = (vx - vy - ω·lever) / R
 *   ω1 = (vx + vy + ω·lever) / R
 *   ω2 = (vx + vy - ω·lever) / R
 *   ω3 = (vx - vy + ω·lever) / R
 */
#ifndef STM32_MECANUM_KINEMATICS_H
#define STM32_MECANUM_KINEMATICS_H

#ifdef __cplusplus
extern "C" {
#endif

/** 麦轮数量 */
#define NUM_WHEELS      4

/** 轮索引，与仿真侧 WHEEL_NAMES 顺序一致 */
enum {
    WHEEL_FRONT_LEFT  = 0,
    WHEEL_FRONT_RIGHT = 1,
    WHEEL_REAR_LEFT   = 2,
    WHEEL_REAR_RIGHT  = 3
};

/** inverse_kinematics() / kinematics_init() 返回码 */
typedef enum {
    KIN_OK        = 0,   /**< 解算成功，四轮均未超限 */
    KIN_SATURATED = -1,  /**< 有轮超过 max_rpm，四轮已按同一比例缩放 */
    KIN_INVALID   = -2   /**< 入参非法 (NULL / 非有限值 / 几何参数非正)，输出已置零 */
} KinematicsStatus;

/** 麦轮底盘几何参数 */
typedef struct {
    float wheel_radius;   /**< 轮半径 R (m) */
    float lx;             /**< 轮距半长，前后方向 (m) */
    float ly;             /**< 轮距半宽，左右方向 (m) */
    float max_rpm;        /**< 单轮最高转速 (RPM) */
} MecanumGeometry;

/** 机器人速度指令 (车体系，右手坐标系，x=前，y=左，ω=逆时针正) */
typedef struct {
    float vx;      /**< m/s */
    float vy;      /**< m/s */
    float omega;   /**< rad/s */
} RobotVelocity;

/**
 * 初始化运动学参数。
 *
 * @param geo 几何参数，各项必须为有限正数
 * @return KIN_OK 或 KIN_INVALID (参数非法时保留上一次的有效配置)
 */
int kinematics_init(const MecanumGeometry *geo);

/** 读取当前生效的几何参数 (只读)。未初始化时返回全零结构的指针。 */
const MecanumGeometry *kinematics_get_geometry(void);

/**
 * 逆运动学：机器人速度 → 四轮目标转速。
 *
 * 超限处理采用**等比缩放**而非逐轮硬钳位：任一轮超过 max_rpm 时，
 * 四轮同乘一个缩放系数。硬钳位会改变各轮转速的比例关系，
 * 使实际运动方向偏离指令方向；等比缩放只降速不改方向。
 * 该策略与仿真侧 mecanum_controller.py 完全一致。
 *
 * @param cmd 输入速度指令
 * @param rpm 输出四轮目标转速 (RPM)，正=该轮正转 (推动车体前进)
 * @return KIN_OK / KIN_SATURATED / KIN_INVALID
 */
int inverse_kinematics(const RobotVelocity *cmd, float rpm[NUM_WHEELS]);

/**
 * 正运动学：四轮实测转速 → 机器人速度 (用于里程计与闭环校验)。
 *
 * @param rpm 四轮实测转速 (RPM)
 * @param out 输出车体速度
 * @return KIN_OK / KIN_INVALID
 */
int forward_kinematics(const float rpm[NUM_WHEELS], RobotVelocity *out);

#ifdef __cplusplus
}
#endif

#endif /* STM32_MECANUM_KINEMATICS_H */
