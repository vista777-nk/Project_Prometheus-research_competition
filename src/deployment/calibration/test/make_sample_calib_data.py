#!/usr/bin/env python3
"""合成标定样本生成器 —— 给 CI 用的"小而真实"的离线数据。

为什么是合成的, 不是 5 张真实棋盘格照片:

真实照片的**真值是未知的**。拿一组照片标定出 fx=520, 没有任何办法判断
这个 520 是对的还是错的 —— 只能看 RMS, 而 RMS 小只说明"这组解自洽",
不说明"这组解正确"。一个把所有角点坐标同时缩放 1.1 倍的 bug 会让
fx 变成 572, RMS 却纹丝不动。

合成数据反过来: 真值是我们自己设的, 所以 CI 能断言"解出来的 fx 与真值
差多少"。这才是标定流水线要证明的事。

代价必须写明 (这份数据**不能**证明的事):
  · 不能证明 OpenCV 的 plumb_bob 模型拟合得了真实镜头 —— 图就是按
    plumb_bob 画出来的, 属于自己考自己。
  · 没有运动模糊、没有卷帘快门、没有对焦呼吸、没有光照不均。
  · 角点检测跑的是干净的高对比度图, 真实照片的检测率会更低。
真实镜头的模型误差, 只能靠实机采一次数据来验 (见 README §5 上机清单)。

用法:
    python3 make_sample_calib_data.py --output-dir /tmp/calib_sample
    python3 make_sample_calib_data.py --output-dir /tmp/x --imu-seconds 120
"""

import argparse
import math
import os
import sys
from typing import List, Sequence, Tuple

import numpy as np

try:
    import cv2
except ImportError:  # pragma: no cover
    print("ERROR: 需要 OpenCV。pip install 'opencv-python-headless>=4.8.0,<5.0.0'",
          file=sys.stderr)
    raise SystemExit(2)


# --- 相机真值 ------------------------------------------------------------
# 数值取得接近 OpenMV / D435i 那一档的 VGA 相机: fx≈520 → HFOV≈63°。
# 刻意让 fx≠fy、cx≠w/2, 这样"把 fy 当 fx 用"或"把主点写死成中心"这类
# bug 会被断言抓到, 而不是恰好蒙对。
IMAGE_WIDTH = 640
IMAGE_HEIGHT = 480
K_TRUE = np.array([[520.0, 0.0, 322.0],
                   [0.0, 519.0, 238.0],
                   [0.0, 0.0, 1.0]], dtype=np.float64)
#: plumb_bob: k1, k2, p1, p2, k3
DIST_TRUE = np.array([0.12, -0.25, 0.001, -0.0008, 0.10], dtype=np.float64)

PATTERN_SIZE = (9, 6)     # 内角点数 (cols, rows)
SQUARE_SIZE = 0.030       # m

#: 12 个视角。整体分布要覆盖 roll/pitch/yaw 各方向的倾斜, 否则
#: 焦距与主点强相关, 标定"能收敛但不准"。
POSES: Sequence[Tuple[Tuple[float, float, float], Tuple[float, float, float]]] = (
    ((0.00, 0.00, 0.00), (-0.13, -0.09, 0.45)),
    ((0.35, 0.00, 0.00), (-0.14, -0.07, 0.42)),
    ((-0.35, 0.00, 0.05), (-0.13, -0.10, 0.44)),
    ((0.00, 0.40, 0.00), (-0.12, -0.09, 0.43)),
    ((0.00, -0.40, 0.00), (-0.15, -0.09, 0.43)),
    ((0.25, 0.25, 0.15), (-0.13, -0.08, 0.40)),
    ((-0.25, 0.25, -0.15), (-0.14, -0.09, 0.41)),
    ((0.25, -0.25, 0.10), (-0.13, -0.09, 0.41)),
    ((-0.25, -0.25, -0.10), (-0.14, -0.08, 0.42)),
    ((0.10, 0.10, 0.60), (-0.12, -0.10, 0.38)),
    ((0.45, 0.15, 0.00), (-0.14, -0.06, 0.46)),
    ((-0.15, -0.45, 0.00), (-0.13, -0.11, 0.47)),
)

#: 超采样倍数。1 倍的话棋盘格边缘有锯齿, cornerSubPix 会系统性偏几十分之一
#: 像素; 2 倍够用, 4 倍慢一倍而 RMS 只再降一点点。
SUPERSAMPLE = 2

BOARD_WHITE = 235
BOARD_BLACK = 25
BACKGROUND = 170          # 标定板四周的浅色静区, findChessboardCorners 需要


