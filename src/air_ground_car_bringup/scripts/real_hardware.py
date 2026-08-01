#!/usr/bin/env python3
"""Linux 实机 UART / I2C 访问层。

这里只实现操作系统访问原语；RPLIDAR 协议、OpenMV 分帧与 ICM42688 寄存器语义
仍由各自驱动负责。HC-SR04 的微秒级 GPIO 时序尚待实机测量与 ADR-0013，不能在
这里用 ``sleep`` 猜一个实现。
"""

from typing import Any, Optional

from hardware_interface import HardwareError, I2CInterface, UARTInterface


def _load_serial_module():
    """加载 pyserial，缺失时给出镜像侧可执行的修复提示。"""
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError(
            "real UART 后端需要 pyserial；镜像中应安装 pyserial>=3.5,<4.0.0"
        ) from exc
    return serial


def _load_smbus_module():
    """优先加载发行版的 smbus，兼容开发环境中的 smbus2。"""
    try:
        import smbus
        return smbus
    except ImportError:
        try:
            import smbus2
            return smbus2
        except ImportError as exc:
            raise RuntimeError(
                "real I2C 后端需要 python3-smbus（或开发环境 smbus2）"
            ) from exc


class RealUART(UARTInterface):
    """基于 pyserial 的 UART 实现。"""

    def __init__(self, serial_module=None) -> None:
        """保存可替换的 pyserial 模块；测试只替环境，不替被测类。"""
        self.serial_module = serial_module or _load_serial_module()
        self.device: Optional[Any] = None

    def open(self, port: str, baudrate: int) -> bool:
        """打开串口；设备缺失、权限错误或占用时返回 False。"""
        self.close()
        serial_error = getattr(self.serial_module, "SerialException", OSError)
        try:
            self.device = self.serial_module.Serial(
                port=port,
                baudrate=int(baudrate),
                timeout=0.0,
                write_timeout=1.0,
            )
        except (OSError, serial_error):
            self.device = None
            return False
        if not getattr(self.device, "is_open", True):
            self.close()
            return False
        return True

    def _require_open(self):
        if self.device is None or not getattr(self.device, "is_open", True):
            raise HardwareError("UART 尚未打开")
        return self.device

    def read(self, n: int, timeout_ms: float) -> bytes:
        """按本次调用的毫秒超时读取，避免把不同驱动的超时粘在对象上。"""
        if n < 0 or timeout_ms < 0.0:
            raise ValueError("UART read length/timeout must be non-negative")
        device = self._require_open()
        device.timeout = float(timeout_ms) / 1000.0
        try:
            return bytes(device.read(int(n)))
        except (OSError, getattr(self.serial_module, "SerialException", OSError)) as exc:
            raise HardwareError(f"UART 读取失败: {exc}") from exc

    def write(self, data: bytes) -> int:
        """完整交给 pyserial 写入并返回实际字节数。"""
        device = self._require_open()
        try:
            return int(device.write(bytes(data)))
        except (OSError, getattr(self.serial_module, "SerialException", OSError)) as exc:
            raise HardwareError(f"UART 写入失败: {exc}") from exc

    def close(self) -> None:
        """幂等关闭；关闭异常不掩盖调用方原始故障。"""
        device, self.device = self.device, None
        if device is None:
            return
        try:
            device.close()
        except (OSError, getattr(self.serial_module, "SerialException", OSError)):
            pass


class RealI2C(I2CInterface):
    """基于 Linux SMBus 的 I2C 实现。"""

    def __init__(self, smbus_module=None) -> None:
        """保存可替换的 smbus 模块，实际总线到 open() 才打开。"""
        self.smbus_module = smbus_module or _load_smbus_module()
        self.bus: Optional[Any] = None
        self.address: Optional[int] = None

    def open(self, bus: int, address: int) -> bool:
        """打开总线并保存 7-bit 从机地址；失败返回 False。"""
        self.close()
        address = int(address)
        if not 0x03 <= address <= 0x77:
            raise ValueError(f"I2C address out of 7-bit range: 0x{address:X}")
        try:
            self.bus = self.smbus_module.SMBus(int(bus))
        except (OSError, IOError):
            self.bus = None
            self.address = None
            return False
        self.address = address
        return True

    def _require_open(self):
        if self.bus is None or self.address is None:
            raise HardwareError("I2C 尚未打开")
        return self.bus, self.address

    def read_register(self, reg: int, length: int) -> bytes:
        """从一个 8-bit 寄存器地址连续读取。"""
        if not 0 <= int(reg) <= 0xFF or int(length) <= 0:
            raise ValueError("I2C register must be 0..255 and length must be positive")
        bus, address = self._require_open()
        try:
            values = bus.read_i2c_block_data(address, int(reg), int(length))
        except (OSError, IOError) as exc:
            raise HardwareError(f"I2C 读取失败: {exc}") from exc
        return bytes(values)

    def write_register(self, reg: int, data: bytes) -> None:
        """向一个 8-bit 寄存器地址写一个或多个字节。"""
        payload = bytes(data)
        if not 0 <= int(reg) <= 0xFF or not payload:
            raise ValueError("I2C register must be 0..255 and data must not be empty")
        bus, address = self._require_open()
        try:
            if len(payload) == 1:
                bus.write_byte_data(address, int(reg), payload[0])
            else:
                bus.write_i2c_block_data(address, int(reg), list(payload))
        except (OSError, IOError) as exc:
            raise HardwareError(f"I2C 写入失败: {exc}") from exc

    def close(self) -> None:
        """幂等关闭 SMBus 文件描述符。"""
        bus, self.bus, self.address = self.bus, None, None
        if bus is None:
            return
        try:
            bus.close()
        except (OSError, IOError):
            pass
