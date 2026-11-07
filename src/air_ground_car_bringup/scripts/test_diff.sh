#!/usr/bin/env bash
set -uo pipefail

timeout_seconds=${TASK03_TIMEOUT_SECONDS:-60}
command_duration=${TASK03_COMMAND_DURATION_SECONDS:-2}
workspace_root=${AIR_GROUND_WS:-${HOME}/air_ground_sim_ws}
model_name=${TASK03_MODEL_NAME:-diff_car}

if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]] ||
   [[ ! "$command_duration" =~ ^[1-9][0-9]*$ ]]; then
  printf '[ERROR] Task-03 timeout values must be positive integers\n' >&2
  exit 2
fi

pass_count=0
fail_count=0
launch_pid=
roscore_pid=
log_dir=$(mktemp -d "${TMPDIR:-/tmp}/task03-diff.XXXXXX")
roslaunch_log="${log_dir}/roslaunch.log"
roscore_log="${log_dir}/roscore.log"
command_log="${log_dir}/commands.log"

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

stop_controller_spawner() {
  local response

  if [[ -z "$launch_pid" ]] || ! process_group_alive "$launch_pid" ||
     ! command -v rosnode >/dev/null 2>&1; then
    return
  fi
  if ! timeout 3 rosnode list 2>/dev/null |
     grep -Fxq /car_controller_spawner; then
    return
  fi

  timeout 5 rosnode kill /car_controller_spawner >/dev/null 2>&1 || true
  for _ in {1..20}; do
    response=$(timeout 2 rosservice call \
      /car/controller_manager/list_controllers 2>/dev/null || true)
    if ! grep -Eq 'name: *(joint_state_controller|diff_drive_controller)' \
       <<<"$response"; then
      return
    fi
    sleep 0.25
  done
}

