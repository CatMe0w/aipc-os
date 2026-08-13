# Boot Flow

This document describes the decision tree that the bootrom entry point runs after the CPU starts to execute from boot ROM.

## CPU Entry

The flow starts when the ARM926EJ-S fetches from address `0x00000000`. That address holds the bootrom reset vector, and it branches immediately to the main bootrom entry at `0x00000020`.

The event that releases the CPU into this path can be power-on, RTC wake, external `#RST`, or another SoC-specific path. This document describes the bootrom control flow only after execution reaches the ROM vector. It does not assume that a normal cold start passes through the external `#RST` pin.

## Initialization

1. Write 0x59DB to SYSCTRL+0x0C, the module clock gate register. A set bit gates a module off, thus this leaves a clock only on the modules that the bootrom needs. nboot later clears the whole register and turns every module clock back on. See [memory-map.md](memory-map.md).
2. Switch the CPU to SVC mode (CPSR = 0x13).

## Boot Override Detection

The function `detect_boot_override` samples the 2-bit strap selector in the raw GPIO4 input register (SYSCTRL+0xC8), bits 6:5:

- Bit 5 is schematic DGPIO[2], the USB_BOOT pin
- Bit 6 is schematic DGPIO[3]

Before it samples, the function sets SYSCTRL+0x94 bits [9:8] to enable the input path for these two pins.

The sampling loop runs 5 times with a delay of 800 ticks between each pass. A pin counts as asserted only when it reads high in all 5 samples. This debounces the input. The return value gives the boot mode:

| Return | Condition                     | SYSCTRL+0x54 | Mode             |
| ------ | ----------------------------- | ------------ | ---------------- |
| 0      | Neither pin consistently high | (unchanged)  | Normal boot      |
| 1      | Only DGPIO[2] asserted (5x)   | 0x01000000   | USB Boot         |
| 2      | Only DGPIO[3] asserted (5x)   | 0x02000000   | AP2-BIOS console |
| 3      | Both asserted (5x)            | 0x05000000   | Diagnostic mode  |

## Mode Dispatch

```
detect_boot_override()
  |
  +- 1 -> usbboot_main_loop()           [never returns]
  |
  +- 2 -> enter_ap2_bios_console()      [never returns]
  |
  +- 3 -> bootrom_diag_mode()           [never returns]
  |
  +- 0 -> Normal boot (continue below)
```

## Normal Boot: Storage Probe Sequence

When there is no override, the bootrom looks for a valid boot image in external storage in this order.

### Step 1: SPI Flash Probe

The bootrom sets SYSCTRL+0x54 = 0x03000000, then calls `probe_spi_boot_source()`, which:

- Configures the SPI controller with the default parameters (divider=16, mode=0x15)
- Steps through address byte counts from 1 to 4
- Reads 0x20 bytes from flash address 0 for each count, and looks for the "ANYKA382" signature at offset +0x04
- Reads the full header (0x118 bytes) on a signature match, then dispatches by image type

See [spi-boot.md](spi-boot.md) for details.

| Return | Action                                         |
| ------ | ---------------------------------------------- |
| 1      | Valid type-8 image -> jump to 0x48000200 (L2)  |
| 2      | Valid type-6 image -> jump to 0x30000000 (DDR) |
| 0      | No valid SPI image found -> continue to NAND   |

### Step 2: NAND Flash Probe

The bootrom sets SYSCTRL+0x54 = 0x04000000, then calls `probe_flash_boot_source()`, which:

- Initializes the NF sequencer hardware
- Steps through 8 sets of probe parameters (`nf_probe_params[0..7]`)
- Issues the probe command sequence for each set, reads 0x20 bytes, and looks for the "ANYKA382" signature
- Reads the full header on a match, then dispatches by image type

See [nand-boot.md](nand-boot.md) for details.

| Return | Action                                            |
| ------ | ------------------------------------------------- |
| 1      | Valid type-8 image -> jump to 0x48000200 (L2)     |
| 2      | Valid type-6 image -> jump to 0x30000000 (DDR)    |
| 0      | No valid NAND image found -> continue to fallback |

### Step 3: Fallback

If both storage probes fail, the bootrom sets SYSCTRL+0x54 = 0x02000000 and enters `enter_ap2_bios_console()`. This interactive UART shell is the last recovery path. It is the same console that boot override mode 2 enters directly.

## Complete Decision Diagram

```
Reset
  |
  v
bootrom_entry
  |
  |  SYSCTRL+0x0C = 0x59DB
  |  CPSR = SVC mode
  v
detect_boot_override()
  |
  +-- 1 (USB Boot) ------------------------------> usbboot_main_loop()
  |
  +-- 2 (AP2-BIOS) -> SYSCTRL+0x54 = 0x02000000 -> enter_ap2_bios_console()
  |
  +-- 3 (Diag) -----> SYSCTRL+0x54 = 0x05000000 -> bootrom_diag_mode()
  |
  +-- 0 (Normal) ---> SYSCTRL+0x54 = 0x03000000
                        |
                        v
                  probe_spi_boot_source()
                        |
                  +-----+-----+
                  1     0     2
                  |     |     |
                  v     |     v
              JMP L2    |   JMP DDR
            0x48000200  |  0x30000000
                        |
                        v
                  SYSCTRL+0x54 = 0x04000000
                  probe_flash_boot_source()
                        |
                  +-----+-----+
                  1     0     2
                  |     |     |
                  v     |     v
              JMP L2    |   JMP DDR
            0x48000200  |  0x30000000
                        |
                        v
                  SYSCTRL+0x54 = 0x02000000
                  enter_ap2_bios_console()
```
