#!/usr/bin/env bash
set -uo pipefail

timeout_seconds=${TASK07_TIMEOUT_SECONDS:-60}
workspace_root=${AIR_GROUND_WS:-${HOME}/air_ground_sim_ws}

if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
  printf '[ERROR] TASK07_TIMEOUT_SECONDS must be a positive integer\n' >&2
  exit 2
fi

pass_count=0
fail_count=0
roscore_pid=
source_pid=
drone_edge_pid=
car_edge_pid=
server_pid=
bridge_pid=
mission_echo_pid=
log_dir=$(mktemp -d "${TMPDIR:-/tmp}/task07-server.XXXXXX")
roscore_log="${log_dir}/roscore.log"
source_log="${log_dir}/runtime_sources.log"
drone_edge_log="${log_dir}/drone_edge.log"
car_edge_log="${log_dir}/car_edge.log"
server_log="${log_dir}/server.log"
bridge_log="${log_dir}/bridge.log"
mission_log="${log_dir}/mission.log"

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
  for pid in \
    "$mission_echo_pid" "$bridge_pid" "$server_pid" "$car_edge_pid" \
    "$drone_edge_pid" "$source_pid" "$roscore_pid"; do
    stop_process_group "$pid"
    [[ -n "$pid" ]] && wait "$pid" 2>/dev/null
  done
  printf 'task07_log_dir=%s\n' "$log_dir"
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

all_launches_alive() {
  local pid
  for pid in "$drone_edge_pid" "$car_edge_pid" "$server_pid" "$bridge_pid"; do
    [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null || return 1
  done
}

check_nodes() {
  local deadline=$((SECONDS + timeout_seconds))
  local nodes
  while ((SECONDS < deadline)); do
    if ! all_launches_alive; then
      fail "Edge, bridge and five server nodes remain running"
      return
    fi
    nodes=$(timeout 5 rosnode list 2>/dev/null || true)
    local complete=true
    for node in \
      /drone_preprocessor /car_preprocessor /edge_server_bridge \
      /tcp_server /world_model /slam_node /eqa_engine /coordinator; do
      grep -Fxq "$node" <<<"$nodes" || complete=false
    done
    if [[ "$complete" == true ]]; then
      pass "Edge, bridge and five server nodes remain running"
      return
    fi
    sleep 1
  done
  fail "Edge, bridge and five server nodes remain running (timeout)"
}

check_topic() {
  local topic=$1
  local pattern=$2
  local label=$3
  local deadline=$((SECONDS + timeout_seconds))
  local sample
  while ((SECONDS < deadline)); do
    all_launches_alive || {
      fail "$label (launch exited)"
      return
    }
    sample=$(timeout 5 rostopic echo -n 1 "$topic" 2>/dev/null || true)
    if grep -Eq "$pattern" <<<"$sample"; then
      pass "$label"
      return
    fi
    sleep 0.5
  done
  fail "$label (topic=${topic}, timeout=${timeout_seconds}s)"
}

check_world_agents() {
  local deadline=$((SECONDS + timeout_seconds))
  local sample
  while ((SECONDS < deadline)); do
    sample=$(
      timeout 5 rostopic echo -n 1 /server/world_state 2>/dev/null || true
    )
    if grep -Eq 'robot_id: *["'\'']?car' <<<"$sample" &&
       grep -Eq 'robot_id: *["'\'']?drone' <<<"$sample"; then
      pass "World Model aggregates fresh car and drone states"
      return
    fi
    sleep 0.5
  done
  fail "World Model aggregates fresh car and drone states"
}

wait_for_log() {
  local file=$1
  local pattern=$2
  local label=$3
  local deadline=$((SECONDS + timeout_seconds))
  while ((SECONDS < deadline)); do
    if grep -Eq "$pattern" "$file" 2>/dev/null; then
      pass "$label"
      return
    fi
    sleep 0.5
  done
  fail "$label (log=${file})"
}

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
export ROS_HOME="${log_dir}/ros_home"
mkdir -p "$ROS_LOG_DIR" "$ROS_HOME"

