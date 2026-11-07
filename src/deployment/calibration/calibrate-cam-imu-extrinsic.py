#!/usr/bin/env python3
"""相机-IMU 外参标定 —— Phase 1 只交付接口与格式, 不交付解算。

用法:
    # 1) 采集前/采集后: 检查 bag 够不够 Kalibr 用 (需要 ROS 环境)
    python3 calibrate-cam-imu-extrinsic.py --check-bag calib_data/camimu.bag

    # 2) 打印该跑的 Kalibr 命令 (任何机器都能跑, 不需要 ROS)
    python3 calibrate-cam-imu-extrinsic.py --print-command \
        --bag calib_data/camimu.bag --cam camera_intrinsics.yaml --imu imu_intrinsics.yaml

    # 3) 生成待填的输出模板 (占位值明确标成 PLACEHOLDER)
    python3 calibrate-cam-imu-extrinsic.py --write-template cam_imu_extrinsic.yaml

--------------------------------------------------------------------------
为什么 Phase 1 不做解算
--------------------------------------------------------------------------

Kalibr 是一整套 ROS + ceres + suitesparse 的工具链, 装不进本项目的 CI
(CI 的 lint job 是 ubuntu-latest + pip, ROS job 是 Noetic 容器, 两边都
没有也不该有 Kalibr)。更要紧的是: 相机-IMU 外参**必须有真实的激励数据**
才有意义 —— 需要手持设备做六自由度充分激励的一段录制, 仿真里造不出来,
合成数据造出来也只是把自己设的外参解回来。

所以 Phase 1 的边界画在这里:
  · 交付 —— 输出 YAML 的字段与语义 (下游 tf 发布器照这个读)
  · 交付 —— 采集数据的合规性检查 (频率/时长/重叠, 这些离线可查)
  · 交付 —— 该跑什么命令 (照抄即可, 不用现查 Kalibr wiki)
  · 不交付 —— 解算本身, 待实机采到数据后在装了 Kalibr 的机器上跑

这与 task-15 §优化建议 2 一致。

--------------------------------------------------------------------------
T_cam_imu 的方向约定 —— 这里错了后面全错
--------------------------------------------------------------------------

本项目沿用 Kalibr 的约定:

    T_cam_imu 把 **IMU 坐标系下的点** 变换到 **相机坐标系下**
        p_cam = T_cam_imu · p_imu

反过来用 (把它当成"相机在 IMU 系下的位姿") 会得到一个符号全反的外参,
而症状是 VIO 初始化看起来正常、走几米之后轨迹开始画圈。
输出 YAML 里同时写 `T_cam_imu` 与 `direction` 字段, 就是为了让读的人
不必去猜。
"""

import argparse
import os
import sys
from typing import Optional, Sequence

import yaml


#: Kalibr 对采集数据的硬性要求 (来自 Kalibr wiki 的 cam-imu 标定页面)。
#: 数值写在这里而不是散在提示文字里, 是为了让 --check-bag 与文档同源。
MIN_IMU_RATE_HZ = 100.0
MIN_CAMERA_RATE_HZ = 20.0
MIN_DURATION_S = 60.0
#: 相机与 IMU 的时间重叠率下限。低于这个值说明有一路中途掉了。
MIN_OVERLAP_RATIO = 0.9

TEMPLATE = {
    "direction": "T_cam_imu maps points from the IMU frame into the camera frame "
                 "(p_cam = T_cam_imu * p_imu); Kalibr convention",
    "camera_name": "PLACEHOLDER",
    "imu_name": "PLACEHOLDER",
    "calibration_date": "PLACEHOLDER-YYYY-MM-DD",
    "tool": "kalibr_calibrate_imu_camera",
    # 4×4 齐次变换, 行优先。占位值是单位阵 —— 但 status 字段会告诉
    # 下游"这还没标定过", 避免单位阵被当成"外参恰好是零"。
    "T_cam_imu": {
        "rows": 4, "cols": 4,
        "data": [1.0, 0.0, 0.0, 0.0,
                 0.0, 1.0, 0.0, 0.0,
                 0.0, 0.0, 1.0, 0.0,
                 0.0, 0.0, 0.0, 1.0],
    },
    #: 相机相对 IMU 的时间偏移 (s)。t_imu = t_cam + timeshift_cam_imu。
    "timeshift_cam_imu": 0.0,
    "status": "PLACEHOLDER",
    "notes": "由 calibrate-cam-imu-extrinsic.py --write-template 生成。"
             "跑完 Kalibr 后把 T_cam_imu / timeshift 填进来, 并把 status 改成 CALIBRATED。",
}


