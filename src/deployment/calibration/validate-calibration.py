#!/usr/bin/env python3
"""标定结果合理性检查 —— 纯 YAML, 不依赖 OpenCV / ROS。

用法:
    python3 validate-calibration.py camera_intrinsics.yaml
    python3 validate-calibration.py imu_intrinsics.yaml
    python3 validate-calibration.py --type camera a.yaml --type imu b.yaml

退出码: 0 全部通过 / 1 有失败项 / 2 用法或解析错误。

**为什么不用 cv2.FileStorage 读**: 输出格式是普通 YAML (ADR-0011),
读它只需要 PyYAML。这不是省一个依赖那么简单 —— 校验脚本必须能在
任何机器上跑, 包括没装 OpenCV 的树莓派和只装了 yamllint 的 CI job。
一个"要先装 OpenCV 才能检查标定结果"的检查, 现场没人会跑。

**这些检查能抓什么、抓不到什么**: 它们全是**合理性**检查, 不是正确性检查。
能抓住"单位写错了""主点填成了图像尺寸""fx/fy 抄反了"这类量级错误;
抓不住"标定板尺寸量错 1mm"这种在所有指标上都自洽的系统偏差 ——
后者只能靠实测距离比对 (README §5)。
"""

import argparse
import math
import sys
from typing import Any, Dict, List, Optional, Sequence, Tuple

import yaml


# --- 相机合理性区间 -------------------------------------------------------
#
# 焦距区间用**视场角**表达, 不用"0.5w < fx < 2.0w"那种像素比例。
#
#   fx = (w/2) / tan(HFOV/2)
#
# 换算过来, 0.5w < fx < 2.0w 等价于 HFOV ∈ (28.1°, 90.0°)。这个区间
# 装不下本项目自己的硬件: RealSense D435i 的**深度**流标称 87°±3° HFOV,
# 对应 fx ≈ 0.527w, 上沿 90° 正好压在 fx = 0.5w 的边界上 —— 也就是说
# 一次完全正常的深度相机标定有可能被判成"焦距不合理"。
# (D435i 彩色流 69.4° → fx ≈ 0.722w, 余量充足; 出问题的是深度流。)
#
# 所以区间按镜头能取到的物理范围定: 20°(长焦) ~ 120°(广角)。
# 再宽就没有筛查意义了, 再窄就会误杀真实硬件。
MIN_HFOV_DEG = 20.0
MAX_HFOV_DEG = 120.0

#: 方形像素假设下 fy/fx 应当≈1。本项目用到的相机 (OpenMV OV7725 / D435i)
#: 都是方形像素; 10% 的容差是留给标定噪声的, 不是留给非方形像素的。
#: 真换了非方形像素的传感器, 这条要改成读数据手册的标称比值。
MAX_FOCAL_ASPECT_DEVIATION = 0.10

#: 主点偏离图像中心的上限 (占图像尺寸的比例)。
MAX_PRINCIPAL_POINT_OFFSET = 0.20

#: RMS 重投影误差上限 (px)。与 calibrate-camera.py 的 --max-rms 默认值一致。
MAX_RMS_PX = 0.5

#: plumb_bob 的径向系数量级上限。k1 超过这个数基本意味着模型选错了
#: (比如该用 equidistant 鱼眼模型)。
MAX_RADIAL_COEFFICIENT = 1.5

CAMERA_REQUIRED_KEYS = (
    "image_width", "image_height", "camera_matrix",
    "distortion_model", "distortion_coefficients",
    "rectification_matrix", "projection_matrix",
)

