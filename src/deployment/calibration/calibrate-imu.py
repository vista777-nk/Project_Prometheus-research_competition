#!/usr/bin/env python3
"""IMU 内参标定 —— 静置数据的 Allan 方差解算。

用法:
    python3 calibrate-imu.py --input imu_static.csv --rate 100 \
        --output imu_intrinsics.yaml --sensor icm42688

输入是一份**静置**采集的 CSV: `timestamp,gx,gy,gz,ax,ay,az`
(陀螺 rad/s, 加速度 m/s²)。从 rosbag 转 CSV 见 convert-bag-to-kalibr.py。

--------------------------------------------------------------------------
它解什么、解不了什么 —— 先说清楚, 免得拿一份 15 分钟的数据当 2 小时用
--------------------------------------------------------------------------

Allan 偏差曲线在两段上给出两个不同的量:

    σ(τ) = N/√τ          τ 小 —— 白噪声, 斜率 -1/2, N 即噪声密度
    σ(τ) = K·√(τ/3)      τ 大 —— 随机游走, 斜率 +1/2, K 即随机游走系数

两条线的交点在

    τ_cross = √3 · N / K

拿本项目的量级代进去: 陀螺 N≈2e-4 rad/s/√Hz, K≈2e-6 rad/s²/√Hz,
得 τ_cross ≈ 173 s。要在 +1/2 斜率段上取到可信的点, τ 至少要到
τ_cross 的几倍, 而重叠式 Allan 方差在 τ 上还要留够聚类数 (经验上
总时长 ≥ 10τ), 于是**总时长要小时量级**。

这就是"静置 2 小时"的由来 —— 不是仪式, 是这条曲线的交点位置决定的。
15 分钟的数据能解出可信的 N (白噪声段 τ 只要几秒), 但解不出 K:
那一段曲线还完全被白噪声压着。本脚本会算出 K, 同时把
`quality.random_walk_reliable` 置为 false 并写明 τ_max ——
validate-calibration.py 会因此判失败。**这是设计如此**, 不是 bug:
一个没有依据的 K 会被 robot_localization 当真, 而它的错法是让滤波器
"看起来在工作"。

加速度计零偏还有一层更麻烦的事, 见 estimate_accel_bias() 的说明。
"""

import argparse
import math
import os
import sys
from datetime import date as date_type
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np
import yaml


GRAVITY = 9.80665

#: 拟合白噪声段用的 τ 区间 (s)。下界避开最短的几个 τ (聚类数虽多但受
#: 量化噪声影响), 上界留在明显早于 τ_cross 的地方。
WHITE_NOISE_TAU_RANGE = (0.05, 2.0)

#: 判"随机游走段可信"的门槛: 最大 τ 至少要到交点的这个倍数,
#: 且该段实测斜率要真的在上升。
RANDOM_WALK_TAU_MARGIN = 3.0
RANDOM_WALK_MIN_SLOPE = 0.2

AXES = ("x", "y", "z")


def load_csv(path: str) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """读取静置 IMU CSV。

    Args:
        path: CSV 路径, 允许 `#` 开头的注释行与一行表头。

    Returns:
        (timestamps, gyro Nx3, accel Nx3)。

    Raises:
        ValueError: 列数不对或没有数据行。
    """
    stamps: List[float] = []
    gyro: List[List[float]] = []
    accel: List[List[float]] = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(",")
            if len(parts) < 7:
                raise ValueError(f"{path}: 需要 7 列 (timestamp,gx,gy,gz,ax,ay,az), "
                                 f"读到 {len(parts)} 列")
            try:
                values = [float(v) for v in parts[:7]]
            except ValueError:
                continue  # 表头行
            stamps.append(values[0])
            gyro.append(values[1:4])
            accel.append(values[4:7])
    if not stamps:
        raise ValueError(f"{path} 里没有数据行")
    return np.array(stamps), np.array(gyro), np.array(accel)