# --- IMU 真值 ------------------------------------------------------------
IMU_RATE_HZ = 100.0
GYRO_BIAS_TRUE = (0.0021, -0.0035, 0.0012)          # rad/s
ACCEL_BIAS_TRUE = (0.038, -0.021, 0.055)            # m/s^2
#: 连续时间噪声密度。ICM42688-P 数据手册量级: 陀螺 ~0.0028 (deg/s)/√Hz,
#: 折合 ~5e-5 rad/s/√Hz; 这里取略大的值让 Allan 曲线在短 τ 段更清楚。
GYRO_NOISE_DENSITY_TRUE = 2.0e-4                    # rad/s/√Hz
ACCEL_NOISE_DENSITY_TRUE = 1.5e-3                   # m/s^2/√Hz
GYRO_RANDOM_WALK_TRUE = 2.0e-6                      # rad/s^2/√Hz
ACCEL_RANDOM_WALK_TRUE = 3.0e-5                     # m/s^3/√Hz

GRAVITY = 9.80665


def _board_texture_lookup(board_x: np.ndarray, board_y: np.ndarray) -> np.ndarray:
    """按标定板平面坐标 (m) 采样黑白格。

    内角点 (0,0) 落在 board 坐标原点, 因此棋盘格本体覆盖
    [-S, cols*S] × [-S, rows*S], 外面是浅色静区。
    """
    cols, rows = PATTERN_SIZE
    inside = (
        (board_x >= -SQUARE_SIZE) & (board_x <= cols * SQUARE_SIZE)
        & (board_y >= -SQUARE_SIZE) & (board_y <= rows * SQUARE_SIZE)
    )
    parity = (np.floor(board_x / SQUARE_SIZE).astype(np.int64)
              + np.floor(board_y / SQUARE_SIZE).astype(np.int64)) % 2
    image = np.full(board_x.shape, BACKGROUND, dtype=np.uint8)
    image[inside] = np.where(parity[inside] == 0, BOARD_WHITE, BOARD_BLACK)
    return image


class CheckerboardRenderer:
    """按真值内参+畸变反向渲染棋盘格图片。

    反向渲染 (逐输出像素反投影到标定板) 而不是正向投影角点再画多边形:
    正向画法没法在畸变下画出正确的直线弯曲, 而畸变正是要标出来的东西。

    归一化视线网格只与 (K, D, 分辨率) 有关, 与位姿无关, 所以只算一次;
    每个位姿只剩一次 3×3 矩阵乘法。
    """

    def __init__(self) -> None:
        """预计算超采样网格的归一化视线。"""
        self.width = IMAGE_WIDTH * SUPERSAMPLE
        self.height = IMAGE_HEIGHT * SUPERSAMPLE
        scaled_k = K_TRUE.copy()
        scaled_k[:2, :] *= SUPERSAMPLE

        us, vs = np.meshgrid(
            np.arange(self.width, dtype=np.float64),
            np.arange(self.height, dtype=np.float64),
        )
        pixels = np.stack([us.ravel(), vs.ravel()], axis=1).reshape(-1, 1, 2)
        normalized = cv2.undistortPoints(pixels, scaled_k, DIST_TRUE).reshape(-1, 2)
        self.rays = np.concatenate(
            [normalized, np.ones((normalized.shape[0], 1))], axis=1
        )

    def render(self, rvec: Sequence[float], tvec: Sequence[float]) -> np.ndarray:
        """渲染一个位姿下的灰度图 (已降采样回标称分辨率)。"""
        rotation, _ = cv2.Rodrigues(np.asarray(rvec, dtype=np.float64).reshape(3, 1))
        translation = np.asarray(tvec, dtype=np.float64).reshape(3, 1)
        # 平面单应 (归一化像面 ← 标定板平面): Hn = [r1 r2 t]
        plane_h = np.hstack([rotation[:, :1], rotation[:, 1:2], translation])
        board = self.rays @ np.linalg.inv(plane_h).T
        scale = board[:, 2]
        # 视线与标定板平面平行时 scale→0。真实位姿下不会发生, 但除零会造出
        # 一整片 NaN 而不报错, 所以显式挡掉。
        safe = np.abs(scale) > 1e-12
        board_x = np.where(safe, board[:, 0] / np.where(safe, scale, 1.0), 1e9)
        board_y = np.where(safe, board[:, 1] / np.where(safe, scale, 1.0), 1e9)
        image = _board_texture_lookup(board_x, board_y).reshape(self.height, self.width)
        return cv2.resize(image, (IMAGE_WIDTH, IMAGE_HEIGHT), interpolation=cv2.INTER_AREA)