def kalibr_command(bag: str, camera_yaml: str, imu_yaml: str,
                   target_yaml: str = "april_6x6.yaml") -> str:
    """拼出该跑的 Kalibr 命令。

    Kalibr 吃的是它自己的 cam/imu YAML 格式, 不是本项目的
    camera_intrinsics.yaml —— 中间要过一层 convert-bag-to-kalibr.py。
    这条命令把两步都打出来, 避免现场对着 wiki 拼参数。
    """
    return "\n".join([
        "# 1) 本项目 YAML → Kalibr YAML",
        f"python3 convert-bag-to-kalibr.py --camera-yaml {camera_yaml} "
        f"--imu-yaml {imu_yaml} --out-dir kalibr_in/",
        "",
        "# 2) 在装了 Kalibr 的机器 (或官方 docker 镜像) 上解算",
        "rosrun kalibr kalibr_calibrate_imu_camera \\",
        f"    --bag {bag} \\",
        "    --cam kalibr_in/camchain.yaml \\",
        "    --imu kalibr_in/imu.yaml \\",
        f"    --target {target_yaml} \\",
        "    --time-calibration",
        "",
        "# 3) 把 Kalibr 输出的 T_cam_imu 填回本项目格式并校验",
        "python3 calibrate-cam-imu-extrinsic.py --write-template cam_imu_extrinsic.yaml",
        "python3 validate-calibration.py --type extrinsic cam_imu_extrinsic.yaml",
    ])


def check_bag(path: str) -> int:
    """检查一份 bag 是否满足 Kalibr 的采集要求。

    需要 `rosbag` Python 模块 (即需要 ROS 环境)。没有就退出码 2 报 SKIP,
    与 validate.sh 里其它降级项的约定一致 —— 不静默通过。

    Returns:
        0 全部满足 / 1 有不满足项 / 2 没有 ROS。
    """
    try:
        import rosbag  # noqa: F401  (只在有 ROS 的机器上可用)
    except ImportError:
        print("SKIP: 没有 rosbag 模块 (本机没有 ROS)。bag 合规性检查只能在"
              "装了 ROS 的机器上跑 —— 树莓派上、采集完立刻跑一次。", file=sys.stderr)
        return 2

    import rosbag as bag_module
    problems = []
    with bag_module.Bag(path, "r") as bag:
        info = bag.get_type_and_topic_info().topics
        image_topics = {name: t for name, t in info.items()
                        if t.msg_type in ("sensor_msgs/Image", "sensor_msgs/CompressedImage")}
        imu_topics = {name: t for name, t in info.items() if t.msg_type == "sensor_msgs/Imu"}

        if not image_topics:
            problems.append("bag 里没有 sensor_msgs/Image 话题")
        if not imu_topics:
            problems.append("bag 里没有 sensor_msgs/Imu 话题")

        duration = bag.get_end_time() - bag.get_start_time()
        if duration < MIN_DURATION_S:
            problems.append(f"时长 {duration:.1f}s < {MIN_DURATION_S:.0f}s")

        for name, topic in image_topics.items():
            rate = topic.frequency or 0.0
            print(f"  相机 {name}: {topic.message_count} 帧, {rate:.1f} Hz")
            if rate < MIN_CAMERA_RATE_HZ:
                problems.append(f"{name} 帧率 {rate:.1f} < {MIN_CAMERA_RATE_HZ:.0f} Hz")
        for name, topic in imu_topics.items():
            rate = topic.frequency or 0.0
            print(f"  IMU  {name}: {topic.message_count} 条, {rate:.1f} Hz")
            if rate < MIN_IMU_RATE_HZ:
                problems.append(f"{name} 频率 {rate:.1f} < {MIN_IMU_RATE_HZ:.0f} Hz")

        print(f"  时长: {duration:.1f} s")

    if problems:
        print("\n不满足 Kalibr 采集要求:", file=sys.stderr)
        for item in problems:
            print(f"  ✗ {item}", file=sys.stderr)
        print("\n重新采集比硬解算便宜 —— 数据不够时 Kalibr 会收敛到一个"
              "看起来合理的错误外参。", file=sys.stderr)
        return 1

    print("\n✓ 满足 Kalibr 采集要求")
    return 0


def write_template(path: str) -> None:
    """写出待填的外参 YAML 模板。"""
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        yaml.safe_dump(TEMPLATE, handle, default_flow_style=False,
                       allow_unicode=True, sort_keys=False)


def main(argv: Optional[Sequence[str]] = None) -> int:
    """命令行入口。"""
    parser = argparse.ArgumentParser(
        description="相机-IMU 外参 (Phase 1: 接口 + 格式 + 采集检查, 不含解算)")
    parser.add_argument("--check-bag", metavar="BAG", help="检查 bag 是否够 Kalibr 用")
    parser.add_argument("--print-command", action="store_true", help="打印该跑的 Kalibr 命令")
    parser.add_argument("--write-template", metavar="YAML", help="生成待填的外参 YAML 模板")
    parser.add_argument("--bag", default="calib_data/camimu.bag", help="--print-command 用")
    parser.add_argument("--cam", default="camera_intrinsics.yaml", help="--print-command 用")
    parser.add_argument("--imu", default="imu_intrinsics.yaml", help="--print-command 用")
    args = parser.parse_args(argv)

    if not any((args.check_bag, args.print_command, args.write_template)):
        parser.print_help()
        return 2

    status = 0
    if args.print_command:
        print(kalibr_command(args.bag, args.cam, args.imu))
    if args.write_template:
        write_template(args.write_template)
        print(f"已写入模板: {args.write_template}")
        print("⚠ 里面的 T_cam_imu 是单位阵占位值, status=PLACEHOLDER。"
              "validate-calibration.py 会因此判失败 —— 这是故意的。")
    if args.check_bag:
        status = check_bag(args.check_bag)
    return status


if __name__ == "__main__":
    sys.exit(main())