def allan_deviation(samples: np.ndarray, rate_hz: float,
                    num_points: int = 60) -> Tuple[np.ndarray, np.ndarray]:
    """重叠式 (overlapping) Allan 偏差。

    用重叠式而不是经典分块式: 同样长度的数据, 重叠式的自由度高得多,
    曲线尾部不会抖成锯齿 —— 而尾部正是要读随机游走的地方。

    Args:
        samples: 一维采样序列 (一个轴)。
        rate_hz: 采样率。
        num_points: τ 取样点数 (对数均布)。

    Returns:
        (taus, deviations)。数据太短时返回两个空数组。
    """
    count = samples.size
    dt = 1.0 / rate_hz
    # 每个 τ 至少要留下 ~9 个独立聚类, 否则估计量本身的方差比信号还大。
    max_m = count // 9
    if max_m < 2:
        return np.empty(0), np.empty(0)

    m_values = np.unique(
        np.floor(np.logspace(0, math.log10(max_m), num_points)).astype(np.int64)
    )
    m_values = m_values[m_values >= 1]

    # θ 是角度/速度增量的积分 (cumulative sum × dt)
    theta = np.concatenate([[0.0], np.cumsum(samples) * dt])

    taus = []
    devs = []
    for m in m_values:
        if theta.size < 2 * m + 2:
            continue
        tau = m * dt
        diff = theta[2 * m:] - 2.0 * theta[m:-m] + theta[:-2 * m]
        variance = np.sum(diff ** 2) / (2.0 * tau ** 2 * diff.size)
        taus.append(tau)
        devs.append(math.sqrt(variance))
    return np.array(taus), np.array(devs)


def fit_slope(taus: np.ndarray, devs: np.ndarray) -> float:
    """在 log-log 上拟合一段曲线的斜率。数据不足返回 nan。"""
    if taus.size < 2:
        return float("nan")
    return float(np.polyfit(np.log10(taus), np.log10(devs), 1)[0])


def fit_noise_density(taus: np.ndarray, devs: np.ndarray) -> Tuple[float, float]:
    """在白噪声段拟合噪声密度 N。

    斜率固定成 -1/2 再取均值, 而不是二自由度自由拟合: 自由拟合会把
    尾部的随机游走一起吸进来, 得到一个偏大的 N 和一个不到 -0.5 的斜率,
    两个数都错但都"看起来合理"。这里额外把实测斜率一并返回, 让人能
    看出这一段到底像不像白噪声。

    Returns:
        (N, 实测斜率)。
    """
    low, high = WHITE_NOISE_TAU_RANGE
    mask = (taus >= low) & (taus <= high)
    if not np.any(mask):
        mask = taus <= max(taus.min() * 10.0, low)
    selected_tau, selected_dev = taus[mask], devs[mask]
    if selected_tau.size == 0:
        return float("nan"), float("nan")
    density = float(np.mean(selected_dev * np.sqrt(selected_tau)))
    return density, fit_slope(selected_tau, selected_dev)


def fit_random_walk(taus: np.ndarray, devs: np.ndarray,
                    noise_density: float) -> Tuple[float, float, bool]:
    """在曲线尾部拟合随机游走系数 K, 并判断这一段是否可信。

    Returns:
        (K, 尾段实测斜率, 是否可信)。
    """
    if taus.size == 0:
        return float("nan"), float("nan"), False
    tail_mask = taus >= taus.max() / 3.0
    tail_tau, tail_dev = taus[tail_mask], devs[tail_mask]
    if tail_tau.size == 0:
        return float("nan"), float("nan"), False

    coefficient = float(np.mean(tail_dev * np.sqrt(3.0 / tail_tau)))
    slope = fit_slope(tail_tau, tail_dev)

    # τ_cross = √3·N/K。K 是刚估出来的那个 (可能偏大), 但用它算出的
    # τ_cross 只是用来判"数据够不够长", 量级对就够了。
    crossover = math.sqrt(3.0) * noise_density / coefficient if coefficient > 0 else math.inf
    reliable = (
        math.isfinite(crossover)
        and taus.max() >= RANDOM_WALK_TAU_MARGIN * crossover
        and slope >= RANDOM_WALK_MIN_SLOPE
    )
    return coefficient, slope, reliable


