"""Linux UART/I2C 实机适配器测试。

替身只模拟 pyserial/smbus 环境；被测对象是生产 ``RealUART`` / ``RealI2C``。
"""

import pytest

from hardware_interface import HardwareError
from real_hardware import RealI2C, RealUART


class FakeSerialError(Exception):
    pass


class FakeSerialPort:
    def __init__(self, **kwargs):
        self.kwargs = kwargs
        self.timeout = kwargs["timeout"]
        self.is_open = True
        self.read_data = b"abc"
        self.written = []

    def read(self, n):
        return self.read_data[:n]

    def write(self, data):
        self.written.append(data)
        return len(data)

    def close(self):
        self.is_open = False


class FakeSerialModule:
    SerialException = FakeSerialError

    def __init__(self, fail=False, closed=False):
        self.fail = fail
        self.closed = closed
        self.port = None

    def Serial(self, **kwargs):
        if self.fail:
            raise FakeSerialError("busy")
        self.port = FakeSerialPort(**kwargs)
        self.port.is_open = not self.closed
        return self.port


class FakeBus:
    def __init__(self, number):
        self.number = number
        self.closed = False
        self.writes = []

    def read_i2c_block_data(self, address, reg, length):
        return list(range(length))

    def write_byte_data(self, address, reg, value):
        self.writes.append(("byte", address, reg, value))

    def write_i2c_block_data(self, address, reg, values):
        self.writes.append(("block", address, reg, values))

    def close(self):
        self.closed = True


class FakeSMBusModule:
    def __init__(self, fail=False):
        self.fail = fail
        self.bus = None

    def SMBus(self, number):
        if self.fail:
            raise OSError("no bus")
        self.bus = FakeBus(number)
        return self.bus


def test_real_uart_opens_with_explicit_serial_settings_and_transfers_bytes():
    module = FakeSerialModule()
    uart = RealUART(module)
    assert uart.open("/dev/rplidar", 115200)
    assert module.port.kwargs == {
        "port": "/dev/rplidar",
        "baudrate": 115200,
        "timeout": 0.0,
        "write_timeout": 1.0,
    }
    assert uart.read(2, 250.0) == b"ab"
    assert module.port.timeout == pytest.approx(0.25)
    assert uart.write(b"xyz") == 3
    assert module.port.written == [b"xyz"]


def test_real_uart_open_failure_is_loud_but_retryable():
    uart = RealUART(FakeSerialModule(fail=True))
    assert not uart.open("/dev/missing", 115200)
    with pytest.raises(HardwareError, match="尚未打开"):
        uart.read(1, 10.0)


def test_real_uart_close_is_idempotent():
    module = FakeSerialModule()
    uart = RealUART(module)
    assert uart.open("/dev/openmv", 115200)
    port = module.port
    uart.close()
    uart.close()
    assert not port.is_open


def test_real_uart_rejects_closed_device_from_serial_constructor():
    uart = RealUART(FakeSerialModule(closed=True))
    assert not uart.open("/dev/rplidar", 115200)
    assert uart.device is None


def test_real_i2c_reads_and_writes_single_and_block_registers():
    module = FakeSMBusModule()
    i2c = RealI2C(module)
    assert i2c.open(1, 0x69)
    assert module.bus.number == 1
    assert i2c.read_register(0x1F, 4) == b"\x00\x01\x02\x03"
    i2c.write_register(0x4E, b"\x0f")
    i2c.write_register(0x50, b"\x08\x09")
    assert module.bus.writes == [
        ("byte", 0x69, 0x4E, 0x0F),
        ("block", 0x69, 0x50, [0x08, 0x09]),
    ]


def test_real_i2c_open_failure_is_retryable():
    i2c = RealI2C(FakeSMBusModule(fail=True))
    assert not i2c.open(1, 0x69)
    with pytest.raises(HardwareError, match="尚未打开"):
        i2c.read_register(0x75, 1)


def test_real_i2c_rejects_non_7_bit_device_address():
    i2c = RealI2C(FakeSMBusModule())
    with pytest.raises(ValueError, match="7-bit"):
        i2c.open(1, 0x80)


def test_real_i2c_close_is_idempotent():
    module = FakeSMBusModule()
    i2c = RealI2C(module)
    assert i2c.open(1, 0x69)
    bus = module.bus
    i2c.close()
    i2c.close()
    assert bus.closed
