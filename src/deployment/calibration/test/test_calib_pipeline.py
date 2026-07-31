#!/usr/bin/env python3
"""标定流水线的离线测试 —— 合成数据进, 真值出。

    python3 test_calib_pipeline.py

退出码: 0 全通过 / 1 有失败 / 2 缺 numpy 或 OpenCV (报 SKIP, 不算失败)。
退出码 2 的约定与 `src/deployment/mavlink/test-mavlink-signing.py` 一致,
由 `src/deployment/validate.sh` §2 解释。

--------------------------------------------------------------------------
这组测试在证明什么
--------------------------------------------------------------------------

标定脚本最容易出的错不是崩溃, 是**解出一个自洽但错误的结果**: 角点坐标
整体缩放、objp 的行列写反、畸变系数顺序抄错 —— 这些 bug 全都不会让
RMS 变大, 因为 RMS 衡量的是"这组解与这组观测有多自洽", 不是"这组解对不对"。

所以这里用**真值已知**的合成数据 (make_sample_calib_data.py), 断言的是
"解出来的 fx 与设进去的 fx 差多少"。这是 RMS 无论如何都给不出的信息。

反过来, 合成数据证明不了 OpenCV 的畸变模型拟合得了真实镜头 ——
图就是按那个模型画出来的。那一条只能靠实机采一次数据来验。
"""

import importlib.util
import math
import os
import shutil
import sys
import tempfile
import unittest
from typing import Any

TEST_DIRECTORY = os.path.dirname(os.path.abspath(__file__))
CALIBRATION_DIRECTORY = os.path.dirname(TEST_DIRECTORY)
sys.path.insert(0, TEST_DIRECTORY)


def load_script(filename: str, module_name: str) -> Any:
    """按路径加载本目录下用连字符命名的脚本。

    `calibrate-camera.py` 这类名字不是合法的 Python 标识符, `import` 不了。
    连字符命名是 src/deployment/ 下既有脚本的惯例 (check-usb3.sh /
    setup-3dr-radio.py / test-mavlink-signing.py), 为了一个测试去改一批
    文件名不划算; 用 importlib 按路径加载即可。
    """
    path = os.path.join(CALIBRATION_DIRECTORY, filename)
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"加载不了 {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


try:
    import numpy  # noqa: F401
    import cv2  # noqa: F401
    import yaml
except ImportError as error:  # pragma: no cover
    print(f"SKIP: 标定流水线测试需要 numpy + OpenCV + PyYAML ({error})", file=sys.stderr)
    raise SystemExit(2)

import make_sample_calib_data as sample  # noqa: E402

calibrate_camera = load_script("calibrate-camera.py", "calibrate_camera")
calibrate_imu = load_script("calibrate-imu.py", "calibrate_imu")
validate_calibration = load_script("validate-calibration.py", "validate_calibration")
convert_to_kalibr = load_script("convert-bag-to-kalibr.py", "convert_bag_to_kalibr")
cam_imu_extrinsic = load_script("calibrate-cam-imu-extrinsic.py", "cam_imu_extrinsic")
generate_report = load_script("generate-calib-report.py", "generate_calib_report")


#: 容差。括号里是 2026-07-31 在 OpenCV 5.0.0 / numpy 2.5.1 上的实测偏差 ——
#: 容差取实测值的 3~10 倍, 给 CI 上钉的 OpenCV 4.8~4.12 留出版本间差异的余量。
#: 取得再松就失去意义 (fx 差 5% 的标定是废的), 再紧就会因为换个小版本而红。
FOCAL_TOLERANCE_RATIO = 0.01      # 实测 +0.18% (fx) / +0.16% (fy)
PRINCIPAL_TOLERANCE_PX = 3.0      # 实测 0.06 / 0.06
MAX_RMS_PX = 0.30                 # 实测 0.137
K1_TOLERANCE = 0.05               # 实测 0.0043
K2_TOLERANCE = 0.10               # 实测 0.0387
GYRO_BIAS_TOLERANCE = 5.0e-4      # 实测 1.0e-5 / 1.0e-6 / 1.9e-5
ACCEL_BIAS_TOLERANCE = 5.0e-3     # 实测 2.3e-4 / 1.4e-4 / 4.1e-4
NOISE_DENSITY_TOLERANCE_RATIO = 0.10   # 实测 +1.0% (gyro) / -1.2% (accel)

IMU_SECONDS = 120.0