set +u
setup_failed=0
source /opt/ros/noetic/setup.bash || setup_failed=1
source "$workspace_root/devel/setup.bash" || setup_failed=1
set -u
if ((setup_failed != 0)); then
  printf '[ERROR] failed to load the ROS runtime environment\n' >&2
  exit 2
fi

package_path=$(rospack find air_ground_lab_server 2>/dev/null) || {
  printf '[ERROR] air_ground_lab_server is not discoverable\n' >&2
  exit 2
}
runtime_source="${package_path}/test/runtime_sources.py"
if [[ ! -r "$runtime_source" ]]; then
  printf '[ERROR] runtime source is missing: %s\n' "$runtime_source" >&2
  exit 2
fi

if ! /usr/bin/python3 -c '
import socket
endpoint = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
endpoint.bind(("127.0.0.1", 9090))
endpoint.close()
' >/dev/null 2>&1; then
  printf '[ERROR] Task-07 TCP port 9090 is already in use\n' >&2
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

setsid /usr/bin/python3 "$runtime_source" >"$source_log" 2>&1 &
source_pid=$!
setsid roslaunch air_ground_drone_bringup drone_edge.launch \
  >"$drone_edge_log" 2>&1 &
drone_edge_pid=$!
setsid roslaunch air_ground_car_bringup car_edge.launch \
  >"$car_edge_log" 2>&1 &
car_edge_pid=$!
setsid roslaunch air_ground_lab_server server.launch \
  >"$server_log" 2>&1 &
server_pid=$!
setsid roslaunch air_ground_com_bridge air_ground_com_bridge.launch \
  start_drone_bridge:=false >"$bridge_log" 2>&1 &
bridge_pid=$!

check_nodes
check_topic /car/observation 'robot_id: *["'\'']?car' \
  "Car edge publishes ICD Observation"
check_topic /car/state 'chassis_type: *["'\'']?diff' \
  "Car edge publishes chassis-aware RobotState"
check_topic /car/capability 'locomotion_type: *["'\'']?ground_wheeled' \
  "Car edge publishes latched Capability"
check_topic /drone/observation 'robot_id: *["'\'']?drone' \
  "Drone edge publishes ICD Observation"
check_topic /drone/state 'is_connected: *True' \
  "Drone edge publishes connectivity-aware RobotState"
check_topic /drone/capability 'locomotion_type: *["'\'']?aerial' \
  "Drone edge publishes latched Capability"
check_topic /car/server_connected 'data: *True' \
  "Task-06 bridge connects to Task-07 TCP server"
check_topic /server/car/observation 'lidar_2d' \
  "TCP server reconstructs car Observation"
check_topic /server/car/state 'robot_id: *["'\'']?car' \
  "TCP server reconstructs car RobotState"
check_topic /server/drone/observation 'robot_id: *["'\'']?drone' \
  "TCP server reconstructs drone Observation"
check_topic /server/drone/state 'robot_id: *["'\'']?drone' \
  "TCP server reconstructs drone RobotState"
check_world_agents

query_result=$(
  timeout 10 rosservice call /server/query_world_state \
    "{query_type: agent, args: [car]}" 2>/dev/null || true
)
if grep -Eq 'found: *True' <<<"$query_result" &&
   grep -Eq 'robot_id: *["'\'']?car' <<<"$query_result"; then
  pass "QueryWorldState returns the requested fresh agent"
else
  fail "QueryWorldState returns the requested fresh agent"
fi

setsid timeout "$timeout_seconds" rostopic echo -n 1 \
  /car/mission >"$mission_log" 2>&1 &
mission_echo_pid=$!
sleep 0.5
timeout 10 rostopic pub -1 /server/eqa/query std_msgs/String \
  "data: 'Where is the red ball?'" >/dev/null 2>&1 || true
wait_for_log "$mission_log" 'query_text: *["'\'']Where is the red ball' \
  "EQA ASK -> Mission -> Coordinator dispatch chain"

printf 'Task-07 results: %d passed, %d failed\n' \
  "$pass_count" "$fail_count"
((fail_count == 0))