# --- IMU 合理性区间 -------------------------------------------------------
#
# 上下界按"消费级 MEMS IMU"的量级定, 覆盖 ICM42688-P 与 Pixhawk 6C 板载的
# ICM-42688/BMI055 这一档。目的是抓单位错误 (deg/s 当成 rad/s 写进来会大 57 倍),
# 不是替代数据手册。
IMU_REQUIRED_KEYS = (
    "gyro_bias", "accel_bias",
    "gyro_noise_density", "accel_noise_density",
    "gyro_random_walk", "accel_random_walk",
)
MAX_GYRO_BIAS = 0.20            # rad/s  (≈11 deg/s, 已经是很差的片子)
MAX_ACCEL_BIAS = 1.0            # m/s^2  (≈0.1 g)
GYRO_NOISE_DENSITY_RANGE = (1e-6, 1e-2)     # rad/s/√Hz
ACCEL_NOISE_DENSITY_RANGE = (1e-5, 1e-1)    # m/s^2/√Hz
GYRO_RANDOM_WALK_RANGE = (1e-9, 1e-3)       # rad/s^2/√Hz
ACCEL_RANDOM_WALK_RANGE = (1e-8, 1e-2)      # m/s^3/√Hz

# --- 相机-IMU 外参合理性区间 ---------------------------------------------
#
#: 相机与 IMU 都装在同一台车/机上, 基线是厘米量级。0.5 m 已经比车体还长,
#: 超过它基本意味着单位写成了 mm 或者平移分量抄串了行。
MAX_EXTRINSIC_BASELINE_M = 0.5
#: Kalibr 解出的相机-IMU 时间偏移。超过 100ms 说明两路数据源的时间戳
#: 不是同一个时钟 (常见于相机走 USB 而 IMU 走 I2C 且没做时间同步)。
MAX_TIMESHIFT_S = 0.1


class CheckList:
    """一组具名检查项, 记录通过/失败并可打印。"""

    def __init__(self, title: str) -> None:
        """初始化。

        Args:
            title: 打印时的小标题。
        """
        self.title = title
        self.results: List[Tuple[str, bool, str]] = []

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        """记录一项检查。

        Args:
            name: 检查项名称。
            ok: 是否通过。
            detail: 实测值等补充说明, 通过时也打印 —— 一份只有勾的报告
                没法判断"通过得多勉强"。

        Returns:
            ok 原样返回, 方便串联。
        """
        self.results.append((name, bool(ok), detail))
        return bool(ok)

    @property
    def ok(self) -> bool:
        """全部通过则为 True。"""
        return all(ok for _, ok, _ in self.results)

    def render(self) -> str:
        """渲染成文本报告。"""
        lines = [f"=== {self.title} ==="]
        for name, ok, detail in self.results:
            mark = "✓" if ok else "✗"
            suffix = f"  ({detail})" if detail else ""
            lines.append(f"  {mark} {name}{suffix}")
        return "\n".join(lines)


def matrix_data(node: Any, name: str) -> List[float]:
    """取出 {rows, cols, data} 矩阵节点的数据, 顺带校验尺寸。

    Raises:
        ValueError: 结构不对或 rows*cols 与 data 长度不符。
    """
    if not isinstance(node, dict):
        raise ValueError(f"{name} 不是 {{rows, cols, data}} 结构")
    for key in ("rows", "cols", "data"):
        if key not in node:
            raise ValueError(f"{name} 缺少 {key}")
    rows, cols, data = int(node["rows"]), int(node["cols"]), list(node["data"])
    if rows * cols != len(data):
        raise ValueError(f"{name} 的 rows×cols={rows * cols} 与 data 长度 {len(data)} 不符")
    return [float(v) for v in data]


def horizontal_fov_deg(fx: float, width: int) -> float:
    """由 fx 反推水平视场角 (度)。"""
    return math.degrees(2.0 * math.atan(0.5 * width / fx))


def focal_bounds(width: int) -> Tuple[float, float]:
    """由视场角区间换算 fx 的允许区间 (px)。"""
    lower = 0.5 * width / math.tan(math.radians(MAX_HFOV_DEG) / 2.0)
    upper = 0.5 * width / math.tan(math.radians(MIN_HFOV_DEG) / 2.0)
    return lower, upper


