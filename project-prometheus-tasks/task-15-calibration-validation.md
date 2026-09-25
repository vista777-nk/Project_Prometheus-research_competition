# Task-15: IMU/相机标定脚本 + Phase 1 集成验证

> **状态：✅ 已完成 (2026-07-31)** | **优先级：🥉 中** | **实际耗时：~2.5h**
>
> **适用环境**：任意 OS（标定脚本为 Python，离线数据驱动）
> **硬件依赖**：无（标定脚本操作 ROS bag 文件，不依赖实时传感器）
> **ROS 依赖**：Python 3 + 标定工具链（Kalibr 等可在 Docker/CI 中验证安装）
>
> **落地与本文的偏差见文末 [§与原方案的偏差](#与原方案的偏差)** —— 有七处，
> 其中三处是本文与仓库现状直接冲突（话题名、输出格式、冒烟测试路径与 job 名）。
> 决策记录见 [ADR-0011](../docs/decisions/ADR-0011.md)。
>
> **2026-08-02 实机 BOM 覆盖**：无人机视觉是可换 D435i CB/双 Pi Camera；
> 车载 OpenMV/ICM42688/A2M12 是一套共享载荷。标定数据必须记录
> `vision_mode`、所装底盘、载荷安装位姿和硬件序列标识；换底盘或换视觉载荷后
> 不得复用旧外参。双 CSI 相机 profile 未确认前不关闭该模式验收。

---

## 前置条件

- 了解相机内参模型（pinhole + distortion）
- 了解 IMU 标定基础（accel bias / gyro bias / misalignment）
- 有 ROS bag 的基本概念
- **不需要**：真实相机、标定板、Ubuntu 20.04 GUI

---

## 目标

为 Phase 1 实机部署准备 **离线可验证** 的标定工具链和集成验证脚本：

### Part A: 标定脚本

1. **相机内参标定**：Intel RealSense D435i RGB + Depth（基于 Kalibr 或 OpenCV）
2. **IMU 标定**：ICM42688 / Pixhawk 6C 板载 IMU（基于 `imu_utils` 或 `allan_variance_ros`）
3. **相机-IMU 外参标定**：D435i RGB → IMU 的 `T_cam_imu`（基于 Kalibr）
4. **标定数据采集脚本**：自动化 bag 录制 + 格式转换

### Part B: Phase 1 集成验证

5. **固件通信回路测试**：树莓派 ↔ STM32/MSPM0 串口通信验证脚本
6. **传感器→Observation 数据流验证**：端到端验证传感器数据经过预处理后到达 World Model
7. **Phase 1 冒烟测试**：一键检查所有 Phase 1 交付物是否就位

---

## 架构影响

| 维度 | 内容 |
|------|------|
| **Affected Capability** | Perception: 相机内参 · IMU 内参 · 相机-IMU 外参 · DevOps: Phase 1 集成验证 |
| **Modified Interface** | 新增标定输出格式: `camera_intrinsics.yaml` → ROS `camera_info` · 新增 `smoke_test_phase1.sh` 冒烟测试入口 |
| **New Dependency** | OpenCV (`cv2.calibrateCamera`) · Kalibr (可选, 相机-IMU 外参) · `allan_variance_ros` (可选, IMU 标定) |
| **ADR Required** | ADR-0011: 标定结果 YAML 格式标准化 (可降级为 §15A.2 的设计段落, 不必独立 ADR) |
| **Risk Level** | 🟢 Low — 标定脚本操作离线数据，不依赖实时硬件 |

> **铁律回顾 (RESEARCH_PHILOSOPHY.md §四「Simulation is the First Robot」)**：  
> 标定脚本在仿真 bag 和实机 bag 上运行完全相同的代码。  
> 换一个相机型号，只需重新采集标定数据，脚本不变。

## 未来演进

| 维度 | 今天 (Phase 1) | 明天 (Phase 2+) |
|------|---------------|-----------------|
| **Replaceable Component** | OpenCV 棋盘格标定 · 手动标定数据采集 | Kalibr (AprilGrid) · OpenVINS 在线标定 · Kimera 语义标定 · Visual-Inertial Foundation Model 自标定 |
| **Permanent Interface** | `camera_intrinsics.yaml` 输出格式 · `validate-calibration.py` 合理性检查项 · `smoke_test_phase1.sh` 冒烟测试框架 | 保持不变 — 换标定算法只改采集+解算，不改验证和输出格式 |
| **Temporary Implementation** | 离线 Python 脚本 · 手动触发标定 · 无标定报告自动生成 | v2: ROS 节点内在线标定 · 标定报告自动 PDF 生成 (含 K Matrix / Distortion / RMS / Allan Variance / 日期 / 序列号) · `calibration_db/` 标定历史数据库 (不覆盖, 可对比长期漂移) |

---

## Part A: 标定脚本

### 15A.1 目录位置

```
src/deployment/calibration/
├── README.md                       # 标定流程总览 + 快速命令
├── record-calib-bag.sh             # 自动化标定数据采集
├── calibrate-camera.py             # 相机内参标定 (OpenCV)
├── calibrate-imu.py                # IMU 标定 (allan_variance)
├── calibrate-cam-imu-extrinsic.py  # 相机-IMU 外参标定
├── convert-bag-to-kalibr.py        # ROS bag → Kalibr 格式
├── validate-calibration.py         # 标定结果验证 (重投影误差)
└── test/
    ├── sample_calib_data/          # 小型离线测试数据 (5 张棋盘格图片 + 1min IMU)
    └── test_calib_pipeline.py      # CI 标定流水线测试
```

### 15A.2 标定数据采集脚本 (`record-calib-bag.sh`)

```bash
#!/bin/bash
# 自动化标定数据采集 — 实机上运行
# 用法: ./record-calib-bag.sh [camera|imu|both]

set -e

TYPE="${1:-both}"
BAG_DIR="${HOME}/calib_data/$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BAG_DIR"

record_camera() {
    echo "=== Camera Calibration Recording ==="
    echo "Instructions:"
    echo "  1. Hold the checkerboard (8x6, 30mm squares)"
    echo "  2. Move it slowly through all corners of the image"
    echo "  3. Tilt ±30° in roll, pitch, yaw"
    echo "  4. Record for ~2 minutes"
    echo ""
    echo "Press ENTER to start recording..."
    read -r

    rosbag record -O "${BAG_DIR}/camera_calib.bag" \
        /drone/rgb/image_raw \
        /drone/rgb/camera_info \
        /drone/depth/image_raw \
        __name:=calib_recorder
    # 注意: 仿真话题名为 /drone/rgb/image_raw，实机 D435i 通常为 /drone/color/image_raw
    # 如话题名不一致，请用 ROS remap 或修改本脚本中的话题名变量

    echo "Camera recording saved to: ${BAG_DIR}/camera_calib.bag"
}

record_imu() {
    echo "=== IMU Calibration Recording ==="
    echo "Instructions:"
    echo "  1. Place the IMU on a flat, stationary surface"
    echo "  2. Do NOT touch it for at least 2 hours (Allan variance needs long static data)"
    echo "  3. Or use the 15-min quick protocol (bias only)"
    echo ""
    echo "Select protocol: [quick(15min)|full(2hr)]"
    read -r PROTOCOL

    DURATION=$([ "$PROTOCOL" = "full" ] && echo "7200" || echo "900")

    rosbag record -O "${BAG_DIR}/imu_calib.bag" \
        /drone/imu/data_raw \
        /car/imu/data_raw \
        --duration="$DURATION" \
        __name:=calib_recorder

    echo "IMU recording saved to: ${BAG_DIR}/imu_calib.bag"
}

case "$TYPE" in
    camera) record_camera ;;
    imu)    record_imu ;;
    both)   record_camera; record_imu ;;
    *)      echo "Usage: $0 [camera|imu|both]" ;;
esac
```

### 15A.3 相机内参标定 (`calibrate-camera.py`)

```python
#!/usr/bin/env python3
"""相机内参标定 — 基于 OpenCV, 支持离线 bag 或图片目录.

用法: python3 calibrate-camera.py --input calib_data/camera_calib.bag
      python3 calibrate-camera.py --input calib_data/images/ --pattern 8x6
"""

import argparse
import glob
import os
import sys

import numpy as np
import cv2


class CameraCalibrator:
    """棋盘格相机标定."""

    def __init__(self, pattern_size=(8, 6), square_size=0.030):
        """
        Args:
            pattern_size: 棋盘格内角点数 (cols, rows)
            square_size: 方格边长 (m)
        """
        self.pattern_size = pattern_size
        self.square_size = square_size
        self.obj_points = []  # 3D 世界坐标
        self.img_points = []  # 2D 图像坐标

        # 生成棋盘格世界坐标
        self.pattern_3d = np.zeros((pattern_size[0] * pattern_size[1], 3), np.float32)
        self.pattern_3d[:, :2] = np.mgrid[0:pattern_size[0],
                                           0:pattern_size[1]].T.reshape(-1, 2)
        self.pattern_3d *= square_size

    def add_image(self, img: np.ndarray) -> bool:
        """添加一张棋盘格图片，检测角点."""
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        ret, corners = cv2.findChessboardCorners(gray, self.pattern_size, None)

        if not ret:
            return False

        # 亚像素精细化
        criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001)
        corners_sub = cv2.cornerSubPix(gray, corners, (11, 11), (-1, -1), criteria)

        self.obj_points.append(self.pattern_3d)
        self.img_points.append(corners_sub)
        return True

    def calibrate(self, image_size):
        """执行标定，返回 (K, dist, rvecs, tvecs)."""
        if len(self.obj_points) < 10:
            raise ValueError(f"Need at least 10 images, got {len(self.obj_points)}")

        ret, K, dist, rvecs, tvecs = cv2.calibrateCamera(
            self.obj_points, self.img_points, image_size, None, None
        )
        return K, dist, rvecs, tvecs, ret

    def reprojection_error(self, K, dist, rvecs, tvecs) -> float:
        """计算平均重投影误差."""
        total_error = 0
        total_points = 0
        for i in range(len(self.obj_points)):
            projected, _ = cv2.projectPoints(
                self.obj_points[i], rvecs[i], tvecs[i], K, dist
            )
            error = cv2.norm(self.img_points[i], projected, cv2.NORM_L2)
            total_error += error ** 2
            total_points += len(self.obj_points[i])
        return np.sqrt(total_error / total_points)


def main():
    parser = argparse.ArgumentParser(description="Camera intrinsic calibration")
    parser.add_argument('--input', required=True, help='Image directory')
    parser.add_argument('--pattern', default='8x6', help='Checkerboard pattern WxH')
    parser.add_argument('--square-size', type=float, default=0.030, help='Square size (m)')
    parser.add_argument('--output', default='camera_intrinsics.yaml', help='Output YAML')
    args = parser.parse_args()

    w, h = map(int, args.pattern.split('x'))
    calib = CameraCalibrator((w, h), args.square_size)

    # 读取图片
    exts = ('*.jpg', '*.jpeg', '*.png', '*.bmp')
    images = []
    for ext in exts:
        images.extend(glob.glob(os.path.join(args.input, ext)))
        images.extend(glob.glob(os.path.join(args.input, ext.upper())))

    if not images:
        print(f"ERROR: No images found in {args.input}", file=sys.stderr)
        sys.exit(1)

    image_size = None
    for path in sorted(images):
        img = cv2.imread(path)
        if img is None:
            continue
        if image_size is None:
            image_size = (img.shape[1], img.shape[0])
        if calib.add_image(img):
            print(f"  ✓ {os.path.basename(path)}")
        else:
            print(f"  ✗ {os.path.basename(path)} (no pattern found)")

    print(f"\nCalibrating with {len(calib.img_points)} images...")
    K, dist, rvecs, tvecs, ret = calib.calibrate(image_size)

    rpe = calib.reprojection_error(K, dist, rvecs, tvecs)

    # 输出结果
    print(f"\n=== Calibration Results ===")
    print(f"Camera matrix K:\n{K}")
    print(f"Distortion:\n{dist}")
    print(f"Reprojection error: {rpe:.4f} px")
    print(f"RMS: {ret:.4f}")

    # 保存 YAML
    fs = cv2.FileStorage(args.output, cv2.FILE_STORAGE_WRITE)
    fs.write('image_width', image_size[0])
    fs.write('image_height', image_size[1])
    fs.write('camera_matrix', K)
    fs.write('distortion_coefficients', dist)
    fs.write('reprojection_error', rpe)
    fs.release()
    print(f"\nResults saved to: {args.output}")

    # 质量检查
    if rpe > 0.5:
        print(f"WARNING: High reprojection error ({rpe:.2f} px). Consider re-calibrating.", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
```

### 15A.4 标定结果验证 (`validate-calibration.py`)

```python
#!/usr/bin/env python3
"""标定结果验证 — 检查标定结果的合理性.

用法: python3 validate-calibration.py camera_intrinsics.yaml
"""

import sys
import numpy as np
import cv2


def validate_camera(yaml_path: str) -> bool:
    """验证相机内参标定结果."""
    fs = cv2.FileStorage(yaml_path, cv2.FILE_STORAGE_READ)
    K = fs.getNode('camera_matrix').mat()
    dist = fs.getNode('distortion_coefficients').mat()
    w = int(fs.getNode('image_width').real())
    h = int(fs.getNode('image_height').real())
    rpe = fs.getNode('reprojection_error').real()
    fs.release()

    checks = []

    # 1. 焦距合理性: 0.5w < fx < 2w
    fx, fy = K[0, 0], K[1, 1]
    checks.append(('fx range', 0.5 * w < fx < 2.0 * w))
    checks.append(('fy range', 0.5 * h < fy < 2.0 * h))

    # 2. 主点合理性: 偏移 < 20% 图像尺寸
    cx, cy = K[0, 2], K[1, 2]
    checks.append(('cx centered', abs(cx - w / 2) < 0.2 * w))
    checks.append(('cy centered', abs(cy - h / 2) < 0.2 * h))

    # 3. 倾斜参数 ≈ 0
    checks.append(('skew near zero', abs(K[0, 1]) < 1e-6))

    # 4. 重投影误差 < 0.5 px
    checks.append(('reprojection < 0.5px', rpe < 0.5))

    print("=== Camera Calibration Validation ===")
    all_ok = True
    for name, ok in checks:
        status = '✓' if ok else '✗'
        print(f"  {status} {name}")
        if not ok:
            all_ok = False

    return all_ok


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} camera_intrinsics.yaml")
        sys.exit(1)

    ok = validate_camera(sys.argv[1])
    sys.exit(0 if ok else 1)
```

---

## Part B: Phase 1 集成验证

### 15B.1 固件通信回路测试 (`src/deployment/test/test-serial-loopback.sh`)

```bash
#!/bin/bash
# 串口通信回路测试 — 树莓派 ↔ STM32/MSPM0
# 用法: ./test-serial-loopback.sh /dev/ttyAMA0 [stm32|mspm0]
# 验证: PING-PONG + 遥测接收

set -e

SERIAL="${1:-/dev/ttyAMA0}"
BOARD="${2:-stm32}"
TIMEOUT=5

echo "=== Serial Loopback Test ==="
echo "Port: $SERIAL | Board: $BOARD | Timeout: ${TIMEOUT}s"

# 使用 Python 脚本发送 ping 并等待 pong
python3 - <<EOF
import serial
import struct
import time

def crc16_ccitt(data):
    """CRC-16/CCITT-FALSE (非反射, poly=0x1021, init=0xFFFF).
    
    与 STM32/MSPM0 固件的 CRC 实现完全一致 (ADR-0003 统一标准).
    测试向量: crc16_ccitt(b'123456789') == 0x29B1
    """
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

def build_frame(cmd, data=b''):
    """构建二进制帧 (CRC 覆盖 CMD+DATA, 不含 SOF/LEN/EOF)."""
    payload = bytes([cmd]) + data
    crc = crc16_ccitt(payload)
    # LEN = CMD(1) + DATA(n) + CRC(2) + EOF(1), max 255
    frame = bytes([0xA5, len(payload) + 3]) + payload + struct.pack('<H', crc) + bytes([0x5A])
    return frame

ser = serial.Serial('${SERIAL}', 115200, timeout=${TIMEOUT})
print(f"Opened {ser.name}")

# Send PING
ping = build_frame(0x03)
print(f"TX PING: {ping.hex()}")
ser.write(ping)
time.sleep(0.5)

# Read response
resp = ser.read(64)
if resp:
    print(f"RX: {resp.hex()}")
    # Check for PONG (CMD=0x13) — 5 bytes payload: major, minor, patch, board_type, chassis_type
    # Frame: SOF(0xA5) LEN(0x08) CMD(0x13) DATA(5 bytes) CRC(2 bytes) EOF(0x5A)
    if len(resp) >= 9:
        # Locate SOF
        sof_idx = resp.find(b'\xa5')
        if sof_idx >= 0 and sof_idx + 8 < len(resp):
            cmd = resp[sof_idx + 2]
            if cmd == 0x13:
                data_start = sof_idx + 3
                fw_major, fw_minor, fw_patch = resp[data_start:data_start+3]
                board_type = resp[data_start + 3]
                chassis_type = resp[data_start + 4]
                board_names = {0x01: 'STM32F407', 0x02: 'MSPM0G3507'}
                chassis_names = {0x01: 'mecanum', 0x02: 'differential'}
                print(f"✓ PONG received!")
                print(f"   FW: v{fw_major}.{fw_minor}.{fw_patch}")
                print(f"   Board: {board_names.get(board_type, f'unknown(0x{board_type:02X})')}")
                print(f"   Chassis: {chassis_names.get(chassis_type, f'unknown(0x{chassis_type:02X})')}")
                # Cross-check with expected CHASSIS env var
                expected = '${CHASSIS:-mecanum}'
                actual = 'mecanum' if chassis_type == 0x01 else 'differential'
                if expected == actual:
                    print("✓ Chassis type matches environment variable")
                else:
                    print(f"⚠ WARNING: chassis mismatch! env={expected}, board={actual}")
                print("✓ Serial communication OK")
            else:
                print(f"✗ Expected PONG(0x13), got CMD=0x{cmd:02X}")
        else:
            print(f"✗ Frame too short or no SOF found")
    else:
        print(f"✗ Response too short ({len(resp)} bytes, expect >= 9)")
else:
    print("✗ No response received (timeout)")

ser.close()
EOF
```

### 15B.2 传感器→Observation 数据流验证

```python
#!/usr/bin/env python3
# src/deployment/test/test-observation-pipeline.py
"""端到端验证: 传感器 raw data → Observation → WorldState.

不依赖 ROS 运行，直接测试数据结构的正确性.
"""

import sys
import unittest


# 模拟 ICD 数据结构 (精简版, 不依赖 rospy)
class MockObservation:
    def __init__(self):
        self.robot_id = ""
        self.modalities = []
        self.rgb = None
        self.depth = None
        self.lidar_ranges = []
        self.ultrasonic_ranges = []


class MockWorldState:
    def __init__(self):
        self.agents = []
        self.landmarks = []
        self.map_2d = None


def preprocess_to_observation(robot_id, modalities, data):
    """模拟 car_preprocessor.py 的核心逻辑."""
    obs = MockObservation()
    obs.robot_id = robot_id
    obs.modalities = modalities
    if 'rgb' in modalities:
        obs.rgb = data.get('rgb')
    if 'depth' in modalities:
        obs.depth = data.get('depth')
    if 'lidar_2d' in modalities:
        obs.lidar_ranges = data.get('lidar_ranges', [])
    if 'ultrasonic' in modalities:
        obs.ultrasonic_ranges = data.get('ultrasonic_ranges', [])
    return obs


class TestObservationPipeline(unittest.TestCase):

    def test_full_pipeline_car(self):
        """车机多模态观测管道."""
        data = {
            'rgb': b'fake_jpeg_data',
            'depth': [[0.5] * 640] * 480,  # 模拟深度图
            'lidar_ranges': [1.0] * 360,
            'ultrasonic_ranges': [0.3, 0.3, 0.3, 0.3],
        }
        obs = preprocess_to_observation(
            'car',
            ['rgb', 'depth', 'lidar_2d', 'ultrasonic'],
            data
        )
        self.assertEqual(obs.robot_id, 'car')
        self.assertEqual(len(obs.modalities), 4)
        self.assertEqual(len(obs.lidar_ranges), 360)
        self.assertEqual(len(obs.ultrasonic_ranges), 4)

    def test_minimal_observation(self):
        """最小观测 (仅 robot_id)."""
        obs = preprocess_to_observation('drone', [], {})
        self.assertEqual(obs.robot_id, 'drone')
        self.assertEqual(obs.modalities, [])

    def test_observation_to_worldstate(self):
        """Observation 聚合到 WorldState."""
        obs1 = preprocess_to_observation('drone', ['rgb'], {'rgb': b'data'})
        obs2 = preprocess_to_observation('car', ['lidar_2d'], {'lidar_ranges': [1.0]*360})

        # 模拟 World Model 聚合
        ws = MockWorldState()
        ws.agents = [obs1, obs2]
        self.assertEqual(len(ws.agents), 2)
        self.assertEqual(ws.agents[0].robot_id, 'drone')
        self.assertEqual(ws.agents[1].robot_id, 'car')


if __name__ == '__main__':
    unittest.main()
```

### 15B.3 Phase 1 冒烟测试 (`smoke_test_phase1.sh`)

```bash
#!/bin/bash
# Phase 1 冒烟测试 — 检查所有 Phase 1 交付物
# 用法: ./smoke_test_phase1.sh
# 退出码: 0=全部通过, 1=有失败项

set -e

PASS=0
FAIL=0
WS="$(cd "$(dirname "$0")/.." && pwd)"

echo "========================================="
echo " Phase 1 Smoke Test"
echo "========================================="

check() {
    local desc="$1"
    local cmd="$2"
    echo -n "  [$desc] ... "
    if eval "$cmd" &>/dev/null; then
        echo "✓"
        PASS=$((PASS + 1))
    else
        echo "✗ FAIL"
        FAIL=$((FAIL + 1))
    fi
}

echo ""
echo "--- Firmware ---"
check "STM32 firmware dir exists"        "[ -d $WS/src/firmware/stm32_mecanum ]"
check "STM32 README exists"              "[ -f $WS/src/firmware/stm32_mecanum/README.md ]"
check "MSPM0 firmware dir exists"        "[ -d $WS/src/firmware/mspm0_diff ]"
check "MSPM0 README exists"              "[ -f $WS/src/firmware/mspm0_diff/README.md ]"

echo ""
echo "--- Deployment ---"
check "Dockerfile.edge exists"           "[ -f $WS/src/deployment/docker/Dockerfile.edge ]"
check "docker-compose exists"            "[ -f $WS/src/deployment/docker/docker-compose.edge.yml ]"
check "systemd car service exists"       "[ -f $WS/src/deployment/systemd/air-ground-car-edge.service ]"
check "systemd drone service exists"     "[ -f $WS/src/deployment/systemd/air-ground-drone-edge.service ]"
check "SSH hardening config exists"      "[ -f $WS/src/deployment/ssh/sshd_hardening.conf ]"

echo ""
echo "--- Sensor Drivers ---"
check "RPLIDAR driver exists"            "[ -f $WS/src/air_ground_car_bringup/scripts/rplidar_driver.py ]"
check "ICM42688 driver exists"           "[ -f $WS/src/air_ground_car_bringup/scripts/icm42688_driver.py ]"
check "HC-SR04 driver exists"            "[ -f $WS/src/air_ground_car_bringup/scripts/hcsr04_driver.py ]"
check "OpenMV bridge exists"             "[ -f $WS/src/air_ground_car_bringup/scripts/openmv_bridge.py ]"
check "Hardware interface ABC exists"    "[ -f $WS/src/air_ground_car_bringup/scripts/hardware_interface.py ]"
check "Mock hardware exists"             "[ -f $WS/src/air_ground_car_bringup/test/mock_hardware.py ]"

echo ""
echo "--- MAVLink Signing ---"
check "Key gen script exists"            "[ -f $WS/src/deployment/mavlink/generate-mavlink-key.sh ]"
check "Signing test script exists"       "[ -f $WS/src/deployment/mavlink/test-mavlink-signing.py ]"
check "PX4 signing params exist"         "[ -f $WS/src/deployment/mavlink/px4-signing.params ]"

echo ""
echo "--- Calibration ---"
check "Calib record script exists"       "[ -f $WS/src/deployment/calibration/record-calib-bag.sh ]"
check "Camera calib script exists"       "[ -f $WS/src/deployment/calibration/calibrate-camera.py ]"
check "Validate calib script exists"     "[ -f $WS/src/deployment/calibration/validate-calibration.py ]"

echo ""
echo "--- CI ---"
check "CI workflow exists"               "[ -f $WS/.github/workflows/ci.yml ]"
check "CI has STM32 job"                 "grep -q 'build-stm32-firmware' $WS/.github/workflows/ci.yml"
check "CI has MSPM0 job"                 "grep -q 'build-mspm0-firmware' $WS/.github/workflows/ci.yml"
check "CI has Docker job"                "grep -q 'build-docker-edge' $WS/.github/workflows/ci.yml"

echo ""
echo "========================================="
echo " Results: $PASS passed, $FAIL failed"
echo "========================================="

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
```

---

## 验收标准

> 勾选状态为 2026-08-01 实际交付结果（格式同 task-14）。带 ⚠ 的条目见文末「与原方案的偏差」。

### Part A 标定

- [x] `calibrate-camera.py` 可处理 ≥5 张棋盘格图片，输出 `camera_intrinsics.yaml` ⚠ 默认最少 10 张，`--min-images` 可调（偏差 3）
- [x] `validate-calibration.py` 对标定结果做合理性检查（5 项全部通过） ⚠ 焦距判据按 HFOV 重定义（偏差 4）
- [x] 标定脚本在 CI 中可用离线样本数据测试（不需要 ROS）—— 16 个流水线用例
- [x] `record-calib-bag.sh` 有明确的操作指南（提示用户如何移动标定板） ⚠ 话题名按仓库现状更正，开录前检查在线（偏差 1）
- [x] **标定报告自动生成** (Phase 1 交付): `generate-calib-report.py` 将 YAML 转换为 Markdown 报告，包含 K Matrix / Distortion Coefficients / RMS Reprojection Error / Calibration Date ⚠ 另含逐张 RMS（偏差 3）
- [x] (Stretch) `calibration_db/` 目录结构占位 + README 说明归档规范 (完整历史数据库为 Phase 2 功能)

### Part B 集成验证

- [x] `test-serial-loopback.sh` 可发送 PING 并判断是否收到 PONG (含 board_type/chassis_type 校验) —— 13 项自测全过
- [x] `test-observation-pipeline.py` 3 个测试用例通过 ⚠ 实际 10 个用例，且测的是真 `CarPreprocessor` 而非模拟件（ADR-0011）
- [x] `smoke_test_phase1.sh` 可检查所有 Phase 1 文件存在性 + CI job 存在性 —— 61 项全绿
- [x] 2026-08-03 Phase 1.5 跟进：增加“任意分支 push 必须触发 CI”契约守卫，当前基线 65 项全绿
- [x] Phase 1 冒烟测试在 GitHub Actions 中可运行 —— 含删除交付物的负向测试
- [x] **三问检查** (每完成一个 Task)：Platform 是否更稳定？ / Research 是否更自由？ / 未来替换硬件是否更简单？ —— 见文末「三问检查」记录

---

## ⓘ 优化建议（混元3 评审）

1. **细化 IMU 标定输出规范**：IMU 标定输出 YAML 应包含以下字段：`gyro_bias` (rad/s), `accel_bias` (m/s²), `gyro_noise_density` (rad/s/√Hz), `accel_noise_density` (m/s²/√Hz), `gyro_random_walk` (rad/s²/√Hz), `accel_random_walk` (m/s³/√Hz)。与 IMU 驱动、robot_localization 滤波器的输入格式对齐。
2. **相机-IMU 外参的 Phase 1 边界**：Kalibr 标定依赖完整 ROS 环境，无法在纯 CI 中运行。Phase 1 仅完成 `calibrate-cam-imu-extrinsic.py` 的接口定义与输出 YAML 格式规范，实际标定待实机数据采集后执行。
3. **升级冒烟测试维度**：`smoke_test_phase1.sh` 在文件存在检查之外，增加一级「接口冒烟」：Python 语法检查 (`python3 -m py_compile`)、模块导入测试 (`python3 -c "import scripts.*"`)、YAML 格式校验 (`yamllint`)。
4. **标定报告模板**：`generate-calib-report.py` 输出的 Markdown 报告应包含以下固定章节：标定时间、硬件序列号、K Matrix、畸变系数、重投影误差 RMS、重投影误差分布图 (ASCII art)、Allan 方差曲线数据 (IMU)、质量评估结论 (PASS/FAIL/NEED_RECALIBRATE)。

---

## 给 Subagent 的执行建议

1. **标定脚本的核心价值在流程自动化**，不在标定算法本身（OpenCV 已经做完了）
2. **离线测试数据 (`sample_calib_data/`) 必须小而真实**：5 张不同角度的棋盘格照片 + 1 分钟静态 IMU bag
3. **冒烟测试是 Phase 1 的"毕业证书"**：它不测试功能正确性，只测试"所有东西都在正确的位置"
4. **标定脚本中的用户指南越详细越好**：实机操作时用户可能是第一次做标定
5. **IMU 标定脚本提供骨架 + 文档即可**：`allan_variance_ros` 工具链需要 ROS 环境，CI 中不跑完整标定

---

## 参考资料

| 文件 | 内容 |
|------|------|
| [Kalibr Wiki](https://github.com/ethz-asl/kalibr/wiki) | 相机-IMU 标定工具链 |
| [OpenCV Camera Calibration](https://docs.opencv.org/4.x/dc/dbb/tutorial_py_calibration.html) | OpenCV 标定教程 |
| `project-prometheus-tasks/PLATFORM.md` §一 | 传感器硬件清单 |
| `project-prometheus-tasks/ICD.md` §二 | Observation / WorldState 定义 |

---

*版本: v1.0 · 日期: 2026-07-28 · Phase 1 · 标定 + 验证*

---

## 与原方案的偏差

> 本文是任务书，落地时按仓库现状调整了七处。ADR 见
> [ADR-0011](../docs/decisions/ADR-0011.md)。约定同 task-13 / task-14：
> **本文不改正文，偏差写在这里**——正文是当初的想法，这一节是实际发生的事。

### 1. 采集脚本的五个话题名，仓库里一个都不存在（§15A.2）

| 本文 | 仓库实际 | 出处 |
|------|---------|------|
| `/drone/rgb/image_raw` | `/drone/camera/rgb/image_raw` | `drone_edge.yaml` |
| `/drone/depth/image_raw` | `/drone/camera/depth/image_raw` | `drone_edge.yaml` |
| `/drone/imu/data_raw` | `/mavros/imu/data` | `drone_edge.yaml` |
| `/car/imu/data_raw` | `/car/imu/data` | `car_edge.yaml` / `real_sensors.yaml` |
| `/drone/rgb/camera_info` | **不存在**（本任务的产出才会造出它） | —— |

`rosbag record` 订阅一个没有发布者的话题**不报错**，录出 0 条消息的 bag。
相机标定要人举着标定板站两分钟，IMU 标定要静置两小时——代价不是"重跑脚本"。

现在：话题名写在脚本顶部的 `*_TOPIC` 变量里，由
`validate_consistency.py::check_calibration_topics()` 断言它们真的出现在三份
配置里（负向测试跑过：改回 `/car/imu/data_raw` 立刻红）；脚本另在开录前跑
`rostopic list` 逐个确认在线，不在线拒绝开录。

### 2. `cv2.FileStorage` 写的 YAML，ROS 读不了（§15A.3 / §15A.4）

本文的「Modified Interface」写的是 `camera_intrinsics.yaml → ROS camera_info`，
但给的实现用 `cv2.FileStorage`。实测（OpenCV 5.0.0）：首行是 `%YAML 1.2` 指令、
矩阵带 `!!opencv-matrix` 标签，`yaml.safe_load()` 抛 `ConstructorError`。
而 `camera_info_manager` 走的正是普通 YAML 解析。

现在：用 PyYAML 直接写 `camera_info` 的键布局（含
`rectification_matrix` / `projection_matrix`），本项目的溯源信息放在
`air_ground_calibration:` 额外段。副作用是 `validate-calibration.py` 不再需要
OpenCV——它必须能在没装 OpenCV 的树莓派上跑。

⚠ `camera_info_manager` 是否忽略那个额外段，本机（无 ROS）核实不了，
已列入 [标定 README §5 上机核实清单](../src/deployment/calibration/README.md)。

### 3. §15A.3 的重投影误差计算：一处重复、一处崩溃

- **重复**：手算的 RMS 与 `cv2.calibrateCamera` 的返回值 `ret` **是同一个数**
  （实测比值 0.999997）。本文把它们当成"重投影误差"和"RMS"两个指标分别打印。
  现在只报一个 `rms_reprojection_error`，另外报**逐张** RMS——那才是
  整体 RMS 给不出的信息（能区分"整体偏差大"和"某两张拖后腿"）。
- **崩溃**：`cv2.norm(img_points[i], projected, cv2.NORM_L2)` 在 OpenCV 5.0 上
  抛 `Input type mismatch`——4.x 的 `findChessboardCorners` 返回 `(N,1,2)`，
  5.0 返回 `(N,2)`，而 `projectPoints` 一直是 `(N,1,2)`。现在统一
  `reshape(-1, 2)` 再用 numpy 算，两个版本都对。

另：§15A.3 的 `calibrate()` 要求 ≥10 张，而验收标准写"≥5 张"。
现在默认 10 张（低于 10 张时畸变系数与主点强相关），并给 `--min-images` 开关。

### 4. §15A.4 的焦距区间装不下本项目的相机

`0.5w < fx < 2.0w` 换算过来是 HFOV ∈ (28.1°, 90.0°)。RealSense D435i 的
**深度**流标称 87°±3° HFOV → `fx ≈ 0.527w`，上沿正好压在 `fx = 0.5w` 的边界上：
一次完全正常的标定有可能被判成"焦距不合理"。

现在：区间按视场角定义为 HFOV ∈ [20°, 120°]（即 `fx ∈ [0.2887w, 2.836w]`），
并在输出里直接打印反推的 HFOV。同时把 `fy` 的判据从"对比图像**高度**"改成
`fy/fx ≈ 1`——`fy` 与图像高度之间本来就没有关系。

### 5. §15B.2 的 `MockObservation` 测的是它自己

本文写了一个二十行的 `preprocess_to_observation()`，注释是"模拟
`car_preprocessor.py` 的核心逻辑"。那三个用例一定会通过，因为它们测的是
那二十行模拟件。真的 `build_messages()` 里有五件它没有的事：新鲜度窗口、
LiDAR 降采样与 `angle_increment` 同步放大、无效距离写 −1.0、
超声波限幅与缺失填 `max_range`、`modalities` 由新鲜度生成——全是实机上
真正会出问题的地方。

现在：用 task-14 已有的 ROS 替身跑**真的** `CarPreprocessor` 和**真的**
`WorldModelStore`，10 条用例。为此给替身补了 15 个消息类与
`Subscriber`/`Timer`/`CvBridge`；`cv2` 用真的，所以 JPEG 压缩那一段是真跑的。

补替身时当场抓到一个替身缺陷：`rospy.Duration(1.0 / publish_rate)`——
`car_preprocessor.py` 的原话——在替身上 `TypeError`（替身只收关键字参数，
真 `rospy.Duration` 收位置参数）。在那之前没有任何测试构造过 `Duration`。

### 6. §15B.1 的 Python 塞在 shell heredoc 里

`bash -n` 只检查 shell 语法，`py_compile` 看不见 heredoc 里的内容，
于是那段代码不被任何门禁覆盖——而它是 ADR-0003 帧协议的**第三份独立实现**。

现在：拆成 `test-serial-loopback.py`（逻辑 + `--self-test`）与
`test-serial-loopback.sh`（转发入口，保留本文的用法）。确认 BOM 后的 10 条协议自测在没有
硬件的机器上跑，黄金帧与固件的 `test_protocol.c::test_pong_golden_frame` 同源。

自测第 10 条记录了一个**本来就存在**的行为：线路上一个杂散 `0xA5` 会让
拆帧状态机空等 165 字节，期间到达的帧全部被吞掉（`protocol_frame.h` 的
`@warning` 早已写明）。解药是空闲重同步，`run_loopback()` 每次 `read()`
读空时调 `parser.reset()`——对应固件 `uart.c` 的 IDLE 中断。

另：本文的解析逻辑是"找第一个 `0xA5` 然后按偏移读"。载荷里允许出现
`0xA5`/`0x5A`（协议不做字节填充），而且固件在 PONG 之外还周期上报 TELEMETRY，
所以那样会随机对错。现在用与固件同构的 LEN + CRC + EOF 状态机。

### 7. §15B.3 冒烟测试的两处路径/名字对不上

- `test/mock_hardware.py` → 实际在 `scripts/mock_hardware.py`。
  mock 后端是**运行时**要加载的（`backend: mock`），不是测试专用件。
- CI job `build-docker-edge` → 仓库里叫 `build-edge-image`。

两处都会产生**永远红的检查**，而看久了就没人看了。现在按实际路径/名字查，
并把 `validate-deployment` / `lint-scripts` / `smoke_test_phase1` 三个 job
一并纳入检查。

冒烟脚本放在仓库根的 `scripts/`（本文的 `WS="$(dirname $0)/.."` 隐含了这个位置），
`make smoke-phase1` 可跑。按 §优化建议 3 升级成三层：交付物存在性 →
语法/YAML 解析/自测真跑 → CI 归属。CI 里另有一步**反向验证**：删掉一个交付物
之后冒烟测试必须失败——否则"毕业证书"是假的。

---

### 采纳的优化建议

四条评审建议全部落地：

1. **IMU 标定输出规范**：六个字段照收，另加 `derived_for_driver` 段——
   `real_sensors.yaml` 收的是**离散标准差**，Allan 解出的是**连续噪声密度**，
   差一个 `√采样率`（100 Hz 下协方差差 100 倍）。这层换算本文没提。
2. **相机-IMU 外参的 Phase 1 边界**：照办。另加了 bag 合规性检查与
   "占位模板必须判失败"——单位阵是合法 SE(3)，只有 `status` 能区分
   "还没标定"和"外参恰好为零"。
3. **升级冒烟测试维度**：照办，见上面第 7 条。YAML 用 PyYAML 真 `load` 一遍
   而不是 `yamllint`——后者查风格，查不出"这份 YAML 根本 load 不出来"。
4. **标定报告模板**：照办，含 ASCII 重投影误差分布图与 log-log Allan 曲线。
   报告的判定阈值用 `importlib` 从 `validate-calibration.py` 加载，不抄第二份。

### 未做的事

- **`sample_calib_data/` 不是 5 张真实照片，是合成真值**（ADR-0011 §决策-4）。
  真实照片的真值未知，只能断言 RMS 小——而 RMS 小是几乎所有错误标定都满足的。
  合成数据能断言"解出来的 fx 与设进去的 fx 差 0.18%"。代价写在
  `make_sample_calib_data.py` 的 docstring 里：它证明不了 `plumb_bob` 拟合得了
  真实镜头，也没有运动模糊/卷帘快门。
- **本机装不上 CI 钉的 OpenCV/numpy 版本**（Windows + Python 3.14，
  `numpy<2.0.0` 没有 cp314 轮子）。所有标定数值是在 OpenCV 5.0.0 / numpy 2.5.1
  上实测的。代码只用两版都稳定的 API，但"CI 上的数与本地一致"这件事，
  第一次 CI 运行之前没有被验证过。
- **三问检查**：Platform 更稳（协议/话题/标定格式三处契约现在都有会报警的检查）；
  Research 更自由（`validate-calibration.py` 零重依赖，换标定算法不动校验和输出格式）；
  换硬件更简单（换相机重采一次数据，脚本不变；换 IMU 只改 `--sensor` 和采样率）。

*落地日期: 2026-07-31 · 执行: subagent · 决策记录: ADR-0011*
