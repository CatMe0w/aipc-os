# ak7802-usbboot

USB boot/download tool for Anyka AK7802 SoC, reverse-engineered from the bootrom.

The bootrom calls this mode "Usbboot". It supports three operations: loading a
program into internal or external memory over USB, writing arbitrary memory and
register values, and transferring execution to a specified address.

**Note:** USB boot does not initialize external RAM. If DDR (`0x30000000`) is the
target, initialize the memory controller first with `poke` commands before using
`write`.

## Install

See the repository root [README](../../README.md) for workspace setup.

On Linux, install the udev rule for non-root access:

```
sudo cp 99-ak7802.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Usage

Enter USB boot mode by pulling DGPIO[2] (DL_JUMP or USB_BOOT pin) high on the
AK7802 before power-on.

All commands wait for the device automatically if it is not yet connected.

```
uv run ak7802-usbboot devices
uv run ak7802-usbboot write  firmware.bin --addr 0x30000000
uv run ak7802-usbboot read   --addr 0x30000000 --len 0x1000 dump.bin
uv run ak7802-usbboot exec   --addr 0x30000000
uv run ak7802-usbboot exec   --addr 0x30000000 --wait
uv run ak7802-usbboot poke   --addr 0x08000054 --value 0x03000000
```

Addresses and values accept both decimal and `0x`-prefixed hexadecimal.

On-chip memory (L2 buffer at `0x48000200`) requires no initialization.

`exec` is fire-and-forget by default. With `--wait`, the tool actively probes
for the BootROM to resume USB boot mode by reading `0x0:4` until it sees the
BootROM entry instruction bytes `06 00 00 EA`.

## Protocol

The device enumerates as `0471:0666` (USB 1.1 full-speed). The host communicates
via EP3 OUT (bulk, 64B) for commands/data and EP2 IN (bulk, 64B) for uploads.

Commands are 64-byte frames with a fixed sync pattern, header/tail magic, and a
one-byte opcode. See [protocol.py](src/ak7802_usbboot/protocol.py) for the full
frame layout and opcode table.
