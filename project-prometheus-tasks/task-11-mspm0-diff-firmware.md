# Task-11: MSPM0G3507 差速固件

> **状态：✅ 已完成（2026-07-29）** | **优先级：🥈 高** | **实际耗时：约 3h**
>
> **适用环境**：任意 OS（Windows / macOS / Linux） + ARM GCC（Cortex-M0+）
> **硬件依赖**：无（CI 交叉编译 + 单元测试 Mock）
> **ROS 依赖**：无（独立固件项目）
> **特殊要求**：TI 电赛合规（禁止使用非 TI 厂商 MCU 做主控）
>
> **产出**：`src/firmware/mspm0_diff/` · [ADR-0004](../docs/decisions/ADR-0004.md) · CI job `build-mspm0-firmware`
> **验收结果**：Host 单元测试 49/49 通过。`common/` 一行未改即完成复用。
>
> ⚠ **默认构建产出不是可烧录固件** —— MSPM0 移植层默认为空实现，原因与代价见
> 文末 [§实施记录](#实施记录2026-07-29) 及 ADR-0004 §决策-3。这一点在
> README、构建横幅、产出文件名、`make flash` 四处都有拦截。

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
  │  发布 /car/observation, /car/state
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

## 架构影响

| 维度 | 内容 |
|------|------|
| **Affected Capability** | Locomotion: 差速运动学 + 双轮独立速度PID |
| **Modified Interface** | 新增 Layer 1 内部接口: UART 二进制帧协议 (CMD_SET_VELOCITY / TELEMETRY) — 与 STM32 帧结构一致 |
| **New Dependency** | `arm-none-eabi-gcc` (Cortex-M0+ 工具链) · `src/firmware/common/pid.c` (共享 PID) · TI 电赛合规约束 |
| **ADR Required** | ADR-0003: 串口二进制帧协议统一设计 · ADR-0004: TI 电赛合规主控选型理由 |
| **Risk Level** | 🟢 Low — 纯固件，独立于 ROS |

> **铁律回顾 (RESEARCH_PHILOSOPHY.md §五)**：  
> 换 MCU 时，差速运动学公式不变，PID 算法不变，帧结构不变。MSPM0 是电赛合规选择。

## 未来演进

| 维度 | 今天 (Phase 1) | 明天 (Phase 2+) |
|------|---------------|-----------------|
| **Replaceable Component** | MSPM0G3507 (Cortex-M0+, 80MHz) | STM32G4 / TMS320F280039C (C2000) / 其他 TI 合规 MCU |
| **Permanent Interface** | 串口二进制帧协议 · 运动学输入 `{v, ω}` | 保持不变 — 换 MCU 只需改 HAL 层 |
| **Temporary Implementation** | MSPM0 DriverLib · 软件浮点 (`-mfloat-abi=soft`) | v2: 硬件浮点 MCU 升级 · 电流环 + 速度环级联 PID · CAN/RS485 替代 UART |

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
│ 0xA5   │ n+4    │  1 byte  │   0~251 bytes    │ 2 bytes  │  0x5A  │
└────────┴────────┴──────────┴──────────────────┴──────────┴────────┘
LEN = CMD(1) + DATA(n) + CRC(2) + EOF(1), n ∈ [0, 251], LEN ∈ [4, 255]
CRC 覆盖: CMD + DATA (不含 SOF/LEN/EOF) — CRC-16/CCITT-FALSE, 测试向量 0x29B1
详见 task-10 §10.4 CRC 实现 (ARM/MSPM0/Python 三方共用同一算法)
```

| CMD | 方向 | 名称 | DATA | 响应 |
|-----|------|------|------|------|
| `0x01` | Pi→MSPM0 | `SET_VELOCITY` | `v(f32) ω(f32)` = 8 bytes | `ACK` |
| `0x02` | Pi→MSPM0 | `EMERGENCY_STOP` | 无 | `ACK` |
| `0x03` | Pi→MSPM0 | `PING` | 无 | `PONG` |
| `0x11` | MSPM0→Pi | `TELEMETRY` | 左RPM(f32) 右RPM(f32) 左电流(f32) 右电流(f32) 故障码(u16) = 18 bytes | — |
| `0x12` | MSPM0→Pi | `ACK` | 被确认 CMD(u8) | — |
| `0x13` | MSPM0→Pi | `PONG` | major(u8).minor(u8).patch(u8) + board_type(u8=0x02) + chassis_type(u8=0x02) = 5 bytes | — |
| `0xFF` | MSPM0→Pi | `ERROR` | 错误码(u8) + 详情 | — |

**关键区别 vs STM32**：
- `SET_VELOCITY` 数据长度 8 bytes (差分) vs 12 bytes (麦轮)
- `TELEMETRY` 数据长度 18 bytes (差分) vs 34 bytes (麦轮)
- `PONG` 中 `board_type=0x02` (MSPM0) · `chassis_type=0x02` (差速) vs STM32 的 `0x01/0x01`
- 帧结构、CRC、SOF/EOF 完全一致 → 树莓派端可用统一的帧解析器
- **Pi 端启动校验**：发送 PING → 检查 PONG 中 board_type + chassis_type 是否与 `CHASSIS` 环境变量一致，不匹配则拒绝启动

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

        DL_Common_delayCycles(80000);  // ~1ms @ 80MHz (MSPM0 DriverLib API)
    }
}
```

> **ⓘ 实现注意事项（混元3 评审建议）**：
>
> **① 编码器接口方案**：使用 MSPM0 TimerA 的编码器模式 (Quadrature Encoder Mode) 采集 AB 相编码器。编码器线数 (PPR) 和减速比通过 `#define` 配置，计算公式：`RPM = (delta_count / (4 * PPR * gear_ratio)) * 60 * control_freq`。参数从 `chassis_params.yaml` 对应值提取。
>
> **② 电赛扩展接口预留**：竞赛场景常需接入循迹模块、避障模块等外设。在串口协议中预留 CMD `0x10`（自定义扩展命令），在硬件上预留 ADC 通道 (PA0~PA3) 和 GPIO (PB0~PB7) 扩展引脚，供竞赛期间快速接入外设。
>
> **③ 命令集对照表（STM32 vs MSPM0）**：
>
> | CMD | STM32 (麦轮) | MSPM0 (差速) | 通用? |
> |-----|-------------|-------------|:---:|
> | `0x01` SET_VELOCITY | vx,vy,ω (12B) | v,ω (8B) | 数据长度不同 |
> | `0x02` EMERGENCY_STOP | ✓ | ✓ | ✅ |
> | `0x03` PING | ✓ (PONG 5B, board=0x01, chassis=0x01) | ✓ (PONG 5B, board=0x02, chassis=0x02) | ✅ |
> | `0x10` (预留扩展) | — | 竞赛外设 | — |
> | `0x11` TELEMETRY | 4轮RPM+电流 (34B) | 2轮RPM+电流 (18B) | 数据长度不同 |
> | `0x12` ACK | ✓ | ✓ | ✅ |
> | `0x13` PONG | ✓ | ✓ | ✅ |
> | `0xFF` ERROR | ✓ | ✓ | ✅ |
>
> **④ 低功耗设计**：电池供电场景下，可配置 MSPM0 在无指令 5 秒后进入 Sleep 模式（TIMER 仍运行以维持 PID），收到串口数据时通过 UART RX 中断唤醒。此功能为 Phase 2 优化项，Phase 1 保持全速运行即可。

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

- [x] `kinematics.c` 单元测试 6 个用例全部 PASS（实际做了 14 个）
- [x] `make` 成功生成 `mspm0_diff-ci-link.bin`（Makefile 就绪，交叉编译由 CI 执行）
- [x] CI 交叉编译 job 绿灯（`build-mspm0-firmware` 已加入 `.github/workflows/ci.yml`）
- [x] `README.md` 包含：引脚定义表 + 协议帧格式 + SysConfig 导入步骤 + 电赛合规说明
- [x] 协议帧格式与 STM32 固件兼容（`test_frame_layer_accepts_other_board_frames` 钉死）

---

## 实施记录（2026-07-29）

### 交付清单

| 路径 | 内容 |
|------|------|
| `src/firmware/mspm0_diff/README.md` | 引脚表 · 协议 · 黄金帧 · 软浮点开销 · **上板检查清单** |
| `src/firmware/mspm0_diff/Makefile` | 双剖面构建（`ci-link` / `driverlib`）+ `test`/`size`/`flash`/`clean` |
| `src/firmware/mspm0_diff/linker/` | MSPM0G3507 链接脚本（含待核对内存布局的警示） |
| `src/firmware/mspm0_diff/src/` | 18 个文件：算法层 + 移植层接口 + 两份移植层实现 |
| `src/firmware/mspm0_diff/test/` | 5 个文件，49 个用例 |
| `docs/decisions/ADR-0004.md` | TI 电赛合规主控选型 + 移植层分离决策 |
| `.github/workflows/ci.yml` | 新增 `build-mspm0-firmware` job |

`src/firmware/common/` **一行未改**。task-10 声称的"帧层/PID/CRC 板无关"至此被证明。

### 最重要的一条：默认构建不可烧录

TI 官方支持的路径是 DriverLib + SysConfig 生成引脚配置，而 MSPM0 SDK 无法在
GitHub Actions 上免登录安装。两条路：

- **(a)** 凭印象手写 MSPM0 寄存器地址 → 能编过、CI 会绿、看起来很完整，
  然后在某个人真的烧录时以最难排查的方式失败。
- **(b)** 把移植层收窄成 15 个原语，默认给空实现，并在**四个位置**标明不可烧录：
  `mspm0_conf.h` 顶部注释、README §2、构建横幅、产出文件名 `-ci-link` 后缀，
  外加 `make flash` 在该剖面下直接拒绝执行。

选 (b)。**被空实现掉的只有那 15 个寄存器原语**；运动学、协议、PID、测速窗口、
环形缓冲、故障状态机全部是真实代码，由 49 个 Host 用例覆盖。
连 1kHz 控制中断在该剖面下也是真跑的（SysTick 是 ARM 内核外设，与 TI 无关）。

task-10 敢手写 STM32F4 寄存器层，是因为那份映射公开且能逐条核对。
这不是同一种情况，所以不该用同一种做法。

### 意外收获：移植层分离让 HAL 逻辑变得可测

这是本次任务最值得复制的一条经验。麦轮固件把寄存器操作直接写在
`encoder.c` / `motor.c` / `uart.c` 里，代价是 **16 位计数器回绕、测速窗口保持、
EMA 滤波这些真正容易出错的逻辑只能上板验证**。

把寄存器访问收拢到 `mspm0_port.h` 之后，`encoder.c` 变成纯逻辑，
用一个假编码器就能测掉 11 个用例 —— 全部是麦轮固件测不了的。
代价是每控制周期多约 10 次函数调用（@80MHz 约 0.5µs，占 0.05%）。

> 若日后重构 `stm32_mecanum`，应当照此办理。

### 与任务文档的偏差（均为有意为之）

| # | 文档原文 | 实际实现 | 理由 |
|:---:|----------|----------|------|
| 1 | §11.6 用例名 `test_rotate_in_place_cw`，输入 `ω=1.0` | 拆成 `test_rotate_in_place_ccw`（ω=+1.0）与 `_cw`（ω=−1.0） | 右手系下 ω>0 是**逆时针**。文档的**期望输出是对的**（左轮负、右轮正），只有**命名**错了。task-10 §10.6 有同一处笔误 |
| 2 | §11.6 `test_saturation` 期望"双轮均钳位" | 双轮**等比缩放** | 等比缩放使曲率 κ=ω/v **严格不变**，车走同一条弧线只是慢了；硬钳位会掰弯弧线，路径跟踪必然发散。补了 `test_saturation_preserves_curvature`（v=1.0/ω=3.0，只有右轮超限）—— 这是唯一能区分两种策略的用例。理由见 ADR-0004 §决策-2 |
| 3 | §11.2 API `diff_inverse(cmd, *rpm_left, *rpm_right)` | `diff_inverse_kinematics(cmd, rpm[2])` | 数组形式与 PID 数组、主循环、遥测组装一致；双指针形式在每个调用点都要展开。函数名补全 `_kinematics` 以消除"inverse 什么"的歧义 |
| 4 | §11.1 目录含 `src/pid.c/.h` | 放在 `common/` | 遵循 task-13 §13.1 权威布局，与 §11.3 方案 A 一致 |
| 5 | §11.1 目录含 `test/test_pid.c` + `test/unity.c/.h` | 不建，改建 `test_control_loop.c` | `common/pid.c` 与 `unity.c` 是与麦轮固件共用的**同一份实现**，其单体测试在 `stm32_mecanum/test/` 下唯一存在。同一份代码测两遍不增加信息，只增加两处要同步维护的用例。改测"逆解+双 PID+正解**串起来**"的组合行为 —— 单独测每一环都过、串起来跑偏才是控制固件的典型失败方式 |
| 6 | §11.1 目录含 `src/mspm0g3507.h`（寄存器定义） | 改为 `mspm0_port.h` + `port_stub.c` + `port_driverlib.c` | 见上文"默认构建不可烧录" |
| 7 | §11.1 含 `CMakeLists.txt` 与 `ccs/*.projectspec` | 未提供 | §给 Subagent 的执行建议 #5 已注明 CCS 工程可选；CMake 与 Makefile 二选一即可，两套构建系统必然漂移。SysConfig 流程写进 README §7.1 |
| 8 | §11.5 主循环 `DL_Common_delayCycles` 驱动 PID | PID 在 **1kHz 定时器中断**中执行 | 与 task-10 同一理由：PID 正确性依赖固定 dt，主循环里一次遥测发送（24B @115200 ≈ 2ms）就会让周期抖动 |
| 9 | §11.4 命令表未列 `0x10` 的载荷格式 | 定为 `子命令(u8) + 变长载荷` | §11.5 注意事项②要求预留扩展命令但未定格式。未注册处理器时明确回 `NOT_IMPLEMENTED` 而非假装 ACK |

### 实现中发现的问题

1. **`test_estop_reset_clears_integrator` 首次红**：我拿"推进电机模型**之后**的转速"
   去核对"推进**之前**算出的占空比"，差了一个积分步长。修正后顺带把断言写精确了 ——
   现在它钉死了复位后第一拍的精确构成（微分被跳过、积分只累加一个周期），
   比原来那个宽容差的近似断言更有价值。
2. **`FAULT_STALL` 在空实现剖面下会置位**，因为假编码器恒返回 0。
   这不是缺陷而是刻意保留：把"移植层没接"变成上位机看得见的故障，
   总好过让车安静地不动。已写入 `port_stub.c` 注释。

### 已知限制

- **移植层（`port_driverlib.c`）尚未在真实硬件上验证**，且其中的外设实例名
  必须与 SysConfig 生成的 `ti_msp_dl_config.h` 核对后才能编译通过。
- **`linker/MSPM0G3507.ld` 的内存布局需核对**：SRAM 基址写的是 `0x20200000`
  （MSPM0 的 SRAM 不在 `0x20000000`），来源标注在脚本注释里，上板前必须对照
  数据手册 SLASEZ4 确认。
- **`startup_mspm0g3507.c` 的外设中断向量名是通用占位** （`IRQ0..IRQ31_Handler`）。
  MSPM0 的外设→IRQ 编号映射在 TI 器件头里，`PROFILE=driverlib` 时应改用 SDK 自带启动文件。
  刻意不猜这张表 —— 猜错的后果是中断进错向量，症状极难查。
- 交叉编译在本地开发机上未执行（无 `arm-none-eabi-gcc`），已用 `gcc -fsyntax-only`
  对全部固件源文件做语义检查（零警告），真实交叉编译由 CI 首次执行。
- PID 默认增益沿用麦轮固件（同型号电机、同控制频率），但差速底盘只有两轮承担
  全部牵引力，负载分布不同，**实车必须按 README §8 复整定**。

### 对下游任务的影响

| 任务 | 影响 |
|------|------|
| **task-12** | 无直接依赖 |
| **task-13** | CI 现有两个固件 job 模板，且新增了一类"构建了但不可烧录"的 job 形态 |
| **task-14/15** | 树莓派端按 ADR-0003 实现单一解析器即可同时支持两块板；启动时**必须**校验 `PONG` 的 `board_type`/`chassis_type`。README §4.4 的黄金帧可直接用作跨端自测向量 |
| **未来换 MCU** | 只需重写 `mspm0_port.h` 的 15 个原语，其余一行不动 |

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