def validate_camera(document: Dict[str, Any]) -> CheckList:
    """检查一份相机内参 YAML。"""
    checks = CheckList("相机内参")

    missing = [key for key in CAMERA_REQUIRED_KEYS if key not in document]
    if not checks.check("camera_info 必需键齐全", not missing,
                        f"缺 {', '.join(missing)}" if missing else "7/7"):
        return checks

    width, height = int(document["image_width"]), int(document["image_height"])
    checks.check("分辨率为正", width > 0 and height > 0, f"{width}x{height}")

    try:
        k = matrix_data(document["camera_matrix"], "camera_matrix")
        d = matrix_data(document["distortion_coefficients"], "distortion_coefficients")
        rect = matrix_data(document["rectification_matrix"], "rectification_matrix")
        proj = matrix_data(document["projection_matrix"], "projection_matrix")
    except ValueError as error:
        checks.check("矩阵结构合法", False, str(error))
        return checks
    checks.check("矩阵结构合法", True, "camera_matrix / distortion / rect / projection")

    fx, fy, cx, cy, skew = k[0], k[4], k[2], k[5], k[1]

    lower, upper = focal_bounds(width)
    fov = horizontal_fov_deg(fx, width) if fx > 0 else float("nan")
    checks.check(
        f"fx 在 HFOV {MIN_HFOV_DEG:.0f}°~{MAX_HFOV_DEG:.0f}° 对应区间内",
        fx > 0 and lower <= fx <= upper,
        f"fx={fx:.1f} → HFOV={fov:.1f}°, 允许 {lower:.1f}~{upper:.1f}",
    )

    aspect = fy / fx if fx else 0.0
    checks.check(
        "fy/fx 接近 1 (方形像素)",
        abs(aspect - 1.0) <= MAX_FOCAL_ASPECT_DEVIATION,
        f"fy/fx={aspect:.4f}, 容差 ±{MAX_FOCAL_ASPECT_DEVIATION:.2f}",
    )

    checks.check(
        "主点靠近图像中心",
        abs(cx - width / 2.0) <= MAX_PRINCIPAL_POINT_OFFSET * width
        and abs(cy - height / 2.0) <= MAX_PRINCIPAL_POINT_OFFSET * height,
        f"cx 偏 {cx - width / 2.0:+.1f}px, cy 偏 {cy - height / 2.0:+.1f}px, "
        f"上限 ±{MAX_PRINCIPAL_POINT_OFFSET * width:.0f}/±{MAX_PRINCIPAL_POINT_OFFSET * height:.0f}",
    )

    # 倾斜项。cv2.calibrateCamera 不解这一项, 恒为 0; 非 0 说明这份 YAML
    # 不是本流水线产出的, 值得看一眼再用。
    checks.check("倾斜项为 0", abs(skew) < 1e-9, f"K[0,1]={skew:g}")

    checks.check("K 的最后一行是 [0,0,1]",
                 abs(k[6]) < 1e-9 and abs(k[7]) < 1e-9 and abs(k[8] - 1.0) < 1e-9,
                 f"[{k[6]:g}, {k[7]:g}, {k[8]:g}]")

    model = str(document["distortion_model"])
    checks.check("畸变模型为 plumb_bob", model == "plumb_bob", model)
    checks.check("畸变系数个数合法", len(d) in (4, 5, 8, 12, 14), f"{len(d)} 个")
    if len(d) >= 2:
        checks.check(
            "径向畸变量级合理",
            all(abs(v) <= MAX_RADIAL_COEFFICIENT for v in (d[0], d[1])),
            f"k1={d[0]:+.4f} k2={d[1]:+.4f}, 上限 ±{MAX_RADIAL_COEFFICIENT}",
        )

    identity = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]
    checks.check("单目未校正: rectification_matrix 为单位阵",
                 all(abs(a - b) < 1e-9 for a, b in zip(rect, identity)), "")

    # P 的左 3×3 必须等于 K, 否则下游用 P 反投影会与用 K 得到不同结果 ——
    # 而 image_geometry 用的是 P。两者不一致的表现是"标定看着对, 投影差几个像素"。
    proj_left = [proj[0], proj[1], proj[2], proj[4], proj[5], proj[6],
                 proj[8], proj[9], proj[10]]
    checks.check("projection_matrix 左 3×3 与 camera_matrix 一致",
                 all(abs(a - b) < 1e-6 for a, b in zip(proj_left, k)), "")
    checks.check("projection_matrix 第 4 列为 0 (单目)",
                 all(abs(proj[i]) < 1e-9 for i in (3, 7, 11)), "")

    metadata = document.get("air_ground_calibration")
    if isinstance(metadata, dict) and "rms_reprojection_error" in metadata:
        rms = float(metadata["rms_reprojection_error"])
        checks.check(f"RMS 重投影误差 < {MAX_RMS_PX} px", rms < MAX_RMS_PX, f"{rms:.4f} px")
        per_view = [float(v) for v in metadata.get("per_view_rms", [])]
        if per_view:
            checks.check("单张最差 RMS 不超过整体的 3 倍",
                         max(per_view) <= 3.0 * rms if rms > 0 else False,
                         f"最差 {max(per_view):.4f} px / 整体 {rms:.4f} px")
        count = int(metadata.get("num_images", 0))
        checks.check("标定图片数 >= 10", count >= 10, f"{count} 张")
    else:
        # 不算失败: --strict-camera-info 生成的文件本来就没有这一段。
        # 但要说出来, 否则"没查"会被读成"查过了"。
        checks.check("溯源段存在 (RMS/张数无法检查)", True,
                     "缺 air_ground_calibration 段 —— 未检查 RMS, 属 strict 模式的预期")

    return checks


