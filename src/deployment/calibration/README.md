# 标定工具链

> IMU / 相机 / 相机-IMU 外参的离线标定与校验。
> 全部脚本**不依赖实时传感器** —— 输入是图片目录、CSV 或 rosbag，任何 OS 都能跑解算部分。

---

## 1. 一分钟版

```bash
# 车机相机内参 (实机上采, 任意机器上解)
ROBOT=car ./record-calib-bag.sh camera                    # 采集 (树莓派上)
python3 convert-bag-to-kalibr.py --bag ~/calib_data/*/camera_calib_car.bag \
    --extract-images out/images --image-topic /car/openmv/image_raw
python3 calibrate-camera.py --input out/images --pattern 9x6 --square-size 0.030 \
    --camera-name car_openmv --output camera_intrinsics.yaml
python3 validate-calibration.py camera_intrinsics.yaml

# IMU 内参
ROBOT=car ./record-calib-bag.sh imu                       # 默认 2 小时, 见 §4
python3 convert-bag-to-kalibr.py --bag ~/calib_data/*/imu_calib_car.bag \
    --imu-csv out/imu_static.csv --imu-topic /car/imu/data
python3 calibrate-imu.py --input out/imu_static.csv --rate 100 \
    --sensor icm42688 --output imu_intrinsics.yaml
python3 validate-calibration.py imu_intrinsics.yaml

# 报告 + 归档
python3 generate-calib-report.py camera_intrinsics.yaml imu_intrinsics.yaml \
    -o calibration_db/2026-08-01_car/REPORT.md
```

依赖：`numpy` · `opencv-python-headless` · `PyYAML`（版本约束见仓库根 `requirements.txt`）。
`validate-calibration.py` 只要 `PyYAML` —— 它必须能在没装 OpenCV 的机器上跑。

---

## 2. 文件

| 文件 | 需要 ROS | 作用 |
|------|:---:|------|
| `record-calib-bag.sh` | ✔ | 采集 bag。录之前先查话题是不是真的在发 |
| `convert-bag-to-kalibr.py` | 部分 | 本项目 YAML → Kalibr；bag → 图片 / CSV（后两者要 ROS） |
| `calibrate-camera.py` | ✘ | 棋盘格内参，输出 ROS `camera_info` 格式 YAML |
| `calibrate-imu.py` | ✘ | Allan 方差 → 零偏 / 噪声密度 / 随机游走 |
| `calibrate-cam-imu-extrinsic.py` | 部分 | Phase 1 只有接口 + 格式 + 采集检查，解算交给 Kalibr |
| `validate-calibration.py` | ✘ | 合理性检查，三种结果通吃 |
| `generate-calib-report.py` | ✘ | YAML → Markdown 报告 |
| `test/make_sample_calib_data.py` | ✘ | 合成样本（真值已知） |
| `test/test_calib_pipeline.py` | ✘ | CI 流水线测试，16 个用例 |
| `calibration_db/` | — | 标定历史归档，见该目录 README |

---

## 3. 输出格式

三份 YAML 的格式决定见 [ADR-0011](../../../docs/decisions/ADR-0011.md)。两条要点：

**相机**：直接写 ROS `camera_info` 的 YAML 布局，用 PyYAML 写，**不用 `cv2.FileStorage`**。
FileStorage 写出来的东西 PyYAML 读不了（`%YAML 1.2` 指令 + `!!opencv-matrix` 标签，
2026-07-31 实测 OpenCV 5.0.0 抛 `ConstructorError`），而 ROS 的 `camera_info_manager`
走的正是普通 YAML 解析。

**IMU**：输出连续时间的**噪声密度**（`rad/s/√Hz`），而 `config/real_sensors.yaml` 的
`icm42688` 段收的是离散**标准差**。两者差一个 `√采样率`：

```
sigma_discrete = noise_density × sqrt(sample_rate_hz)
```

`imu_intrinsics.yaml` 的 `derived_for_driver` 段已经把换算做好了，照抄即可。
**漏掉这一步，100 Hz 下填进去的协方差会小 100 倍**，滤波器会把 IMU 当成基准真值。

---

## 4. 为什么 IMU 要静置两小时

Allan 偏差曲线上两段各给一个量：

```
σ(τ) = N/√τ         τ 小 —— 白噪声，斜率 -1/2，N = 噪声密度
σ(τ) = K·√(τ/3)     τ 大 —— 随机游走，斜率 +1/2，K = 随机游走系数
```

