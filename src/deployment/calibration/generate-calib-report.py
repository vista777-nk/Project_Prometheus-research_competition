#!/usr/bin/env python3
"""标定报告生成 —— YAML → Markdown。

用法:
    python3 generate-calib-report.py camera_intrinsics.yaml -o report.md
    python3 generate-calib-report.py camera_intrinsics.yaml imu_intrinsics.yaml \
        -o calibration_db/2026-07-31_car/REPORT.md

报告要回答的只有一个问题: **这次标定能不能用**。所以每一节的最后都是一句
结论 (PASS / NEED_RECALIBRATE), 而不是把数字堆完就完事 —— 数字堆完
让人自己判断, 等于把判断成本转嫁给了三个月后已经忘记细节的自己。

误差分布用 ASCII 柱状图, 不生成 PNG: 报告要能在 SSH 终端、Git diff、
工单里原样读。一张 PNG 在这三个地方都是一个文件名。
"""

import argparse
import math
import os
import sys
from typing import Any, Dict, List, Optional, Sequence

import yaml

# validate-calibration.py 用连字符命名 (与本目录其它脚本一致), 不能直接 import。
# 用 importlib 按路径加载, 而不是把阈值在这里抄一遍 —— 抄一遍就会漂移,
# 到时候报告说 PASS 而校验说 FAIL, 谁也不知道该信哪个。
import importlib.util

_VALIDATOR_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "validate-calibration.py")
_spec = importlib.util.spec_from_file_location("validate_calibration", _VALIDATOR_PATH)
if _spec is None or _spec.loader is None:  # pragma: no cover
    raise SystemExit(f"ERROR: 找不到 {_VALIDATOR_PATH}")
validator = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(validator)


BAR_WIDTH = 40


def ascii_bar_chart(values: Sequence[float], labels: Sequence[str],
                    unit: str = "px") -> List[str]:
    """横向 ASCII 柱状图。空输入返回空列表。"""
    if not values:
        return []
    peak = max(values)
    if peak <= 0:
        return ["(全为 0)"]
    lines = []
    label_width = max(len(label) for label in labels)
    for label, value in zip(labels, values):
        filled = int(round(BAR_WIDTH * value / peak))
        bar = "█" * filled + "·" * (BAR_WIDTH - filled)
        lines.append(f"{label:<{label_width}}  {bar}  {value:.4f} {unit}")
    return lines


def log_scale_curve(taus: Sequence[float], deviations: Sequence[float],
                    rows: int = 12, cols: int = 56) -> List[str]:
    """Allan 偏差曲线的 log-log ASCII 图。

    分辨率很低, 目的不是读数, 是看**形状**: 左半段应当是 -1/2 的下降,
    右半段若已经拐上来说明数据够长。一眼能看出"曲线还在往下掉"就够了 ——
    那意味着随机游走系数没有依据。
    """
    if len(taus) < 2:
        return ["(数据点不足)"]
    log_tau = [math.log10(t) for t in taus]
    log_dev = [math.log10(d) if d > 0 else float("-inf") for d in deviations]
    finite = [v for v in log_dev if math.isfinite(v)]
    if not finite:
        return ["(全为 0)"]

    x_lo, x_hi = min(log_tau), max(log_tau)
    y_lo, y_hi = min(finite), max(finite)
    x_span = (x_hi - x_lo) or 1.0
    y_span = (y_hi - y_lo) or 1.0

    grid = [[" "] * cols for _ in range(rows)]
    for lt, ld in zip(log_tau, log_dev):
        if not math.isfinite(ld):
            continue
        col = min(cols - 1, int((lt - x_lo) / x_span * (cols - 1)))
        row = min(rows - 1, int((y_hi - ld) / y_span * (rows - 1)))
        grid[row][col] = "*"

    lines = [f"  σ(τ) [{10 ** y_hi:.2e} .. {10 ** y_lo:.2e}]"]
    for index, row in enumerate(grid):
        prefix = "  |" if index else "  ┬"
        lines.append(prefix + "".join(row))
    lines.append("  └" + "─" * cols)
    lines.append(f"   τ = {10 ** x_lo:.2f}s {' ' * (cols - 24)} τ = {10 ** x_hi:.1f}s")
    return lines


def render_matrix(data: Sequence[float], rows: int, cols: int,
                  indent: str = "    ") -> List[str]:
    """把展平的矩阵渲染成对齐的文本块。"""
    cells = [f"{float(v): .6f}" for v in data]
    width = max(len(c) for c in cells)
    lines = []
    for r in range(rows):
        row = "  ".join(f"{cells[r * cols + c]:>{width}}" for c in range(cols))
        lines.append(f"{indent}[ {row} ]")
    return lines