def generate_images(output_dir: str) -> List[str]:
    """生成全部棋盘格图片, 返回写出的路径。"""
    os.makedirs(output_dir, exist_ok=True)
    renderer = CheckerboardRenderer()
    written = []
    for index, (rvec, tvec) in enumerate(POSES):
        image = renderer.render(rvec, tvec)
        path = os.path.join(output_dir, f"checkerboard_{index:02d}.png")
        if not cv2.imwrite(path, image):
            raise RuntimeError(f"写图失败: {path}")
        written.append(path)
    return written


def generate_imu_log(path: str, seconds: float = 120.0, seed: int = 20260731) -> str:
    """生成静置 IMU 日志 (CSV), 含已知的 bias / 噪声密度 / 随机游走。

    离散采样的白噪声标准差与连续噪声密度的关系是
        sigma_discrete = density * sqrt(rate)
    这条换算是本项目 IMU 标定输出与 real_sensors.yaml 之间的桥
    (见 calibrate-imu.py 与 ADR-0011), 生成端和解算端必须用同一条。

    Args:
        path: 输出 CSV 路径。
        seconds: 时长 (s)。Allan 方差要看到 bias instability 谷底需要小时级数据,
            CI 里只跑两分钟, 因此只断言短 τ 段的噪声密度。
        seed: 随机种子, 固定以保证 CI 可复现。

    Returns:
        写出的路径。
    """
    rng = np.random.default_rng(seed)
    count = int(seconds * IMU_RATE_HZ)
    dt = 1.0 / IMU_RATE_HZ

    gyro_sigma = GYRO_NOISE_DENSITY_TRUE * math.sqrt(IMU_RATE_HZ)
    accel_sigma = ACCEL_NOISE_DENSITY_TRUE * math.sqrt(IMU_RATE_HZ)
    # 随机游走: 每步增量的标准差 = density * sqrt(dt)
    gyro_walk_step = GYRO_RANDOM_WALK_TRUE * math.sqrt(dt)
    accel_walk_step = ACCEL_RANDOM_WALK_TRUE * math.sqrt(dt)

    gyro_drift = np.cumsum(rng.normal(0.0, gyro_walk_step, size=(count, 3)), axis=0)
    accel_drift = np.cumsum(rng.normal(0.0, accel_walk_step, size=(count, 3)), axis=0)

    gyro = (np.array(GYRO_BIAS_TRUE) + gyro_drift
            + rng.normal(0.0, gyro_sigma, size=(count, 3)))
    # 静置在水平面上: z 轴读到 +1g。
    accel = (np.array(ACCEL_BIAS_TRUE) + np.array([0.0, 0.0, GRAVITY]) + accel_drift
             + rng.normal(0.0, accel_sigma, size=(count, 3)))
    stamps = np.arange(count, dtype=np.float64) * dt

    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("# 合成静置 IMU 日志 (make_sample_calib_data.py)\n")
        handle.write(f"# rate_hz={IMU_RATE_HZ} seconds={seconds} seed={seed}\n")
        handle.write("timestamp,gx,gy,gz,ax,ay,az\n")
        for index in range(count):
            handle.write(
                f"{stamps[index]:.4f},"
                f"{gyro[index, 0]:.9f},{gyro[index, 1]:.9f},{gyro[index, 2]:.9f},"
                f"{accel[index, 0]:.9f},{accel[index, 1]:.9f},{accel[index, 2]:.9f}\n"
            )
    return path


def main(argv: Sequence[str] = None) -> int:
    """命令行入口。"""
    parser = argparse.ArgumentParser(description="生成合成标定样本 (棋盘格 + 静置 IMU)")
    parser.add_argument("--output-dir", required=True, help="输出目录")
    parser.add_argument("--imu-seconds", type=float, default=120.0, help="IMU 日志时长 (s)")
    parser.add_argument("--skip-imu", action="store_true", help="只生成图片")
    args = parser.parse_args(argv)

    images_dir = os.path.join(args.output_dir, "images")
    paths = generate_images(images_dir)
    print(f"棋盘格图片 {len(paths)} 张 -> {images_dir}")
    print(f"  真值 fx={K_TRUE[0, 0]} fy={K_TRUE[1, 1]} "
          f"cx={K_TRUE[0, 2]} cy={K_TRUE[1, 2]}  dist={DIST_TRUE}")

    if not args.skip_imu:
        imu_path = os.path.join(args.output_dir, "imu_static.csv")
        generate_imu_log(imu_path, args.imu_seconds)
        print(f"静置 IMU 日志 {args.imu_seconds:.0f}s @ {IMU_RATE_HZ:.0f}Hz -> {imu_path}")
        print(f"  真值 gyro_bias={GYRO_BIAS_TRUE}  accel_bias={ACCEL_BIAS_TRUE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