交点在 `τ_cross = √3·N/K`。本项目 IMU 的量级（N ≈ 2e-4 rad/s/√Hz，
K ≈ 2e-6 rad/s²/√Hz）代进去约 **173 秒**；要在 +1/2 段上取到有统计意义的点，
τ 得到交点的几倍，而重叠式 Allan 方差在每个 τ 上还要留够聚类数（经验值：总时长 ≥ 10τ）。

于是总时长落在小时量级。这不是仪式，是这条曲线的交点位置决定的。

**15 分钟能解出什么**：噪声密度和零偏 —— 这两个白噪声段就够了，
足以让 `robot_localization` 起步。**解不出什么**：随机游走。
`calibrate-imu.py` 会把 `quality.random_walk_reliable` 置 `false`、退出码 1，
`validate-calibration.py` 会判失败。只想要噪声密度时加 `--allow-short-log`。

---

## 5. 上机核实清单

以下几条**在本机核实不了**，第一次上实机时必须逐条过一遍，结论写进 `Research_Diary.md`：

| # | 要核实的事 | 怎么核实 | 没核实的后果 |
|---|-----------|---------|-------------|
| 1 | `camera_info_manager` 是否接受 `air_ground_calibration` 这个额外段 | `rosrun camera_info_manager …` 加载一次，或起相机节点看有没有 warning | 若被拒，用 `--strict-camera-info` 重新生成 |
| 2 | 棋盘格实际方格边长 | 游标卡尺量 10 格取平均，除以 10 | 尺寸差 1%，外参平移就差 1%；内参 K 不受影响 |
| 3 | 标定结果的绝对精度 | 用标定好的相机测一段已知长度（如 1.000 m 直尺），比对 | RMS 小只说明自洽，不说明正确 |
| 4 | IMU 静置面的水平度 | 水平仪；或换 3 个朝向各采一段看零偏是否一致 | 倾斜 0.5° → 0.086 m/s² 假零偏，与真零偏同量级 |
| 5 | OpenMV 图像话题是否真的存在 | `rostopic hz /car/openmv/image_raw` | Phase 1 的 `openmv_bridge.py` 只发 `detections`，**图像话题可能没有发布者** |
| 6 | （承 ADR-0010）PX4 是否支持 MAVLink 签名、参数名 | `nsh> param show MAV_*` | 见 ADR-0010 §影响 |
| 7 | （承 ADR-0010）MAVROS 的签名参数入口 | `rosparam list \| grep -i sign` | 同上 |

> 第 5 条要特别注意：`record-calib-bag.sh` 会在录制前检查话题是否在线并拒绝开录，
> 所以它不会静默录出空 bag —— 但你得先把发图像的节点起起来。

---

## 6. 测试

```bash
# 完整流水线 (生成合成数据 → 标定 → 校验 → 转换 → 报告)
python3 test/test_calib_pipeline.py

# 话题名与仓库配置的交叉引用 (只要 PyYAML)
python3 ../validate_consistency.py
```

`test_calib_pipeline.py` 用**真值已知**的合成数据，断言的是"解出来的 fx 与设进去的
fx 差多少"—— 这是 RMS 给不出的信息。2026-07-31 实测（OpenCV 5.0.0 / numpy 2.5.1）：

| 量 | 真值 | 解出 | 偏差 |
|------|------|------|------|
| `fx` | 520.0 | 520.917 | +0.18% |
| `fy` | 519.0 | 519.856 | +0.16% |
| `cx` | 322.0 | 321.941 | −0.06 px |
| `cy` | 238.0 | 237.939 | −0.06 px |
| RMS | — | 0.137 px | — |
| `gyro_bias` | (2.1, −3.5, 1.2)e−3 | (2.110, −3.499, 1.219)e−3 | < 2e−5 |
| `gyro_noise_density` | 2.0e−4 | 2.020e−4 | +1.0% |
| `accel_noise_density` | 1.5e−3 | 1.482e−3 | −1.2% |

合成数据**证明不了**的事：OpenCV 的 `plumb_bob` 模型拟合得了真实镜头
（图就是按那个模型画出来的），以及运动模糊 / 卷帘快门 / 光照不均下的检测率。
那些只能靠 §5 的上机核实。

---

## 7. 参考

- [ADR-0011](../../../docs/decisions/ADR-0011.md) —— 标定输出格式与 Phase 1 边界
- [Kalibr Wiki](https://github.com/ethz-asl/kalibr/wiki) —— 相机-IMU 标定
- [OpenCV Camera Calibration](https://docs.opencv.org/4.x/dc/dbb/tutorial_py_calibration.html)
- `project-prometheus-tasks/task-15-calibration-validation.md` §与原方案的偏差
