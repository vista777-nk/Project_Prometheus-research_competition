# Task-11: MSPM0G3507 差速固件

> **状态：🔴 待开始** | **优先级：🥈 高** | **预计耗时：5h（编码+测试）/ 2.5h（仅骨架+CI）**
>
> **适用环境**：任意 OS（Windows / macOS / Linux） + TI CCS 或 GCC for MSPM0
> **硬件依赖**：无（CI 交叉编译 + 单元测试 Mock）
> **ROS 依赖**：无（独立固件项目）
> **特殊要求**：TI 电赛合规（禁止使用非 TI 厂商 MCU 做主控）

---

## 前置条件

- 已安装 TI Code Composer Studio (CCS) 或 GCC for MSPM0（`arm-none-eabi-gcc` 支持 Cortex-M0+）
- 了解 MSPM0G3507 基本参数：80MHz Cortex-M0+、128KB Flash、32KB SRAM
- 了解差速底盘运动学：`v, ω → 左轮 RPM, 右轮 RPM`
- **不需要**：真实 MSPM0 硬件、Ubuntu 20.04

---

## 目标

为差速底盘构建一套 **TI 电赛合规** 的 MSPM0G3507 下位机固件：

1. **差速运动学解算**：`v, ω → 左右轮 RPM`
2. **双轮独立 PID 速度环**：520 编码器电机闭环控制
3. **TI 电赛合规串口协议**：与树莓派5 UART 通信（二进制帧，区别于 STM32 但结构一致）
4. **CI 交叉编译**：GitHub Actions 自动编译 + 固件 artifact
5. **单元测试**：Mock 编码器，验证运动学和 PID

---

## 架构定位

```
树莓派5 (Layer 2 Edge Node)
  │  rosnode: car_preprocessor.py
  │  发布 /car/observation, /car/robot_state
  │
  │  UART (/dev/ttyAMA1 或 USB-UART, 115200 8N1)
  │  协议: 二进制帧 + CRC16 (与 STM32 帧格式高度相似)
  ▼
MSPM0G3507 (Layer 1 Hardware)  ← 本任务
  │  接收: v (m/s), ω (rad/s)
  │  差速运动学 → 左右轮目标 RPM
  │  双轮独立 PID 速度环 (1kHz)
  │  编码器读取 (AB 相, Timer 捕获)
  │  上报: 左右轮实际 RPM + 电流 + 故障码
  ▼
2× 520 编码器电机 (差速底盘, TI 电赛亚克力车架)
```

**电赛合规要点**：
- 主控芯片必须是 TI 厂商 MCU（MSPM0G3507 符合）
- 禁止使用树莓派、STM32、Arduino 等非 TI 主控做运动闭环
- 树莓派5 仅做感知 + 通信中继，不参与电机控制

---

## 可执行步骤

### 11.1 创建固件项目骨架

```bash
cd /path/to/research_compitition
mkdir -p src/firmware/mspm0_diff
cd src/firmware/mspm0_diff
```

目录结构（与 STM32 项目保持一致）：

```
src/firmware/mspm0_diff/
├── README.md                    # 引脚定义 + 协议帧格式 + CCS 导入说明
├── Makefile                     # CLI 编译（GCC for MSPM0）
├── CMakeLists.txt               # CMake 工具链 (arm-none-eabi-gcc)
├── linker/
│   └── MSPM0G3507.ld            # 链接脚本
├── src/
│   ├── main.c                   # 入口：初始化 + 1kHz 主循环
│   ├── kinematics.c/.h          # 差速运动学解算
│   ├── pid.c/.h                 # PID 控制器 (复用 task-10 的 PID 代码)
│   ├── encoder.c/.h             # 编码器 AB 相捕获 (Timer 输入捕获模式)
│   ├── motor.c/.h               # PWM 输出 (Timer PWM 模式)
│   ├── protocol.c/.h            # 串口帧协议 (与 STM32 帧结构一致)
│   ├── uart.c/.h                # UART 收发
│   └── mspm0g3507.h             # 寄存器定义 / DriverLib 引用
├── test/
│   ├── test_kinematics.c        # 差速运动学单元测试
│   ├── test_pid.c               # PID 测试 (可复用 task-10)
│   ├── test_protocol.c          # 协议帧测试
│   └── unity.c/.h               # Unity Test 框架
└── ccs/                         # (可选) TI CCS 工程文件
    └── mspm0_diff.projectspec
```

### 11.2 差速运动学解算 (`kinematics.c`)

差速底盘比麦轮简单得多——只有两个自由度：

