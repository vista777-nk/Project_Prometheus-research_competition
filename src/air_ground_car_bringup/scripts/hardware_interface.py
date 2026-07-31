#!/usr/bin/env python3
"""硬件抽象层：隔离传感器驱动的 ROS 逻辑与物理硬件访问。

本模块定义 Layer 1（物理传感器）↔ Layer 2（协议翻译）之间唯一的边界。
驱动骨架只依赖这里的抽象基类，不 import 任何 `serial` / `smbus` / `pigpio`。

后果是：换一个 LiDAR 型号、把超声波从 GPIO 挪到 MCU 上，
`car_preprocessor.py` 与全部单元测试都不需要改动。

后端选择由参数 `~backend` 决定：

    mock — 假数据后端，任意 OS 可跑，CI 与无硬件冒烟测试用
    real — 真实硬件后端，**Phase 1 未实现**，调用时立刻抛异常

`real` 刻意不提供"能跑但是空转"的实现。一个 open() 永远返回 True、
read() 永远返回空的假后端，会让实机上的驱动安静地发布全零数据 ——
那是本仓库反复记录过的那类假绿灯 (ADR-0008)。要么真的读到硬件，
要么在启动时就大声失败。

设计理由详见 ADR-0009。
"""

from abc import ABC, abstractmethod


class HardwareError(RuntimeError):
    """硬件访问失败。驱动捕获它并进入重连退避，不让节点崩掉。"""


class UARTInterface(ABC):
    """通用 UART 接口 (RPLIDAR / OpenMV 共用)。"""

    @abstractmethod
    def open(self, port: str, baudrate: int) -> bool:
        """打开串口，返回是否成功。不抛异常，失败由调用方决定退避策略。"""

    @abstractmethod
    def read(self, n: int, timeout_ms: float) -> bytes:
        """读取 n 字节；超时返回已读到的部分（可能为空）。"""

    @abstractmethod
    def write(self, data: bytes) -> int:
        """写入数据，返回实际写入字节数。"""

    @abstractmethod
    def close(self) -> None:
        """关闭串口。允许在未打开时调用。"""


class I2CInterface(ABC):
    """通用 I2C 接口 (ICM42688)。"""

    @abstractmethod
    def open(self, bus: int, address: int) -> bool:
        """打开 I2C 总线并锁定从机地址，返回是否成功。"""

    @abstractmethod
    def read_register(self, reg: int, length: int) -> bytes:
        """从寄存器 reg 连续读 length 字节。"""

    @abstractmethod
    def write_register(self, reg: int, data: bytes) -> None:
        """向寄存器 reg 写入 data。"""

    @abstractmethod
    def close(self) -> None:
        """关闭总线。允许在未打开时调用。"""


class GPIOInterface(ABC):
    """通用 GPIO 接口 (HC-SR04)。

    注意这里**没有** `pulse_in` + `sleep` 的组合。

    HC-SR04 需要一个 10μs 的触发脉冲，而 Linux 非实时内核上
    `rospy.sleep(0.00001)` 的实际精度是毫秒级（~1-10ms，差三个数量级）。
    把 write/sleep/pulse_in 三个原语暴露给 Python 层，等于邀请上层写出
    一段"看起来在做微秒时序、实际做不到"的代码 —— 它在 Mock 下测试全过，
    在实机上量出的距离全是噪声。

    因此整个 μs 级时序被压进 `trigger_and_measure()` 这一个方法里：
    实现它的人必须真的解决时序问题（pigpio 硬件定时 / 交给 MCU /
    换 UART 输出的模块，三条路见 ADR-0009 §待决），无法用 Python sleep 糊过去。
    """

    @abstractmethod
    def setup(self, pin: int, direction: str) -> None:
        """配置引脚方向。direction: 'in' | 'out'。"""

    @abstractmethod
    def write(self, pin: int, value: int) -> None:
        """写数字电平 (0 | 1)。"""

    @abstractmethod
    def read(self, pin: int) -> int:
        """读数字电平 (0 | 1)。"""

    @abstractmethod
    def trigger_and_measure(
        self,
        trig_pin: int,
        echo_pin: int,
        pulse_us: float,
        timeout_us: float,
    ) -> float:
        """发一次触发脉冲并测回波宽度，返回微秒；超时返回 -1.0。

        Args:
            trig_pin: Trig 引脚编号。
            echo_pin: Echo 引脚编号。
            pulse_us: 触发脉冲宽度 (HC-SR04 要求 ≥10μs)。
            timeout_us: 等待回波的超时时间。

        Returns:
            回波高电平持续时间 (μs)；超时或无回波返回 -1.0。
        """


_BACKENDS = ("mock", "real")


def _reject_real(kind: str):
    """构造 real 后端时统一的失败路径。"""
    raise NotImplementedError(
        f"{kind} 的 real 后端在 Phase 1 未实现 —— "
        "骨架只交付接口层与数据处理层，硬件访问层留给实机阶段填。"
        "见 ADR-0009。当前请用 backend:=mock 启动。"
    )


def create_uart(backend: str) -> UARTInterface:
    """按后端名构造 UART 实例。"""
    if backend == "mock":
        from mock_hardware import MockUART
        return MockUART()
    if backend == "real":
        _reject_real("UART")
    raise ValueError(f"unknown backend '{backend}', expected one of {_BACKENDS}")


def create_i2c(backend: str) -> I2CInterface:
    """按后端名构造 I2C 实例。"""
    if backend == "mock":
        from mock_hardware import MockI2C
        return MockI2C()
    if backend == "real":
        _reject_real("I2C")
    raise ValueError(f"unknown backend '{backend}', expected one of {_BACKENDS}")


def create_gpio(backend: str) -> GPIOInterface:
    """按后端名构造 GPIO 实例。"""
    if backend == "mock":
        from mock_hardware import MockGPIO
        return MockGPIO()
    if backend == "real":
        _reject_real("GPIO")
    raise ValueError(f"unknown backend '{backend}', expected one of {_BACKENDS}")