cleanup() {
  set +e
  stop_controller_spawner
  stop_process_group "$launch_pid"
  [[ -n "$launch_pid" ]] && wait "$launch_pid" 2>/dev/null
  stop_process_group "$roscore_pid"
  [[ -n "$roscore_pid" ]] && wait "$roscore_pid" 2>/dev/null
  printf 'task03_log_dir=%s\n' "$log_dir"
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

check_model_spawned() {
  local deadline=$((SECONDS + timeout_seconds))
  local response

  while ((SECONDS < deadline)); do
    if ! launch_alive; then
      fail "Gazebo model spawned (roslaunch exited; log=${roslaunch_log})"
      return
    fi
    response=$(timeout 5 rosservice call /gazebo/get_model_state \
      "{model_name: '${model_name}', relative_entity_name: 'world'}" \
      2>/dev/null || true)
    if grep -Eq 'success: *(True|true)' <<<"$response"; then
      pass "Gazebo model spawned"
      return
    fi
    sleep 1
  done
  fail "Gazebo model spawned (timeout=${timeout_seconds}s)"
}

check_controllers() {
  local deadline=$((SECONDS + timeout_seconds))
  local response

  while ((SECONDS < deadline)); do
    if ! launch_alive; then
      fail "ros_control controllers running (roslaunch exited)"
      return
    fi
    response=$(timeout 5 rosservice call \
      /car/controller_manager/list_controllers 2>/dev/null || true)
    if printf '%s\n' "$response" | /usr/bin/python3 -c '
import sys
import yaml

data = yaml.safe_load(sys.stdin.read()) or {}
states = {
    item.get("name"): item.get("state")
    for item in data.get("controller", [])
}
expected = ("joint_state_controller", "diff_drive_controller")
raise SystemExit(0 if all(states.get(name) == "running" for name in expected) else 1)
'; then
      pass "ros_control controllers running"
      return
    fi
    sleep 1
  done
  fail "ros_control controllers running (timeout=${timeout_seconds}s)"
}

check_topic_sample() {
  local topic=$1
  local pattern=$2
  local label=$3
  local deadline=$((SECONDS + timeout_seconds))
  local sample

  while ((SECONDS < deadline)); do
    if ! launch_alive; then
      fail "$label (roslaunch exited)"
      return
    fi
    sample=$(timeout 5 rostopic echo -n 1 "$topic" 2>/dev/null || true)
    if grep -Eq "$pattern" <<<"$sample"; then
      pass "$label"
      return
    fi
    sleep 1
  done
  fail "$label (topic=${topic}, timeout=${timeout_seconds}s)"
}

get_model_pose() {
  local response
  response=$(timeout 5 rosservice call /gazebo/get_model_state \
    "{model_name: '${model_name}', relative_entity_name: 'world'}" \
    2>/dev/null) || return 1

  printf '%s\n' "$response" | /usr/bin/python3 -c '
import math
import sys
import yaml

data = yaml.safe_load(sys.stdin.read())
if not data or not data.get("success"):
    raise SystemExit(1)
position = data["pose"]["position"]
orientation = data["pose"]["orientation"]
x = float(position["x"])
y = float(position["y"])
qx = float(orientation["x"])
qy = float(orientation["y"])
qz = float(orientation["z"])
qw = float(orientation["w"])
yaw = math.atan2(
    2.0 * (qw * qz + qx * qy),
    1.0 - 2.0 * (qy * qy + qz * qz),
)
print(f"{x:.9f} {y:.9f} {yaw:.9f}")
'
}

send_twist() {
  local linear_x=$1
  local angular_z=$2
  local status=0

  timeout "${command_duration}s" rostopic pub -r 20 /car/cmd_vel \
    geometry_msgs/Twist \
    "{linear: {x: ${linear_x}, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: ${angular_z}}}" \
    >>"$command_log" 2>&1 || status=$?
  if ((status != 0 && status != 124)); then
    return 1
  fi

  timeout 5 rostopic pub -1 /car/cmd_vel geometry_msgs/Twist \
    "{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}" \
    >>"$command_log" 2>&1 || return 1
  sleep 1
}

motion_is_valid() {
  local mode=$1
  shift
  /usr/bin/python3 -c '
import math
import sys

mode = sys.argv[1]
x0, y0, yaw0, x1, y1, yaw1 = map(float, sys.argv[2:])
forward_delta = (
    (x1 - x0) * math.cos(yaw0)
    + (y1 - y0) * math.sin(yaw0)
)
yaw_delta = math.atan2(
    math.sin(yaw1 - yaw0),
    math.cos(yaw1 - yaw0),
)

valid = {
    "forward": forward_delta > 0.10,
    "backward": forward_delta < -0.10,
    "turn": abs(yaw_delta) > 0.35,
}[mode]
raise SystemExit(0 if valid else 1)
' "$mode" "$@"
}

check_motion() {
  local mode=$1
  local linear_x=$2
  local angular_z=$3
  local label=$4
  local start_pose
  local end_pose

  start_pose=$(get_model_pose) || {
    fail "$label (could not sample initial pose)"
    return
  }
  if ! send_twist "$linear_x" "$angular_z"; then
    fail "$label (failed to publish /car/cmd_vel)"
    return
  fi
  end_pose=$(get_model_pose) || {
    fail "$label (could not sample final pose)"
    return
  }

  if motion_is_valid "$mode" $start_pose $end_pose; then
    pass "$label"
  else
    fail "$label (start=${start_pose}, end=${end_pose})"
  fi
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
if ! /usr/bin/python3 -c 'import yaml' >/dev/null 2>&1; then
  printf '[ERROR] python3-yaml is required by the verification script\n' >&2
  exit 2
fi

if ! rosparam get /rosversion >/dev/null 2>&1; then
  setsid roscore >"$roscore_log" 2>&1 &
  roscore_pid=$!
  if ! wait_for_master; then
    printf '[ERROR] ROS master did not become ready\n' >&2
    exit 2
  fi
fi

if timeout 5 rosservice list 2>/dev/null |
   grep -Eq '^/(gazebo|get_world_properties|car/controller_manager)'; then
  printf '[ERROR] Gazebo or /car controller manager is already running\n' >&2
  exit 2
fi

setsid roslaunch air_ground_car_bringup car_diff.launch \
  gui:=false headless:=true paused:=false model_name:="$model_name" \
  >"$roslaunch_log" 2>&1 &
launch_pid=$!

check_model_spawned
check_controllers
check_topic_sample /car/joint_states 'left_wheel_joint' \
  "Wheel encoder joint states publishing"
check_topic_sample /car/odom 'child_frame_id:.*base_link' \
  "Project odometry publishing"
check_motion forward 0.25 0.0 "Forward command moves car forward"
check_motion backward -0.25 0.0 "Reverse command moves car backward"
check_motion turn 0.0 0.8 "Angular command turns car"

printf 'Task-03 results: %d passed, %d failed\n' "$pass_count" "$fail_count"
((fail_count == 0))
