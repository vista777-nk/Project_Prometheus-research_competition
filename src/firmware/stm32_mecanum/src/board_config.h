/**
 * @file board_config.h
 * @brief STM32F407VET6 麦轮底盘板级配置 —— 引脚、几何、编码器、PID、时序
 *
 * 本文件是固件里唯一的"魔数集散地"。所有可整定量集中在此，
 * 换车架 / 换电机 / 换接线只改这里，不动算法代码。
 *
 * 几何参数与仿真侧保持一致，来源：
 *   src/air_ground_car_bringup/config/chassis_params.yaml : mecanum_chassis
 *   src/air_ground_car_bringup/urdf/mecanum_chassis.urdf.xacro
 * 二者不一致会导致"仿真能跑、实车跑偏"，改动时务必同步。
 */
#ifndef STM32_MECANUM_BOARD_CONFIG_H
#define STM32_MECANUM_BOARD_CONFIG_H

/* ===================== 一、底盘几何 ===================== */

/** 轮半径 (m) —— chassis_params.yaml: mecanum_chassis.wheel_radius */
#define CHASSIS_WHEEL_RADIUS_M      0.033f
/** 前后轴距 (m) —— chassis_params.yaml: mecanum_chassis.wheel_base */
#define CHASSIS_WHEEL_BASE_M        0.20f
/** 左右轮距 (m) —— chassis_params.yaml: mecanum_chassis.track_width */
#define CHASSIS_TRACK_WIDTH_M       0.18f
/** 轮距半长 Lx (前后方向) */
#define CHASSIS_LX_M                (CHASSIS_WHEEL_BASE_M * 0.5f)
/** 轮距半宽 Ly (左右方向) */
#define CHASSIS_LY_M                (CHASSIS_TRACK_WIDTH_M * 0.5f)

/** 电机空载最高转速 (RPM)，520 减速电机 @12V 实测值，整定后修改 */
#define MOTOR_MAX_RPM               330.0f

/* ===================== 二、编码器 ===================== */

/** 编码器每转脉冲数 (电机轴侧) —— chassis_params.yaml: motor_encoder_ppr */
#define ENCODER_PPR                 11.0f
/** 减速比 (电机轴 : 输出轴)，520 电机常见 1:30 */
#define ENCODER_GEAR_RATIO          30.0f
/** 定时器编码器模式的倍频系数 (AB 双相双边沿 = 4×) */
#define ENCODER_QUADRATURE          4.0f
/** 输出轴每转计数 = PPR × 倍频 × 减速比 = 1320 */
#define ENCODER_COUNTS_PER_REV      (ENCODER_PPR * ENCODER_QUADRATURE * ENCODER_GEAR_RATIO)

/* 转速测量窗口。
 * 单纯按 1ms 采样算转速，分辨率只有 60000/(1320×1) ≈ 45 RPM/计数 —— 这个台阶
 * 比整个调速范围的 1/8 还大，PID 会被量化噪声牵着走。改成 10ms 窗口后
 * 分辨率降到约 4.5 RPM，代价是速度反馈延迟 10ms (相对 80ms 的电机时间常数可接受)。
 * 速度环仍然跑满 1kHz，只是每 10 次才拿到一个新的测量值。
 * Phase 2 若要更高精度，应改用 M/T 法 (同时测计数与相邻边沿间隔)。 */
#define ENCODER_SPEED_WINDOW_TICKS  10u
/** 转速一阶低通系数 (EMA)，0=完全不更新，1=不滤波 */
#define ENCODER_RPM_FILTER_ALPHA    0.4f

/* ===================== 三、控制时序 ===================== */

/** 速度环控制频率 (Hz)，由 TIM6 中断驱动 */
#define CONTROL_FREQ_HZ             1000u
/** 速度环周期 (s) */
#define CONTROL_DT_S                (1.0f / (float)CONTROL_FREQ_HZ)
/** 遥测上报周期 (ms) = 20Hz */
#define TELEMETRY_PERIOD_MS         50u
/** 指令看门狗：超过该时间未收到 SET_VELOCITY 即刹停 (ms)
 *  与仿真侧 mecanum_controller.py 的 ~command_timeout=0.5s 对齐 */
#define CMD_TIMEOUT_MS              500u

/* ===================== 四、PID 整定参数 ===================== */

/* 速度环 PID：输入 RPM 误差，输出 PWM 占空比 [-1, 1]。
   kp 的量纲是 (占空比 / RPM)：满量程 330 RPM 对应满占空比 1.0，
   因此静态前馈级别的增益约为 1/330 ≈ 0.003。 */
#define PID_KP_DEFAULT              0.0035f
#define PID_KI_DEFAULT              0.030f
#define PID_KD_DEFAULT              0.00004f
/** 积分限幅 (RPM·s)。整定准则：ki × integral_limit ≥ output_limit，
 *  否则积分器无法独立顶到满占空比 —— 表现为"带载后转速永远差一截"。
 *  0.030 × 40 = 1.2，留 20% 余量覆盖电池掉压与负载增大。 */