def validate_imu(document: Dict[str, Any]) -> CheckList:
    """检查一份 IMU 内参 YAML。"""
    checks = CheckList("IMU 内参")

    missing = [key for key in IMU_REQUIRED_KEYS if key not in document]
    if not checks.check("必需字段齐全", not missing,
                        f"缺 {', '.join(missing)}" if missing else f"{len(IMU_REQUIRED_KEYS)}/6"):
        return checks

    gyro_bias = [float(v) for v in document["gyro_bias"]]
    accel_bias = [float(v) for v in document["accel_bias"]]
    checks.check("gyro_bias / accel_bias 均为三轴",
                 len(gyro_bias) == 3 and len(accel_bias) == 3,
                 f"{len(gyro_bias)} / {len(accel_bias)}")
    checks.check(f"|gyro_bias| < {MAX_GYRO_BIAS} rad/s",
                 all(abs(v) < MAX_GYRO_BIAS for v in gyro_bias),
                 f"max {max(abs(v) for v in gyro_bias):.5f}")
    checks.check(f"|accel_bias| < {MAX_ACCEL_BIAS} m/s²",
                 all(abs(v) < MAX_ACCEL_BIAS for v in accel_bias),
                 f"max {max(abs(v) for v in accel_bias):.5f}")

    ranges = {
        "gyro_noise_density": (GYRO_NOISE_DENSITY_RANGE, "rad/s/√Hz"),
        "accel_noise_density": (ACCEL_NOISE_DENSITY_RANGE, "m/s²/√Hz"),
        "gyro_random_walk": (GYRO_RANDOM_WALK_RANGE, "rad/s²/√Hz"),
        "accel_random_walk": (ACCEL_RANDOM_WALK_RANGE, "m/s³/√Hz"),
    }
    for key, ((low, high), unit) in ranges.items():
        value = float(document[key])
        checks.check(f"{key} 在 MEMS 量级内",
                     low <= value <= high,
                     f"{value:.3e} {unit}, 允许 {low:.0e}~{high:.0e}")

    # 离散标准差与连续密度的换算是 real_sensors.yaml 的入口 (ADR-0011)。
    # 写进 YAML 的派生值要与密度自洽, 否则驱动里填的协方差和标定报告对不上。
    rate = document.get("sample_rate_hz")
    derived = document.get("derived_for_driver")
    if rate and isinstance(derived, dict):
        rate = float(rate)
        for density_key, derived_key in (
            ("gyro_noise_density", "gyro_noise_stddev"),
            ("accel_noise_density", "accel_noise_stddev"),
        ):
            if derived_key not in derived:
                checks.check(f"{derived_key} 存在", False, "derived_for_driver 段缺该键")
                continue
            expected = float(document[density_key]) * math.sqrt(rate)
            actual = float(derived[derived_key])
            checks.check(
                f"{derived_key} = {density_key}×√{rate:.0f}",
                math.isclose(actual, expected, rel_tol=1e-6),
                f"{actual:.6e} vs 期望 {expected:.6e}",
            )
    else:
        checks.check("driver 派生值段存在", False,
                     "缺 sample_rate_hz 或 derived_for_driver —— "
                     "没有它 real_sensors.yaml 没法从本文件取值")

    # 随机游走可信度。calibrate-imu.py 在静置数据不够长时会把它置 false,
    # 这里必须**判失败**而不是提示 —— 一个由白噪声段外推出来的随机游走系数
    # 会被 robot_localization 原样采信, 而它的错法是让滤波器看起来在工作。
    quality = document.get("quality")
    if isinstance(quality, dict):
        reliable = bool(quality.get("random_walk_reliable"))
        tau_max = float(quality.get("allan_tau_max_s", 0.0))
        crossover = float(quality.get("white_noise_crossover_tau_s", 0.0))
        checks.check(
            "random_walk 由足够长的静置数据解出",
            reliable,
            f"status={quality.get('status')}, τ_max={tau_max:.1f}s, "
            f"白噪声/随机游走交点 τ≈{crossover:.1f}s",
        )
    else:
        checks.check("quality 段存在", False,
                     "缺 quality —— 无法判断随机游走系数是否可信")

    return checks


