#!/usr/bin/env python3
"""相机内参标定 —— 棋盘格 + OpenCV, 离线图片目录驱动。

用法:
    python3 calibrate-camera.py --input calib_data/images/ --pattern 9x6 \
        --square-size 0.030 --camera-name car_openmv --output camera_intrinsics.yaml

设计约束 (三条, 都不是随便定的):

1. **输出格式是 ROS `camera_info` 的 YAML, 用 PyYAML 写, 不用 cv2.FileStorage。**
   cv2.FileStorage 写出来的东西 PyYAML 读不了 —— 首行是 `%YAML 1.2` / `%YAML:1.0`
   指令, 矩阵带 `!!opencv-matrix` 标签, `yaml.safe_load()` 直接抛
   ConstructorError (2026-07-31 实测, OpenCV 5.0.0)。而 ROS 的
   camera_info_manager 走的就是普通 YAML 解析。任务书里"标定输出 →
   ROS camera_info"这条接口, 用 FileStorage 是接不上的。详见 ADR-0011。

2. **不依赖 rosbag。** 本脚本只吃图片目录。从 bag 里抽图是 convert-bag-to-kalibr.py
   的事 (那一步需要 ROS 环境), 分开之后本脚本在任何装了 OpenCV 的机器上都能跑,
   CI 里也能跑。

3. **形状无关地算重投影误差。** OpenCV 4.x 的 findChessboardCorners 返回
   (N,1,2), 5.0 返回 (N,2); projectPoints 一直返回 (N,1,2)。任务书原文的
   `cv2.norm(img_points[i], projected, cv2.NORM_L2)` 在 5.0 上直接抛
   "Input type mismatch" (实测)。这里统一 reshape(-1,2) 再用 numpy 算。

顺带一个实测结论: 手算的 RMS 与 cv2.calibrateCamera 的返回值 `ret`
**是同一个数** (2026-07-31 实测比值 0.999997)。所以本脚本只报一个
`rms_reprojection_error`, 不去伪造"重投影误差"和"RMS"两个指标。
"""

import argparse
import glob
import math
import os
import sys
from datetime import date as date_type
from typing import List, Optional, Sequence, Tuple

import numpy as np
import yaml

try:
    import cv2
except ImportError:  # pragma: no cover - 环境问题, 不是逻辑分支
    print("ERROR: 需要 OpenCV。pip install 'opencv-python-headless>=4.8.0,<5.0.0'",
          file=sys.stderr)
    raise SystemExit(2)


IMAGE_EXTENSIONS = ("*.jpg", "*.jpeg", "*.png", "*.bmp", "*.tiff")

#: 默认最少图片数。低于 10 张时 k1/k2 与主点强相关, 解出来的内参看着漂亮
#: 但换个视角就崩。OpenCV 官方教程用 14 张不是巧合。
DEFAULT_MIN_IMAGES = 10

#: 默认 RMS 上限 (px)。超过就认为标定不可用, 退出码 1。
DEFAULT_MAX_RMS = 0.5


