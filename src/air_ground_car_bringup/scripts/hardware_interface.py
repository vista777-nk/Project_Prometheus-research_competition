#!/usr/bin/env python3
"""硬件抽象层：隔离传感器驱动的 ROS 逻辑与物理硬件访问。

本模块定义 Layer 1（物理传感器）↔ Layer 2（协议翻译）之间唯一的边界。
驱动只依赖这里的抽象基类，不 import 任何 `serial` / `smbus`。

后果是：换一个 LiDAR 型号或 UART 接法，
`car_preprocessor.py` 与全部单元测试都不需要改动。

后端选择由参数 `~backend` 决定：

    mock — 假数据后端，任意 OS 可跑，CI 与无硬件冒烟测试用
    real — UART / I2C 使用 Linux 实机后端

`real` 刻意不提供"能跑但是空转"的占位实现。一个 open() 永远返回 True、
read() 永远返回空的假后端，会让实机上的驱动安静地发布全零数据 ——
那是本仓库反复记录过的那类假绿灯 (ADR-0008)。UART / I2C 要么访问真实
Linux 设备，要么明确失败。HC-SR04 已按 ADR-0018 下沉到底盘 MCU。

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


_BACKENDS = ("mock", "real")


def create_uart(backend: str) -> UARTInterface:
    """按后端名构造 UART 实例。"""
    if backend == "mock":
        from mock_hardware import MockUART
        return MockUART()
    if backend == "real":
        from real_hardware import RealUART
        return RealUART()
    raise ValueError(f"unknown backend '{backend}', expected one of {_BACKENDS}")


def create_i2c(backend: str) -> I2CInterface:
    """按后端名构造 I2C 实例。"""
    if backend == "mock":
        from mock_hardware import MockI2C
        return MockI2C()
    if backend == "real":
        from real_hardware import RealI2C
        return RealI2C()
    raise ValueError(f"unknown backend '{backend}', expected one of {_BACKENDS}")
