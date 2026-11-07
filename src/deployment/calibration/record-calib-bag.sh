#!/usr/bin/env bash
# =============================================================================
# record-calib-bag.sh — 标定数据采集 (实机上运行)
#
#   ./record-calib-bag.sh camera          # 相机内参
#   ./record-calib-bag.sh imu             # IMU 静置
#   ./record-calib-bag.sh camimu          # 相机-IMU 外参 (六自由度激励)
#   ROBOT=drone ./record-calib-bag.sh camera
#
# 环境变量:
#   ROBOT      car (默认) | drone
#   BAG_DIR    输出目录, 默认 ~/calib_data/<时间戳>
#   DURATION   覆盖默认时长 (s)
#
# -----------------------------------------------------------------------------
# 话题名从哪来
# -----------------------------------------------------------------------------
# 下面这几个话题名与仓库里这三份配置一致, 不是照着任务书抄的:
#
#   src/air_ground_car_bringup/config/car_edge.yaml        车机 topics 段
#   src/air_ground_drone_bringup/config/drone_edge.yaml    无人机 topics 段
#   src/air_ground_car_bringup/config/real_sensors.yaml    实机驱动
#
# task-15 原文写的是 /drone/rgb/image_raw、/drone/imu/data_raw、
# /car/imu/data_raw 和 /drone/rgb/camera_info —— **四个话题在本仓库里
# 一个都不存在**。rosbag record 对不存在的话题不报错, 它就那么等着,
# 录出一个 0 条消息的 bag。等回到桌前打开 bag 才发现, 而标定板已经收了。
#
# 一致性由 test/test_calib_pipeline.py 的话题交叉引用测试守着 (同
# src/deployment/validate.sh §7 的做法)。改这里必须同时改那边, 否则 CI 红。
# =============================================================================

set -euo pipefail

ROBOT="${ROBOT:-car}"
TYPE="${1:-}"
BAG_DIR="${BAG_DIR:-${HOME}/calib_data/$(date +%Y%m%d_%H%M%S)}"

# --- 话题表 (与 *_edge.yaml 逐字一致) ---------------------------------------
# 只列标定要录的话题。雷达 (/car/scan) 刻意不在这里 —— 本任务不标定雷达,
# 列出来会变成一个没人录、也没人验的死变量 (shellcheck SC2034 会抓)。
CAR_IMAGE_TOPIC="/car/openmv/image_raw"
CAR_IMU_TOPIC="/car/imu/data"
DRONE_RGB_TOPIC="/drone/camera/rgb/image_raw"
DRONE_DEPTH_TOPIC="/drone/camera/depth/image_raw"
DRONE_IMU_TOPIC="/mavros/imu/data"

# --- 默认时长 ---------------------------------------------------------------
# 相机: 2 分钟够采到 ~1200 帧, 按 --stride 10 抽出 120 帧, 远超标定所需的 10 张。
# IMU:  见 calibrate-imu.py 的量级推导 —— 随机游走要小时级, 这里默认 2 小时。
# 相机-IMU: Kalibr 要求 >= 60s 的六自由度激励, 给 90s 留余量。
CAMERA_DURATION="${DURATION:-120}"
IMU_DURATION="${DURATION:-7200}"
CAMIMU_DURATION="${DURATION:-90}"

usage() {
    cat <<'USAGE'
用法: [ROBOT=car|drone] ./record-calib-bag.sh <camera|imu|camimu>

  camera   相机内参 —— 手持棋盘格, 铺满画面各处
  imu      IMU 静置 —— 放平别碰, 默认 2 小时
  camimu   相机-IMU 外参 —— 六自由度充分激励, 默认 90 秒

环境变量: ROBOT (默认 car) · BAG_DIR · DURATION (覆盖默认时长, 单位 s)
USAGE
}

die() {
    echo "错误: $1" >&2
    exit 1
}

# 录制前确认每个话题**真的在发**。
#
# 这一步是本脚本最有价值的部分。rosbag record 订阅一个没有发布者的话题时
# 完全不报错, 录出来的 bag 里那个话题一条消息都没有。标定数据是一次性的
# (人已经举着标定板站了两分钟、IMU 已经静置了两小时), 发现得晚就是重来。
require_topics() {
    local missing=0 topic
    local live
    live="$(rostopic list 2>/dev/null || true)"
    [[ -n "${live}" ]] || die "rostopic list 没有输出 —— roscore 没起来, 或 ROS_MASTER_URI 不对"

    for topic in "$@"; do
        if grep -qx -- "${topic}" <<<"${live}"; then
            echo "  ✓ ${topic}"
        else
            echo "  ✗ ${topic} —— 没有这个话题" >&2
            missing=1
        fi
    done
    [[ ${missing} -eq 0 ]] || die "有话题不在线。先把对应的驱动/launch 起起来再录。"
}

confirm() {
    echo ""
    read -r -p "准备好了按 ENTER 开始 (Ctrl-C 放弃) ..." _
}

record() {
    local name="$1" duration="$2"
    shift 2
    local bag="${BAG_DIR}/${name}.bag"
    mkdir -p "${BAG_DIR}"

    echo ""
    echo "开始录制 ${duration}s -> ${bag}"
    rosbag record -O "${bag}" --duration="${duration}" "$@" __name:=calib_recorder

    echo ""
    echo "录完了: ${bag}"
    # 立刻自查一遍。空 bag 在这里就能看出来, 不用等回到桌前。
    rosbag info "${bag}" || true
}