class CameraCalibrator:
    """棋盘格相机标定。

    Attributes:
        pattern_size: 棋盘格**内角点**数 (cols, rows)。8x6 的格子有 7x5 个内角点,
            这里填的是内角点数 —— 填错的表现是所有图片都检测不到角点。
        square_size: 方格边长 (m)。它只影响外参 (tvec) 的尺度, 不影响 K 和畸变。
    """

    def __init__(self, pattern_size: Tuple[int, int] = (9, 6),
                 square_size: float = 0.030) -> None:
        """初始化标定器。

        Args:
            pattern_size: 内角点数 (cols, rows)。
            square_size: 方格边长 (m)，必须为正。
        """
        if pattern_size[0] < 2 or pattern_size[1] < 2:
            raise ValueError("pattern_size 的两个维度都必须 >= 2")
        if not math.isfinite(square_size) or square_size <= 0.0:
            raise ValueError("square_size 必须是有限正数")

        self.pattern_size = pattern_size
        self.square_size = square_size
        self.obj_points: List[np.ndarray] = []
        self.img_points: List[np.ndarray] = []
        self.image_names: List[str] = []

        cols, rows = pattern_size
        self.pattern_3d = np.zeros((cols * rows, 3), np.float32)
        self.pattern_3d[:, :2] = np.mgrid[0:cols, 0:rows].T.reshape(-1, 2)
        self.pattern_3d *= square_size

    def add_image(self, image: np.ndarray, name: str = "") -> bool:
        """检测一张图片里的棋盘格角点并入库。

        Args:
            image: BGR 或灰度图。
            name: 图片名，仅用于报告。

        Returns:
            True 表示检测到完整棋盘格。
        """
        gray = image if image.ndim == 2 else cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        found, corners = cv2.findChessboardCorners(gray, self.pattern_size, None)
        if not found:
            return False

        criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001)
        refined = cv2.cornerSubPix(gray, corners, (11, 11), (-1, -1), criteria)

        self.obj_points.append(self.pattern_3d)
        self.img_points.append(refined)
        self.image_names.append(name)
        return True

    def calibrate(self, image_size: Tuple[int, int],
                  min_images: int = DEFAULT_MIN_IMAGES):
        """执行标定。

        Args:
            image_size: (width, height)。
            min_images: 最少图片数。

        Returns:
            (K, dist, rvecs, tvecs, rms)，rms 即 OpenCV 的整体 RMS 重投影误差 (px)。
        """
        if len(self.obj_points) < min_images:
            raise ValueError(
                f"至少需要 {min_images} 张能检测到棋盘格的图片, 当前只有 "
                f"{len(self.obj_points)} 张"
            )
        rms, camera_matrix, dist, rvecs, tvecs = cv2.calibrateCamera(
            self.obj_points, self.img_points, image_size, None, None
        )
        return camera_matrix, dist, rvecs, tvecs, rms

    def per_view_rms(self, camera_matrix, dist, rvecs, tvecs) -> List[float]:
        """逐张图片的 RMS 重投影误差 (px)。

        报告里的误差分布图用这个。整体 RMS 只有一个数, 看不出是"整体偏差大"
        还是"某一两张拖后腿" —— 后者的处理方式是删掉那两张重算, 前者是重拍。
        """
        result = []
        for index in range(len(self.obj_points)):
            projected, _ = cv2.projectPoints(
                self.obj_points[index], rvecs[index], tvecs[index], camera_matrix, dist
            )
            # 形状无关: 4.x 给 (N,1,2), 5.0 给 (N,2)。见模块 docstring 约束 3。
            observed = np.asarray(self.img_points[index], dtype=np.float64).reshape(-1, 2)
            expected = np.asarray(projected, dtype=np.float64).reshape(-1, 2)
            residual = observed - expected
            result.append(float(np.sqrt(np.mean(np.sum(residual ** 2, axis=1)))))
        return result


def horizontal_fov_deg(fx: float, width: int) -> float:
    """由 fx 反推水平视场角 (度)。

    输出报告里带上它, 是因为"fx=520"对人没有意义, 而"HFOV=63°"一眼能看出
    是不是自己手里那颗镜头。validate-calibration.py 的合理性区间也建在它上面。
    """
    return math.degrees(2.0 * math.atan(0.5 * width / fx))


def build_camera_info(camera_matrix, dist, width: int, height: int,
                      camera_name: str) -> dict:
    """按 ROS camera_info YAML 布局组装字典 (不含本项目的元数据段)。

    键名与 `camera_calibration` 包写出来的一致, 因此 `camera_info_manager`
    能直接加载。`rectification_matrix` 取单位阵、`projection_matrix` 取 [K|0]
    是单目未校正相机的标准取值。
    """
    k_flat = [float(v) for v in np.asarray(camera_matrix, dtype=np.float64).reshape(-1)]
    d_flat = [float(v) for v in np.asarray(dist, dtype=np.float64).reshape(-1)]
    projection = np.zeros((3, 4), dtype=np.float64)
    projection[:, :3] = np.asarray(camera_matrix, dtype=np.float64)
    return {
        "image_width": int(width),
        "image_height": int(height),
        "camera_name": camera_name,
        "camera_matrix": {"rows": 3, "cols": 3, "data": k_flat},
        # 5 个系数 (k1,k2,p1,p2,k3) 对应 plumb_bob。若将来换鱼眼模型,
        # 这里要一并改成 equidistant, 否则下游按 plumb_bob 去畸变会错得很隐蔽。
        "distortion_model": "plumb_bob",
        "distortion_coefficients": {"rows": 1, "cols": len(d_flat), "data": d_flat},
        "rectification_matrix": {
            "rows": 3, "cols": 3,
            "data": [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0],
        },
        "projection_matrix": {
            "rows": 3, "cols": 4,
            "data": [float(v) for v in projection.reshape(-1)],
        },
    }


