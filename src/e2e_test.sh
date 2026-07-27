#!/usr/bin/env bash
# Task-09 end-to-end validation for the complete air-ground simulation.
set -uo pipefail

timeout_seconds=${TASK09_TIMEOUT_SECONDS:-90}
workspace_root=${AIR_GROUND_WS:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}
px4_root=${PX4_AUTOPILOT_DIR:-${HOME}/PX4-Autopilot}
log_dir=$(mktemp -d "${TMPDIR:-/tmp}/task09-e2e.XXXXXX")
roslaunch_log=${log_dir}/roslaunch.log
roscore_log=${log_dir}/roscore.log
xvfb_log=${log_dir}/xvfb.log
mission_log=${log_dir}/mission.log

pass_count=0
fail_count=0
launch_pid=
roscore_pid=
xvfb_pid=
mission_pid=

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
  [[ -n "$pid" ]] || return 1
  kill -0 -- "-$pid" 2>/dev/null || kill -0 "$pid" 2>/dev/null
}

stop_process_group() {
  local pid=$1
  [[ -n "$pid" ]] || return
  process_group_alive "$pid" || return

  kill -INT -- "-$pid" 2>/dev/null || kill -INT "$pid" 2>/dev/null || true
  for _ in {1..20}; do
    process_group_alive "$pid" || return
    sleep 0.25
  done
  kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
  for _ in {1..10}; do
    process_group_alive "$pid" || return
    sleep 0.25
  done
  kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
}

