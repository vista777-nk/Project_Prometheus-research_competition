#!/usr/bin/env python3
"""pytest 夹具：把 scripts/ 放进搜索路径，并在收集测试**之前**装上 ROS 替身。

`test/host/` 下的测试在纯 Python 环境里跑 —— 不需要 ROS、不需要 roscore、
不需要硬件，Windows / macOS / CI 的 ubuntu-latest 都能跑。
它们与上一级目录的 `test_*.py`（需要真 ROS，由 catkin test 跑）刻意分开放，
因为两组测试的运行环境不同，混在一个目录里只会让两边都跑不起来。

替身必须在模块导入期就装好：驱动骨架在 `import` 时就 `import rospy`，
等到夹具执行才装就晚了。
"""

import sys
from pathlib import Path

TEST_DIRECTORY = Path(__file__).resolve().parent
SCRIPT_DIRECTORY = TEST_DIRECTORY.parents[1] / "scripts"
for path in (str(TEST_DIRECTORY), str(SCRIPT_DIRECTORY)):
    if path not in sys.path:
        sys.path.insert(0, path)

import ros_stub  # noqa: E402

ros_stub.install()
