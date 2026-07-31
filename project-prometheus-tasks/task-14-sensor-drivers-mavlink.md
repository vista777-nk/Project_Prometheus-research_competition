# Task-14: 实机传感器驱动骨架 + MAVLink 2 签名

> **状态：✅ 已完成（2026-07-31）** | **优先级：🥉 中** | **实际耗时：~2.5h**
>
> **适用环境**：任意 OS（驱动骨架为纯 Python 代码，MAVLink 签名为 Python 脚本）
> **硬件依赖**：无（骨架代码通过接口抽象 + Mock 测试验证）
> **ROS 依赖**：Python 3 + rospy（CI 中导入检查，不运行完整 ROS）
>
> ⚠ **执行前必读**：本文档写于 2026-07-28。落地时发现文档给的**三处接口
> 与仓库里已经跑起来的仿真契约对不上**（IMU 话题、超声波消息类型、雷达角度范围），
> 且 §14B.5 的签名自测脚本断言恒为假。这几处照抄的话 CI 会全绿、实机会哑。
> 请先读文末的[「与原方案的偏差」](#与原方案的偏差2026-07-31-实际执行)再动手。
> 决策依据见 [ADR-0009](../docs/decisions/ADR-0009.md) 与 [ADR-0010](../docs/decisions/ADR-0010.md)。

---

## 前置条件

- 了解本项目 ICD.md §二 的接口定义（Observation / RobotState）
- 了解 PLATFORM.md §一 的实机传感器配置
- 了解 MAVLink 2 协议基础（消息签名机制）
- **不需要**：真实传感器硬件、Ubuntu 20.04、Gazebo

---

## 目标

为 Phase 1 实机部署准备两件关键的基础设施：

### Part A: 实机传感器驱动骨架

为 4 类车机传感器编写"接口正确、逻辑可测、待实机填硬件访问"的 ROS Node 骨架：

| 传感器 | 接口 | 驱动骨架 Node | 仿真对照 |
|--------|------|---------------|----------|
| RPLIDAR A1 | UART (115200) | `rplidar_driver.py` | Gazebo `libgazebo_ros_laser.so` |
| ICM42688 | I2C (0x68) | `icm42688_driver.py` | Gazebo `libgazebo_ros_imu.so` |
| HC-SR04 ×4 | GPIO | `hcsr04_driver.py` | Gazebo `libgazebo_ros_ultrasonic.so` |
| OpenMV 云台 | UART (串口) | `openmv_bridge.py` | 无仿真对照（Phase 0 未建模） |

**骨架的含义**：
- 接口层（ROS topic 发布 / 参数读取）→ **完整实现**
- 硬件访问层（UART/I2C/GPIO 读写）→ **抽象为 `HardwareInterface` 类，提供 Mock 实现**
- 数据处理层（协议解析 / 单位转换）→ **完整实现**

### Part B: MAVLink 2 消息签名配置

1. 密钥生成脚本（随机 32-byte 密钥）
2. MAVLink 签名模式参数模板（`.params` 文件）
3. MAVROS 签名验证配置
4. 签名正确性自测脚本

---

## 架构影响

| 维度 | 内容 |
|------|------|
| **Affected Capability** | Perception: 2D LiDAR / IMU / Ultrasonic / OpenMV 实机驱动 · Communication: MAVLink 2 签名安全 |
| **Modified Interface** | 新增 `HardwareInterface` ABC (UART/I2C/GPIO) — 定义 Layer 1↔Layer 2 的硬件抽象边界 · 新增 MAVLink 签名配置层 |
| **New Dependency** | `pymavlink` (签名自测) · OpenCV (RPLIDAR 可选) · `openssl` (密钥生成) |
| **ADR Required** | ADR-0009: 引入 HardwareInterface ABC 抽象层的设计理由 · ADR-0010: MAVLink 2 签名启用时机选择 |
| **Risk Level** | 🟡 Medium — 传感器驱动涉及真实硬件时序，但骨架 + Mock 已将风险隔离在 HardwareInterface 层 |

> **铁律回顾 (RESEARCH_PHILOSOPHY.md §三「Everything Produces Knowledge」)**：  
> 传感器驱动输出的是 `Observation` (Knowledge)，不是 `Image` (Raw Data)。  
> 换一个 LiDAR 型号，只要实现了同一个 `HardwareInterface`，`car_preprocessor.py` 无需改动。

## 未来演进

| 维度 | 今天 (Phase 1) | 明天 (Phase 2+) |
|------|---------------|-----------------|
| **Replaceable Component** | RPLIDAR A1 · ICM42688 · HC-SR04 · OpenMV | RPLIDAR S2 · BMI270 · TFmini Plus · OAK-D Lite |
| **Permanent Interface** | `HardwareInterface` ABC · `/car/scan` (LaserScan) · `/car/imu` (Imu) · `/car/ultrasonic/*` (Range) · `/car/openmv/detections` (String) | 保持不变 — 换传感器只需写新的 Driver，不改 Preprocessor |
| **Temporary Implementation** | 骨架代码中硬件访问留空 (Mock only) · MAVLink 签名仅自测 | v2: 真实 UART/I2C/GPIO 实现 · MAVLink 签名在实机上启用 · 传感器时间戳硬件同步 (PTP) |

---

## Part A: 传感器驱动骨架

### 14A.1 目录位置

> **重要**：驱动骨架属于 Layer 2 Bridge（协议翻译层）——将硬件传感器协议翻译为 ROS 标准话题。  
> 存放于 `air_ground_car_bringup` 包中（与现有仿真节点同包，通过 launch 参数切换仿真/实机模式）。  
> **注意与 ICD 的关系**：ICD §五 声明 Layer 1 原始话题 (`/car/scan`, `/car/imu`) "不在接口稳定承诺范围内"。  
> 驱动骨架发布的是 Layer 1 话题，`car_preprocessor.py` 将它们聚合为 ICD 承诺稳定的 Layer 3 `Observation.msg`。  
> 因此换传感器时，`car_preprocessor.py` 不需改动（它只消费 `Observation`）。

```
src/air_ground_car_bringup/
├── scripts/
│   ├── car_preprocessor.py      # 已有 (仿真)
│   ├── mecanum_controller.py    # 已有 (仿真)
│   ├── chassis_swapper.py       # 已有
│   ├── gimbal_controller.py     # 已有
│   ├── rplidar_driver.py        # 新增 ←
│   ├── icm42688_driver.py       # 新增 ←
│   ├── hcsr04_driver.py         # 新增 ←
│   └── openmv_bridge.py         # 新增 ←
├── config/
│   └── real_sensors.yaml        # 新增: 实机传感器参数
├── launch/
│   └── car_edge_real.launch     # 新增: 实机模式 launch
└── test/
    ├── test_rplidar_driver.py   # 新增
    ├── test_icm42688_driver.py  # 新增
    └── test_hcsr04_driver.py    # 新增
```

### 14A.2 硬件抽象层设计

定义一个统一的 `HardwareInterface` 抽象基类，所有驱动骨架依赖它：

```python
# src/air_ground_car_bringup/scripts/hardware_interface.py
"""硬件抽象层：隔离传感器驱动的 ROS 逻辑与物理硬件访问。

实机部署时替换为真实 I2C/UART/GPIO 实现；
CI/仿真时使用 Mock 实现。
"""

from abc import ABC, abstractmethod
from typing import List, Optional


class UARTInterface(ABC):
    """通用 UART 接口 (RPLIDAR, OpenMV 共用)."""

    @abstractmethod
    def open(self, port: str, baudrate: int) -> bool:
        """打开串口，返回是否成功."""
        ...

    @abstractmethod
    def read(self, n: int, timeout_ms: float) -> bytes:
        """读取 n 字节，超时返回已读取的部分."""
        ...

    @abstractmethod
    def write(self, data: bytes) -> int:
        """写入数据，返回写入字节数."""
        ...

    @abstractmethod
    def close(self) -> None:
        ...


class I2CInterface(ABC):
    """通用 I2C 接口 (ICM42688)."""

    @abstractmethod
    def open(self, bus: int, address: int) -> bool:
        ...

    @abstractmethod
    def read_register(self, reg: int, length: int) -> bytes:
        ...

    @abstractmethod
    def write_register(self, reg: int, data: bytes) -> None:
        ...

    @abstractmethod
    def close(self) -> None:
        ...


class GPIOInterface(ABC):
    """通用 GPIO 接口 (HC-SR04)."""

    @abstractmethod
    def setup(self, pin: int, direction: str) -> None:
        """direction: 'in' | 'out'."""
        ...

    @abstractmethod
    def write(self, pin: int, value: int) -> None:
        ...

    @abstractmethod
    def read(self, pin: int) -> int:
        ...

    @abstractmethod
    def pulse_in(self, pin: int, state: int, timeout_us: float) -> float:
        """测量脉冲宽度 (HC-SR04 核心方法)."""
        ...
```

**Mock 实现**（测试用）：

```python
# src/air_ground_car_bringup/test/mock_hardware.py
"""Mock 硬件接口，用于 CI 测试。提供可控的假数据."""

from scripts.hardware_interface import UARTInterface, I2CInterface, GPIOInterface


class MockUART(UARTInterface):
    def __init__(self, fake_data: bytes = b""):
        self.fake_data = fake_data
        self.written = b""
        self.is_open = False

    def open(self, port, baudrate):
        self.is_open = True
        return True

    def read(self, n, timeout_ms=100):
        data = self.fake_data[:n]
        self.fake_data = self.fake_data[n:]
        return data

    def write(self, data):
        self.written += data
        return len(data)

    def close(self):
        self.is_open = False


class MockI2C(I2CInterface):
    def __init__(self):
        self.registers = {}

    def open(self, bus, address):
        return True

    def read_register(self, reg, length):
        return self.registers.get(reg, b'\x00' * length)

    def write_register(self, reg, data):
        self.registers[reg] = data

    def close(self):
        pass


class MockGPIO(GPIOInterface):
    def __init__(self, pulse_durations: dict = None):
        self.pulse_durations = pulse_durations or {}

    def setup(self, pin, direction):
        pass

    def write(self, pin, value):
        pass

    def read(self, pin):
        return 0

    def pulse_in(self, pin, state, timeout_us):
        return self.pulse_durations.get(pin, 0.0)
```

### 14A.3 RPLIDAR A1 驱动骨架 (`rplidar_driver.py`)

```python
#!/usr/bin/env python3
"""RPLIDAR A1 驱动骨架 — 接口正确、待实机填 hardware_interface。

发布: /car/scan (sensor_msgs/LaserScan)
参数: ~port (default /dev/ttyUSB0), ~baudrate (default 115200)
"""

import rospy
from sensor_msgs.msg import LaserScan
from hardware_interface import UARTInterface


class RPLidarDriver:
    """RPLIDAR A1 / A2 驱动。通过依赖注入 UARTInterface 解耦硬件。"""

    # RPLIDAR A1 规格
    ANGLE_MIN = 0.0          # rad
    ANGLE_MAX = 6.28318      # rad (360°)
    ANGLE_INCREMENT = 0.01745  # rad (1°)
    RANGE_MIN = 0.15         # m
    RANGE_MAX = 12.0         # m
    SCAN_FREQ = 10.0         # Hz

    def __init__(self, uart: UARTInterface):
        self.uart = uart
        self.pub = rospy.Publisher('/car/scan', LaserScan, queue_size=10)

        # 从参数服务器读取配置
        self.port = rospy.get_param('~port', '/dev/ttyUSB0')
        self.baudrate = rospy.get_param('~baudrate', 115200)

    def start(self):
        """启动雷达电机 + 开始扫描.
        
        实机 TODO:
            1. uart.open(port, baudrate)
            2. 发送启动扫描命令 (0xA5 0x20)
            3. 等待响应头 (0xA5 0x5A)
            4. 进入数据读取循环
        """
        if not self.uart.open(self.port, self.baudrate):
            rospy.logerr(f"Failed to open RPLIDAR on {self.port}")
            return

        rospy.loginfo(f"RPLIDAR started on {self.port}")
        rate = rospy.Rate(self.SCAN_FREQ)

        # 当前用空 scan 占位，实机替换为真实解析循环
        while not rospy.is_shutdown():
            scan = self._read_one_scan()
            if scan:
                self.pub.publish(scan)
            rate.sleep()

    def _read_one_scan(self):
        """读取一圈扫描数据 → LaserScan. 
        
        实机 TODO: 解析 RPLIDAR 二进制协议.
        当前: 返回空 scan 骨架 (360 个 max_range 值).
        """
        scan = LaserScan()
        scan.header.stamp = rospy.Time.now()
        scan.header.frame_id = "car/lidar_link"
        scan.angle_min = self.ANGLE_MIN
        scan.angle_max = self.ANGLE_MAX
        scan.angle_increment = self.ANGLE_INCREMENT
        scan.range_min = self.RANGE_MIN
        scan.range_max = self.RANGE_MAX
        scan.ranges = [self.RANGE_MAX] * 360  # 占位
        return scan

    def stop(self):
        self.uart.close()


if __name__ == '__main__':
    rospy.init_node('rplidar_driver')
    # 实机: from real_hardware import RealUART
    # 仿真/测试: from mock_hardware import MockUART
    from mock_hardware import MockUART
    driver = RPLidarDriver(MockUART())
    driver.start()
```

### 14A.4 ICM42688 IMU 驱动骨架 (`icm42688_driver.py`)

```python
#!/usr/bin/env python3
"""ICM42688-P 6-axis IMU 驱动骨架.

发布: /car/imu (sensor_msgs/Imu)
参数: ~bus (default 1), ~address (default 0x68)
"""

import rospy
from sensor_msgs.msg import Imu
from hardware_interface import I2CInterface


class ICM42688Driver:
    # ICM42688 寄存器地址 (参考数据手册)
    WHO_AM_I  = 0x75
    ACCEL_XH  = 0x1F
    GYRO_XH   = 0x25
    # 默认量程
    ACCEL_SCALE = 16384.0  # ±2g → LSB/g
    GYRO_SCALE  = 131.0    # ±250dps → LSB/(deg/s)

    def __init__(self, i2c: I2CInterface):
        self.i2c = i2c
        self.pub = rospy.Publisher('/car/imu', Imu, queue_size=10)
        self.bus = rospy.get_param('~bus', 1)
        self.address = rospy.get_param('~address', 0x68)

    def start(self):
        if not self.i2c.open(self.bus, self.address):
            rospy.logerr(f"Failed to open I2C bus {self.bus} addr 0x{self.address:02X}")
            return

        # 芯片 ID 校验
        whoami = self.i2c.read_register(self.WHO_AM_I, 1)
        if whoami[0] != 0x47:  # ICM42688-P 预期值
            rospy.logwarn(f"Unexpected WHO_AM_I: 0x{whoami[0]:02X}, expected 0x47")

        rospy.loginfo("ICM42688 initialized")
        rate = rospy.Rate(100)  # 100Hz

        while not rospy.is_shutdown():
            imu = self._read_imu()
            if imu:
                self.pub.publish(imu)
            rate.sleep()

    def _read_imu(self):
        """读取 IMU 数据 → Imu 消息.

        实机 TODO: 从 I2C 寄存器读取原始值并转换为 SI 单位.
        """
        imu = Imu()
        imu.header.stamp = rospy.Time.now()
        imu.header.frame_id = "car/imu_link"
        # 占位: 无旋转，仅重力
        imu.orientation.w = 1.0
        imu.linear_acceleration.z = 9.81
        return imu

    def stop(self):
        self.i2c.close()
```

### 14A.5 HC-SR04 超声波驱动骨架 (`hcsr04_driver.py`)

```python
#!/usr/bin/env python3
"""HC-SR04 超声波传感器驱动骨架 (4 路).

发布: /car/ultrasonic (sensor_msgs/Range ×4) 或自定义 array
"""

import rospy
from sensor_msgs.msg import Range
from hardware_interface import GPIOInterface


class HCSR04Driver:
    # HC-SR04 规格
    SPEED_OF_SOUND = 343.0  # m/s
    TIMEOUT_MS = 30         # 超时 ≈ 5m
    PINS = {
        'front':  {'trig': 17, 'echo': 27},
        'rear':   {'trig': 22, 'echo': 23},
        'left':   {'trig': 5,  'echo': 6},
        'right':  {'trig': 13, 'echo': 19},
    }

    def __init__(self, gpio: GPIOInterface):
        self.gpio = gpio
        self.pubs = {}
        for name in self.PINS:
            self.pubs[name] = rospy.Publisher(
                f'/car/ultrasonic/{name}', Range, queue_size=5)

    def start(self):
        for name, pins in self.PINS.items():
            self.gpio.setup(pins['trig'], 'out')
            self.gpio.setup(pins['echo'], 'in')

        rospy.loginfo("HC-SR04 ×4 initialized")
        rate = rospy.Rate(20)  # 20Hz

        while not rospy.is_shutdown():
            for name, pins in self.PINS.items():
                distance = self._measure(pins['trig'], pins['echo'])
                msg = Range()
                msg.header.stamp = rospy.Time.now()
                msg.header.frame_id = f"car/ultrasonic_{name}_link"
                msg.radiation_type = Range.ULTRASOUND
                msg.field_of_view = 0.26  # ~15°
                msg.min_range = 0.02
                msg.max_range = 4.0
                msg.range = distance
                self.pubs[name].publish(msg)
            rate.sleep()

    def _measure(self, trig_pin: int, echo_pin: int) -> float:
        """发送 10μs 脉冲，测量回波时间 → 距离 (m).

        实机 TODO: GPIO 时序控制.
        """
        self.gpio.write(trig_pin, 0)
        rospy.sleep(0.000002)  # 2μs
        self.gpio.write(trig_pin, 1)
        rospy.sleep(0.000010)  # 10μs
        self.gpio.write(trig_pin, 0)

        duration_us = self.gpio.pulse_in(echo_pin, 1, self.TIMEOUT_MS * 1000)
        distance = (duration_us / 1_000_000.0) * self.SPEED_OF_SOUND / 2.0
        return min(max(distance, 0.02), 4.0)

    def stop(self):
        pass
```

> **⚠ 架构决策点 (ADR-0013 待定)**： 上例中 `rospy.sleep(0.000002)` 在 Linux 非实时内核上实际精度为毫秒级（~1-10ms），远不足以产生 10μs 触发脉冲。HC-SR04 的微秒级时序有以下方案：
>
> | 方案 | 适用场景 | 决策 |
> |------|---------|:---:|
> | **A: pigpio 硬件定时** | Pi GPIO 直接控制，需安装 pigpiod 守护进程 | 🔄 待定 |
> | **B: 挂到 MCU 上** | 超声波 Trig/Echo 接 STM32/MSPM0，距离值随 TELEMETRY 帧上报（需扩展遥测帧 Data 字段） | 🔄 待定（推荐, 与 task-10/11 的遥测布局联动） |
> | **C: 专用超声波模块 (I2C/UART)** | 如 JSN-SR04T 通过 UART 输出，无需 GPIO 时序 | 🔄 待定 |
>
> **Phase 1 处理**：骨架代码保留 Python 方案示意，实机阶段根据 ADR-0013 决策选择方案 A/B/C。子任务不要求在 Phase 1 解决此问题。

### 14A.6 OpenMV 云台桥接骨架 (`openmv_bridge.py`)

```python
#!/usr/bin/env python3
"""OpenMV 云台相机桥接骨架.

接收 OpenMV 通过 UART 发来的目标检测结果 (JSON)，
转发为 ROS topic (/car/openmv/detections).
"""

import rospy
import json
from std_msgs.msg import String
from hardware_interface import UARTInterface


class OpenMVBridge:
    def __init__(self, uart: UARTInterface):
        self.uart = uart
        self.pub = rospy.Publisher('/car/openmv/detections', String, queue_size=10)
        self.port = rospy.get_param('~port', '/dev/ttyAMA2')
        self.baudrate = rospy.get_param('~baudrate', 115200)

    def start(self):
        if not self.uart.open(self.port, self.baudrate):
            rospy.logerr(f"Failed to open OpenMV on {self.port}")
            return

        rospy.loginfo("OpenMV bridge started")
        buf = b""

        while not rospy.is_shutdown():
            data = self.uart.read(256, timeout_ms=100)
            if data:
                buf += data
                # 帧分隔符 '\n'
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    self._process_line(line)

    def _process_line(self, line: bytes):
        """解析 OpenMV JSON 检测结果.
        
        预期格式: {"objects":[{"label":"red_ball","x":120,"y":80,"w":30,"h":30,"conf":0.95}]}
        """
        try:
            obj = json.loads(line.decode('utf-8', errors='replace'))
            self.pub.publish(String(data=json.dumps(obj)))
        except (json.JSONDecodeError, UnicodeDecodeError):
            rospy.logwarn_throttle(10, f"OpenMV: invalid JSON: {line[:60]}")

    def stop(self):
        self.uart.close()
```

### 14A.7 单元测试 (`test_rplidar_driver.py`)

```python
#!/usr/bin/env python3
"""RPLIDAR 驱动骨架单元测试 — 验证接口正确性，无需硬件."""

import unittest


class TestRPLidarDriver(unittest.TestCase):

    def test_scan_message_structure(self):
        """验证 LaserScan 消息结构符合 RPLIDAR A1 规格."""
        from scripts.rplidar_driver import RPLidarDriver
        from mock_hardware import MockUART

        driver = RPLidarDriver(MockUART())
        scan = driver._read_one_scan()

        self.assertEqual(len(scan.ranges), 360)
        self.assertAlmostEqual(scan.angle_min, 0.0)
        self.assertAlmostEqual(scan.angle_max, 6.28318, places=4)
        self.assertEqual(scan.range_max, 12.0)

    def test_uart_open_fails_gracefully(self):
        """串口打开失败时不应崩溃."""
        from scripts.rplidar_driver import RPLidarDriver
        from mock_hardware import MockUART

        class FailingUART(MockUART):
            def open(self, port, baudrate):
                return False

        driver = RPLidarDriver(FailingUART())
        # 不应该抛异常 (在 ROS 环境外可能因为 rospy 报错, CI 中 mock rospy)
        self.assertFalse(driver.uart.is_open)


if __name__ == '__main__':
    unittest.main()
```

---

## Part B: MAVLink 2 消息签名

### 14B.1 背景

MAVLink 2 支持消息签名（Message Signing），防止：
- 数传链路劫持（注入恶意 MAVLink 消息）
- 重放攻击
- 未授权地面站控制无人机

**Phase 1 启用签名的时机**：实机首次飞行前（目前只需准备好脚本和参数模板）。

### 14B.2 密钥生成脚本

```bash
# src/deployment/mavlink/generate-mavlink-key.sh
#!/bin/bash
# 生成 32 字节随机 MAVLink 签名密钥
# 用法: ./generate-mavlink-key.sh
# 输出: 密钥文件 (16 进制) — 不提交到仓库！

set -e

KEYFILE="mavlink_secret.key"
if [ -f "$KEYFILE" ]; then
    echo "ERROR: $KEYFILE already exists. Delete it manually if you want to regenerate."
    echo "WARNING: Regenerating the key will break communication with all existing peers."
    exit 1
fi

# 生成 32 字节随机密钥
openssl rand -hex 32 > "$KEYFILE"
chmod 600 "$KEYFILE"

echo "MAVLink signing key generated: $KEYFILE"
echo "HEX: $(cat $KEYFILE)"
echo ""
echo "IMPORTANT:"
echo "1. Copy this key to ALL peers (drone Pixhawk + car Pi + ground station)"
echo "2. Do NOT commit this file to Git"
echo "3. Store a backup in a secure offline location"
```

### 14B.3 PX4 签名参数模板

```bash
# src/deployment/mavlink/px4-signing.params
# PX4 MAVLink 2 签名参数 — 通过 QGroundControl 或 param load 导入

# 启用 MAVLink 2 (必须)
MAV_0_CONFIG=101      # TELEM1 port
MAV_0_MODE=2          # Normal (MAVLink 2)
MAV_0_RATE=57600      # 3DR SiK 默认波特率

# 签名参数
MAV_0_SIGNING=1       # Enable signing on TELEM1
# MAV_0_SIGN_KEY — 由地面站通过安全通道设置，不写在参数文件中
```

### 14B.4 MAVROS 签名验证配置

```yaml
# src/air_ground_drone_bringup/config/mavros_signing.yaml
# MAVROS 签名配置 — 实机部署时使用

mavros:
  signing:
    enabled: true
    # signing_key: !env MAVLINK_SIGN_KEY  # 从环境变量读取，不硬编码
    allow_unsigned: false         # 实机: 拒绝未签名消息
    accept_unsigned_cmd: false    # 实机: 拒绝未签名命令
```

### 14B.5 签名正确性自测脚本

```python
#!/usr/bin/env python3
# src/deployment/mavlink/test-mavlink-signing.py
"""MAVLink 签名自测 — 验证签名/验签流程，无需真实飞控."""

import os
import sys
from pymavlink import mavutil


def test_signing_roundtrip():
    """生成密钥 → 签名 → 验签 往返测试 (黑盒断言: 签名后帧长增量)."""
    secret_key = os.urandom(32)

    # 创建签名连接
    mav = mavutil.mavlink.MAVLink(srcSystem=1, srcComponent=1, use_native=False)
    mav.signing.secret_key = secret_key

    # 签名一条心跳消息
    msg = mav.heartbeat_encode(
        type=mavutil.mavlink.MAV_TYPE_QUADROTOR,
        autopilot=mavutil.mavlink.MAV_AUTOPILOT_PX4,
        base_mode=0, custom_mode=0, system_status=0
    )
    # 未签名前帧长
    unsigned_len = len(msg.pack(mav))

    # 启用签名后帧长
    mav.signing.secret_key = secret_key
    signed_msg = msg.pack(mav)
    signed_len = len(signed_msg)

    # 黑盒断言: 签名后帧长 = 原始帧长 + 13 (MAVLink 2 签名开销)
    assert signed_len == unsigned_len + 13, \
        f"Signature overhead mismatch: {signed_len} != {unsigned_len} + 13"

    # 验签往返: 用同一密钥解包, 不应报签名错误
    try:
        mav2 = mavutil.mavlink.MAVLink(srcSystem=1, srcComponent=1, use_native=False)
        mav2.signing.secret_key = secret_key
        decoded = mav2.decode(signed_msg)
        assert decoded is not None, "Decode failed after signing roundtrip"
    except Exception as e:
        assert False, f"Signing roundtrip failed: {e}"

    print("✓ MAVLink 2 签名往返测试通过 (黑盒)")
    print(f"   未签名帧长: {unsigned_len} bytes")
    print(f"   签名后帧长: {signed_len} bytes (+13)")
    return True


if __name__ == '__main__':
    try:
        test_signing_roundtrip()
        sys.exit(0)
    except Exception as e:
        print(f"✗ 测试失败: {e}", file=sys.stderr)
        sys.exit(1)
```

### 14B.6 `.gitignore` 补充

确保密钥文件不会误提交：

```gitignore
# MAVLink signing keys (CRITICAL: never commit!)
mavlink_secret.key
*_secret.key
*.secret
```

---

## 验收标准

> 勾选状态为 2026-07-31 实际执行结果。带 ⚠ 的条目做了偏差处理，见文末。

### Part A 传感器骨架

- [x] 4 个驱动骨架文件语法正确（`python3 -m py_compile` 通过）
- [x] `hardware_interface.py` 抽象基类定义完整（3 个 ABC）
- [x] `mock_hardware.py` 提供所有 3 个接口的 Mock 实现（改放 `scripts/`，见偏差 5）
- [x] RPLIDAR 骨架可发布结构正确的 `LaserScan`（360 个 range） ⚠ 角度范围 `-π..π` 而非 `0..2π`（偏差 1）
- [x] ICM42688 骨架可发布结构正确的 `Imu` ⚠ 话题 `/car/imu/data`（偏差 1）
- [x] HC-SR04 骨架可发布 4 路测距 ⚠ 消息类型为 `LaserScan` 而非 `Range`（偏差 1）
- [x] OpenMV 骨架可解析 JSON 并发布
- [x] 至少 1 个骨架有单元测试 — 实际 4 个驱动 + 1 份契约测试，**69 个用例**

### Part B MAVLink 签名

- [x] `generate-mavlink-key.sh` 可生成 32-byte 密钥，权限 600 ⚠ 默认路径移出仓库（偏差 8）
- [x] `test-mavlink-signing.py` 往返测试通过 — **5/5**（原文脚本断言恒为假，见偏差 6）
- [x] 密钥文件已在 `.gitignore` 中排除（三道防线，ADR-0010 §决策-4）
- [x] PX4 签名参数模板语法正确 ⚠ **签名段刻意留空** —— 参数名未核实（偏差 7）

---

## ⓘ 优化建议（混元3 评审）

1. **参数配置外置化**：传感器量程、频率、I2C 地址等参数从硬编码迁移至 YAML 配置文件（`config/real_sensors.yaml`），与仿真端 `car_sensors.yaml` 格式对齐，方便不同硬件型号快速适配。
2. **时间同步机制**：多传感器时钟不同步是实机感知的核心痛点。建议明确时间戳基准统一为 ROS 系统时间（`rospy.Time.now()`），未来若需硬件同步可引入 PPS（Pulse Per Second）信号。
3. **异常恢复机制**：UART 断开、I2C 挂死等硬件异常场景下，驱动应实现自动重连（UART: 间隔 2s 重试 `open()`；I2C: 发送 STOP 条件后重新 START），并 `rospy.logwarn_throttle` 告警，不静默失败。
4. **OpenMV 协议字段明确**：目标检测 JSON 的字段定义需与 ICD Observation 语义对齐：`{"objects":[{"label":str, "x":int, "y":int, "w":int, "h":int, "conf":float}]}`。坐标系：图像左上角为原点，x 向右，y 向下。
5. **MAVLink 密钥分发流程**：密钥通过 SSH 安全复制到 Pi（`scp mavlink_secret.key pi@192.168.1.x:/etc/air-ground/`），chmod 600；Pixhawk 端通过 QGroundControl 安全通道设置 `MAV_0_SIGN_KEY` 参数。密钥定期轮换（建议每季度）需同步更新所有节点。

---

## 给 Subagent 的执行建议

1. **驱动骨架的核心价值在接口设计**，不在硬件访问代码。把 `HardwareInterface` ABC 做好就是成功
2. **所有骨架必须通过 `python3 -m py_compile` 无语法错误**（CI lint job 会检查）
3. **Mock 实现必须覆盖驱动中用到的所有硬件操作方法**，否则 CI 测试无法运行
4. **MAVLink 签名密钥生成后立即 `.gitignore`**，这是安全红线
5. **驱动骨架中的 `if __name__ == '__main__'` 段用 Mock，不依赖 ROS**：方便在任意 OS 上快速验证
6. **OpenMV 的 JSON 协议格式与电赛队伍协商**：目前用通用格式占位，实机需对齐
7. **CI 归属策略**：驱动骨架测试由 `lint-scripts` job 的子步骤执行 (`python3 -m py_compile` + `python3 -m pytest test/ --mock-rospy`)。**不在** ROS 容器的 `catkin test` 中运行（因为骨架 import rospy 但不依赖 roscore，mock 即可）。`test_rplidar_driver.py` 中的 `unittest.main()` 通过 `--mock-rospy` fixture 在纯 Python 环境执行。

---

## 与原方案的偏差（2026-07-31 实际执行）

本文档写于 2026-07-28。逐条记录偏差与理由 —— 决策依据见
[ADR-0009](../docs/decisions/ADR-0009.md)（Part A）与
[ADR-0010](../docs/decisions/ADR-0010.md)（Part B）。

### 偏差 1（Part A）：三处接口与仿真既有契约不一致，全部按仿真侧改

这是本次最重要的一条。三处都**不会有任何报错**：

| 项 | 本文档 | 仓库实际（仿真 + `car_preprocessor.py`） | 照文档做的后果 |
|---|---|---|---|
| IMU 话题 | `/car/imu` | `/car/imu/data`（`libgazebo_ros_imu_sensor.so` 的 topicName） | Observation 永远没有 imu 模态 |
| 超声波类型 | `sensor_msgs/Range` | `sensor_msgs/LaserScan`（单束射线） | 订阅端类型不匹配 → **永不连接** |
| 雷达角度 | `angle_min=0, angle_max=2π` | `-π .. π` | 整张地图沿前后轴镜像 |

判据是"哪个有下游"：预处理器已经在跑，Gazebo 插件已经在发，Phase 0 端到端已经绿。
驱动骨架是新来的一方，职责是接上现有的管子。

并且这条契约不靠人记 —— `test/host/test_topic_contract.py` 交叉比对
`real_sensors.yaml` × `car_edge.yaml` × `car_sensors.yaml` ×
`car_preprocessor.py` 的 `DIRECTIONS` × URDF 的 link 名，任一处漂移当场红。

> `Range` 语义上确实更贴切，改它要连 URDF 插件 + 预处理器 + Phase 0 回归一起改，
> 是独立的一次重构。记为债务（ADR-0009 §影响）。

### 偏差 2（Part A）：`pulse_in` + `rospy.sleep` 换成一个 `trigger_and_measure`

§14A.5 的 `rospy.sleep(0.000010)` 在 Linux 非实时内核上实际精度是毫秒级，
与要求的 10μs **差 100–1000 倍**。文档自己在 §14A.5 下方也标了这个问题（ADR-0013 待定）。

处置不是"留着示意"，而是把整段 μs 时序压进 `GPIOInterface.trigger_and_measure()`
一个方法里，Python 层拿不到 write/sleep/pulse_in 这套半成品原语 ——
实现它的人必须真的解决时序（pigpio / MCU / UART 模块三选一，仍归 ADR-0013）。

### 偏差 3（Part A）：`real` 后端**拒绝构造**，不提供空实现

`create_uart('real')` 等直接 `raise NotImplementedError`。
一个 `open()` 永远返回 True、`read()` 永远返回空的后端，会让实机上的驱动
安静地发布全零数据 —— 就是 ADR-0008 反复记录的那类假绿灯的硬件版。

### 偏差 4（Part A）：参数进 YAML，且**测试直接读那份 YAML**

§14A.3~14A.6 的示例把量程、引脚、频率写成类常量。改为全部进
`config/real_sensors.yaml`（CONVENTIONS §4.3），并且 `test/host/fixtures.py`
直接加载仓库里那份 YAML，不在测试里另抄参数字典 ——
抄一份的话，测试验的是抄件，实机加载的是 YAML，漂移时测试照样全绿。

### 偏差 5（Part A）：目录与测试组织

| 文档 | 实际 | 理由 |
|---|---|---|
| `test/mock_hardware.py` | `scripts/mock_hardware.py` | mock 是运行期后端（`backend:=mock` 无硬件冒烟），不只是测试设施 |
| `test/test_*.py` 平铺 | `test/host/` 子目录 | 与需要真 ROS 的既有 3 份测试分开：运行环境不同，混放会两边都跑不起来 |
| 只要求 1 份单测 | 5 份，69 个用例 | 协议解析/量纲换算/契约一致性都是纯逻辑，值得钉死 |
| — | 新增 `test/test_ros_stub_fidelity.py` | ROS 替身自己也要被验证：替身抄错字段名的话，驱动跟着写错，两边一致、测试全绿、实机哑 |

### 偏差 6（Part B）：§14B.5 的自测脚本断言恒为假

原文两次 `pack()` 之间只设了 `signing.secret_key`，没设 `sign_outgoing`。
实测（pymavlink 2.4.49）：

```
未签名 17 → 设 secret_key 后 17 → 原文断言 assert 17 == 17 + 13 → False
```

另外原文用的 `mavutil.mavlink` 是 MAVLink **1** 的 v10 方言，
解签名帧会报 `invalid MAVLink message length. Got 22 expected 9`（指错方向的报错）。

改正后并把断言从 1 条扩到 5 条 —— 只测"签名后长度 +13"证明不了签名有用，
一个把 13 个零字节贴在帧尾的实现也能过。实测全部通过：

| # | 断言 | 实测 |
|---|---|---|
| 1 | 签名开销恰好 13 字节 | 21 → 34 ✓ |
| 2 | 同密钥可解出 HEARTBEAT | ✓ |
| 3 | 错误密钥被拒 | `Invalid signature` ✓ |
| 4 | 重放被拒 | `Invalid signature` ✓ |
| 5 | 未签名帧被拒 | `Invalid signature` ✓ |

### 偏差 7（Part B）：签名参数名核实不了 → 不写进生效配置

§14B.3 的 `MAV_0_SIGNING=1` 与 §14B.4 的 `mavros/signing/*` **一个都没能核实**：
原文引用的 PX4 v1.14 签名文档页打不开，检索到的官方页面只在 `main`
（v1.18 时间线）路径下；本机无 ROS，`rosparam list` 跑不了。

处置：`px4-signing.params` 只保留能确定的传输参数，签名段留空并写明原因；
`mavros_signing.yaml` 整份标为"不被任何 launch 加载"的核实清单。

理由是这两类文件的失败方式一样 —— `param load` 会跳过未知参数并继续，
`rosparam` 会老实把没人读的键塞进参数服务器。两种情况下**配置文件的存在本身
成了"功能已启用"的假证据**。核实步骤已写进两个文件顶部，task-15 上机时处理。

### 偏差 8（Part B）：密钥默认路径移出仓库

§14B.2 的 `KEYFILE="mavlink_secret.key"` 写在当前目录 ——
在 `src/deployment/mavlink/` 里跑一次就往仓库里落一个密钥文件。
改为默认 `~/.config/air-ground/`，并显式拒绝往 Git 工作区写，
沿用 `generate-ssh-keys.sh` 的做法（第一道防线是脚本自己不往仓库写）。

> **这个脚本的冒烟测试抓到了它自己的两个 bug**，两个都属于"守卫恒不生效"：
>
> 1. `openssl rand -hex 32` 在 Git-Bash 下输出带 CRLF，那个 `\r`
>    会成为密钥内容的一部分 —— 开发机生成、Linux 上使用，两边密钥不一致，
>    症状是"密钥明明一样却验签失败"。
> 2. 第一版的仓库守卫拿 `git rev-parse --show-toplevel` 的输出与 `pwd`
>    做前缀比较。Git for Windows 下这两者格式不同（`e:/Vista/...` vs
>    `/e/Vista/...`），比较**恒不相等**，守卫恒不生效 —— 实测时它眼睁睁
>    把密钥写进了仓库。改用 `git rev-parse --is-inside-work-tree`。
>
> 记在这里是因为第 2 条正是本仓库反复出现的那个模式的又一例：
> 一段看起来在做检查、实际什么都拦不住的代码。它和 7/30 的
> `grep $'\r'`（MSYS 静默剥 CR）是同一个成因 —— **Windows 与 Linux 的
> 路径/行尾差异，会把守卫悄悄变成装饰**。写完守卫必须做一次负向测试。

### 偏差 9（CI）：§CI 归属策略按原文执行，但 Part B 挂 `validate.sh`

| 检查 | 归属 | 理由 |
|---|---|---|
| 驱动 `py_compile` + `pytest` | `lint-scripts` job（按原文） | 跑在 ROS 替身上，只要 python3 + pytest |
| 替身忠实度 | ROS 容器 `catkin test` | 只有那里有真 `sensor_msgs` |
| MAVLink 签名自测 | `validate.sh` §2 → `validate-deployment` job | 它验的是**部署配置**，而 `validate.sh` 已是部署配置的唯一入口 |

原文的 `pytest test/ --mock-rospy` 没有实现：`--mock-rospy` 需要自定义 pytest 选项，
而 `test/host/conftest.py` 在收集期装替身即可，不需要额外开关。

---

## 提交前本地实测

按 ADR-0008 §决策-1 的要求，凡设成阻塞的检查都在本地跑过全仓：

| 检查 | 工具与版本 | 范围 | 结果 |
|---|---|---|---|
| flake8 | 7.1.1（与 CI 同版） | 新增 17 个 `*.py`（连同同目录既有文件一并扫） | **0 条** |
| shellcheck | 0.10.0（与 CI 同版） | 全仓 23 个 `*.sh` | **0 条**（severity≥warning） |
| yamllint | 1.38.0（与 CI 同版） | 全仓 16 个 YAML | **0 条**（按 LF 规范化后的内容跑，见 Research_Diary 2026-07-31 §4） |
| py_compile | Python 3.13 | 7 个驱动骨架模块 | 全过 |
| pytest | 8.3.4 | `test/host/` | **69 passed** |
| MAVLink 签名自测 | pymavlink 2.4.49 | 5 条断言 | **5/5** |
| `validate.sh` | — | 全部 | 通过 29 · 失败 0 · 跳过 1（systemd-analyze，非 Linux） |
| 密钥脚本负向测试 | — | 3 例 | 拒绝写仓库（已有目录/不存在的子目录）✓ · 拒绝覆盖已有密钥 ✓ · 仓库外正常生成 64 hex ✓ |

**未在本地验证的**（诚实记录）：
`catkin build` / `catkin test`（本机无 ROS Noetic），因此
`test_ros_stub_fidelity.py` 与新增的 `catkin_install_python` 条目
以 CI 首轮输出为准；`car_edge_real.launch` 未真跑过 roslaunch；
密钥文件的 `0600` 权限在 Windows 上验不了（MSYS 不落实 chmod），
Linux/树莓派侧以 `umask 077` + `chmod 600` 双保险。

---

## 参考资料


| 文件 | 内容 |
|------|------|
| `src/air_ground_car_bringup/scripts/car_preprocessor.py` | 现有传感器预处理节点（仿真） |
| `src/air_ground_car_bringup/config/car_sensors.yaml` | 仿真传感器参数 |
| `project-prometheus-tasks/PLATFORM.md` §一 | 实机传感器清单 |
| `project-prometheus-tasks/ICD.md` §二 | Observation 消息定义 |
| `SECURITY.md` | MAVLink 签名安全要求 |
| [PX4 MAVLink Signing Docs](https://docs.px4.io/v1.14/en/advanced_config/mavlink_signing.html) | 官方签名文档 |

---

*版本: v1.0 · 日期: 2026-07-28 · Phase 1 · 传感器骨架 + MAVLink 安全*