def validate_extrinsic(document: Dict[str, Any]) -> CheckList:
    """检查一份相机-IMU 外参 YAML。

    刻意不引 numpy: 本脚本要能在只装了 PyYAML 的机器上跑 (见模块 docstring),
    而 3×3 的正交性和行列式手算就是十几行。
    """
    checks = CheckList("相机-IMU 外参")

    if not checks.check("T_cam_imu 存在", "T_cam_imu" in document):
        return checks
    try:
        transform = matrix_data(document["T_cam_imu"], "T_cam_imu")
    except ValueError as error:
        checks.check("T_cam_imu 结构合法", False, str(error))
        return checks
    if not checks.check("T_cam_imu 是 4×4", len(transform) == 16, f"{len(transform)} 个元素"):
        return checks

    # 占位模板的 status 是 PLACEHOLDER。单位阵本身是合法的 SE(3),
    # 所有几何检查都会通过 —— 只有 status 能区分"没标定"和"外参恰好为零"。
    status = str(document.get("status", ""))
    checks.check("status 已填写 (非 PLACEHOLDER)",
                 status not in ("", "PLACEHOLDER"),
                 f"status={status or '缺失'}")

    checks.check("direction 字段写明了变换方向",
                 "T_cam_imu" in str(document.get("direction", "")),
                 str(document.get("direction", "缺失"))[:60])

    rotation = [transform[0:3], transform[4:7], transform[8:11]]
    translation = [transform[3], transform[7], transform[11]]

    checks.check("最后一行为 [0,0,0,1]",
                 all(abs(transform[12 + i]) < 1e-9 for i in range(3))
                 and abs(transform[15] - 1.0) < 1e-9,
                 f"{[round(transform[12 + i], 6) for i in range(4)]}")

    # R^T·R 应为单位阵
    max_orthonormality_error = 0.0
    for i in range(3):
        for j in range(3):
            dot = sum(rotation[k][i] * rotation[k][j] for k in range(3))
            expected = 1.0 if i == j else 0.0
            max_orthonormality_error = max(max_orthonormality_error, abs(dot - expected))
    checks.check("旋转块正交", max_orthonormality_error < 1e-6,
                 f"max|RᵀR - I| = {max_orthonormality_error:.2e}")

    determinant = (
        rotation[0][0] * (rotation[1][1] * rotation[2][2] - rotation[1][2] * rotation[2][1])
        - rotation[0][1] * (rotation[1][0] * rotation[2][2] - rotation[1][2] * rotation[2][0])
        + rotation[0][2] * (rotation[1][0] * rotation[2][1] - rotation[1][1] * rotation[2][0])
    )
    # det = -1 是镜像, 不是旋转。它的来源通常是某一轴的符号在转格式时抄反了,
    # 而这类错误在轨迹上表现为"走直线正常、一转弯就发散"。
    checks.check("旋转块行列式为 +1 (不是镜像)", abs(determinant - 1.0) < 1e-6,
                 f"det = {determinant:.9f}")

    baseline = math.sqrt(sum(v * v for v in translation))
    checks.check(f"相机-IMU 基线 < {MAX_EXTRINSIC_BASELINE_M} m",
                 baseline < MAX_EXTRINSIC_BASELINE_M,
                 f"|t| = {baseline:.4f} m")

    timeshift = document.get("timeshift_cam_imu")
    if timeshift is None:
        checks.check("timeshift_cam_imu 存在", False, "缺失")
    else:
        checks.check(f"|timeshift_cam_imu| < {MAX_TIMESHIFT_S} s",
                     abs(float(timeshift)) < MAX_TIMESHIFT_S,
                     f"{float(timeshift):+.6f} s")

    return checks


