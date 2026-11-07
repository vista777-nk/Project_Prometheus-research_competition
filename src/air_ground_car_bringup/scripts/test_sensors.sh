#!/usr/bin/env bash
set -uo pipefail

timeout_seconds=${TASK05_TIMEOUT_SECONDS:-60}
workspace_root=${AIR_GROUND_WS:-${HOME}/air_ground_sim_ws}

if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
  printf '[ERROR] TASK05_TIMEOUT_SECONDS must be a positive integer\n' >&2
  exit 2
fi

pass_count=0
fail_count=0
launch_pid=
roscore_pid=
xvfb_pid=
log_dir=$(mktemp -d "${TMPDIR:-/tmp}/task05-sensors.XXXXXX")
roslaunch_log="${log_dir}/roslaunch.log"
roscore_log="${log_dir}/roscore.log"
xvfb_log="${log_dir}/xvfb.log"
probe_log="${log_dir}/probes.log"

pass() {
  printf '[PASS] %s\n' "$1"
  pass_count=$((pass_count + 1))
}

fail() {
  printf '[FAIL] %s\n' "$1"
  fail_count=$((fail_count + 1))
}

process_group_alive() {
  local pid=$1
  kill -0 -- "-$pid" 2>/dev/null || kill -0 "$pid" 2>/dev/null
}

stop_process_group() {
  local pid=$1
  if [[ -z "$pid" ]] || ! process_group_alive "$pid"; then
    return
  fi

  kill -INT -- "-$pid" 2>/dev/null || kill -INT "$pid" 2>/dev/null || true
  for _ in {1..20}; do
    process_group_alive "$pid" || return
    sleep 0.5
  done
  kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
  for _ in {1..10}; do
    process_group_alive "$pid" || return
    sleep 0.5
  done
  kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
}

stop_chassis_swapper() {
  local response

  if [[ -z "$launch_pid" ]] || ! process_group_alive "$launch_pid" ||
     ! command -v rosnode >/dev/null 2>&1; then
    return
  fi
  if ! timeout 3 rosnode list 2>/dev/null |
     grep -Fxq /chassis_swapper; then
    return
  fi

  timeout 10 rosnode kill /chassis_swapper >/dev/null 2>&1 || true
  for _ in {1..30}; do
    response=$(timeout 2 rosservice call \
      /car/controller_manager/list_controllers 2>/dev/null || true)
    if ! grep -Eq 'state: *(running|stopped)' <<<"$response"; then
      return
    fi
    sleep 0.25
  done
}

cleanup() {
  set +e
  stop_chassis_swapper
  stop_process_group "$launch_pid"
  [[ -n "$launch_pid" ]] && wait "$launch_pid" 2>/dev/null
  stop_process_group "$roscore_pid"
  [[ -n "$roscore_pid" ]] && wait "$roscore_pid" 2>/dev/null
  stop_process_group "$xvfb_pid"
  [[ -n "$xvfb_pid" ]] && wait "$xvfb_pid" 2>/dev/null
  printf 'task05_log_dir=%s\n' "$log_dir"
}

on_signal() {
  local exit_code=$1
  trap - INT TERM
  exit "$exit_code"
}

trap cleanup EXIT
trap 'on_signal 130' INT
trap 'on_signal 143' TERM

launch_alive() {
  [[ -n "$launch_pid" ]] && kill -0 "$launch_pid" 2>/dev/null
}

wait_for_master() {
  local deadline=$((SECONDS + 30))
  while ((SECONDS < deadline)); do
    rosparam get /rosversion >/dev/null 2>&1 && return 0
    sleep 1
  done
  return 1
}

start_virtual_display() {
  local display_number=

  if [[ -n "${DISPLAY:-}" ]]; then
    return
  fi
  if ! command -v Xvfb >/dev/null 2>&1; then
    printf '[ERROR] DISPLAY is unset and Xvfb is not installed\n' >&2
    exit 2
  fi

  for candidate in {90..199}; do
    if [[ ! -e "/tmp/.X${candidate}-lock" ]] &&
       [[ ! -S "/tmp/.X11-unix/X${candidate}" ]]; then
      display_number=$candidate
      break
    fi
  done
  if [[ -z "$display_number" ]]; then
    printf '[ERROR] no free X display number found\n' >&2
    exit 2
  fi

  setsid Xvfb ":${display_number}" -screen 0 1280x1024x24 \
    -nolisten tcp >"$xvfb_log" 2>&1 &
  xvfb_pid=$!
  export DISPLAY=":${display_number}"
  export LIBGL_ALWAYS_SOFTWARE=1

  for _ in {1..20}; do
    [[ -S "/tmp/.X11-unix/X${display_number}" ]] && return
    if ! process_group_alive "$xvfb_pid"; then
      printf '[ERROR] Xvfb exited during startup; log=%s\n' "$xvfb_log" >&2
      exit 2
    fi
    sleep 0.25
  done
  printf '[ERROR] Xvfb did not become ready; log=%s\n' "$xvfb_log" >&2
  exit 2
}