def build_metadata(rms: float, per_view: Sequence[float], image_names: Sequence[str],
                   pattern_size: Tuple[int, int], square_size: float,
                   width: int, fx: float, fy: float,
                   calibration_date: str, serial: str) -> dict:
    """本项目自有的溯源段。

    ⚠ 这一段是 camera_info 规范之外的键。camera_info_manager 按键名读取,
    照实现看会忽略未知键 —— 但**本机没有 ROS, 这一条没有实测过**。
    上机第一件事就是核实 (核实步骤见 README §5), 若真被拒, 用
    `--strict-camera-info` 生成不含本段的版本, 溯源信息另存 sidecar。
    """
    return {
        "calibration_date": calibration_date,
        "hardware_serial": serial,
        "rms_reprojection_error": float(rms),
        "per_view_rms": [float(v) for v in per_view],
        "image_names": list(image_names),
        "num_images": len(per_view),
        "checkerboard_inner_corners": [int(pattern_size[0]), int(pattern_size[1])],
        "square_size_m": float(square_size),
        "horizontal_fov_deg": horizontal_fov_deg(fx, width),
        "focal_aspect_ratio": float(fy / fx) if fx else 0.0,
        "opencv_version": cv2.__version__,
        "tool": "calibrate-camera.py",
    }


def load_images(directory: str) -> List[str]:
    """列出目录下所有图片，按文件名排序 (跨平台顺序一致)。"""
    paths: List[str] = []
    for extension in IMAGE_EXTENSIONS:
        paths.extend(glob.glob(os.path.join(directory, extension)))
        paths.extend(glob.glob(os.path.join(directory, extension.upper())))
    # Windows 上大小写不敏感, 上面两轮 glob 会把同一个文件收两遍。
    return sorted(set(os.path.normcase(os.path.abspath(p)) for p in paths))


def run(input_dir: str, pattern_size: Tuple[int, int], square_size: float,
        output_path: str, camera_name: str, min_images: int, max_rms: float,
        serial: str, calibration_date: str, strict_camera_info: bool,
        quiet: bool = False) -> Tuple[dict, float]:
    """跑完整标定流程并落盘。

    Returns:
        (写进 YAML 的字典, RMS)。

    Raises:
        FileNotFoundError: 目录里没有可读图片。
        ValueError: 可用图片数不足, 或图片尺寸不一致。
    """
    def say(message: str) -> None:
        if not quiet:
            print(message)

    if input_dir.endswith(".bag"):
        raise ValueError(
            "本脚本只吃图片目录, 不读 rosbag (读 bag 需要 ROS 环境)。"
            "先用 convert-bag-to-kalibr.py --extract-images 抽帧, 再跑本脚本。"
        )

    paths = load_images(input_dir)
    if not paths:
        raise FileNotFoundError(f"{input_dir} 里没有找到图片 ({', '.join(IMAGE_EXTENSIONS)})")

    calibrator = CameraCalibrator(pattern_size, square_size)
    image_size: Optional[Tuple[int, int]] = None
    for path in paths:
        image = cv2.imread(path)
        if image is None:
            say(f"  ! {os.path.basename(path)} (读不出来, 跳过)")
            continue
        size = (image.shape[1], image.shape[0])
        if image_size is None:
            image_size = size
        elif size != image_size:
            # 混着不同分辨率标定出来的 K 没有意义, 而且不会有任何报错。
            raise ValueError(
                f"图片尺寸不一致: {os.path.basename(path)} 是 {size}, 之前是 {image_size}"
            )
        if calibrator.add_image(image, os.path.basename(path)):
            say(f"  + {os.path.basename(path)}")
        else:
            say(f"  - {os.path.basename(path)} (没找到 {pattern_size[0]}x{pattern_size[1]} 棋盘格)")

    assert image_size is not None  # 上面 paths 非空且至少读出一张才会走到这
    say(f"\n用 {len(calibrator.img_points)}/{len(paths)} 张图片标定 ...")
    camera_matrix, dist, rvecs, tvecs, rms = calibrator.calibrate(image_size, min_images)
    per_view = calibrator.per_view_rms(camera_matrix, dist, rvecs, tvecs)

    fx, fy = float(camera_matrix[0, 0]), float(camera_matrix[1, 1])
    document = build_camera_info(camera_matrix, dist, image_size[0], image_size[1],
                                 camera_name)
    if not strict_camera_info:
        document["air_ground_calibration"] = build_metadata(
            rms, per_view, calibrator.image_names, pattern_size, square_size,
            image_size[0], fx, fy, calibration_date, serial,
        )

    with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
        yaml.safe_dump(document, handle, default_flow_style=False,
                       allow_unicode=True, sort_keys=False)

    say("\n=== 标定结果 ===")
    say(f"分辨率      : {image_size[0]}x{image_size[1]}")
    say(f"K           : fx={fx:.3f} fy={fy:.3f} "
        f"cx={camera_matrix[0, 2]:.3f} cy={camera_matrix[1, 2]:.3f}")
    say(f"畸变        : {np.asarray(dist).reshape(-1)}")
    say(f"水平视场角  : {horizontal_fov_deg(fx, image_size[0]):.1f}°")
    say(f"RMS 重投影  : {rms:.4f} px  (逐张最大 {max(per_view):.4f} px)")
    say(f"\n已写入: {output_path}")
    return document, rms