record_camera() {
    local topics=()
    case "${ROBOT}" in
        car)   topics=("${CAR_IMAGE_TOPIC}") ;;
        drone) topics=("${DRONE_RGB_TOPIC}" "${DRONE_DEPTH_TOPIC}") ;;
        *)     die "ROBOT 只能是 car 或 drone, 收到 ${ROBOT}" ;;
    esac

    cat <<'GUIDE'
=== 相机内参采集 ===

标定板: 9x6 内角点 (即 10x7 个方格), 方格边长 30 mm。
        内角点数不是方格数 —— 填错的表现是一张都检测不到。
        板子必须**平**: 打印稿贴在泡沫板/亚克力上, 不要拿在手里让它弯。

怎么举:
  1. 让棋盘格铺满画面的**四个角**, 各拍几张 —— 畸变系数全靠边缘的点解出来,
     只在画面中央拍的话 k1/k2 基本是猜的。
  2. 绕三个轴各倾斜 ±30° 左右。正对着拍十几张解不出焦距:
     所有视角都相似时, 焦距和距离是简并的。
  3. 远近各拍几组 (0.3 m ~ 1.0 m)。
  4. 慢。运动模糊会让角点检测悄悄偏几分之一像素, 而 RMS 看不出来。

采完跑:
  python3 convert-bag-to-kalibr.py --bag <bag> --extract-images out/images \
      --image-topic <话题>
  python3 calibrate-camera.py --input out/images --pattern 9x6 --square-size 0.030
GUIDE

    echo ""
    echo "检查话题:"
    require_topics "${topics[@]}"
    confirm
    record "camera_calib_${ROBOT}" "${CAMERA_DURATION}" "${topics[@]}"
}

record_imu() {
    local topic
    case "${ROBOT}" in
        car)   topic="${CAR_IMU_TOPIC}" ;;
        drone) topic="${DRONE_IMU_TOPIC}" ;;
        *)     die "ROBOT 只能是 car 或 drone, 收到 ${ROBOT}" ;;
    esac

    cat <<GUIDE
=== IMU 静置采集 ===

  1. 把整机放在**不会被碰到**的水平面上 (地面比桌面好: 桌子会被人撞)。
  2. 关掉一切会振动的东西: 电机、风扇、云台。
  3. 默认录 ${IMU_DURATION} 秒 ($((IMU_DURATION / 60)) 分钟), 期间不要靠近。

为什么要这么久: Allan 曲线的白噪声段 (斜率 -1/2) 和随机游走段 (斜率 +1/2)
交点在 τ ≈ √3·N/K, 本项目的 IMU 量级下约 170 秒; 要在 +1/2 段上取到可信的点,
总时长得是它的几十倍。15 分钟能解出噪声密度, 但解不出随机游走 ——
calibrate-imu.py 会把 quality.random_walk_reliable 置 false 并判失败。
详见该脚本的 docstring。

只想要噪声密度和零偏 (够 robot_localization 起步) 时:
  DURATION=900 ROBOT=${ROBOT} ./record-calib-bag.sh imu
  python3 calibrate-imu.py --input imu.csv --allow-short-log
GUIDE

    echo ""
    echo "检查话题:"
    require_topics "${topic}"
    confirm
    record "imu_calib_${ROBOT}" "${IMU_DURATION}" "${topic}"
}

record_camimu() {
    local topics=()
    case "${ROBOT}" in
        car)   topics=("${CAR_IMAGE_TOPIC}" "${CAR_IMU_TOPIC}") ;;
        drone) topics=("${DRONE_RGB_TOPIC}" "${DRONE_IMU_TOPIC}") ;;
        *)     die "ROBOT 只能是 car 或 drone, 收到 ${ROBOT}" ;;
    esac

    cat <<'GUIDE'
=== 相机-IMU 外参采集 ===

前置: 相机内参和 IMU 内参都已经标定完并通过 validate-calibration.py。
      外参解算把内参当已知量, 内参不准的话外参会把误差吸收进去。

怎么动 (这一项与相机内参采集相反, 要的是**激励**而不是覆盖):
  1. 手持整机对着标定板 (AprilGrid 优先, 棋盘格也可以)。
  2. 绕三个轴各来回转几次, 沿三个方向各来回平移几次 —— 六个自由度都要激励到。
     少激励一个轴, 对应的那一维外参就是不可观的, 而 Kalibr 照样会给你一个数。
  3. 动作要有加速度: 匀速平移对 IMU 是"没发生任何事"。
  4. 全程保持标定板在画面里。
  5. 至少 60 秒。

采完跑:
  python3 calibrate-cam-imu-extrinsic.py --check-bag <bag>
  python3 calibrate-cam-imu-extrinsic.py --print-command   # 照着跑 Kalibr
GUIDE

    echo ""
    echo "检查话题:"
    require_topics "${topics[@]}"
    confirm
    record "camimu_calib_${ROBOT}" "${CAMIMU_DURATION}" "${topics[@]}"
}

command -v rosbag >/dev/null 2>&1 || die "找不到 rosbag —— 先 source /opt/ros/noetic/setup.bash"

case "${TYPE}" in
    camera) record_camera ;;
    imu)    record_imu ;;
    camimu) record_camimu ;;
    ""|-h|--help|help) usage; exit 0 ;;
    *)      usage; exit 2 ;;
esac

echo ""
echo "全部数据在: ${BAG_DIR}"
echo "归档规范见 calibration_db/README.md"
