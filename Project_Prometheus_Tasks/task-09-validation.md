# Task-09: 仿真集成验证 + 端到端测试

## 前置条件

- Task-01~08 全部完成
- `make build` 无错误
- 至少 4GB 空闲内存用于同时运行 PX4 SITL + Gazebo + ROS nodes

## 目标

对整套空地联合仿真系统进行端到端（E2E）集成测试，验证数据从传感器→边缘→服务器→决策→执行的完整闭环。

---

## 9.1 端到端集成测试脚本

**文件：`~/air_ground_sim_ws/src/e2e_test.sh`**

```bash
#!/bin/bash
# ============================================================
# End-to-End Integration Test
# Verifies the complete air-ground pipeline.
# ============================================================
set -e

PASS=0
FAIL=0
WS="$HOME/air_ground_sim_ws"
source "$WS/devel/setup.bash"

green() { echo -e "\033[32m$1\033[0m"; }
red()   { echo -e "\033[31m$1\033[0m"; }
check() {
    if eval "$2" 2>/dev/null; then
        green "  [PASS] $1"
        PASS=$((PASS + 1))
    else
        red "  [FAIL] $1"
        FAIL=$((FAIL + 1))
    fi
}

echo "============================================================"
echo "  Air-Ground Simulation - E2E Integration Test"
echo "============================================================"

# Cleanup any previous runs
make kill 2>/dev/null || true
sleep 2

# ── Phase 1: Launch full system ─────────────────────────────
echo ""
echo "--- Phase 1: Launching Full System ---"
roslaunch air_ground_sim.launch chassis:=diff gui:=false headless:=true &
SIM_PID=$!
echo "  System PID: $SIM_PID"

echo "  Waiting for PX4 + Gazebo + ROS to fully initialize..."
# Wait for critical nodes
for i in $(seq 1 30); do
    if rosnode list 2>/dev/null | grep -q "mavros" && \
       rosnode list 2>/dev/null | grep -q "car_preprocessor" && \
       rosnode list 2>/dev/null | grep -q "tcp_server"; then
        echo "  All core nodes ready after ${i}s"
        break
    fi
    sleep 1
done
sleep 5  # Extra settling time

# ── Phase 2: Model Spawn Verification ───────────────────────
echo ""
echo "--- Phase 2: Model Spawn ---"
check "Drone (PX4) spawned" \
    "rosservice call /gazebo/get_model_state '{model_name: \"iris\"}' 2>/dev/null | grep -q success || rosservice call /gazebo/get_model_state '{model_name: \"iris_depth_camera\"}' 2>/dev/null | grep -q success"
check "Car (diff) spawned" \
    "rosservice call /gazebo/get_model_state '{model_name: \"diff_car\"}' 2>/dev/null | grep -q success"

# ── Phase 3: Sensor Data Flow ───────────────────────────────
echo ""
echo "--- Phase 3: Sensor Data ---"

# Drone sensors
check "Depth camera (/drone/depth_camera/depth/image_raw)" \
    "timeout 5 rostopic echo /drone/depth_camera/depth/image_raw -n 1 2>/dev/null | grep -q height"
check "GPS (/mavros/global_position/global)" \
    "timeout 5 rostopic echo /mavros/global_position/global -n 1 2>/dev/null | grep -q latitude"
check "Drone IMU (/mavros/imu/data)" \
    "timeout 5 rostopic echo /mavros/imu/data -n 1 2>/dev/null | grep -q angular_velocity"

# Car sensors
check "OpenMV (/car/openmv/image_raw)" \
    "timeout 5 rostopic echo /car/openmv/image_raw -n 1 2>/dev/null | grep -q height"
check "LiDAR (/car/scan)" \
    "timeout 5 rostopic echo /car/scan -n 1 2>/dev/null | grep -q ranges"
check "Car IMU (/car/imu/data)" \
    "timeout 5 rostopic echo /car/imu/data -n 1 2>/dev/null | grep -q angular_velocity"
check "Ultrasonic front (/car/ultrasonic/front)" \
    "timeout 5 rostopic echo /car/ultrasonic/front -n 1 2>/dev/null | grep -q ranges"
check "Odometry (/car/odom)" \
    "timeout 5 rostopic echo /car/odom -n 1 2>/dev/null | grep -q pose"

# ── Phase 4: Preprocessing Pipeline ─────────────────────────
echo ""
echo "--- Phase 4: Edge Preprocessing ---"
check "Drone observation (/drone/observation)" \
    "timeout 5 rostopic echo /drone/observation -n 1 2>/dev/null | grep -q robot_id"
check "Drone state (/drone/state)" \
    "timeout 5 rostopic echo /drone/state -n 1 2>/dev/null | grep -q robot_id"
check "Car observation (/car/observation)" \
    "timeout 5 rostopic echo /car/observation -n 1 2>/dev/null | grep -q robot_id"
check "Car state (/car/state)" \
    "timeout 5 rostopic echo /car/state -n 1 2>/dev/null | grep -q robot_id"

# ── Phase 5: Communication Bridge ───────────────────────────
echo ""
echo "--- Phase 5: Communication Bridge ---"
check "Drone heartbeat via bridge (/drone/heartbeat)" \
    "timeout 5 rostopic echo /drone/heartbeat -n 1 2>/dev/null | grep -q data"
check "Drone pose via bridge (/drone/pose)" \
    "timeout 5 rostopic echo /drone/pose -n 1 2>/dev/null | grep -q position"

# Check bridge node health
check "Drone-car bridge running" \
    "rosnode list 2>/dev/null | grep -q drone_car_bridge"
check "Edge-server bridge running" \
    "rosnode list 2>/dev/null | grep -q edge_server_bridge"

# ── Phase 6: Server Data Reception ──────────────────────────
echo ""
echo "--- Phase 6: Server Reception ---"
check "TCP server running" \
    "rosnode list 2>/dev/null | grep -q tcp_server"
check "World Model running" \
    "rosnode list 2>/dev/null | grep -q world_model"
check "WorldState published (/server/world_state)" \
    "timeout 5 rostopic echo /server/world_state -n 1 2>/dev/null | grep -q agents"
check "SLAM node running" \
    "rosnode list 2>/dev/null | grep -q slam_node"
check "EQA engine running" \
    "rosnode list 2>/dev/null | grep -q eqa_engine"
check "Coordinator running" \
    "rosnode list 2>/dev/null | grep -q coordinator"

# ── Phase 7: End-to-End Command Flow ────────────────────────
echo ""
echo "--- Phase 7: E2E Command Flow ---"

# 7.1 Send EQA query
echo "  Sending EQA query..."
rostopic pub -1 /server/eqa/query std_msgs/String "data: 'Find the nearest obstacle'" 2>/dev/null
sleep 2

# 7.2 Verify coordinator dispatched command (via cmd_vel as placeholder)
check "Coordinator processed EQA query → car cmd_vel" \
    "timeout 3 rostopic echo /car/cmd_vel -n 1 2>/dev/null | grep -q linear"

# ── Phase 8: Chassis Swap Test ──────────────────────────────
echo ""
echo "--- Phase 8: Chassis Swap ---"
rosservice call /car/swap_chassis "target_chassis: 'mecanum'" 2>/dev/null
sleep 4
check "Swapped to mecanum chassis" \
    "rosservice call /gazebo/get_model_state '{model_name: \"mecanum_car\"}' 2>/dev/null | grep -q success"

rosservice call /car/swap_chassis "target_chassis: 'diff'" 2>/dev/null
sleep 4
check "Swapped back to diff chassis" \
    "rosservice call /gazebo/get_model_state '{model_name: \"diff_car\"}' 2>/dev/null | grep -q success"

# ── Phase 9: Stress / Lightweight Check ─────────────────────
echo ""
echo "--- Phase 9: Lightweight Laptop Check ---"
CPU_USAGE=$(top -bn1 | grep "Cpu(s)" | awk '{print $2+$4}')
MEM_USED=$(free -m | awk '/Mem:/ {print $3}')
MEM_TOTAL=$(free -m | awk '/Mem:/ {print $2}')
echo "  CPU Usage: ${CPU_USAGE}%"
echo "  Memory: ${MEM_USED}MB / ${MEM_TOTAL}MB"

if (( $(echo "$CPU_USAGE < 80" | bc -l) )); then
    green "  [PASS] CPU within limits"
else
    red "  [WARN] CPU high (${CPU_USAGE}%). Consider reducing sensor rates."
fi

if (( MEM_USED < MEM_TOTAL * 80 / 100 )); then
    green "  [PASS] Memory within limits"
else
    red "  [WARN] Memory high (${MEM_USED}MB). Close other applications."
fi

# ── Cleanup ─────────────────────────────────────────────────
echo ""
echo "--- Cleanup ---"
kill $SIM_PID 2>/dev/null
sleep 3
make kill 2>/dev/null || true

# ── Summary ─────────────────────────────────────────────────
echo ""
echo "============================================================"
echo "  RESULTS:  $(green "$PASS passed")  |  $(red "$FAIL failed")"
echo "============================================================"
if [ $FAIL -eq 0 ]; then
    green "  ALL TESTS PASSED! The system is ready for development."
else
    red "  $FAIL test(s) failed. Review the output above."
fi
exit $FAIL
```