```
// 差速运动学:
// v = (v_left + v_right) / 2         → 线速度
// ω = (v_right - v_left) / track     → 角速度 (track = 轮间距, m)
//
// 逆解:
// v_left  = v - ω * track / 2
// v_right = v + ω * track / 2
//
// RPM = v_wheel * 60 / (2 * π * R)
// RPM_left  = (v - ω * track/2) * 60 / (2 * π * R)
// RPM_right = (v + ω * track/2) * 60 / (2 * π * R)
```

```c
// kinematics.h
#ifndef DIFF_KINEMATICS_H
#define DIFF_KINEMATICS_H

typedef struct {
    float wheel_radius;    // 轮半径 (m)
    float track;           // 轮间距 (m), 两轮中心距离
    float max_rpm;         // 电机最大转速
} DiffGeometry;

/** 机器人速度指令 (右手坐标系, x=前, ω=逆时针正) */
typedef struct {
    float v;       // 线速度 (m/s)
    float omega;   // 角速度 (rad/s)
} DiffVelocity;

void diff_kinematics_init(const DiffGeometry *geo);

/** 差速逆解: (v, ω) → 左右轮目标 RPM
 * @param cmd        输入: 期望速度
 * @param rpm_left   输出: 左轮 RPM
 * @param rpm_right  输出: 右轮 RPM
 * @return 0=成功, -1=超限钳位
 */
int diff_inverse(const DiffVelocity *cmd, float *rpm_left, float *rpm_right);

#endif
```

### 11.3 PID 控制器

**直接复用 task-10 的 `pid.c/.h`**。差速底盘只用 2 个 PID 实例（左右轮各一），四轮麦轮用 4 个。PID 算法完全一样。

实现方式：
- 方案 A（推荐）：仓库级共享，`src/firmware/common/pid.c` → 两个固件项目通过 `-I../common` 引用
- 方案 B：各自拷贝一份（简单但代码重复）

> **推荐方案 A**。在 `src/firmware/common/` 下放置共享模块（PID、CRC、Unity Test 框架等）。

### 11.4 串口通信协议 (`protocol.c`)

帧格式与 task-10 **完全一致**（便于统一解析），仅命令集缩减：

```
┌────────┬────────┬──────────┬──────────────────┬──────────┬────────┐
│  SOF   │  LEN   │   CMD    │      DATA        │  CRC16   │  EOF   │
│ 0xA5   │ n+4    │  1 byte  │   0~252 bytes    │ 2 bytes  │  0x5A  │
└────────┴────────┴──────────┴──────────────────┴──────────┴────────┘
```

| CMD | 方向 | 名称 | DATA | 响应 |
|-----|------|------|------|------|
| `0x01` | Pi→MSPM0 | `SET_VELOCITY` | `v(f32) ω(f32)` = 8 bytes | `ACK` |
| `0x02` | Pi→MSPM0 | `EMERGENCY_STOP` | 无 | `ACK` |
| `0x03` | Pi→MSPM0 | `PING` | 无 | `PONG` |
| `0x11` | MSPM0→Pi | `TELEMETRY` | 左RPM(f32) 右RPM(f32) 左电流(f32) 右电流(f32) 故障码(u16) = 18 bytes | — |
| `0x12` | MSPM0→Pi | `ACK` | 被确认 CMD(u8) | — |
| `0x13` | MSPM0→Pi | `PONG` | major(u8).minor(u8).patch(u8) | — |
| `0xFF` | MSPM0→Pi | `ERROR` | 错误码(u8) + 详情 | — |

**关键区别 vs STM32**：
- `SET_VELOCITY` 数据长度 8 bytes (差分) vs 12 bytes (麦轮)
- `TELEMETRY` 数据长度 18 bytes (差分) vs 34 bytes (麦轮)
- 帧结构、CRC、SOF/EOF 完全一致 → 树莓派端可用统一的帧解析器

### 11.5 主循环逻辑

```c
void main(void) {
    // MSPM0 初始化 (80MHz)
    SystemInit();
    diff_kinematics_init(&geo);
    pid_init(&pid_left, ...);
    pid_init(&pid_right, ...);
    encoder_init();   // TimerA0/A1 编码器模式
    motor_init();     // TimerG0/G1 PWM
    uart_init();      // UART0, 115200, DMA

    DiffVelocity cmd = {0};
    uint32_t last_telemetry = 0;

    while (1) {
        // 1. 串口帧解析
        Frame frame;
        if (protocol_parse(&frame)) {
            switch (frame.cmd) {
                case CMD_SET_VELOCITY:
                    memcpy(&cmd, frame.data, 8);
                    protocol_send_ack(CMD_SET_VELOCITY);
                    break;
                case CMD_EMERGENCY_STOP:
                    cmd = (DiffVelocity){0};
                    motor_all_stop();
                    protocol_send_ack(CMD_EMERGENCY_STOP);
                    break;
                case CMD_PING:
                    protocol_send_pong(FW_MAJOR, FW_MINOR, FW_PATCH);
                    break;
            }
        }

        // 2. 读取编码器
        float rpm_left = encoder_get_rpm(0);
        float rpm_right = encoder_get_rpm(1);

        // 3. 差速逆解
        float target_left, target_right;
        diff_inverse(&cmd, &target_left, &target_right);

        // 4. PID + PWM
        motor_set_duty(0, pid_update(&pid_left,  target_left,  rpm_left,  0.001f));
        motor_set_duty(1, pid_update(&pid_right, target_right, rpm_right, 0.001f));

        // 5. 遥测 (20Hz)
        if (timer_expired(&last_telemetry, 50)) {
            float currents[2];
            motor_get_currents(currents);
            protocol_send_telemetry(rpm_left, rpm_right, currents[0], currents[1], fault_code);
        }

        // 6. 故障检测
        fault_code = check_faults();

        __delay_cycles(80000);  // ~1ms @ 80MHz
    }
}
```