check_model_spawned() {
  local model_name=$1
  local label=$2
  local deadline=$((SECONDS + timeout_seconds))
  local response

  while ((SECONDS < deadline)); do
    if ! launch_alive; then
      fail "$label (roslaunch exited; log=${roslaunch_log})"
      return
    fi
    response=$(timeout 5 rosservice call /gazebo/get_model_state \
      "{model_name: '${model_name}', relative_entity_name: 'world'}" \
      2>/dev/null || true)
    if grep -Eq 'success: *(True|true)' <<<"$response"; then
      pass "$label"
      return
    fi
    sleep 1
  done
  fail "$label (timeout=${timeout_seconds}s)"
}

check_controllers() {
  local label=$1
  shift
  local deadline=$((SECONDS + timeout_seconds))
  local response

  while ((SECONDS < deadline)); do
    if ! launch_alive; then
      fail "$label (roslaunch exited)"
      return
    fi
    response=$(timeout 5 rosservice call \
      /car/controller_manager/list_controllers 2>/dev/null || true)
    if printf '%s\n' "$response" | /usr/bin/python3 -c '
import sys
import yaml

data = yaml.safe_load(sys.stdin.read()) or {}
running = {
    item.get("name")
    for item in data.get("controller", [])
    if item.get("state") == "running"
}
expected = set(sys.argv[1:])
raise SystemExit(0 if running == expected else 1)
' "$@"; then
      pass "$label"
      return
    fi
    sleep 1
  done
  fail "$label (timeout=${timeout_seconds}s)"
}

spawn_obstacles() {
  if timeout "$timeout_seconds" /usr/bin/python3 -c '
import rospy
from gazebo_msgs.srv import SpawnModel
from geometry_msgs.msg import Pose

rospy.init_node("task05_obstacle_spawner", anonymous=True, disable_signals=True)
rospy.wait_for_service("/gazebo/spawn_sdf_model", timeout=20.0)
spawn = rospy.ServiceProxy("/gazebo/spawn_sdf_model", SpawnModel)
for name, x, y in (
    ("task05_front", 0.70, 0.0),
    ("task05_rear", -0.70, 0.0),
    ("task05_left", 0.0, 0.70),
    ("task05_right", 0.0, -0.70),
):
    sdf = """<sdf version="1.6"><model name="%s"><static>true</static>
      <link name="body"><collision name="collision"><geometry><box>
      <size>0.30 0.30 0.50</size></box></geometry></collision>
      <visual name="visual"><geometry><box><size>0.30 0.30 0.50</size>
      </box></geometry></visual></link></model></sdf>""" % name
    pose = Pose()
    pose.position.x = x
    pose.position.y = y
    pose.position.z = 0.25
    response = spawn(name, sdf, "", pose, "world")
    if not response.success:
        raise RuntimeError("%s: %s" % (name, response.status_message))
' >>"$probe_log" 2>&1; then
    pass "Four-direction sensor obstacles spawned"
  else
    fail "Four-direction sensor obstacles spawned (log=${probe_log})"
  fi
}