```bash
chmod +x ~/air_ground_sim_ws/src/e2e_test.sh
```

## 9.2 快速冒烟测试（日常开发用）

**文件：`~/air_ground_sim_ws/src/quick_smoke.sh`**

```bash
#!/bin/bash
# Quick smoke test (30 seconds) — run before committing code.
source ~/air_ground_sim_ws/devel/setup.bash

roslaunch air_ground_sim.launch chassis:=diff gui:=false headless:=true &
PID=$!
sleep 20  # Wait for everything to boot

# Bare minimum checks
echo "--- Quick Smoke ---"
rostopic echo /car/observation -n 1 2>/dev/null | grep -q robot_id && echo "[OK] Car observation" || echo "[FAIL] Car observation"
rostopic echo /drone/heartbeat -n 1 2>/dev/null | grep -q data && echo "[OK] Drone heartbeat" || echo "[FAIL] Drone heartbeat"
rostopic echo /server/world_state -n 1 2>/dev/null | grep -q agents && echo "[OK] WorldState" || echo "[FAIL] WorldState"
rosnode list 2>/dev/null | grep -q coordinator && echo "[OK] Coordinator" || echo "[FAIL] Coordinator"

kill $PID 2>/dev/null
make kill 2>/dev/null
```

```bash
chmod +x ~/air_ground_sim_ws/src/quick_smoke.sh
```