def estimate_accel_bias(accel: np.ndarray, gravity_axis: str,
                        gravity_sign: float) -> Tuple[List[float], float]:
    """静置水平假设下的加速度计零偏。

    ⚠ 这个估计的精度上限由**摆放水平度**决定, 不由 IMU 决定。
    倾斜 θ 会在水平轴上造出 g·sinθ 的假零偏:

        θ = 0.5°  →  0.086 m/s²
        θ = 1.0°  →  0.171 m/s²

    而消费级 MEMS 加速度计的真实零偏就在 0.02~0.10 m/s² 这一档 ——
    也就是说, **半度的摆放误差就足以淹没被测量本身**。

    要真正把零偏与安装倾角分开, 需要六面翻转 (6-position tumble) 或
    在标定台上做多姿态最小二乘, 那是 Phase 2 的事。Phase 1 输出的
    accel_bias 只作为 robot_localization 的初值, 并在 YAML 里带上
    `accel_bias_method: static_level_assumption` 自曝这一点。

    Args:
        accel: Nx3 加速度采样 (m/s²)。
        gravity_axis: 重力所在轴 "x"/"y"/"z"。
        gravity_sign: +1 表示该轴静置读数为 +g。

    Returns:
        (三轴零偏, 重力矢量模长的实测值)。
    """
    mean = accel.mean(axis=0)
    expected = np.zeros(3)
    expected[AXES.index(gravity_axis)] = gravity_sign * GRAVITY
    return [float(v) for v in (mean - expected)], float(np.linalg.norm(mean))


def worst_axis(per_axis: Dict[str, float]) -> Tuple[str, float]:
    """取三轴里最差 (最大) 的一项。"""
    axis = max(per_axis, key=lambda k: per_axis[k])
    return axis, per_axis[axis]


