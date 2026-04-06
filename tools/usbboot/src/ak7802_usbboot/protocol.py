"""
AK7802 USB boot protocol constants and frame construction.

Reversed from AK7802 bootrom. The bootrom exposes a vendor-specific
USB protocol over a single interface with one interrupt and two bulk endpoints.

USB enumeration:
    VID:PID   = 0x0471:0x0666
    bcdUSB    = 0x0110  (USB 1.1, full-speed)
    EP0       = control, maxpacket=16
    EP2 IN    = bulk, maxpacket=64  (device -> host, upload path)
    EP3 OUT   = bulk, maxpacket=64  (host -> device, commands and data)

Command frame format (64 bytes, written to EP3 OUT):
    0x00-0x1B  sync_pad   28 bytes, all 0x60
    0x1C-0x1D  reserved
    0x1E-0x1F  header_magic = 0x0052  (little-endian)
    0x20-0x30  reserved
    0x31       opcode     (1 byte)
    0x32-0x35  addr       (u32 little-endian)
    0x36-0x39  arg0       (u32 little-endian, typically length)
    0x3A-0x3D  arg1       (u32 little-endian)
    0x3E-0x3F  tail_magic = 0x1413  (little-endian)

Data frames (during a DOWNLOAD_BEGIN session):
    Raw payload bytes packed into 64-byte USB packets, written to EP3 OUT.
    The device distinguishes data from commands: a packet is treated as a command
    only when both magic values match AND the first 28 bytes are all 0x60.
    Arbitrary payload bytes will not satisfy both conditions simultaneously.
"""

import struct

VID = 0x0471
PID = 0x0666

EP_BULK_IN  = 0x82  # EP2 IN:  device -> host
EP_BULK_OUT = 0x03  # EP3 OUT: host -> device

FRAME_SIZE = 64

HEADER_MAGIC = 0x0052
TAIL_MAGIC   = 0x1413
SYNC_BYTE    = 0x60

# Opcodes (from handle_usbboot_packet, bootrom offset 0x3CE4)
OPCODE_WRITE32        = 0x1F  # Write 32-bit value to addr; arg0=value
OPCODE_DOWNLOAD_DONE  = 0x3C  # End download session
OPCODE_DOWNLOAD_BEGIN = 0x3F  # Begin download; addr=target, arg0=byte_count
OPCODE_UPLOAD_BEGIN   = 0x7F  # Begin upload; addr=source, arg0=byte_count
OPCODE_EXECUTE        = 0x9F  # Branch to addr; no return


def build_cmd_frame(opcode: int, addr: int = 0, arg0: int = 0, arg1: int = 0) -> bytes:
    """Build a 64-byte command frame for EP3 OUT."""
    frame = bytearray(64)
    frame[0x00:0x1C] = bytes([SYNC_BYTE] * 28)
    struct.pack_into('<H', frame, 0x1E, HEADER_MAGIC)
    frame[0x31] = opcode
    struct.pack_into('<I', frame, 0x32, addr)
    struct.pack_into('<I', frame, 0x36, arg0)
    struct.pack_into('<I', frame, 0x3A, arg1)
    struct.pack_into('<H', frame, 0x3E, TAIL_MAGIC)
    return bytes(frame)