### 11.6 单元测试

| 测试用例 | 输入 | 期望输出 |
|----------|------|----------|
| `test_straight_forward` | v=0.5, ω=0 | RPM_left = RPM_right > 0 |
| `test_straight_backward` | v=-0.5, ω=0 | RPM_left = RPM_right < 0 |
| `test_rotate_in_place_cw` | v=0, ω=1.0 | RPM_left < 0, RPM_right > 0, \|left\| = \|right\| |
| `test_curve_forward` | v=0.5, ω=0.5 | RPM_right > RPM_left > 0 |
| `test_zero_input` | (0, 0) | RPM_left = RPM_right = 0 |
| `test_saturation` | v=5.0 (超 max) | 双轮均钳位 |

### 11.7 CI 编译 (与 Task-13 联动)

```yaml
  build-mspm0-firmware:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install ARM GCC
        run: sudo apt install -y gcc-arm-none-eabi
      - name: Build MSPM0 Diff Firmware
        run: |
          cd src/firmware/mspm0_diff
          make -j$(nproc)
      - name: Run Unit Tests (Host)
        run: |
          cd src/firmware/mspm0_diff
          make test
      - name: Upload Firmware Artifact
        uses: actions/upload-artifact@v4
        with:
          name: mspm0-diff-firmware
          path: src/firmware/mspm0_diff/build/*.bin
```

---

## 验收标准

- [ ] `kinematics.c` 单元测试 6 个用例全部 PASS
- [ ] `make` 成功生成 `mspm0_diff.bin`
- [ ] CI 交叉编译 job 绿灯
- [ ] `README.md` 包含：MSPM0G3507 引脚定义表 + 协议帧格式 + CCS 导入步骤 + 电赛合规说明
- [ ] 协议帧格式与 STM32 固件兼容（树莓派端可用同一个帧解析器，仅命令子集不同）

---

## 与 task-10 的代码复用

| 模块 | 复用方式 |
|------|----------|
| `pid.c/.h` | 通过 `src/firmware/common/pid.c` 共享（推荐在 task-11 之前先建立 common 目录） |
| `protocol.c/.h` | 帧结构相同但命令集不同 → 共享帧打包/解包/CRC 代码，各自定义命令表 |
| `unity.c/.h` | 共享测试框架 |
| `kinematics.c/.h` | **各自独立实现**（差速与麦轮运动学不同） |

---

## 参考资料

| 文件 | 内容 |
|------|------|
| `src/air_ground_car_bringup/urdf/diff_chassis.urdf.xacro` | 差速底盘几何参数（轮径、轮距） |
| `src/air_ground_car_bringup/config/diff_chassis_control.yaml` | ros_control PID 参数参考 |
| `project-prometheus-tasks/ICD.md` §三 | 控制接口抽象定义 |
| `project-prometheus-tasks/PLATFORM.md` §一 | 硬件环境表 |
| `project-prometheus-tasks/task-10-stm32-mecanum-firmware.md` | STM32 固件（共享 PID/协议/测试框架） |

---

## 给 Subagent 的执行建议

1. **如果 task-10 已完成**：直接复用 `common/pid.c`、协议帧打包逻辑、Unity 测试框架
2. **如果 task-10 未开始**：先创建 `src/firmware/common/` 目录，放置 PID + CRC + Unity
3. MSPM0 是 Cortex-M0+ 核心（无 FPU），**所有浮点运算用软件模拟**（`-mfloat-abi=soft`）。运动学计算量很小，性能足够
4. TI 电赛合规检查：确保主控是 MSPM0G3507（非 STM32 / Arduino / 树莓派），这在 README 中声明即可
5. CCS 工程文件 `.projectspec` 是可选的（CLI `make` 已经能满足 CI）

---

*版本: v1.0 · 日期: 2026-07-28 · Phase 1 · 与 task-10 共享 `src/firmware/common/`*