def parse_pattern(text: str) -> Tuple[int, int]:
    """解析 "9x6" 形式的内角点数。"""
    parts = text.lower().split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("pattern 格式应为 WxH, 如 9x6")
    try:
        cols, rows = int(parts[0]), int(parts[1])
    except ValueError:
        raise argparse.ArgumentTypeError("pattern 的两个维度都必须是整数")
    return cols, rows


def main(argv: Optional[Sequence[str]] = None) -> int:
    """命令行入口。退出码: 0 通过 / 1 RMS 超限或标定失败 / 2 环境或用法错误。"""
    parser = argparse.ArgumentParser(description="相机内参标定 (棋盘格 + OpenCV)")
    parser.add_argument("--input", required=True, help="棋盘格图片所在目录")
    parser.add_argument("--pattern", type=parse_pattern, default="9x6",
                        help="棋盘格内角点数 WxH (默认 9x6)")
    parser.add_argument("--square-size", type=float, default=0.030,
                        help="方格边长, 单位 m (默认 0.030)")
    parser.add_argument("--output", default="camera_intrinsics.yaml", help="输出 YAML 路径")
    parser.add_argument("--camera-name", default="camera",
                        help="camera_info 的 camera_name, 如 car_openmv / drone_d435i_color")
    parser.add_argument("--min-images", type=int, default=DEFAULT_MIN_IMAGES,
                        help=f"最少可用图片数 (默认 {DEFAULT_MIN_IMAGES})")
    parser.add_argument("--max-rms", type=float, default=DEFAULT_MAX_RMS,
                        help=f"RMS 上限, 超过则退出码 1 (默认 {DEFAULT_MAX_RMS} px)")
    parser.add_argument("--serial", default="unknown",
                        help="硬件序列号, 写进溯源段 (D435i 可用 rs-enumerate-devices 查)")
    parser.add_argument("--date", default=None,
                        help="标定日期 YYYY-MM-DD, 默认取今天")
    parser.add_argument("--strict-camera-info", action="store_true",
                        help="只写 camera_info 标准键, 不带本项目的溯源段")
    args = parser.parse_args(argv)

    pattern = args.pattern if isinstance(args.pattern, tuple) else parse_pattern(args.pattern)
    calibration_date = args.date or date_type.today().isoformat()

    try:
        _, rms = run(args.input, pattern, args.square_size, args.output,
                     args.camera_name, args.min_images, args.max_rms,
                     args.serial, calibration_date, args.strict_camera_info)
    except (FileNotFoundError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    if rms > args.max_rms:
        print(f"WARNING: RMS 重投影误差 {rms:.3f} px 超过上限 {args.max_rms} px, "
              "建议重新采集 —— 多换几个视角、把棋盘格铺满画面四角。",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