def detect_type(document: Dict[str, Any]) -> Optional[str]:
    """按键名判断这是相机、IMU 还是相机-IMU 外参的标定结果。"""
    if "camera_matrix" in document:
        return "camera"
    if "T_cam_imu" in document:
        return "extrinsic"
    if "gyro_noise_density" in document or "gyro_bias" in document:
        return "imu"
    return None


def validate_file(path: str, forced_type: Optional[str] = None) -> CheckList:
    """读取并校验一份标定 YAML。

    Raises:
        ValueError: 文件不是映射, 或类型判断不出来。
    """
    with open(path, "r", encoding="utf-8") as handle:
        document = yaml.safe_load(handle)
    if not isinstance(document, dict):
        raise ValueError(f"{path} 的顶层不是键值映射")

    kind = forced_type or detect_type(document)
    if kind == "camera":
        return validate_camera(document)
    if kind == "imu":
        return validate_imu(document)
    if kind == "extrinsic":
        return validate_extrinsic(document)
    raise ValueError(
        f"{path} 认不出是相机 / IMU / 外参标定结果 "
        "(既无 camera_matrix, 也无 T_cam_imu 或 gyro_bias); 用 --type 显式指定"
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    """命令行入口。"""
    parser = argparse.ArgumentParser(description="标定结果合理性检查")
    parser.add_argument("files", nargs="+", help="标定结果 YAML (可给多个)")
    parser.add_argument("--type", choices=("camera", "imu", "extrinsic"), default=None,
                        help="强制指定类型, 默认按键名自动判断")
    args = parser.parse_args(argv)

    all_ok = True
    for path in args.files:
        try:
            checks = validate_file(path, args.type)
        except (OSError, ValueError, yaml.YAMLError) as error:
            print(f"ERROR: {error}", file=sys.stderr)
            return 2
        print(checks.render())
        print()
        all_ok = all_ok and checks.ok

    print("全部通过" if all_ok else "有失败项 —— 见上面的 ✗")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