#: 2026-07-31 本地基准 (Windows / Python 3.14 / OpenCV 5.0.0 / numpy 2.5.1)。
#:
#: 存在的理由: 本机装不上 CI 钉的 numpy<2.0.0 (没有 cp314 轮子), 所以上面那些
#: 容差是在**与 CI 不同的一套版本**上测出来的。容差本身留了 3~10 倍余量,
#: 单看绿灯说明不了两边的数是不是真的一致 —— 而"差在容差内但系统性偏移"
#: 恰恰是版本差异会有的样子。
#:
#: 所以让测试把实测值连同基准一起打出来: 第一次 CI 运行的日志里就能直接读到
#: 两套版本的逐项对比, 不必靠推断。差异显著时回来改容差并在 ADR-0011 里记一笔。
LOCAL_BASELINE = {
    "environment": "Python 3.14.6 / OpenCV 5.0.0 / numpy 2.5.1 (Windows)",
    "fx": 520.917403,
    "fy": 519.855720,
    "cx": 321.941470,
    "cy": 237.939336,
    "rms": 0.137087,
    "k1": 0.115673,
    "k2": -0.211268,
    "gyro_noise_density": 2.020377e-04,
    "accel_noise_density": 1.482102e-03,
    "gyro_bias_z": 0.001219,
}


