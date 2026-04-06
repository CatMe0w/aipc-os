"""
USB transport layer for the AK7802 USB boot protocol.
"""

import time
from typing import Callable

import usb.core
import usb.util

from .protocol import (
    VID, PID, EP_BULK_IN, EP_BULK_OUT, FRAME_SIZE,
    OPCODE_DOWNLOAD_BEGIN, OPCODE_DOWNLOAD_DONE,
    OPCODE_UPLOAD_BEGIN, OPCODE_EXECUTE, OPCODE_WRITE32,
    build_cmd_frame,
)

# USB transfer timeout in milliseconds.
# Generous to accommodate slow host-controller scheduling on all platforms.
_TIMEOUT_MS = 3000


class DeviceNotFoundError(Exception):
    pass


class AK7802:
    """
    Represents an open connection to an AK7802 device in USB boot mode.
    Callers should not instantiate this directly; use find_device() instead.
    """

    def __init__(self, dev: usb.core.Device) -> None:
        self._dev = dev
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
        dev.set_configuration()

    # ------------------------------------------------------------------
    # Low-level I/O
    # ------------------------------------------------------------------

    def _write(self, data: bytes) -> None:
        self._dev.write(EP_BULK_OUT, data, _TIMEOUT_MS)

    def _read(self, length: int) -> bytes:
        return bytes(self._dev.read(EP_BULK_IN, length, _TIMEOUT_MS))

    def _send_cmd(self, opcode: int, addr: int = 0, arg0: int = 0, arg1: int = 0) -> None:
        self._write(build_cmd_frame(opcode, addr, arg0, arg1))

    # ------------------------------------------------------------------
    # Protocol operations
    # ------------------------------------------------------------------

    def write_mem(
        self,
        addr: int,
        data: bytes,
        progress: Callable[[int], None] | None = None,
    ) -> None:
        """
        Download data to device RAM starting at addr.

        The device writes received bytes in 4-byte (u32) steps. If the payload
        length is not a multiple of 4, the device would write 1-3 garbage bytes
        past the end. Pad to 4-byte alignment here to prevent that.

        progress: optional callable receiving the number of bytes just sent,
                  called after each chunk. Suitable for wrapping with tqdm.update.
        """
        rem = len(data) % 4
        if rem:
            data = data + b'\x00' * (4 - rem)
        self._send_cmd(OPCODE_DOWNLOAD_BEGIN, addr=addr, arg0=len(data))
        offset = 0
        while offset < len(data):
            chunk = data[offset:offset + FRAME_SIZE]
            self._write(chunk)
            written = len(chunk)
            offset += written
            if progress is not None:
                progress(written)
        self._send_cmd(OPCODE_DOWNLOAD_DONE)

    def read_mem(
        self,
        addr: int,
        length: int,
        progress: Callable[[int], None] | None = None,
    ) -> bytes:
        """
        Upload length bytes from device RAM starting at addr.

        After sending all data the device unconditionally transmits a ZLP to
        signal end-of-transfer. Drain it so it does not pollute the next read.

        progress: optional callable receiving the number of bytes just received.
        """
        self._send_cmd(OPCODE_UPLOAD_BEGIN, addr=addr, arg0=length)
        buf = bytearray()
        remaining = length
        while remaining > 0:
            chunk = self._read(min(FRAME_SIZE, remaining))
            buf.extend(chunk)
            received = len(chunk)
            remaining -= received
            if progress is not None:
                progress(received)
        # Drain the trailing ZLP the device always sends after the data.
        try:
            self._dev.read(EP_BULK_IN, FRAME_SIZE, 200)
        except usb.core.USBTimeoutError:
            pass  # no ZLP arrived within the drain window - that is fine
        return bytes(buf)

    def execute(self, addr: int) -> None:
        """
        Branch to addr on the device. The device will not respond after this;
        the USB session ends immediately from the host's perspective.
        """
        self._send_cmd(OPCODE_EXECUTE, addr=addr)

    def poke(self, addr: int, value: int) -> None:
        """Write a single 32-bit value to a device address."""
        self._send_cmd(OPCODE_WRITE32, addr=addr, arg0=value)


def find_device(wait: bool = True) -> AK7802:
    """
    Locate the AK7802 device.

    If wait=True (default), poll every 100 ms until the device appears,
    printing a one-time "Waiting for device" message. This matches the
    fastboot UX for devices that need a hardware strap to enter USB boot mode.

    If wait=False, raise DeviceNotFoundError immediately when not found.
    """
    announced = False
    while True:
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is not None:
            return AK7802(dev)
        if not wait:
            raise DeviceNotFoundError(
                f"AK7802 not found (VID={VID:#06x} PID={PID:#06x}). "
                "Pull DGPIO[2] high to enter USB boot mode."
            )
        if not announced:
            print(f"< waiting for device {VID:#06x}:{PID:#06x} >")
            announced = True
        time.sleep(0.1)
