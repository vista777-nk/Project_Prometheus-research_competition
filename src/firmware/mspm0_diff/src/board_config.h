/**
 * @file board_config.h
 * @brief MSPM0G3507 差速底盘板级配置 —— 几何、编码器、PID、时序、引脚
 *
 * 本文件是固件里唯一的"魔数集散地"。所有可整定量集中在此，
 * 换车架 / 换电机 / 换接线只改这里，不动算法代码。
 *
 * 几何参数与仿真侧保持一致，来源：
 *   src/air_ground_car_bringup/config/chassis_params.yaml : diff_chassis
 *   src/air_ground_car_bringup/urdf/diff_chassis.urdf.xacro
 * 二者不一致会导致"仿真能跑、实车跑偏"，改动时务必同步。
 *
 * ⚠ 引脚映射 (§六) 是**按 LP-MSPM0G3507 LaunchPad 的建议排布**，
 *   尚未经 TI SysConfig 生成核对。上板前必须走 README §7 的核对流程。
 */
#ifndef MSPM0_DIFF_BOARD_CONFIG_H
#define MSPM0_DIFF_BOARD_CONFIG_H

/* ===================== 一、底盘几何 ===================== */

/** 轮半径 (m) —— chassis_params.yaml: diff_chassis.wheel_radius */
#define CHASSIS_WHEEL_RADIUS_M      0.033f
/** 轮间距 (m) —— chassis_params.yaml: diff_chassis.track_width */
#define CHASSIS_TRACK_WIDTH_M       0.18f

/** 电机空载最高转速 (RPM)，520 减速电机 @12V 实测值，整定后修改。
 *  与麦轮固件同型号电机，因此取值一致。
 *
 *  能力换算：330 RPM → 轮缘线速度 330/60 × 2π × 0.033 = 1.140 m/s。
 *  yaml 里 max_linear_speed=1.0 / max_angular_speed=3.0 各自都够用，
 *  但**两者不可同时取满**：v=1.0 且 ω=3.0 时右轮需要 1.27 m/s (367 RPM)，
 *  超出 330 RPM，逆解会按 0.898 等比缩放。这是底盘的物理极限，不是 bug；
 *  上位机若需要严格跟踪轨迹，应自行把速度指令限制在可行域内。 */
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

/* 转速测量窗口 —— 与麦轮固件同一套推理，结论也一样。
 * 按 1ms 采样直接算转速，分辨率只有 60000/(1320×1) ≈ 45 RPM/计数，
 * 比整个调速范围的 1/7 还大，PID 会被量化噪声牵着走。
 * 改成 10ms 窗口后分辨率约 4.5 RPM，代价是速度反馈延迟 10ms
 * (相对 80ms 的电机时间常数可接受)。速度环仍然跑满 1kHz，
 * 只是每 10 次才拿到一个新的测量值。
 * Phase 2 若要更高精度，应改用 M/T 法 (同时测计数与相邻边沿间隔)。 */
#define ENCODER_SPEED_WINDOW_TICKS  10u
/** 转速一阶低通系数 (EMA)，0=完全不更新，1=不滤波 */
#define ENCODER_RPM_FILTER_ALPHA    0.4f

/* ===================== 三、控制时序 ===================== */

/** 速度环控制频率 (Hz)，由 TIMG0 周期中断驱动 */
#define CONTROL_FREQ_HZ             1000u
/** 速度环周期 (s) */
#define CONTROL_DT_S                (1.0f / (float)CONTROL_FREQ_HZ)
/** 遥测上报周期 (ms) = 20Hz */
#define TELEMETRY_PERIOD_MS         50u
/** 指令看门狗：超过该时间未收到 SET_VELOCITY 即刹停 (ms) */
#define CMD_TIMEOUT_MS              500u

/* ===================== 四、PID 整定参数 ===================== */

/* 速度环 PID：输入 RPM 误差，输出 PWM 占空比 [-1, 1]。
   与麦轮固件同型号电机 + 同控制频率，因此默认增益一致；
   但差速底盘负载分布不同 (只有两轮承担全部牵引力)，实车必须复整定。

   kp 的量纲是 (占空比 / RPM)：满量程 330 RPM 对应满占空比 1.0，
   因此静态前馈级别的增益约为 1/330 ≈ 0.003。 */
#define PID_KP_DEFAULT              0.0035f
#define PID_KI_DEFAULT              0.030f
#define PID_KD_DEFAULT              0.00004f
/** 积分限幅 (RPM·s)。整定准则：ki × integral_limit ≥ output_limit，
 *  否则积分器无法独立顶到满占空比 —— 表现为"带载后转速永远差一截"。
 *  这条准则是 task-10 被负载扰动测试逼出来的，别再踩一次。
 *  0.030 × 40 = 1.2，留 20% 余量覆盖电池掉压与负载增大。 */