def camera_section(document: Dict[str, Any], source: str) -> List[str]:
    """渲染相机内参一节。"""
    checks = validator.validate_camera(document)
    meta = document.get("air_ground_calibration", {}) or {}
    width = int(document["image_width"])
    height = int(document["image_height"])
    k = validator.matrix_data(document["camera_matrix"], "camera_matrix")
    d = validator.matrix_data(document["distortion_coefficients"],
                              "distortion_coefficients")
    fx, fy, cx, cy = k[0], k[4], k[2], k[5]

    lines = [
        "## 相机内参",
        "",
        "| 项 | 值 |",
        "|------|------|",
        f"| 来源文件 | `{os.path.basename(source)}` |",
        f"| 相机名 | `{document.get('camera_name', '—')}` |",
        f"| 标定时间 | {meta.get('calibration_date', '未记录')} |",
        f"| 硬件序列号 | `{meta.get('hardware_serial', '未记录')}` |",
        f"| 分辨率 | {width} × {height} |",
        f"| 标定板 | {meta.get('checkerboard_inner_corners', '—')} 内角点 · "
        f"方格 {meta.get('square_size_m', '—')} m |",
        f"| 图片数 | {meta.get('num_images', '—')} |",
        f"| OpenCV | {meta.get('opencv_version', '—')} |",
        "",
        "### K Matrix",
        "",
        "```",
    ]
    lines += render_matrix(k, 3, 3)
    lines += [
        "```",
        "",
        f"- `fx` = {fx:.4f} · `fy` = {fy:.4f} · `fy/fx` = {fy / fx:.5f}",
        f"- 主点 `(cx, cy)` = ({cx:.4f}, {cy:.4f})，"
        f"相对图像中心偏移 ({cx - width / 2:+.2f}, {cy - height / 2:+.2f}) px",
        f"- 水平视场角 ≈ **{validator.horizontal_fov_deg(fx, width):.2f}°**",
        "",
        "### 畸变系数",
        "",
        f"模型: `{document.get('distortion_model')}` — "
        f"顺序 (k1, k2, p1, p2, k3)",
        "",
        "```",
    ]
    lines += render_matrix(d, 1, len(d))
    lines += ["```", ""]

    per_view = [float(v) for v in meta.get("per_view_rms", [])]
    names = [str(v) for v in meta.get("image_names", [])]
    rms = meta.get("rms_reprojection_error")
    lines += ["### 重投影误差", ""]
    if rms is not None:
        lines.append(f"整体 RMS = **{float(rms):.4f} px** "
                     f"(上限 {validator.MAX_RMS_PX} px)")
        lines.append("")
    if per_view:
        if len(names) != len(per_view):
            names = [f"#{i:02d}" for i in range(len(per_view))]
        lines += ["```"] + ascii_bar_chart(per_view, names) + ["```", ""]
        lines.append(f"逐张最差 {max(per_view):.4f} px / 最好 {min(per_view):.4f} px。"
                     "个别图片明显偏高时, 删掉那几张重算比整组重拍便宜。")
        lines.append("")
    else:
        lines += ["(该文件不含逐张误差 —— 用 `--strict-camera-info` 生成的版本没有溯源段)", ""]

    lines += ["### 合理性检查", "", "```"] + checks.render().splitlines() + ["```", ""]
    return lines, checks.ok


