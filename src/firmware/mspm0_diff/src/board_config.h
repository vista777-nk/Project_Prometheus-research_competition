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
 * ⚠ 引脚映射 (§六) 已按 MSPM0G3507 Rev.C 数据手册与 LaunchPad Rev.D 40-pin
 *   接口重排，但尚未由本工程 SysConfig 生成物验证。上板前必须走 README §7。
 */
#ifndef MSPM0_DIFF_BOARD_CONFIG_H
#define MSPM0_DIFF_BOARD_CONFIG_H

/* ===================== 一、底盘几何 ===================== */

/** 轮半径 (m) —— chassis_params.yaml: diff_chassis.wheel_radius */
#define CHASSIS_WHEEL_RADIUS_M      0.031f
/** 轮间距 (m) —— chassis_params.yaml: diff_chassis.track_width */
#define CHASSIS_TRACK_WIDTH_M       0.166f

/** MC520P30 @12V 空载最高转速 (RPM)，供应方给定 360±20 RPM。
 *
 *  能力换算：360 RPM → 轮缘线速度 360/60 × 2π × 0.031 = 1.169 m/s。
 *  yaml 里 max_linear_speed=1.0 / max_angular_speed=3.0 各自都够用，
 *  但**两者不可同时取满**：v=1.0 且 ω=3.0 时右轮需要 1.27 m/s (367 RPM)，
 *  超出 360 RPM，逆解会等比缩放。这是底盘的物理极限，不是 bug；
 *  上位机若需要严格跟踪轨迹，应自行把速度指令限制在可行域内。 */
#define MOTOR_MAX_RPM               360.0f

/* ===================== 二、编码器 ===================== */

/** 编码器每转脉冲数（电机轴侧）。 */
#define ENCODER_PPR                 13.0f
/** 减速比 (电机轴 : 输出轴)。 */
#define ENCODER_GEAR_RATIO          30.0f
/** 定时器编码器模式的倍频系数 (AB 双相双边沿 = 4×) */
#define ENCODER_QUADRATURE          4.0f
/** 输出轴每转计数 = PPR × 倍频 × 减速比 = 1560 */
#define ENCODER_COUNTS_PER_REV      (ENCODER_PPR * ENCODER_QUADRATURE * ENCODER_GEAR_RATIO)

/* 转速测量窗口 —— 与麦轮固件同一套推理，结论也一样。
 * 按 1ms 采样直接算转速，分辨率只有 60000/(1560×1) ≈ 38.5 RPM/计数，
 * 比整个调速范围的 1/7 还大，PID 会被量化噪声牵着走。
 * 改成 10ms 窗口后分辨率约 3.85 RPM，代价是速度反馈延迟 10ms
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
/** 四路超声波完整快照上报周期 (ms)；实际触发由移植层轮询调度。 */
#define ULTRASONIC_PERIOD_MS        50u
/** 指令看门狗：超过该时间未收到 SET_VELOCITY 即刹停 (ms) */
#define CMD_TIMEOUT_MS              500u

/* ===================== 四、PID 整定参数 ===================== */

/* 速度环 PID：输入 RPM 误差，输出 PWM 占空比 [-1, 1]。
   与麦轮固件同型号电机 + 同控制频率，因此默认增益一致；
   但差速底盘负载分布不同 (只有两轮承担全部牵引力)，实车必须复整定。

   kp 的量纲是 (占空比 / RPM)：360 RPM 空载值仅给出静态前馈量级，
   真实带载增益必须台架整定。 */
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

/** DRV8871 仅通过 ILIM 电阻内部限流，不向 MCU 输出模拟电流。
 *  当前 BOM 没有外部分流/放大电路，因此 TELEMETRY 电流字段发送 NaN，
 *  软件过流位不启用。本正数只满足通用故障配置的参数约束。 */
