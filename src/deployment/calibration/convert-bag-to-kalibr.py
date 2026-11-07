#!/usr/bin/env python3
"""格式转换 —— 本项目标定 YAML ↔ Kalibr, 以及 rosbag 抽帧。

用法:
    # 本项目 YAML → Kalibr 输入 (纯离线, 不需要 ROS)
    python3 convert-bag-to-kalibr.py --camera-yaml camera_intrinsics.yaml \
        --imu-yaml imu_intrinsics.yaml --out-dir kalibr_in/ \
        --cam-topic /car/openmv/image_raw --imu-topic /car/imu/data

    # bag → 图片目录 (需要 ROS + cv_bridge)
    python3 convert-bag-to-kalibr.py --bag camera_calib.bag \
        --extract-images out/images --image-topic /car/openmv/image_raw

    # bag → 静置 IMU CSV (需要 ROS)
    python3 convert-bag-to-kalibr.py --bag imu_calib.bag \
        --imu-csv out/imu_static.csv --imu-topic /car/imu/data

分成"纯离线"和"要 ROS"两组不是偶然: 前者在 CI 里跑得动, 后者只能在
树莓派/开发机上跑。要 ROS 的那两个入口在没有 ROS 时退出码 2 报 SKIP,
不会静默产出一个空目录 —— 空目录接着喂给标定脚本, 报出来的错会指向
完全不相干的地方。

--------------------------------------------------------------------------
⚠ 转成 Kalibr 时 k3 会被丢掉
--------------------------------------------------------------------------

Kalibr 的 `radtan` 畸变模型只有 4 个系数 (k1, k2, p1, p2), 而 OpenCV 的
`plumb_bob` 默认解 5 个 (k1, k2, p1, p2, k3)。转过去时 **k3 无处可放**。

代价是可算的: k3 项对归一化半径 r 的贡献是 k3·r⁶。以本项目合成样本解出的
k3 ≈ -0.005、画幅角点 r ≈ 0.63 (VGA + fx≈520) 计, 该项约 -6.2e-4,
折合到像素约 0.2 px —— 在角点处、且与 k1/k2 的重新拟合部分抵消。
对相机-IMU 外参 (要的是几十毫米量级的平移) 这个量级不影响结论。

但**不能反过来用**: 拿 Kalibr 的 4 系数结果回填本项目的 camera_intrinsics.yaml
时必须把 k3 显式写 0, 而不是留着上一次标定的旧值。本脚本只做正向转换,
就是为了不给这个错误留入口。
"""

import argparse
import os
import sys
from typing import Any, Dict, List, Optional, Sequence

import yaml


def load_yaml(path: str) -> Dict[str, Any]:
    """读一份 YAML, 顶层必须是映射。"""
    with open(path, "r", encoding="utf-8") as handle:
        document = yaml.safe_load(handle)
    if not isinstance(document, dict):
        raise ValueError(f"{path} 的顶层不是键值映射")
    return document


def matrix_data(node: Any, name: str) -> List[float]:
    """取 {rows, cols, data} 矩阵节点的数据。"""
    if not isinstance(node, dict) or "data" not in node:
        raise ValueError(f"{name} 不是 {{rows, cols, data}} 结构")
    return [float(v) for v in node["data"]]


def to_kalibr_camchain(camera: Dict[str, Any], topic: str) -> Dict[str, Any]:
    """本项目 camera_intrinsics.yaml → Kalibr camchain.yaml。"""
    k = matrix_data(camera["camera_matrix"], "camera_matrix")
    d = matrix_data(camera["distortion_coefficients"], "distortion_coefficients")
    if str(camera.get("distortion_model")) != "plumb_bob":
        raise ValueError(
            f"只支持 plumb_bob → radtan 的转换, 收到 {camera.get('distortion_model')}"
        )
    if len(d) < 4:
        raise ValueError(f"radtan 需要至少 4 个畸变系数, 收到 {len(d)} 个")
    return {
        "cam0": {
            "camera_model": "pinhole",
            # Kalibr 的顺序是 [fu, fv, pu, pv], 与 K 的排布不同, 不能直接展平
            "intrinsics": [k[0], k[4], k[2], k[5]],
            "distortion_model": "radtan",
            # 只取前 4 个 —— k3 丢弃, 见模块 docstring 的量化说明
            "distortion_coeffs": d[:4],
            "resolution": [int(camera["image_width"]), int(camera["image_height"])],
            "rostopic": topic,
        }
    }


def to_kalibr_imu(imu: Dict[str, Any], topic: str) -> Dict[str, Any]:
    """本项目 imu_intrinsics.yaml → Kalibr imu.yaml。

    字段是一一对应的 —— 这不是巧合: 本项目的 IMU 输出字段就是照着
    Kalibr / robot_localization 共同需要的那四个量定的 (ADR-0011)。
    """
    return {
        "rostopic": topic,
        "update_rate": float(imu["sample_rate_hz"]),
        "accelerometer_noise_density": float(imu["accel_noise_density"]),
        "accelerometer_random_walk": float(imu["accel_random_walk"]),
        "gyroscope_noise_density": float(imu["gyro_noise_density"]),
        "gyroscope_random_walk": float(imu["gyro_random_walk"]),
    }


def write_yaml(document: Dict[str, Any], path: str) -> None:
    """落盘一份 YAML (LF 行尾, UTF-8)。"""
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        yaml.safe_dump(document, handle, default_flow_style=False,
                       allow_unicode=True, sort_keys=False)