cleanup() {
  set +e
  stop_process_group "$mission_pid"
  stop_process_group "$launch_pid"
  [[ -n "$launch_pid" ]] && wait "$launch_pid" 2>/dev/null
  stop_process_group "$roscore_pid"
  [[ -n "$roscore_pid" ]] && wait "$roscore_pid" 2>/dev/null
  stop_process_group "$xvfb_pid"
  [[ -n "$xvfb_pid" ]] && wait "$xvfb_pid" 2>/dev/null
  # Gazebo/PX4 children can detach from roslaunch's process group.
  timeout 15 make -C "$workspace_root" kill >/dev/null 2>&1 || true
  printf 'task09_log_dir=%s\n' "$log_dir"
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

launch_alive() {
  process_group_alive "$launch_pid"
}

start_virtual_display() {
  local display_number=

  [[ -n "${DISPLAY:-}" ]] && return 0
  command -v Xvfb >/dev/null 2>&1 || {
    printf '[ERROR] DISPLAY is unset and Xvfb is not installed\n' >&2
    exit 2
  }

  for candidate in {90..199}; do
    if [[ ! -e "/tmp/.X${candidate}-lock" ]] &&
       [[ ! -S "/tmp/.X11-unix/X${candidate}" ]]; then
      display_number=$candidate
      break
    fi
  done
  [[ -n "$display_number" ]] || {
    printf '[ERROR] no free X display number found\n' >&2
    exit 2
  }

  setsid Xvfb ":${display_number}" -screen 0 1280x1024x24 \
    -nolisten tcp >"$xvfb_log" 2>&1 &
  xvfb_pid=$!
  export DISPLAY=":${display_number}"
  export LIBGL_ALWAYS_SOFTWARE=1

  for _ in {1..20}; do
    [[ -S "/tmp/.X11-unix/X${display_number}" ]] && return 0
    process_group_alive "$xvfb_pid" || break
    sleep 0.25
  done
  printf '[ERROR] Xvfb did not become ready; log=%s\n' "$xvfb_log" >&2
  exit 2
}

check_node() {
  local node=$1
  local label=$2
  local deadline=$((SECONDS + timeout_seconds))
  local nodes

  while ((SECONDS < deadline)); do
    launch_alive || {
      fail "$label (roslaunch exited; log=$roslaunch_log)"
      return
    }
    nodes=$(timeout 5 rosnode list 2>/dev/null || true)
    if grep -Fxq "$node" <<<"$nodes"; then
      pass "$label"
      return
    fi
    sleep 1
  done
  fail "$label (node=$node, timeout=${timeout_seconds}s)"
}

check_topic() {
  local topic=$1
  local pattern=$2
  local label=$3
  local deadline=$((SECONDS + timeout_seconds))
  local sample

  while ((SECONDS < deadline)); do
    launch_alive || {
      fail "$label (roslaunch exited; log=$roslaunch_log)"
      return
    }
    sample=$(timeout 5 rostopic echo --noarr -n 1 "$topic" 2>/dev/null || true)
    if grep -Eq "$pattern" <<<"$sample"; then
      pass "$label"
      return
    fi
    sleep 1
  done
  fail "$label (topic=$topic, timeout=${timeout_seconds}s)"
}

check_model() {
  local model=$1
  local label=$2
  local deadline=$((SECONDS + timeout_seconds))
  local response

  while ((SECONDS < deadline)); do
    launch_alive || {
      fail "$label (roslaunch exited; log=$roslaunch_log)"
      return
    }
    response=$(timeout 5 rosservice call /gazebo/get_model_state \
      "{model_name: '${model}', relative_entity_name: 'world'}" \
      2>/dev/null || true)
    if grep -Eq 'success: *(True|true)' <<<"$response"; then
      pass "$label"
      return
    fi
    sleep 1
  done
  fail "$label (model=$model, timeout=${timeout_seconds}s)"
}

check_mission_flow() {
  local deadline

  rm -f "$mission_log"
  setsid timeout "$timeout_seconds" rostopic echo --noarr -n 1 \
    /car/mission >"$mission_log" 2>&1 &
  mission_pid=$!
  sleep 1
  timeout 5 rostopic pub -1 /server/eqa/query std_msgs/String \
    "data: 'Find the nearest obstacle'" >/dev/null 2>&1 || true

  deadline=$((SECONDS + timeout_seconds))
  while ((SECONDS < deadline)); do
    if grep -Eq 'query_text:.*Find the nearest obstacle' "$mission_log" 2>/dev/null &&
       grep -Eq 'robot_id:.*car' "$mission_log" 2>/dev/null; then
      pass "EQA query -> World Model ASK -> Coordinator -> /car/mission"
      stop_process_group "$mission_pid"
      mission_pid=
      return
    fi
    launch_alive || break
    sleep 1
  done
  fail "EQA query -> World Model ASK -> Coordinator -> /car/mission"
  printf '  Mission log: %s\n' "$mission_log"
  stop_process_group "$mission_pid"
  mission_pid=
}

if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
  printf '[ERROR] TASK09_TIMEOUT_SECONDS must be a positive integer\n' >&2
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
if [[ ! -r "$px4_root/Tools/simulation/gazebo-classic/setup_gazebo.bash" ||
      ! -x "$px4_root/build/px4_sitl_default/bin/px4" ]]; then
  printf '[ERROR] PX4 SITL runtime is incomplete under %s\n' "$px4_root" >&2
  exit 2
fi
export ROS_LOG_DIR="${log_dir}/ros"
mkdir -p "$ROS_LOG_DIR"
set +u
setup_failed=0
source "$workspace_root/scripts/setup_runtime.sh" px4 || setup_failed=1
set -u
if ((setup_failed != 0)); then
  printf '[ERROR] failed to load the ROS/PX4 runtime environment\n' >&2
  exit 2
fi
for command_name in roslaunch rosnode rostopic rosservice rosparam timeout setsid; do
  command -v "$command_name" >/dev/null 2>&1 || {
    printf '[ERROR] required command is missing: %s\n' "$command_name" >&2
    exit 2
  }
done

start_virtual_display
timeout 15 make -C "$workspace_root" kill >/dev/null 2>&1 || true

if ! rosparam get /rosversion >/dev/null 2>&1; then
  setsid roscore >"$roscore_log" 2>&1 &
  roscore_pid=$!
  wait_for_master || {
    printf '[ERROR] ROS master did not become ready; log=%s\n' "$roscore_log" >&2
    exit 2
  }
fi

printf '%s\n' '============================================================'
printf '%s\n' '  Air-Ground Simulation - E2E Integration Test'
printf '%s\n' '============================================================'
printf '%s\n' '--- Phase 1: Launch full system ---'
setsid roslaunch air_ground_bringup air_ground_sim.launch \
  chassis:=diff gui:=false headless:=true interactive:=false \
  >"$roslaunch_log" 2>&1 &
launch_pid=$!

for node in \
  /drone_preprocessor /car_preprocessor /drone_car_bridge \
  /edge_server_bridge /tcp_server /world_model /slam_node \
  /eqa_engine /coordinator; do
  check_node "$node" "Core node running: $node"
done

printf '%s\n' '--- Phase 2: Model spawn ---'
check_model iris 'PX4 drone model spawned'
check_model diff_car 'Differential-drive car model spawned'

printf '%s\n' '--- Phase 3: Sensor data ---'
check_topic /drone/camera/depth/image_raw 'height:' \
  'Drone depth camera data'
check_topic /mavros/global_position/global 'latitude:' \
  'Drone GPS data'
check_topic /mavros/imu/data 'angular_velocity:' \
  'Drone IMU data'
check_topic /car/openmv/image_raw 'height:' 'Car OpenMV image data'
check_topic /car/scan 'ranges:' 'Car LiDAR data'
check_topic /car/imu/data 'angular_velocity:' 'Car IMU data'
check_topic /car/ultrasonic/front 'ranges:' 'Car front ultrasonic data'
check_topic /car/odom 'pose:' 'Car odometry data'

printf '%s\n' '--- Phase 4: Edge preprocessing ---'
check_topic /drone/observation 'robot_id:.*drone' \
  'Drone Observation published'
check_topic /drone/state 'robot_id:.*drone' \
  'Drone RobotState published'
check_topic /car/observation 'robot_id:.*car' \
  'Car Observation published'
check_topic /car/state 'robot_id:.*car' \
  'Car RobotState published'

printf '%s\n' '--- Phase 5: Communication bridge ---'
check_topic /drone/heartbeat 'data:' 'Drone heartbeat bridge output'
check_topic /drone/pose 'position:' 'Drone pose bridge output'
check_topic /server/car/observation 'robot_id:.*car' \
  'TCP server receives car Observation'
check_topic /server/drone/state 'robot_id:.*drone' \
  'TCP server receives drone RobotState'

printf '%s\n' '--- Phase 6: Server reception ---'
check_topic /server/world_state 'agents:' 'World Model publishes WorldState'
check_node /slam_node 'SLAM node running'
check_node /eqa_engine 'EQA engine running'
check_node /coordinator 'Coordinator running'

printf '%s\n' '--- Phase 7: End-to-end command flow ---'
check_mission_flow

printf '%s\n' '--- Phase 8: Runtime resources ---'
available_mb=$(awk '/MemAvailable:/ {printf "%d", $2 / 1024}' /proc/meminfo)
if [[ "$available_mb" =~ ^[0-9]+$ ]]; then
  printf '  Available memory: %s MB\n' "$available_mb"
  if ((available_mb >= 4096)); then
    pass 'At least 4 GB memory available for the simulation'
  else
    fail 'At least 4 GB memory available for the simulation'
  fi
else
  fail 'Could not read available memory'
fi

printf '%s\n' '============================================================'
printf '  RESULTS: %d passed | %d failed\n' "$pass_count" "$fail_count"
printf '%s\n' '============================================================'
if ((fail_count == 0)); then
  printf '[PASS] Task-09 E2E validation completed successfully\n'
else
  printf '[FAIL] Task-09 E2E validation found %d failure(s)\n' "$fail_count"
  printf '  Full launch log: %s\n' "$roslaunch_log"
fi
exit "$((fail_count == 0 ? 0 : 1))"
