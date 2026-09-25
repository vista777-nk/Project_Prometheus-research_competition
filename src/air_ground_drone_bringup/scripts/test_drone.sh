#!/usr/bin/env bash
set -uo pipefail

timeout_seconds=${TASK02_TIMEOUT_SECONDS:-60}
px4_root=${PX4_AUTOPILOT_DIR:-${HOME}/PX4-Autopilot}
workspace_root=${AIR_GROUND_WS:-${HOME}/air_ground_sim_ws}

if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
  printf '[ERROR] TASK02_TIMEOUT_SECONDS must be a positive integer\n' >&2
  exit 2
fi

pass_count=0
fail_count=0
launch_pid=
roscore_pid=
xvfb_pid=
log_dir=$(mktemp -d "${TMPDIR:-/tmp}/task02-drone.XXXXXX")
roslaunch_log="${log_dir}/roslaunch.log"
roscore_log="${log_dir}/roscore.log"
xvfb_log="${log_dir}/xvfb.log"

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

cleanup() {
  set +e
  stop_process_group "$launch_pid"
  [[ -n "$launch_pid" ]] && wait "$launch_pid" 2>/dev/null
  stop_process_group "$roscore_pid"
  [[ -n "$roscore_pid" ]] && wait "$roscore_pid" 2>/dev/null
  stop_process_group "$xvfb_pid"
  [[ -n "$xvfb_pid" ]] && wait "$xvfb_pid" 2>/dev/null
  printf 'task02_log_dir=%s\n' "$log_dir"
}

on_signal() {
  local exit_code=$1
  trap - INT TERM
  exit "$exit_code"
}

trap cleanup EXIT
trap 'on_signal 130' INT
trap 'on_signal 143' TERM

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

check_topic_sample() {
  local topic=$1
  local pattern=$2
  local label=$3
  local deadline=$((SECONDS + timeout_seconds))
  local sample

  while ((SECONDS < deadline)); do
    if [[ -n "$launch_pid" ]] && ! kill -0 "$launch_pid" 2>/dev/null; then
      fail "$label (roslaunch exited; log=${roslaunch_log})"
      return
    fi
    sample=$(timeout 5 rostopic echo --noarr -n 1 "$topic" 2>/dev/null || true)
    if grep -Eq "$pattern" <<<"$sample"; then
      pass "$label"
      return
    fi
    sleep 2
  done
  fail "$label (topic=$topic, timeout=${timeout_seconds}s)"
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
  printf '[ERROR] workspace setup not found: %s/devel/setup.bash\n' "$workspace_root" >&2
  exit 2
fi
if [[ ! -r "$px4_root/Tools/simulation/gazebo-classic/setup_gazebo.bash" ]]; then
  printf '[ERROR] PX4 Gazebo setup not found under %s\n' "$px4_root" >&2
  exit 2
fi
if [[ ! -x "$px4_root/build/px4_sitl_default/bin/px4" ]]; then
  printf '[ERROR] PX4 SITL binary not found under %s\n' "$px4_root" >&2
  exit 2
fi

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PYTHONNOUSERSITE=1
unset PYTHONHOME PYTHONPATH CONDA_PREFIX CONDA_DEFAULT_ENV
export ROS_LOG_DIR="${log_dir}/ros"
export GAZEBO_RESOURCE_PATH="${GAZEBO_RESOURCE_PATH:-}"
export GAZEBO_PLUGIN_PATH="${GAZEBO_PLUGIN_PATH:-}"
export GAZEBO_MODEL_PATH="${GAZEBO_MODEL_PATH:-}"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"

# The upstream Gazebo, ROS and PX4 setup scripts read optional variables without
# defaults. Temporarily disable nounset so this script also works in a clean shell.
set +u
setup_failed=0
source /usr/share/gazebo/setup.sh || setup_failed=1
source /opt/ros/noetic/setup.bash || setup_failed=1
source "$workspace_root/devel/setup.bash" || setup_failed=1
source "$px4_root/Tools/simulation/gazebo-classic/setup_gazebo.bash" \
  "$px4_root" "$px4_root/build/px4_sitl_default" || setup_failed=1
set -u
if ((setup_failed != 0)); then
  printf '[ERROR] failed to load Gazebo, ROS or PX4 runtime environment\n' >&2
  exit 2
fi

export ROS_PACKAGE_PATH="${ROS_PACKAGE_PATH}:${px4_root}:${px4_root}/Tools/simulation/gazebo-classic/sitl_gazebo-classic"

if ldd /opt/ros/noetic/lib/libgazebo_ros_openni_kinect.so 2>/dev/null |
   grep -Fq 'libDepthCameraPlugin.so => not found'; then
  printf '[ERROR] Gazebo depth camera plugin cannot resolve libDepthCameraPlugin.so\n' >&2
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

setsid roslaunch air_ground_drone_bringup drone_sitl.launch \
  gui:=false interactive:=false start_sensors:=true \
  >"$roslaunch_log" 2>&1 &
launch_pid=$!

check_topic_sample /mavros/state 'connected: *True' \
  'MAVROS connected'
check_topic_sample /iris/camera/rgb/image_raw 'height: *480' \
  'Depth camera RGB image publishing'
check_topic_sample /iris/camera/depth/image_raw 'height: *480' \
  'Depth camera depth image publishing'
check_topic_sample /drone/camera/rgb/image_raw 'height: *480' \
  'Project RGB image relay publishing'
check_topic_sample /drone/camera/depth/image_raw 'height: *480' \
  'Project depth image relay publishing'
check_topic_sample /mavros/global_position/global 'latitude:' \
  'MAVROS GPS publishing'
check_topic_sample /mavros/imu/data 'angular_velocity:' \
  'MAVROS IMU publishing'
check_topic_sample /drone/gps/local_pose 'position:' \
  'GPS converted to local ENU pose'
check_topic_sample /drone/camera/depth/points_downsampled 'width: *[1-9][0-9]*' \
  'Depth point cloud downsampling publishing'

printf 'Task-02 results: %d passed, %d failed\n' "$pass_count" "$fail_count"
((fail_count == 0))