## 9.3 最终检查清单

完成所�?9 �?task 后，逐一确认�?

| # | 检查项 | 命令 |
|---|--------|------|
| 1 | 工作空间编译无错�?| `catkin build` |
| 2 | 自定义消息可�?| `rosmsg show air_ground_interfaces/SensorFusion` |
| 3 | 无人机可启动 | `make launch-drone`（等 15s，`Ctrl+C`�?|
| 4 | 差速小车可启动 | `make launch-car` |
| 5 | 麦轮小车可启�?| `make launch-car-mecanum` |
| 6 | 所有传感器话题有数�?| `make test-sensors` |
| 7 | 通信桥正常转�?| `make test-bridge` |
| 8 | 服务器节点运�?| `make test-server` |
| 9 | 完整系统启动 | `make launch-full` |
| 10 | E2E 测试全 PASS | `bash src/e2e_test.sh` |

## 9.4 已知限制与 TODO

| 限制 | 说明 | 后续计划 |
|------|------|---------|
| 麦轮摩擦近似 | Gazebo 无法原生模拟麦轮辊子，用 `mu2�?` 近似 | 实机上验证即�?|
| 云台为速度控制 | Gazebo 中云台是 velocity-controlled joint | �?PID 位置�?|
| MAVLink 解析不完�?| 当前只解�?heartbeat �?global_position | 按需扩展 pymavlink 解析 |
| �?world 模型 | 使用空白世界，无障碍�?| 后续加障碍物世界测试 SLAM |
| TCP 无加�?| 仿真中明文传输，实机需�?TLS | 实机部署时处�?|

## 交付产物

1. `e2e_test.sh` 可验证整条数据链路（传感器→边缘→服务器→命令→执行�?
2. `quick_smoke.sh` 供日常开发快速验�?
3. 最终检查清单全�?�?