#define PID_INTEGRAL_LIMIT          40.0f
/** 输出限幅 = 满占空比 */
#define PID_OUTPUT_LIMIT            1.0f

/* ===================== 五、故障阈值 ===================== */

/** 单电机过流阈值 (A)，超过即全部停机。
 *  差速底盘只有两个电机分担整车牵引，单机电流天然比麦轮高，
 *  因此阈值比麦轮固件的 2.5A 略放宽。 */
#define FAULT_CURRENT_LIMIT_A       3.0f
/** 堵转判据：目标 RPM 高于该值但实测 RPM 低于 STALL_RPM_FLOOR 持续 STALL_TIME_MS */
#define FAULT_STALL_TARGET_RPM      30.0f
#define FAULT_STALL_RPM_FLOOR       3.0f
#define FAULT_STALL_TIME_MS         800u

/* ===================== 六、引脚映射 =====================
 *
 * ⚠ 以下为 LP-MSPM0G3507 LaunchPad 的**建议**排布，必须用 TI SysConfig
 *   重新生成并核对后才能上板。核对流程见 README §7。
 *   固件代码只依赖这里的符号名，改接线不动算法。
 *
 * 轮序号约定 (俯视图，车头朝上)：
 *      [0] 左轮 LEFT    [1] 右轮 RIGHT
 */

/** 电机 PWM：TIMA0_C0 / TIMA0_C1 两路互补输出到 TB6612 的 PWMA / PWMB */
#define MOTOR_PWM_PIN_LEFT          "PB4"    /* TIMA0_C0 */
#define MOTOR_PWM_PIN_RIGHT         "PB1"    /* TIMA0_C1 */
/** PWM 载频 (Hz)，20kHz 避开可听频段 */
#define MOTOR_PWM_FREQ_HZ           20000u

/** 方向控制：TB6612 双路 IN，每轮 2 根 */
#define MOTOR_DIR_PIN_LEFT_A        "PB6"
#define MOTOR_DIR_PIN_LEFT_B        "PB7"
#define MOTOR_DIR_PIN_RIGHT_A       "PB8"
#define MOTOR_DIR_PIN_RIGHT_B       "PB9"

/* 编码器定时器：TIMG8 / TIMG7 正交编码器模式 (对应轮 0 / 1)
 *   TIMG8 : PA12 / PA13
 *   TIMG7 : PA14 / PA15
 * 若某轮方向与车体约定相反，翻转下面的符号即可，不要改接线。 */
#define ENCODER_DIR_SIGN_LEFT       (+1)
#define ENCODER_DIR_SIGN_RIGHT      (-1)

/** 电流采样：ADC0 通道，接 TB6612 分流电阻后的运放输出 */
#define CURRENT_ADC_CHANNEL_LEFT    4u    /* PA24 / ADC0_CH4 */
#define CURRENT_ADC_CHANNEL_RIGHT   5u    /* PA25 / ADC0_CH5 */
/** 电流采样标定：分流电阻 + 运放增益折算，单位 A/LSB (12bit @3.3V) */
#define CURRENT_ADC_SCALE_A_PER_LSB 0.00806f

/** 上位机串口：UART0 TX=PA10 RX=PA11，对接树莓派 /dev/ttyAMA1 */
#define UART_BAUDRATE               115200u

/** 硬件急停输入。按**常闭 (NC)** 接法：
 *  回路完好且未按下时把引脚拉到低电平；按下或线缆断开都会因内部上拉变成高电平
 *  → 触发急停。断线即停，失效安全。
 *  这是脱离串口通信的最后一道防线，在 1kHz 控制中断里直接检测。 */
#define ESTOP_PIN                   "PA18"

/** 是否要求急停回路必须存在。
 *  1 = 生产配置：未接急停开关时引脚被上拉为高，固件开机即锁死，电机不动。
 *  0 = 台架调试：忽略急停引脚。**上车前必须改回 1**。 */
#define ESTOP_REQUIRE_HARDWARE      1

/** 状态指示灯：LaunchPad 板载 LED */
#define LED_PIN                     "PA0"

/* ===================== 七、电赛扩展预留 =====================
 *
 * 竞赛现场常要临时接循迹 / 避障 / 灰度模块。为了不在赛场上改固件结构，
 * 这里预留一组资源，配合协议里的 CMD_EXTENSION (0x10) 使用。
 * 详见 README §6 与 task-11 §11.5 注意事项②。
 */

/** 预留 ADC 通道数 (循迹灰度阵列等模拟量传感器) */
#define EXT_ADC_CHANNEL_COUNT       4u
/** 预留 GPIO 数 (避障开关量、蜂鸣器、拨码开关等) */
#define EXT_GPIO_COUNT              8u

#endif /* MSPM0_DIFF_BOARD_CONFIG_H */