run_probe() {
  local kind=$1
  shift
  timeout "$timeout_seconds" /usr/bin/python3 -c '
import math
import sys

import rospy
import tf2_ros
from sensor_msgs.msg import CameraInfo, Image, Imu, JointState, LaserScan


def sample(topic, message_type):
    return rospy.wait_for_message(topic, message_type, timeout=20.0)


def finite(values):
    return all(math.isfinite(float(value)) for value in values)


rospy.init_node("task05_sensor_probe", anonymous=True, disable_signals=True)
kind = sys.argv[1]
if kind == "camera":
    image = sample("/car/openmv/image_raw", Image)
    info = sample("/car/openmv/camera_info", CameraInfo)
    assert (image.width, image.height) == (320, 240)
    assert image.encoding == "rgb8"
    assert image.step == 960 and len(image.data) == 320 * 240 * 3
    assert image.header.frame_id == "openmv_camera_optical_link"
    assert (info.width, info.height) == (320, 240)
    assert info.header.frame_id == "openmv_camera_optical_link"
    assert info.K[0] > 0.0 and info.K[4] > 0.0
elif kind == "lidar":
    scan = sample("/car/scan", LaserScan)
    ranges = [value for value in scan.ranges if math.isfinite(value)]
    assert scan.header.frame_id == "lidar_link"
    assert len(scan.ranges) == 360
    assert abs(scan.range_min - 0.15) < 0.01
    assert abs(scan.range_max - 12.0) < 0.01
    assert ranges and 0.30 < min(ranges) < 1.00
elif kind == "ultrasonic":
    for direction in ("front", "rear", "left", "right"):
        topic = "/car/ultrasonic/" + direction
        scan = sample(topic, LaserScan)
        assert scan.header.frame_id == "ultrasonic_%s_link" % direction
        assert len(scan.ranges) == 1
        assert abs(scan.range_min - 0.02) < 0.01
        assert abs(scan.range_max - 4.0) < 0.01
        assert math.isfinite(scan.ranges[0])
        assert 0.30 < scan.ranges[0] < 1.00
elif kind == "imu":
    imu = sample("/car/imu/data", Imu)
    assert imu.header.frame_id == "imu_link"
    assert finite((
        imu.orientation.x, imu.orientation.y, imu.orientation.z,
        imu.orientation.w, imu.angular_velocity.x,
        imu.angular_velocity.y, imu.angular_velocity.z,
        imu.linear_acceleration.x, imu.linear_acceleration.y,
        imu.linear_acceleration.z,
    ))
elif kind == "tf":
    buffer = tf2_ros.Buffer()
    listener = tf2_ros.TransformListener(buffer)
    for frame in (
        "openmv_camera_optical_link", "lidar_link", "imu_link",
        "ultrasonic_front_link", "ultrasonic_rear_link",
        "ultrasonic_left_link", "ultrasonic_right_link",
    ):
        buffer.lookup_transform(
            "base_link", frame, rospy.Time(0), rospy.Duration(10.0)
        )
elif kind == "gimbal":
    expected_pan = float(sys.argv[2])
    expected_tilt = float(sys.argv[3])
    deadline = rospy.Time.now() + rospy.Duration(20.0)
    while not rospy.is_shutdown() and rospy.Time.now() < deadline:
        state = sample("/car/joint_states", JointState)
        positions = dict(zip(state.name, state.position))
        if (
            abs(positions.get("gimbal_pan_joint", 99.0) - expected_pan) < 0.08
            and abs(
                positions.get("gimbal_tilt_joint", 99.0) - expected_tilt
            ) < 0.08
        ):
            break
    else:
        raise RuntimeError("gimbal did not reach target")
else:
    raise ValueError("unknown probe: " + kind)
' "$kind" "$@" >>"$probe_log" 2>&1
}

check_probe() {
  local kind=$1
  local label=$2
  shift 2

  if run_probe "$kind" "$@"; then
    pass "$label"
  else
    fail "$label (probe=${kind}, log=${probe_log})"
  fi
}

check_sensor_suite() {
  local phase=$1
  check_probe camera "${phase}: OpenMV image and CameraInfo valid"
  check_probe lidar "${phase}: 360-point LiDAR detects obstacles"
  check_probe ultrasonic \
    "${phase}: all four ultrasonic beams detect obstacles"
  check_probe imu "${phase}: IMU data finite"
  check_probe tf "${phase}: sensor TF tree complete"
}

publish_gimbal_targets() {
  local status=0

  timeout 5 rostopic pub -1 /car/gimbal/pan/command std_msgs/Float64 \
    "{data: 2.0}" >>"$probe_log" 2>&1 || status=1
  timeout 5 rostopic pub -1 /car/gimbal/tilt/command std_msgs/Float64 \
    "{data: -2.0}" >>"$probe_log" 2>&1 || status=1
  return "$status"
}

call_swap() {
  local target=$1
  local label=$2
  local response

  response=$(timeout "$timeout_seconds" rosservice call \
    /car/swap_chassis "{target_chassis: '${target}'}" \
    2>/dev/null || true)
  if grep -Eq 'success: *(True|true)' <<<"$response"; then
    pass "$label"
    return 0
  fi
  fail "$label (response=${response//$'\n'/ })"
  return 1
}

