# calibration_db/ —— 标定历史归档

> **Phase 1 只有归档规范和目录结构，没有数据库。**
> 完整的历史数据库（跨次对比、漂移曲线、自动回归）是 Phase 2 的事。

---

## 为什么不覆盖

标定结果是**有日期的测量值**，不是配置。同一台车三个月后重标一次，
两次的 `fx` 差 3%，这件事本身就是信息：

- 差得多 → 镜头被撞过 / 螺丝松了 / 焦环被人拧过；
- 差得少但方向一致 → 温度或老化引起的系统漂移；
- 完全不差 → 大概率是有人把上次的文件复制过来了。

覆盖掉旧结果，上面三种情况全都变成"看不出来"。所以这里只增不改。

---

## 目录规范

```
calibration_db/
├── README.md                       # 本文件
└── <YYYY-MM-DD>_<robot>[_<后缀>]/  # 一次标定 = 一个目录
    ├── REPORT.md                   # generate-calib-report.py 产出
    ├── camera_intrinsics.yaml
    ├── imu_intrinsics.yaml
    ├── cam_imu_extrinsic.yaml      # 有则放
    └── NOTES.md                    # 手写: 当时的环境、异常、为什么重标
```

例：`2026-08-01_car/`、`2026-08-01_drone/`、`2026-09-15_car_after-lens-swap/`。

**`robot` 只能是 `car` 或 `drone`**，与 `ICD.md` 的 `robot_id` 一致。
同一天标两次就在后缀里说明原因，不要用 `_v2`——三个月后没人知道 v2 是什么。

---

## 生效的那一份放哪

归档目录是历史，**不是运行时读取的路径**。当前生效的标定值走这两处：

| 结果 | 落到哪 | 怎么落 |
|------|-------|-------|
| 相机内参 | `camera_info` 话题 / `camera_info_manager` 的 URL | 部署时把 YAML 拷到设备上并在 launch 里指向它 |
| IMU 噪声 | `src/air_ground_car_bringup/config/real_sensors.yaml` 的 `icm42688` 段 | 抄 `imu_intrinsics.yaml` 的 `derived_for_driver` 段（**注意是离散标准差，不是密度**） |

改 `real_sensors.yaml` 时，在 commit message 里写清楚取自哪个归档目录。
这是目前唯一把"运行时的值"和"它的出处"连起来的东西 —— Phase 2 会做成自动的。

---

## .gitignore

标定 YAML 和报告**要入库**（它们是文本、体积小、是实验记录的一部分）。
原始 bag 和抽出来的图片**不入库**（几百 MB 起步），仓库根 `.gitignore`
已经拦了 `*.bag`。需要保留原始数据时另找存储，并在 `NOTES.md` 里写明位置。

---

## Phase 2 会补什么

- 跨次对比脚本：`compare-calibration.py old/ new/` → 逐参数差异 + 是否超出重标阈值
- 漂移曲线：同一台设备的 `fx` / `gyro_bias` 随时间的变化图
- 回归门禁：新标定与上一次差异超过阈值时要求人工确认
- 自动关联：`real_sensors.yaml` 里带上它取自哪个归档目录的引用

见 `project-prometheus-tasks/task-15-calibration-validation.md` §未来演进。