def calibrate(timestamps: np.ndarray, gyro: np.ndarray, accel: np.ndarray,
              rate_hz: Optional[float], gravity_axis: str,
              gravity_sign: float) -> Dict[str, object]:
    """跑完整 Allan 解算, 返回待写入 YAML 的字典。"""
    if rate_hz is None:
        span = float(timestamps[-1] - timestamps[0])
        if span <= 0:
            raise ValueError("时间戳没有前进, 无法推断采样率; 用 --rate 显式指定")
        rate_hz = (timestamps.size - 1) / span
    duration = timestamps.size / rate_hz

    gyro_density: Dict[str, float] = {}
    gyro_walk: Dict[str, float] = {}
    accel_density: Dict[str, float] = {}
    accel_walk: Dict[str, float] = {}
    white_slopes: Dict[str, float] = {}
    tail_slopes: Dict[str, float] = {}
    reliability: List[bool] = []
    curves: Dict[str, object] = {}
    tau_max = 0.0

    for index, axis in enumerate(AXES):
        for label, data, densities, walks in (
            ("gyro", gyro, gyro_density, gyro_walk),
            ("accel", accel, accel_density, accel_walk),
        ):
            taus, devs = allan_deviation(data[:, index], rate_hz)
            if taus.size == 0:
                raise ValueError(
                    f"数据太短, 算不出 Allan 曲线 (只有 {timestamps.size} 个采样)"
                )
            density, white_slope = fit_noise_density(taus, devs)
            coefficient, tail_slope, reliable = fit_random_walk(taus, devs, density)
            densities[axis] = density
            walks[axis] = coefficient
            white_slopes[f"{label}_{axis}"] = white_slope
            tail_slopes[f"{label}_{axis}"] = tail_slope
            reliability.append(reliable)
            tau_max = max(tau_max, float(taus.max()))
            if axis == "z":
                # 曲线数据只存 z 轴一条 (报告画图用)。三轴全存会让 YAML
                # 膨胀到几百行, 而看趋势一条就够。
                curves[f"{label}_allan"] = {
                    "tau_s": [float(v) for v in taus],
                    "deviation": [float(v) for v in devs],
                    "axis": "z",
                }

    gyro_bias = [float(v) for v in gyro.mean(axis=0)]
    accel_bias, gravity_magnitude = estimate_accel_bias(accel, gravity_axis, gravity_sign)

    # 写进 YAML 的是三轴里**最差**的那一个, 不是平均。
    # 噪声参数是拿去填协方差的, 填小了滤波器会过度相信 IMU。
    gyro_density_axis, gyro_density_value = worst_axis(gyro_density)
    accel_density_axis, accel_density_value = worst_axis(accel_density)
    _, gyro_walk_value = worst_axis(gyro_walk)
    _, accel_walk_value = worst_axis(accel_walk)

    random_walk_reliable = all(reliability)
    crossover = (math.sqrt(3.0) * gyro_density_value / gyro_walk_value
                 if gyro_walk_value > 0 else float("inf"))

    return {
        "sensor": None,          # 由调用方填
        "calibration_date": None,
        "sample_rate_hz": float(rate_hz),
        "duration_s": float(duration),
        "num_samples": int(timestamps.size),

        "gyro_bias": gyro_bias,                          # rad/s
        "accel_bias": accel_bias,                        # m/s^2
        "gyro_noise_density": gyro_density_value,        # rad/s/√Hz
        "accel_noise_density": accel_density_value,      # m/s^2/√Hz
        "gyro_random_walk": gyro_walk_value,             # rad/s^2/√Hz
        "accel_random_walk": accel_walk_value,           # m/s^3/√Hz

        "units": {
            "gyro_bias": "rad/s",
            "accel_bias": "m/s^2",
            "gyro_noise_density": "rad/s/sqrt(Hz)",
            "accel_noise_density": "m/s^2/sqrt(Hz)",
            "gyro_random_walk": "rad/s^2/sqrt(Hz)",
            "accel_random_walk": "m/s^3/sqrt(Hz)",
        },

        # real_sensors.yaml 的 icm42688 段收的是**离散**标准差, 不是密度。
        # 换算 sigma = density × √rate。少了这一步, 直接把密度填进驱动,
        # 100Hz 下协方差会小 100 倍 —— 滤波器会把 IMU 当成基准真值。
        "derived_for_driver": {
            "note": "填进 config/real_sensors.yaml 的 icm42688 段; "
                    "sigma = noise_density × sqrt(sample_rate_hz)",
            "gyro_noise_stddev": gyro_density_value * math.sqrt(rate_hz),
            "accel_noise_stddev": accel_density_value * math.sqrt(rate_hz),
        },

        "accel_bias_method": "static_level_assumption",
        "measured_gravity_magnitude": gravity_magnitude,

        "quality": {
            "random_walk_reliable": random_walk_reliable,
            "allan_tau_max_s": tau_max,
            "white_noise_crossover_tau_s": crossover,
            # 下界, 不是估计值: random_walk_reliable=false 时 K 一定被高估
            # (尾段还压着白噪声), 而 τ_cross=√3·N/K 与 K 成反比, 于是
            # 交点被低估、所需时长跟着被低估。按这个数去采一次多半还是不够,
            # 只能说明"至少要这么久"。
            "required_duration_s_lower_bound": crossover * RANDOM_WALK_TAU_MARGIN * 10.0,
            "white_noise_slope": white_slopes,
            "tail_slope": tail_slopes,
            "worst_axis": {"gyro": gyro_density_axis, "accel": accel_density_axis},
            "status": "PASS" if random_walk_reliable else "NEED_LONGER_LOG",
        },
        "allan_curves": curves,
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    """命令行入口。退出码: 0 / 1 随机游走段不可信 / 2 用法或数据错误。"""
    parser = argparse.ArgumentParser(description="IMU 内参标定 (Allan 方差)")
    parser.add_argument("--input", required=True, help="静置 IMU CSV")
    parser.add_argument("--rate", type=float, default=None,
                        help="采样率 Hz, 默认由时间戳推断")
    parser.add_argument("--output", default="imu_intrinsics.yaml", help="输出 YAML")
    parser.add_argument("--sensor", default="unknown",
                        help="传感器标识, 如 icm42688 / px4_internal")
    parser.add_argument("--gravity-axis", choices=AXES, default="z",
                        help="静置时重力所在轴 (默认 z)")
    parser.add_argument("--gravity-sign", type=float, default=1.0,
                        help="该轴静置读数的符号: +1 表示读到 +g (默认 +1)")
    parser.add_argument("--date", default=None, help="标定日期 YYYY-MM-DD")
    parser.add_argument("--allow-short-log", action="store_true",
                        help="随机游走段不可信时仍返回 0 (只想要噪声密度时用)")
    args = parser.parse_args(argv)

    if args.input.endswith(".bag"):
        print("ERROR: 本脚本吃 CSV, 不读 rosbag。先跑 "
              "convert-bag-to-kalibr.py --imu-csv 把 bag 转成 CSV。", file=sys.stderr)
        return 2

    try:
        timestamps, gyro, accel = load_csv(args.input)
        result = calibrate(timestamps, gyro, accel, args.rate,
                           args.gravity_axis, args.gravity_sign)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    result["sensor"] = args.sensor
    result["calibration_date"] = args.date or date_type.today().isoformat()

    os.makedirs(os.path.dirname(os.path.abspath(args.output)) or ".", exist_ok=True)
    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        yaml.safe_dump(result, handle, default_flow_style=False,
                       allow_unicode=True, sort_keys=False)

    quality = result["quality"]
    print("=== IMU 标定结果 ===")
    print(f"采样        : {result['num_samples']} 个 @ {result['sample_rate_hz']:.2f} Hz "
          f"({result['duration_s']:.0f} s)")
    print(f"gyro_bias   : {['%+.6f' % v for v in result['gyro_bias']]} rad/s")
    print(f"accel_bias  : {['%+.6f' % v for v in result['accel_bias']]} m/s²"
          f"  (静置水平假设, 见脚本 docstring)")
    print(f"噪声密度    : gyro {result['gyro_noise_density']:.4e} rad/s/√Hz · "
          f"accel {result['accel_noise_density']:.4e} m/s²/√Hz")
    print(f"随机游走    : gyro {result['gyro_random_walk']:.4e} · "
          f"accel {result['accel_random_walk']:.4e}")
    print(f"τ_max       : {quality['allan_tau_max_s']:.1f} s · "
          f"白噪声/随机游走交点 τ≈{quality['white_noise_crossover_tau_s']:.0f} s")
    print(f"\n已写入: {args.output}")

    if not quality["random_walk_reliable"]:
        print(
            f"\nWARNING: 随机游走段不可信 (status={quality['status']})。\n"
            f"  Allan 曲线只到 τ={quality['allan_tau_max_s']:.1f} s, 而白噪声段与"
            f"随机游走段的交点在 τ≈{quality['white_noise_crossover_tau_s']:.0f} s。\n"
            f"  静置时长**至少**要到 "
            f"{quality['required_duration_s_lower_bound'] / 60.0:.0f} 分钟 —— 这是下界, "
            f"不是目标值: 尾段被白噪声压着时 K 被高估, 交点跟着被低估。\n"
            f"  实践上按 2 小时协议采 (见脚本 docstring 的量级推导)。\n"
            f"  噪声密度 (gyro/accel_noise_density) 不受影响, 可以直接用。",
            file=sys.stderr,
        )
        return 0 if args.allow_short_log else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