def extract_images(bag_path: str, topic: str, out_dir: str, stride: int) -> int:
    """从 bag 里抽帧成 PNG。需要 ROS (rosbag + cv_bridge)。

    Returns:
        0 成功 / 2 没有 ROS。
    """
    try:
        import rosbag
        from cv_bridge import CvBridge
        import cv2
    except ImportError as error:
        print(f"SKIP: 抽帧需要 ROS 环境 (rosbag + cv_bridge + cv2): {error}",
              file=sys.stderr)
        return 2

    os.makedirs(out_dir, exist_ok=True)
    bridge = CvBridge()
    written = 0
    with rosbag.Bag(bag_path, "r") as bag:
        for index, (_, message, _) in enumerate(bag.read_messages(topics=[topic])):
            if index % stride:
                continue
            if hasattr(message, "format"):     # CompressedImage
                image = bridge.compressed_imgmsg_to_cv2(message, "bgr8")
            else:
                image = bridge.imgmsg_to_cv2(message, "bgr8")
            cv2.imwrite(os.path.join(out_dir, f"frame_{written:04d}.png"), image)
            written += 1
    print(f"抽出 {written} 帧 -> {out_dir}")
    if written < 10:
        # 不当成错误退出: 有可能只是话题名写错了, 让人看着数字自己判断
        # 比这里替他决定要好。但必须说出来。
        print(f"WARNING: 只有 {written} 帧, 标定至少要 10 帧。"
              f"确认 --image-topic {topic} 是 bag 里真实存在的话题。", file=sys.stderr)
    return 0


def extract_imu_csv(bag_path: str, topic: str, out_path: str) -> int:
    """从 bag 里导出 IMU CSV (calibrate-imu.py 的输入格式)。

    Returns:
        0 成功 / 2 没有 ROS。
    """
    try:
        import rosbag
    except ImportError as error:
        print(f"SKIP: 导出 IMU CSV 需要 rosbag: {error}", file=sys.stderr)
        return 2

    os.makedirs(os.path.dirname(os.path.abspath(out_path)) or ".", exist_ok=True)
    count = 0
    with rosbag.Bag(bag_path, "r") as bag, \
            open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# 由 convert-bag-to-kalibr.py 从 {os.path.basename(bag_path)} 导出\n")
        handle.write(f"# topic={topic}\n")
        handle.write("timestamp,gx,gy,gz,ax,ay,az\n")
        base = None
        for _, message, _ in bag.read_messages(topics=[topic]):
            stamp = message.header.stamp.to_sec()
            # 时间戳改成相对起点: 绝对 ROS 时间是 1.7e9 量级, 用 float 存到
            # CSV 再读回来只剩微秒级精度, 而 Allan 方差要的是采样间隔的稳定性。
            base = stamp if base is None else base
            angular = message.angular_velocity
            linear = message.linear_acceleration
            handle.write(
                f"{stamp - base:.6f},"
                f"{angular.x:.9f},{angular.y:.9f},{angular.z:.9f},"
                f"{linear.x:.9f},{linear.y:.9f},{linear.z:.9f}\n"
            )
            count += 1
    print(f"导出 {count} 条 IMU 采样 -> {out_path}")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    """命令行入口。退出码: 0 成功 / 1 输入有问题 / 2 缺 ROS 或用法错误。"""
    parser = argparse.ArgumentParser(description="标定数据格式转换 (本项目 ↔ Kalibr, bag 抽帧)")
    parser.add_argument("--camera-yaml", help="本项目 camera_intrinsics.yaml")
    parser.add_argument("--imu-yaml", help="本项目 imu_intrinsics.yaml")
    parser.add_argument("--out-dir", default="kalibr_in", help="Kalibr YAML 输出目录")
    parser.add_argument("--cam-topic", default="/car/openmv/image_raw",
                        help="写进 camchain.yaml 的话题名")
    parser.add_argument("--imu-topic", default="/car/imu/data",
                        help="写进 imu.yaml 的话题名, 也是 bag 抽取用的话题")
    parser.add_argument("--bag", help="待抽取的 rosbag")
    parser.add_argument("--extract-images", metavar="DIR", help="抽帧输出目录")
    parser.add_argument("--image-topic", default="/car/openmv/image_raw", help="抽帧话题")
    parser.add_argument("--stride", type=int, default=10,
                        help="抽帧间隔 (默认每 10 帧取 1 帧, 相邻帧信息冗余)")
    parser.add_argument("--imu-csv", metavar="CSV", help="IMU CSV 输出路径")
    args = parser.parse_args(argv)

    did_something = False
    status = 0

    try:
        if args.camera_yaml:
            camchain = to_kalibr_camchain(load_yaml(args.camera_yaml), args.cam_topic)
            path = os.path.join(args.out_dir, "camchain.yaml")
            write_yaml(camchain, path)
            print(f"已写入: {path}  (⚠ k3 已丢弃, 见脚本 docstring)")
            did_something = True

        if args.imu_yaml:
            imu = to_kalibr_imu(load_yaml(args.imu_yaml), args.imu_topic)
            path = os.path.join(args.out_dir, "imu.yaml")
            write_yaml(imu, path)
            print(f"已写入: {path}")
            did_something = True
    except (OSError, KeyError, ValueError, yaml.YAMLError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    if args.extract_images:
        if not args.bag:
            print("ERROR: --extract-images 需要同时给 --bag", file=sys.stderr)
            return 2
        status = extract_images(args.bag, args.image_topic, args.extract_images,
                                max(1, args.stride)) or status
        did_something = True

    if args.imu_csv:
        if not args.bag:
            print("ERROR: --imu-csv 需要同时给 --bag", file=sys.stderr)
            return 2
        status = extract_imu_csv(args.bag, args.imu_topic, args.imu_csv) or status
        did_something = True

    if not did_something:
        parser.print_help()
        return 2
    return status


if __name__ == "__main__":
    sys.exit(main())