#define PID_INTEGRAL_LIMIT          40.0f
/** 输出限幅 = 满占空比 */
#define PID_OUTPUT_LIMIT            1.0f

/* ===================== 五、故障阈值 ===================== */

/** 单电机过流阈值 (A)，超过即全部停机 */
#define FAULT_CURRENT_LIMIT_A       2.5f
/** 堵转判据：目标 RPM 高于该值但实测 RPM 低于 STALL_RPM_FLOOR 持续 STALL_TIME_MS */
#define FAULT_STALL_TARGET_RPM      30.0f
#define FAULT_STALL_RPM_FLOOR       3.0f
#define FAULT_STALL_TIME_MS         800u

/* ===================== 六、引脚映射 =====================
 *
 * 详细接线表见 README.md §引脚定义。此处仅给出固件依赖的编号常量。
 * 轮序号约定 (俯视图，车头朝上)：
 *      [0] 左前    [1] 右前
 *      [2] 左后    [3] 右后
 */

/** 电机 PWM：TIM1_CH1..CH4，AF1 */
#define MOTOR_PWM_PORT              GPIOE
#define MOTOR_PWM_PIN_FL            9u    /* PE9  — TIM1_CH1 */
#define MOTOR_PWM_PIN_FR            11u   /* PE11 — TIM1_CH2 */
#define MOTOR_PWM_PIN_RL            13u   /* PE13 — TIM1_CH3 */
#define MOTOR_PWM_PIN_RR            14u   /* PE14 — TIM1_CH4 */
/** PWM 载频 (Hz)，20kHz 避开可听频段 */
#define MOTOR_PWM_FREQ_HZ           20000u

/** 方向控制：TB6612 双路 IN，每轮 2 根，全部在 GPIOD */
#define MOTOR_DIR_PORT              GPIOD
#define MOTOR_DIR_PIN_FL_A          0u    /* PD0 */
#define MOTOR_DIR_PIN_FL_B          1u    /* PD1 */
#define MOTOR_DIR_PIN_FR_A          2u    /* PD2 */
#define MOTOR_DIR_PIN_FR_B          3u    /* PD3 */
#define MOTOR_DIR_PIN_RL_A          4u    /* PD4 */
#define MOTOR_DIR_PIN_RL_B          5u    /* PD5 */
#define MOTOR_DIR_PIN_RR_A          6u    /* PD6 */
#define MOTOR_DIR_PIN_RR_B          7u    /* PD7 */

/* 编码器定时器：TIM2/3/4/5 编码器模式 (对应轮 0/1/2/3)
 *   TIM2 : PA15 / PB3  (AF1)
 *   TIM3 : PA6  / PA7  (AF2)
 *   TIM4 : PB6  / PB7  (AF2)
 *   TIM5 : PA0  / PA1  (AF2)
 * 若某轮方向与车体约定相反，翻转下面的符号即可，不要改接线。 */
#define ENCODER_DIR_SIGN_FL         (+1)
#define ENCODER_DIR_SIGN_FR         (-1)
#define ENCODER_DIR_SIGN_RL         (+1)
#define ENCODER_DIR_SIGN_RR         (-1)

/** 电流采样：ADC1 IN10..IN13 = PC0..PC3 */
#define CURRENT_ADC_CHANNEL_FL      10u
#define CURRENT_ADC_CHANNEL_FR      11u
#define CURRENT_ADC_CHANNEL_RL      12u
#define CURRENT_ADC_CHANNEL_RR      13u
/** 电流采样标定：分流电阻 + 运放增益折算，单位 A/LSB (12bit @3.3V) */
#define CURRENT_ADC_SCALE_A_PER_LSB 0.00806f

/** 上位机串口：USART1 TX=PA9 RX=PA10 (AF7)，对接树莓派 /dev/ttyAMA0 */
#define UART_BAUDRATE               115200u

/** 硬件急停输入：PB0，接物理急停开关。
 *  按**常闭 (NC)** 接法：回路完好且未按下时把引脚拉到低电平；
 *  按下或线缆断开都会因内部上拉变成高电平 → 触发急停。断线即停，失效安全。
 *  这是脱离串口通信的最后一道防线，在 1kHz 控制中断里直接检测。 */
#define ESTOP_PORT                  GPIOB
#define ESTOP_PIN                   0u

/** 是否要求急停回路必须存在。
 *  1 = 生产配置：未接急停开关时引脚被上拉为高，固件开机即锁死，电机不动。
 *  0 = 台架调试：忽略急停引脚。**上车前必须改回 1**。 */
#define ESTOP_REQUIRE_HARDWARE      1

/** 状态指示灯：PC13，1kHz 中断里分频闪烁 */
#define LED_PORT                    GPIOC
#define LED_PIN                     13u

#endif /* STM32_MECANUM_BOARD_CONFIG_H */
