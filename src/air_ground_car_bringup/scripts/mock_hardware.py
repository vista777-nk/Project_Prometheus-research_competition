#!/usr/bin/env python3
"""Mock 硬件后端：可控假数据，任意 OS 可跑。

用途有两个：

1. CI 单元测试 —— 测试自己往 Mock 里塞字节，断言驱动解析出的消息；
2. 无硬件冒烟 —— `car_edge_real.launch backend:=mock` 验证节点能起、
   参数能读、话题能建、断流重连能走到，**不验证任何数据正确性**。

刻意保持"设备无关"：Mock 不知道 ICM42688 的 WHO_AM_I 是 0x47，
所以 mock 模式下驱动会照常报芯片 ID 不符的告警 —— 那是对的，
mock 后端本来就没有芯片。让 Mock 假装成某个具体型号会让测试失去意义。
"""

from typing import Dict, Optional

from hardware_interface import GPIOInterface, I2CInterface, UARTInterface


class MockUART(UARTInterface):
    """假串口。read() 从预置缓冲区里取，write() 全部记录下来。"""

    def __init__(self, fake_data: bytes = b"", loop: bool = False) -> None:
        """初始化。

        Args:
            fake_data: 预置的待读字节流。
            loop: 缓冲区读空后是否从头循环（模拟持续输出的传感器）。
        """
        self._source = bytes(fake_data)
        self._buffer = bytes(fake_data)
        self._loop = loop
        self.written = b""
        self.is_open = False
        self.open_calls = 0

    def feed(self, data: bytes) -> None:
        """在测试中途追加待读数据。"""
        self._buffer += bytes(data)

    def open(self, port: str, baudrate: int) -> bool:
        self.open_calls += 1
        self.is_open = True
        return True

    def read(self, n: int, timeout_ms: float = 100.0) -> bytes:
        if not self._buffer and self._loop and self._source:
            self._buffer = self._source
        data = self._buffer[:n]
        self._buffer = self._buffer[n:]
        return data

    def write(self, data: bytes) -> int:
        self.written += bytes(data)
        return len(data)

    def close(self) -> None:
        self.is_open = False


class MockI2C(I2CInterface):
    """假 I2C。寄存器按**字节**存放，连续读会跨寄存器地址自增。

    地址自增是真实 I2C 传感器的行为（ICM42688 一次 burst 读 12 字节
    覆盖 0x1F..0x2A）。如果 Mock 把 `read_register(reg, 12)` 当成
    "取 reg 这一个键的值"，驱动里的 burst 解析逻辑就永远测不到。
    """

    def __init__(self, registers: Optional[Dict[int, int]] = None) -> None:
        """初始化。

        Args:
            registers: 初始寄存器镜像 {地址: 字节值}，缺省全 0。
        """
        self.registers: Dict[int, int] = dict(registers or {})
        self.is_open = False
        self.writes = []

    def open(self, bus: int, address: int) -> bool:
        self.is_open = True
        return True

    def read_register(self, reg: int, length: int) -> bytes:
        return bytes(self.registers.get(reg + i, 0) & 0xFF for i in range(length))

    def write_register(self, reg: int, data: bytes) -> None:
        self.writes.append((reg, bytes(data)))
        for offset, value in enumerate(bytes(data)):
            self.registers[reg + offset] = value

    def close(self) -> None:
        self.is_open = False


class MockGPIO(GPIOInterface):
    """假 GPIO。回波宽度由 echo_us 按 echo 引脚预置，缺省为超时 (-1.0)。"""

    def __init__(self, echo_us: Optional[Dict[int, float]] = None) -> None:
        """初始化。

        Args:
            echo_us: {echo 引脚: 回波宽度 μs}。未列出的引脚返回 -1.0（超时）。
        """
        self.echo_us: Dict[int, float] = dict(echo_us or {})
        self.directions: Dict[int, str] = {}
        self.levels: Dict[int, int] = {}
        self.triggers = []

    def setup(self, pin: int, direction: str) -> None:
        self.directions[pin] = direction

    def write(self, pin: int, value: int) -> None:
        self.levels[pin] = int(value)

    def read(self, pin: int) -> int:
        return self.levels.get(pin, 0)

    def trigger_and_measure(
        self,
        trig_pin: int,
        echo_pin: int,
        pulse_us: float,
        timeout_us: float,
    ) -> float:
        self.triggers.append((trig_pin, echo_pin, pulse_us, timeout_us))
        width = self.echo_us.get(echo_pin, -1.0)
        # 回波比超时窗还长 = 实机上根本等不到下降沿，与"没有回波"同样处理
        if width < 0.0 or width > timeout_us:
            return -1.0
        return width