#define FAULT_CURRENT_LIMIT_A       3.6f
/** 堵转判据：目标 RPM 高于该值但实测 RPM 低于 STALL_RPM_FLOOR 持续 STALL_TIME_MS */
#define FAULT_STALL_TARGET_RPM      30.0f
#define FAULT_STALL_RPM_FLOOR       3.0f
#define FAULT_STALL_TIME_MS         800u

/* ===================== 六、引脚映射 =====================
 *
 * ⚠ 先前答复中的 PB4=TIMA0_C0、PB1=TIMA0_C1、TIMG7=QEI 均不成立，
 *   不得按旧表接线。下面方案只使用 LaunchPad 40-pin 已引出的引脚；仍须以
 *   SysConfig 无冲突生成 + 示波器实测为最终冻结门槛。
 *   固件代码只依赖这里的符号名，改接线不动算法。
 *
 * 轮序号约定 (俯视图，车头朝上)：
 *      [0] 左轮 LEFT    [1] 右轮 RIGHT
 */

/** DRV8871 IN1：TIMA0_C0 / TIMA0_C1 输出 PWM */
#define MOTOR_PWM_PIN_LEFT          "PB8"    /* TIMA0_C0 */
#define MOTOR_PWM_PIN_RIGHT         "PB9"    /* TIMA0_C1 */
/** PWM 载频 (Hz)，20kHz 避开可听频段 */
#define MOTOR_PWM_FREQ_HZ           20000u

/** DRV8871 IN2：每轮 1 根方向 GPIO。 */
#define MOTOR_DIR_PIN_LEFT          "PB6"
#define MOTOR_DIR_PIN_RIGHT         "PB7"

/* MSPM0G3507 只有 TIMG8 一路支持 QEI，无法让两轮都用硬件 QEI。
 * 两路统一采用 GPIO 双边沿 + Gray 码查表软件解码，避免左右实现不对称：
 *   左轮 : PA12 / PA13
 *   右轮 : PA15 / PA16
 * 360RPM、1560 count/rev 时两轮合计约 18.7k edge/s，32MHz 下仍须上板量 ISR 占用。
 * 若某轮方向与车体约定相反，翻转下面的符号即可，不要改接线。 */
#define ENCODER_PIN_LEFT_A          "PA12"
#define ENCODER_PIN_LEFT_B          "PA13"
#define ENCODER_PIN_RIGHT_A         "PA15"
#define ENCODER_PIN_RIGHT_B         "PA16"
#define ENCODER_DIR_SIGN_LEFT       (+1)
#define ENCODER_DIR_SIGN_RIGHT      (-1)

/** 上位机串口：UART0 TX=PA10 RX=PA11；Pi 容器内统一映射为 /dev/mcu。 */
#define UART_BAUDRATE               115200u

/** IA6B iBUS：接收机 iBUS-SERVO → UART1_RX=PA9，115200 8N1；PA8 不接。 */
#define RC_IBUS_RX_PIN              "PA9"
#define RC_IBUS_BAUDRATE            115200u

/** HC-SR04 仅单路轮流触发；每个 Echo 必须先经 2.2k/3.3k 分压再入 MCU。 */
#define ULTRASONIC_TRIG_PINS        "PB0,PB1,PB4,PB13"
#define ULTRASONIC_ECHO_PINS        "PA17,PA22,PA24,PA25"

/** 硬件急停输入。按**常闭 (NC)** 接法：
 *  回路完好且未按下时把引脚拉到低电平；按下或线缆断开都会因内部上拉变成高电平
 *  → 触发急停。断线即停，失效安全。
 *  这是脱离串口通信的最后一道防线，在 1kHz 控制中断里直接检测。 */
#define ESTOP_PIN                   "PA18"

/** 是否要求急停回路必须存在。
 *  1 = 生产配置：未接急停开关时引脚被上拉为高，固件开机即锁死，电机不动。
 *  0 = 台架调试：忽略急停引脚。**上车前必须改回 1**。 */
#define ESTOP_REQUIRE_HARDWARE      1

/** 状态指示灯：机器人扩展板 D1（电子组样例已验证）。 */
#define LED_PIN                     "PB2"

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