class CalibrationPipelineTest(unittest.TestCase):
    """整条流水线跑一遍: 生成 → 标定 → 校验 → 转换 → 报告。"""

    workdir = ""
    images_dir = ""
    imu_csv = ""
    camera_yaml = ""
    imu_yaml = ""
    #: 实测值, 由 test_01 / test_10 填, tearDownClass 打印
    measured: dict = {}

    @classmethod
    def setUpClass(cls) -> None:
        """生成一次合成数据, 全部用例共用 (渲染 12 张图要几秒)。"""
        cls.workdir = tempfile.mkdtemp(prefix="calib_pipeline_")
        cls.images_dir = os.path.join(cls.workdir, "images")
        sample.generate_images(cls.images_dir)
        cls.imu_csv = os.path.join(cls.workdir, "imu_static.csv")
        sample.generate_imu_log(cls.imu_csv, seconds=IMU_SECONDS)
        cls.camera_yaml = os.path.join(cls.workdir, "camera_intrinsics.yaml")
        cls.imu_yaml = os.path.join(cls.workdir, "imu_intrinsics.yaml")

    @classmethod
    def tearDownClass(cls) -> None:
        """打印实测值对比表, 然后清掉临时目录。"""
        cls.print_measurement_report()
        shutil.rmtree(cls.workdir, ignore_errors=True)

    @classmethod
    def print_measurement_report(cls) -> None:
        """把本次实测值与本地基准并排打出来。

        断言只回答"在不在容差内", 这张表回答"和另一套 OpenCV/numpy 比差多少"。
        后者是版本差异唯一能被看见的地方 —— 系统性偏移完全可以躲在容差里面。
        """
        if not cls.measured:
            return
        print("\n" + "=" * 72)
        print("标定实测值 vs 本地基准")
        print(f"  本次运行 : Python {sys.version.split()[0]} / "
              f"OpenCV {cv2.__version__} / numpy {numpy.__version__}")
        print(f"  本地基准 : {LOCAL_BASELINE['environment']}")
        print("-" * 72)
        print(f"  {'量':<22}{'本次':>16}{'基准':>16}{'相对差':>14}")
        for key, value in cls.measured.items():
            baseline = LOCAL_BASELINE.get(key)
            if baseline is None:
                continue
            if abs(baseline) > 1e-12:
                delta = f"{(value - baseline) / abs(baseline) * 100:+.4f}%"
            else:
                delta = f"{value - baseline:+.3e}"
            print(f"  {key:<22}{value:>16.6f}{baseline:>16.6f}{delta:>14}"
                  if abs(value) >= 1e-3 else
                  f"  {key:<22}{value:>16.6e}{baseline:>16.6e}{delta:>14}")
        print("=" * 72)
        print("  两套版本的差异若超过 0.1%, 回 ADR-0011 §影响 记一笔并复核容差。")
        print("=" * 72)

    def read_yaml(self, path: str) -> dict:
        """读回一份 YAML。"""
        with open(path, "r", encoding="utf-8") as handle:
            return yaml.safe_load(handle)

    # --- 相机 ---------------------------------------------------------------

    def test_01_camera_recovers_ground_truth(self):
        """标定解出来的内参要接近设进去的真值 —— RMS 小不代表解对了。"""
        document, rms = calibrate_camera.run(
            self.images_dir, sample.PATTERN_SIZE, sample.SQUARE_SIZE,
            self.camera_yaml, "synthetic_vga", 10, MAX_RMS_PX,
            "SYNTHETIC", "2026-07-31", strict_camera_info=False, quiet=True,
        )
        k = document["camera_matrix"]["data"]
        fx_true, fy_true = sample.K_TRUE[0, 0], sample.K_TRUE[1, 1]
        cx_true, cy_true = sample.K_TRUE[0, 2], sample.K_TRUE[1, 2]

        self.assertLess(abs(k[0] - fx_true) / fx_true, FOCAL_TOLERANCE_RATIO,
                        f"fx={k[0]:.3f} 与真值 {fx_true} 差得太多")
        self.assertLess(abs(k[4] - fy_true) / fy_true, FOCAL_TOLERANCE_RATIO,
                        f"fy={k[4]:.3f} 与真值 {fy_true} 差得太多")
        self.assertLess(abs(k[2] - cx_true), PRINCIPAL_TOLERANCE_PX)
        self.assertLess(abs(k[5] - cy_true), PRINCIPAL_TOLERANCE_PX)
        self.assertLess(rms, MAX_RMS_PX)

        d = document["distortion_coefficients"]["data"]
        self.assertLess(abs(d[0] - sample.DIST_TRUE[0]), K1_TOLERANCE)
        self.assertLess(abs(d[1] - sample.DIST_TRUE[1]), K2_TOLERANCE)

        type(self).measured.update({
            "fx": k[0], "fy": k[4], "cx": k[2], "cy": k[5],
            "rms": rms, "k1": d[0], "k2": d[1],
        })

    def test_02_all_twelve_views_detected(self):
        """12 个视角应当全部检测到棋盘格 —— 检测率掉下来说明渲染或参数变了。"""
        calibrator = calibrate_camera.CameraCalibrator(sample.PATTERN_SIZE,
                                                       sample.SQUARE_SIZE)
        for path in calibrate_camera.load_images(self.images_dir):
            calibrator.add_image(cv2.imread(path), os.path.basename(path))
        self.assertEqual(len(calibrator.img_points), len(sample.POSES))

    def test_03_camera_yaml_is_plain_yaml(self):
        """输出必须是 PyYAML 读得动的普通 YAML —— 这是 ADR-0011 的核心。

        ROS 的 camera_info_manager 走普通 YAML 解析。只要这条成立,
        标定结果就能直接喂给 ROS; 不成立的话, "标定输出 → camera_info"
        这条接口就是断的。
        """
        document = self.read_yaml(self.camera_yaml)
        for key in validate_calibration.CAMERA_REQUIRED_KEYS:
            self.assertIn(key, document)
        self.assertEqual(document["distortion_model"], "plumb_bob")

    def test_04_opencv_filestorage_stays_incompatible(self):
        """cv2.FileStorage 写的 YAML, PyYAML 读不了 —— ADR-0011 的前提。

        这条是**锁前提**, 不是锁行为: 它证明"不用 FileStorage"这个决定
        今天仍然有理由。哪天 OpenCV 改得 PyYAML 能读了, 这条会红 ——
        那时该做的是回去重读 ADR-0011, 而不是把这个断言删掉。
        """
        path = os.path.join(self.workdir, "filestorage_probe.yaml")
        storage = cv2.FileStorage(path, cv2.FILE_STORAGE_WRITE)
        storage.write("camera_matrix", numpy.eye(3))
        storage.release()
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("opencv-matrix", text)
        with self.assertRaises(yaml.YAMLError):
            yaml.safe_load(text)

    def test_05_camera_validation_passes(self):
        """合成标定结果应当通过全部合理性检查。"""
        checks = validate_calibration.validate_file(self.camera_yaml)
        failed = [name for name, ok, _ in checks.results if not ok]
        self.assertEqual(failed, [], f"未通过: {failed}\n{checks.render()}")

    def test_06_too_few_images_is_rejected(self):
        """图片不够时必须报错, 不能"用 3 张也给你解一个"。"""
        calibrator = calibrate_camera.CameraCalibrator(sample.PATTERN_SIZE,
                                                       sample.SQUARE_SIZE)
        paths = calibrate_camera.load_images(self.images_dir)[:3]
        for path in paths:
            calibrator.add_image(cv2.imread(path), os.path.basename(path))
        with self.assertRaises(ValueError):
            calibrator.calibrate((sample.IMAGE_WIDTH, sample.IMAGE_HEIGHT))

    def test_07_mixed_resolution_is_rejected(self):
        """混着不同分辨率标定出来的 K 没有意义, 而且不会自己报错。"""
        mixed = os.path.join(self.workdir, "mixed")
        os.makedirs(mixed, exist_ok=True)
        paths = calibrate_camera.load_images(self.images_dir)
        for path in paths:
            shutil.copy(path, mixed)
        odd = cv2.resize(cv2.imread(paths[0]), (320, 240))
        cv2.imwrite(os.path.join(mixed, "zz_half.png"), odd)
        with self.assertRaises(ValueError):
            calibrate_camera.run(mixed, sample.PATTERN_SIZE, sample.SQUARE_SIZE,
                                 os.path.join(self.workdir, "mixed.yaml"),
                                 "mixed", 10, MAX_RMS_PX, "x", "2026-07-31",
                                 strict_camera_info=False, quiet=True)

    # --- IMU ----------------------------------------------------------------

    def test_10_imu_recovers_bias_and_noise(self):
        """Allan 解算要能把设进去的零偏和噪声密度解回来。"""
        timestamps, gyro, accel = calibrate_imu.load_csv(self.imu_csv)
        result = calibrate_imu.calibrate(timestamps, gyro, accel,
                                         sample.IMU_RATE_HZ, "z", 1.0)
        result["sensor"] = "synthetic"
        result["calibration_date"] = "2026-07-31"
        with open(self.imu_yaml, "w", encoding="utf-8", newline="\n") as handle:
            yaml.safe_dump(result, handle, default_flow_style=False,
                           allow_unicode=True, sort_keys=False)

        for axis, expected in enumerate(sample.GYRO_BIAS_TRUE):
            self.assertLess(abs(result["gyro_bias"][axis] - expected),
                            GYRO_BIAS_TOLERANCE, f"gyro_bias[{axis}]")
        for axis, expected in enumerate(sample.ACCEL_BIAS_TRUE):
            self.assertLess(abs(result["accel_bias"][axis] - expected),
                            ACCEL_BIAS_TOLERANCE, f"accel_bias[{axis}]")

        for key, truth in (
            ("gyro_noise_density", sample.GYRO_NOISE_DENSITY_TRUE),
            ("accel_noise_density", sample.ACCEL_NOISE_DENSITY_TRUE),
        ):
            self.assertLess(abs(result[key] - truth) / truth,
                            NOISE_DENSITY_TOLERANCE_RATIO,
                            f"{key} = {result[key]:.4e}, 真值 {truth:.4e}")

        type(self).measured.update({
            "gyro_noise_density": result["gyro_noise_density"],
            "accel_noise_density": result["accel_noise_density"],
            "gyro_bias_z": result["gyro_bias"][2],
        })

    def test_11_driver_stddev_conversion(self):
        """写给驱动的离散标准差 = 噪声密度 × √采样率。

        real_sensors.yaml 的 icm42688 段收的是离散标准差。漏掉这一步换算,
        100 Hz 下的协方差会小 100 倍, 滤波器会把 IMU 当基准真值。
        """
        document = self.read_yaml(self.imu_yaml)
        rate = document["sample_rate_hz"]
        for density_key, derived_key in (
            ("gyro_noise_density", "gyro_noise_stddev"),
            ("accel_noise_density", "accel_noise_stddev"),
        ):
            expected = document[density_key] * math.sqrt(rate)
            self.assertAlmostEqual(document["derived_for_driver"][derived_key],
                                   expected, places=12)

    def test_12_short_log_flags_random_walk_unreliable(self):
        """两分钟的数据解不出随机游走 —— 脚本必须自曝, 而不是给个数就完事。

        反向验证: 这一项**必须**判失败。如果哪天它通过了, 说明可信度判定
        失效了, 而失效的表现是一个凭空外推出来的随机游走系数被
        robot_localization 原样采信。
        """
        document = self.read_yaml(self.imu_yaml)
        self.assertFalse(document["quality"]["random_walk_reliable"])
        self.assertEqual(document["quality"]["status"], "NEED_LONGER_LOG")

        checks = validate_calibration.validate_file(self.imu_yaml)
        failed = [name for name, ok, _ in checks.results if not ok]
        self.assertEqual(
            failed, ["random_walk 由足够长的静置数据解出"],
            f"期望只有随机游走一项失败, 实际: {failed}\n{checks.render()}",
        )

    # --- 转换与外参 ----------------------------------------------------------

    def test_20_kalibr_conversion_drops_k3(self):
        """转 Kalibr 时 radtan 只有 4 个系数, k3 必须被丢掉而不是硬塞进去。"""
        camera = self.read_yaml(self.camera_yaml)
        camchain = convert_to_kalibr.to_kalibr_camchain(camera, "/car/openmv/image_raw")
        cam0 = camchain["cam0"]
        self.assertEqual(len(cam0["distortion_coeffs"]), 4)
        self.assertEqual(cam0["distortion_model"], "radtan")
        # Kalibr 的 intrinsics 顺序是 [fu, fv, pu, pv], 与 K 展平不同
        k = camera["camera_matrix"]["data"]
        self.assertEqual(cam0["intrinsics"], [k[0], k[4], k[2], k[5]])
        self.assertEqual(cam0["resolution"],
                         [camera["image_width"], camera["image_height"]])

    def test_21_kalibr_imu_fields_map_one_to_one(self):
        """IMU 输出字段与 Kalibr 的四个噪声参数一一对应 (ADR-0011 的设计目标)。"""
        imu = self.read_yaml(self.imu_yaml)
        converted = convert_to_kalibr.to_kalibr_imu(imu, "/car/imu/data")
        self.assertEqual(converted["gyroscope_noise_density"], imu["gyro_noise_density"])
        self.assertEqual(converted["accelerometer_noise_density"], imu["accel_noise_density"])
        self.assertEqual(converted["gyroscope_random_walk"], imu["gyro_random_walk"])
        self.assertEqual(converted["accelerometer_random_walk"], imu["accel_random_walk"])
        self.assertEqual(converted["update_rate"], imu["sample_rate_hz"])

    def test_22_extrinsic_template_fails_validation(self):
        """外参模板里的单位阵是占位值, 必须判失败。

        单位阵是完全合法的 SE(3), 所有几何检查都会通过 —— 只有 status
        能区分"还没标定"和"外参恰好为零"。这条就是在守那个字段。
        """
        path = os.path.join(self.workdir, "cam_imu_extrinsic.yaml")
        cam_imu_extrinsic.write_template(path)
        checks = validate_calibration.validate_file(path)
        failed = [name for name, ok, _ in checks.results if not ok]
        self.assertEqual(failed, ["status 已填写 (非 PLACEHOLDER)"],
                         f"实际失败项: {failed}\n{checks.render()}")

    def test_23_extrinsic_rejects_mirror_rotation(self):
        """行列式为 -1 的"旋转"是镜像, 必须被拒。

        它的来源通常是某一轴符号在转格式时抄反了, 症状是走直线正常、
        一转弯就发散 —— 靠跑一遍很难定位, 靠这一行断言很容易。
        """
        document = dict(cam_imu_extrinsic.TEMPLATE)
        document["status"] = "CALIBRATED"
        document["T_cam_imu"] = {
            "rows": 4, "cols": 4,
            "data": [1.0, 0.0, 0.0, 0.02,
                     0.0, -1.0, 0.0, 0.01,      # y 轴符号反了 → det = -1
                     0.0, 0.0, 1.0, 0.03,
                     0.0, 0.0, 0.0, 1.0],
        }
        checks = validate_calibration.validate_extrinsic(document)
        failed = [name for name, ok, _ in checks.results if not ok]
        self.assertIn("旋转块行列式为 +1 (不是镜像)", failed)

    # --- 报告 ---------------------------------------------------------------

    def test_30_report_contains_required_sections(self):
        """报告要包含任务书 §优化建议 4 列的固定章节。"""
        report = generate_report.build_report([self.camera_yaml, self.imu_yaml],
                                              "流水线测试报告")
        for section in ("K Matrix", "畸变系数", "重投影误差", "Allan 偏差曲线",
                        "零偏与噪声", "填进驱动的派生值", "合理性检查"):
            self.assertIn(section, report, f"报告缺少「{section}」一节")
        # IMU 那份是 NEED_LONGER_LOG, 所以整体结论必须是 NEED_RECALIBRATE
        self.assertIn("**结论: NEED_RECALIBRATE**", report)

    def test_31_report_thresholds_come_from_validator(self):
        """报告里的阈值必须取自 validate-calibration.py, 不能抄第二份。

        断言的是报告正文里出现的那个数**就是** validator 里的常量:
        改了 validator 的阈值而忘了改报告, 这条会红。
        """
        report = generate_report.build_report([self.camera_yaml], "x")
        self.assertIn(f"上限 {validate_calibration.MAX_RMS_PX} px", report)


if __name__ == "__main__":
    unittest.main(verbosity=2)
