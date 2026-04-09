# Boot Flow

This document describes the complete decision tree executed by the bootrom
entry point after reset.

## Initialization

1. Write 23003 (0x59DB) to SYSCTRL+0x0C. Purpose is unclear; likely a clock
   or watchdog configuration [unverified].
2. Switch the CPU to Supervisor mode with IRQ and FIQ disabled (CPSR = 0x13).

## Boot Override Detection

The function `detect_boot_override` samples the 2-bit strap selector exposed
through raw GPIO4 input register (SYSCTRL+0xC8) bits 6:5:

- Bit 5 corresponds to spec DGPIO[2] (USB_BOOT pin)
- Bit 6 corresponds to spec DGPIO[3]

Before sampling, SYSCTRL+0x94 bits [9:8] are set to enable the input path for
these two pins.

The sampling loop runs 5 iterations with an 800-tick delay between each. A pin
is considered asserted only if it reads high in all 5 samples (debounce). The
return value encodes the boot mode:

| Return | Condition                     | rRTC_BOOTMOD | Mode             |
| ------ | ----------------------------- | ------------ | ---------------- |
| 0      | Neither pin consistently high | (unchanged)  | Normal boot      |
| 1      | Only DGPIO[2] asserted (×5)   | 0x01000000   | USB Boot         |
| 2      | Only DGPIO[3] asserted (×5)   | 0x02000000   | AP2-BIOS console |
| 3      | Both asserted (×5)            | 0x05000000   | Diagnostic mode  |

## Mode Dispatch

```
detect_boot_override()
  │
  ├─ 1 -> usbboot_main_loop()           [never returns]
  │
  ├─ 2 -> enter_ap2_bios_console()      [never returns]
  │
  ├─ 3 -> bootrom_diag_mode()           [never returns]
  │
  └─ 0 -> Normal boot (continue below)
```

## Normal Boot: Storage Probe Sequence

When no override is detected, the bootrom attempts to find a valid boot image
from external storage in the following order:

### Step 1: SPI Flash Probe

Sets rRTC_BOOTMOD = 0x03000000, then calls `probe_spi_boot_source()`.

- Configures the SPI controller with default parameters (divider=16, mode=0x15)
- Iterates address byte counts from 1 to 4
- For each count, reads 0x20 bytes from flash address 0 and checks for the
  "ANYKA382" signature at offset +0x04
- On signature match, reads the full header (0x118 bytes), dispatches by
  image type

See [spi-boot.md](spi-boot.md) for details.

| Return | Action                                        |
| ------ | --------------------------------------------- |
| 1      | Valid type-8 image -> jump to 0x48000200 (L2)  |
| 2      | Valid type-6 image -> jump to 0x30000000 (DDR) |
| 0      | No valid SPI image found -> continue to NAND   |

### Step 2: NAND Flash Probe

Sets rRTC_BOOTMOD = 0x04000000, then calls `probe_flash_boot_source()`.

- Initializes the NF sequencer hardware
- Iterates through 8 sets of probe parameters (`nf_probe_params[0..7]`)
- For each set, issues the probe command sequence, reads 0x20 bytes, and checks
  for the "ANYKA382" signature
- On match, reads the full header and dispatches by image type

See [nand-boot.md](nand-boot.md) for details.

| Return | Action                                           |
| ------ | ------------------------------------------------ |
| 1      | Valid type-8 image -> jump to 0x48000200 (L2)     |
| 2      | Valid type-6 image -> jump to 0x30000000 (DDR)    |
| 0      | No valid NAND image found -> continue to fallback |

### Step 3: Fallback

If both storage probes fail, the bootrom sets rRTC_BOOTMOD = 0x02000000 and
enters `enter_ap2_bios_console()`, providing an interactive UART shell as a
last-resort recovery path. This is the same console entered directly by boot
override mode 2.

## Complete Decision Diagram

```
Reset
  │
  ▼
bootrom_entry
  │  SYSCTRL+0x0C = 0x59DB
  │  CPSR = SVC mode, IRQs off
  │
  ▼
detect_boot_override()
  │
  ├── 1 (USB Boot) ──────────────────► usbboot_main_loop()
  │
  ├── 2 (AP2-BIOS) ─► rRTC_BOOTMOD=0x02000000 ──► enter_ap2_bios_console()
  │
  ├── 3 (Diag) ─────► rRTC_BOOTMOD=0x05000000 ──► bootrom_diag_mode()
  │
  └── 0 (Normal) ───► rRTC_BOOTMOD=0x03000000
                        │
                        ▼
                  probe_spi_boot_source()
                        │
                  ┌─────┼─────┐
                  1     0     2
                  │     │     │
                  ▼     │     ▼
              JMP L2    │   JMP DDR
            0x48000200  │  0x30000000
                        │
                        ▼
                  rRTC_BOOTMOD=0x04000000
                  probe_flash_boot_source()
                        │
                  ┌─────┼─────┐
                  1     0     2
                  │     │     │
                  ▼     │     ▼
              JMP L2    │   JMP DDR
            0x48000200  │  0x30000000
                        │
                        ▼
                  rRTC_BOOTMOD=0x02000000
                  enter_ap2_bios_console()
```