if [[ ! -r /usr/share/gazebo/setup.sh ]]; then
  printf '[ERROR] Gazebo Classic setup not found\n' >&2
  exit 2
fi
if [[ ! -r /opt/ros/noetic/setup.bash ]]; then
  printf '[ERROR] ROS Noetic setup not found\n' >&2
  exit 2
fi
if [[ ! -r "$workspace_root/devel/setup.bash" ]]; then
  printf '[ERROR] workspace setup not found: %s/devel/setup.bash\n' \
    "$workspace_root" >&2
  exit 2
fi

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PYTHONNOUSERSITE=1
unset PYTHONHOME PYTHONPATH
unset CONDA_PREFIX CONDA_DEFAULT_ENV CONDA_PROMPT_MODIFIER CONDA_SHLVL
unset CONDA_AUTO_PATH CONDA_EXE CONDA_PYTHON_EXE _CE_CONDA _CE_M
export ROS_LOG_DIR="${log_dir}/ros"
export GAZEBO_RESOURCE_PATH="${GAZEBO_RESOURCE_PATH:-}"
export GAZEBO_PLUGIN_PATH="${GAZEBO_PLUGIN_PATH:-}"
export GAZEBO_MODEL_PATH="${GAZEBO_MODEL_PATH:-}"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"

set +u
setup_failed=0
source /usr/share/gazebo/setup.sh || setup_failed=1
source /opt/ros/noetic/setup.bash || setup_failed=1
source "$workspace_root/devel/setup.bash" || setup_failed=1
set -u
if ((setup_failed != 0)); then
  printf '[ERROR] failed to load Gazebo or ROS runtime environment\n' >&2
  exit 2
fi
if ! /usr/bin/python3 -c \
  'import gazebo_msgs, sensor_msgs, tf2_ros, yaml' >/dev/null 2>&1; then
  printf '[ERROR] Task-05 Python ROS dependencies are unavailable\n' >&2
  exit 2
fi

start_virtual_display

if ! rosparam get /rosversion >/dev/null 2>&1; then
  setsid roscore >"$roscore_log" 2>&1 &
  roscore_pid=$!
  if ! wait_for_master; then
    printf '[ERROR] ROS master did not become ready\n' >&2
    exit 2
  fi
fi

if timeout 5 rosservice list 2>/dev/null |
   grep -Eq '^/(gazebo|car/controller_manager)'; then
  printf '[ERROR] Gazebo or /car controller manager is already running\n' >&2
  exit 2
fi

setsid roslaunch air_ground_car_bringup car_mecanum.launch \
  gui:=false headless:=true paused:=false \
  >"$roslaunch_log" 2>&1 &
launch_pid=$!

mecanum_controllers=(
  joint_state_controller
  gimbal_pan_controller
  gimbal_tilt_controller
  front_left_wheel_controller
  front_right_wheel_controller
  rear_left_wheel_controller
  rear_right_wheel_controller
)
diff_controllers=(
  joint_state_controller
  gimbal_pan_controller
  gimbal_tilt_controller
  diff_drive_controller
)

check_model_spawned mecanum_car "Gazebo mecanum model spawned"
check_controllers "Mecanum and gimbal controllers running" \
  "${mecanum_controllers[@]}"
spawn_obstacles
check_sensor_suite "Mecanum initial"

if publish_gimbal_targets; then
  pass "Stable gimbal command topics accept targets"
else
  fail "Stable gimbal command topics accept targets"
fi
check_probe gimbal "Gimbal commands clamp to +90/-45 degrees" \
  1.57079632679 -0.78539816339

if call_swap diff "Swap service switches mecanum to diff"; then
  check_model_spawned diff_car "Diff model spawned after swap"
  check_controllers "Diff and gimbal controllers running after swap" \
    "${diff_controllers[@]}"
  check_sensor_suite "Diff after swap"
  check_probe gimbal "Gimbal target restored after diff swap" \
    1.57079632679 -0.78539816339
fi

if call_swap mecanum "Swap service restores mecanum chassis"; then
  check_model_spawned mecanum_car "Mecanum model restored after swap"
  check_controllers "Mecanum and gimbal controllers restored" \
    "${mecanum_controllers[@]}"
  check_sensor_suite "Mecanum after round trip"
  check_probe gimbal "Gimbal target restored after mecanum swap" \
    1.57079632679 -0.78539816339
fi

printf 'Task-05 results: %d passed, %d failed\n' "$pass_count" "$fail_count"
((fail_count == 0))
