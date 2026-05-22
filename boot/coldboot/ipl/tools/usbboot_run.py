#!/usr/bin/env python3
"""Upload boot/coldboot/ipl/ipl.bin to DDR via bootrom usbboot and execute it.

Requires the device to be in USB boot mode (DGPIO[2] high at power-on).
DDR is initialized in-place by running the matching aipc-ddr-init stub, so
nboot is bypassed entirely. The IPL is loaded at 0x30038000 (same address
nboot would have used) and executed without --wait, because the IPL is not
expected to return to bootrom usbboot.

In addition to DDR init, the script replicates nboot's NAND init sequence so
that the IPL's BAK fallback path (a port of nboot's ECC-aware NAND read) can
work without nboot ever running. This includes:

  - the bootrom `nf_boot_hw_init` equivalent (sharepin / clock / L2CTR /
    timing reg 0), which on production paths is done by the bootrom NAND
    probe but is skipped in USB boot mode;
  - the v1.58.2 `nboot_init_nand_params` equivalent: block-1 NF timing
    registers, ECC engine enable bit, and the five 32-bit runtime geometry
    variables that nboot stores at 0x30E00D00..0x30E00D13 and reads back
    from inside `nf_read_page_with_ecc`.

All values are verbatim from extracted/nboot.nb0 v1.58.2.

Run from the ipl directory:
    uv run tools/usbboot_run.py --firmware 1.58.2
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO_ROOT / "tools" / "usbboot" / "src"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "ddr-init" / "src"))

from ak7802_usbboot.transport import AK7802, find_device
from aipc_ddr_init.cli import ddr_init

IPL_LOAD_ADDR = 0x30038000
DEFAULT_IPL_BIN = REPO_ROOT / "boot" / "coldboot" / "ipl" / "ipl.bin"


def _rmw32(dev: AK7802, addr: int, clear_mask: int, set_mask: int) -> None:
    cur = struct.unpack("<I", dev.read_mem(addr, 4))[0]
    new = (cur & ~clear_mask) | set_mask
    dev.poke(addr, new & 0xFFFFFFFF)


def nand_init(dev: AK7802, firmware: str) -> None:
    """Replicate bootrom nf_boot_hw_init + nboot_init_nand_params (v1.58.2)."""
    if firmware != "1.58.2":
        raise NotImplementedError(
            f"nand_init constants for firmware {firmware} not yet extracted; "
            f"v1.58.2 only for now"
        )

    # bootrom nf_boot_hw_init equivalent (ROM @0x2648).
    # Done by bootrom in normal-boot NAND probe path; skipped in USB boot mode.
    _rmw32(dev, 0x08000074, 0x00000018, 0x00000008)   # sharepin: clear [4:3], set bit 3
    _rmw32(dev, 0x08000078, 0x00000000, 0x00C70200)   # sharepin: NF clock + I/O paths
    _rmw32(dev, 0x2002C090, 0x00000E00, 0x00000000)   # L2CTR_ASSIGN_REG1: clear [11:9]
    _rmw32(dev, 0x2002C088, 0x00000000, 0x00010000)   # L2CTR_BUF0_7_CFG: enable (bit 16)
    _rmw32(dev, 0x2002C088, 0x00000000, 0x01000000)   # L2CTR_BUF0_7_CFG: flush  (bit 24)
    _rmw32(dev, 0x2002C084, 0x00000000, 0x30000000)   # L2CTR_DMA_PATH_CFG: bits [29:28]
    dev.poke(0x2002A05C, 0x000F5BD1)                  # NF timing reg 0 (block 0) default

    # nboot_init_nand_params equivalent (v1.58.2).
    # Block-1 NF timing dwords are verbatim from nboot.nb0 @0x7C/0x80.
    # Semantics of individual bits not decoded; do not interpret.
    dev.poke(0x2002A15C, 0x00030230)                  # NF timing reg A, nboot@0x7C
    dev.poke(0x2002A160, 0x00040203)                  # NF timing reg B, nboot@0x80
    dev.poke(0x2002B000, 0x00010000)                  # ECC/DMA control: engine enable

    # Runtime geometry variables nboot stores at 0x30E00D00..D13 and reads
    # back from inside nf_read_page_with_ecc / nf_emit_addr_cycles / etc.
    # Our IPL is a near-verbatim port of that code and reads the same DDR
    # locations.
    dev.poke(0x30E00D00, 64)        # pages_per_block (v4)         nboot@0x6A u16
    dev.poke(0x30E00D04, 3)         # page-addr cycles (v6)        nboot@0x75 u8
    dev.poke(0x30E00D08, 4)         # chunks_per_page = page_size >> 9 (big_page)
    dev.poke(0x30E00D0C, 2048)      # page_size (v3)               nboot@0x68 u16
    dev.poke(0x30E00D10, 2)         # col-addr cycles (v5)         nboot@0x73 u8


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, choices=("1.58.2", "1.88"),
                        help="DDR init script variant matching the target device")
    parser.add_argument("--bin", type=Path, default=DEFAULT_IPL_BIN,
                        help="Path to ipl.bin (default: boot/coldboot/ipl/ipl.bin)")
    parser.add_argument("--addr", type=lambda s: int(s, 0), default=IPL_LOAD_ADDR,
                        help=f"Load address (default: {IPL_LOAD_ADDR:#x})")
    parser.add_argument("--skip-nand-init", action="store_true",
                        help="Skip NAND init pokes (use for SD-only IPL builds)")
    args = parser.parse_args()

    if not args.bin.exists():
        print(f"ipl.bin not found: {args.bin}", file=sys.stderr)
        print("Build it first: make -C boot/coldboot/ipl", file=sys.stderr)
        return 1

    payload = args.bin.read_bytes()
    print(f"ipl.bin: {len(payload)} bytes")

    dev = find_device(wait=True)
    print(f"device:  {dev}")

    label, stub_path = ddr_init(dev, firmware=args.firmware)
    print(f"DDR init done ({label}, stub={stub_path.name})")

    if args.skip_nand_init:
        print("NAND init skipped (--skip-nand-init)")
    else:
        nand_init(dev, firmware=args.firmware)
        print(f"NAND init done ({label}, inline pokes)")

    dev.write_mem(args.addr, payload)
    print(f"wrote {len(payload):#x} bytes to {args.addr:#010x}")

    dev.execute(args.addr, wait=False)
    print(f"executing at {args.addr:#010x}; bootrom usbboot will not resume")
    return 0


if __name__ == "__main__":
    sys.exit(main())