def imu_section(document: Dict[str, Any], source: str) -> List[str]:
    """渲染 IMU 内参一节。"""
    checks = validator.validate_imu(document)
    quality = document.get("quality", {}) or {}
    derived = document.get("derived_for_driver", {}) or {}

    lines = [
        "## IMU 内参",
        "",
        "| 项 | 值 |",
        "|------|------|",
        f"| 来源文件 | `{os.path.basename(source)}` |",
        f"| 传感器 | `{document.get('sensor', '—')}` |",
        f"| 标定时间 | {document.get('calibration_date', '未记录')} |",
        f"| 采样 | {document.get('num_samples', '—')} 个 @ "
        f"{float(document.get('sample_rate_hz', 0)):.2f} Hz "
        f"({float(document.get('duration_s', 0)):.0f} s) |",
        "",
        "### 零偏与噪声",
        "",
        "| 参数 | 值 | 单位 |",
        "|------|------|------|",
    ]
    for key, unit in (
        ("gyro_bias", "rad/s"),
        ("accel_bias", "m/s²"),
        ("gyro_noise_density", "rad/s/√Hz"),
        ("accel_noise_density", "m/s²/√Hz"),
        ("gyro_random_walk", "rad/s²/√Hz"),
        ("accel_random_walk", "m/s³/√Hz"),
    ):
        value = document.get(key)
        if isinstance(value, list):
            text = "[" + ", ".join(f"{float(v):+.6f}" for v in value) + "]"
        else:
            text = f"{float(value):.6e}" if value is not None else "—"
        lines.append(f"| `{key}` | {text} | {unit} |")

    lines += [
        "",
        f"> 加速度计零偏用的是**静置水平假设** (`{document.get('accel_bias_method')}`)。",
        f"> 实测重力模长 {float(document.get('measured_gravity_magnitude', 0)):.5f} m/s²。",
        "> 摆放倾斜 0.5° 就会造出 0.086 m/s² 的假零偏 —— 与被测零偏同量级。",
        "> 要把零偏和安装倾角分开需要六面翻转标定 (Phase 2)。",
        "",
        "### 填进驱动的派生值",
        "",
        "`config/real_sensors.yaml` 的 `icm42688` 段收的是**离散标准差**, 不是密度:",
        "",
        "```yaml",
        "icm42688:",
        f"  gyro_noise_stddev: {float(derived.get('gyro_noise_stddev', 0)):.6e}",
        f"  accel_noise_stddev: {float(derived.get('accel_noise_stddev', 0)):.6e}",
        "```",
        "",
        "换算 `sigma = noise_density × √sample_rate_hz`。"
        "漏掉这一步会让 100 Hz 下的协方差小 100 倍。",
        "",
    ]

    curves = document.get("allan_curves", {}) or {}
    for label, title in (("gyro_allan", "陀螺"), ("accel_allan", "加速度计")):
        curve = curves.get(label)
        if not curve:
            continue
        lines += [
            f"### Allan 偏差曲线 — {title} ({curve.get('axis', '?')} 轴)",
            "",
            "```",
        ]
        lines += log_scale_curve(curve.get("tau_s", []), curve.get("deviation", []))
        lines += ["```", ""]

    lines += [
        f"τ_max = {float(quality.get('allan_tau_max_s', 0)):.1f} s · "
        f"白噪声/随机游走交点 τ ≈ "
        f"{float(quality.get('white_noise_crossover_tau_s', 0)):.1f} s · "
        f"状态 `{quality.get('status', '—')}`",
        "",
        "### 合理性检查",
        "",
        "```",
    ] + checks.render().splitlines() + ["```", ""]
    return lines, checks.ok


def build_report(paths: Sequence[str], title: str) -> str:
    """把若干标定 YAML 汇成一份 Markdown 报告。"""
    body: List[str] = []
    verdicts: List[bool] = []
    for path in paths:
        with open(path, "r", encoding="utf-8") as handle:
            document = yaml.safe_load(handle)
        if not isinstance(document, dict):
            raise ValueError(f"{path} 的顶层不是键值映射")
        kind = validator.detect_type(document)
        if kind == "camera":
            section, ok = camera_section(document, path)
        elif kind == "imu":
            section, ok = imu_section(document, path)
        else:
            raise ValueError(f"{path} 认不出是相机还是 IMU 标定结果")
        body += section
        verdicts.append(ok)

    overall = "PASS" if all(verdicts) else "NEED_RECALIBRATE"
    header = [
        f"# {title}",
        "",
        f"**结论: {overall}**",
        "",
        "| 输入 | 判定 |",
        "|------|------|",
    ]
    for path, ok in zip(paths, verdicts):
        header.append(f"| `{os.path.basename(path)}` | {'PASS' if ok else 'FAIL'} |")
    header += [
        "",
        "> 本报告由 `generate-calib-report.py` 生成, 判定阈值与 "
        "`validate-calibration.py` 是同一份代码 (importlib 按路径加载, 不抄第二份)。",
        "",
        "---",
        "",
    ]
    return "\n".join(header + body).rstrip() + "\n"


def main(argv: Optional[Sequence[str]] = None) -> int:
    """命令行入口。退出码: 0 报告结论 PASS / 1 NEED_RECALIBRATE / 2 用法错误。"""
    parser = argparse.ArgumentParser(description="标定报告生成 (YAML → Markdown)")
    parser.add_argument("files", nargs="+", help="标定结果 YAML")
    parser.add_argument("-o", "--output", default="-",
                        help="输出 Markdown 路径, - 表示打印到 stdout")
    parser.add_argument("--title", default="标定报告", help="报告标题")
    args = parser.parse_args(argv)

    try:
        report = build_report(args.files, args.title)
    except (OSError, ValueError, yaml.YAMLError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    if args.output == "-":
        print(report)
    else:
        os.makedirs(os.path.dirname(os.path.abspath(args.output)) or ".", exist_ok=True)
        with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(report)
        print(f"已写入: {args.output}")

    return 0 if "**结论: PASS**" in report else 1


if __name__ == "__main__":
    sys.exit(main())
