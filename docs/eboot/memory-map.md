# Memory Map and Register Reference

This document lists the memory regions and the registers that EBOOT uses beyond the set in [docs/bootrom/memory-map.md](../bootrom/memory-map.md). That document is a prerequisite. An entry already there does not appear again here, unless EBOOT adds new fields or its own use of the register.

## Address Space Overview (EBOOT additions)

| Base Address | End Address | Region |
| --- | --- | --- |
| 0x20010000 | 0x200100FF | LCD controller |
| 0x20024000 | 0x20024023 | SPI controller block used by CH374 and ENC28J60 |
| 0x20025000 | 0x20025023 | Second SPI-controller slot, reachable through `spi_init_controller(1)` |
| 0x30000000 | 0x33FFFFFF | DDR SDRAM, 64 MB, wraps at the 64 MB boundary |

The current EBOOT code actively uses physical `0x20024000`. `ch374_init` reaches it through `spi_init_controller(0)`, and `enc28j60_init` hard-codes the same base directly. `spi_init_controller(1)` can also select `0x20025000`, but no caller in this image uses that path. The vendor numbering of these SPI blocks therefore stays unresolved here.

## SYSCTRL Register Additions (base 0x08000000)

EBOOT touches several SYSCTRL offsets beyond the bootrom set. The new ones are below. [gpio-driver.md](gpio-driver.md) covers the clock, interrupt and GPIO groups at `+0x7C..+0xFC` in detail.

| Offset | Usage |
| --- | --- |
| +0x04 | CPU PLL configuration (see _CPU Clock Formula_ below) |
| +0x0C | Peripheral reset and clock gate. Bit 3 = LCD clock enable, inverted polarity, clear to enable. Bit 19 = LCD reset pulse. |
| +0x2C | PWM high and low time: `(high_ticks << 16) | low_ticks`. Base tick = 12 MHz. |
| +0x74 | Sharepin mux register 0. The bootrom documents its existence, and EBOOT uses new bits. |
| +0x78 | Sharepin mux register 1, mixed polarity. See `gpio-driver.md`. |
| +0x9C | GPIO bank 0 auxiliary config, 32-bit, 1 bit per GPIO1 pin `[partial]` |
| +0xA0 | GPIO bank 1 auxiliary config, with `pin - 32` as the bit index `[partial]` |
| +0xA4 | GPIO bank 2 auxiliary config, with `pin - 64` as the bit index `[partial]` |
| +0xA8 | GPIO bank 3 auxiliary config, with `pin - 96` as the bit index `[partial]` |
| +0xDC | The DDR init script of nboot writes `0` here. EBOOT maps this register during the clock and reset setup, but no confirmed EBOOT path writes it. The analogous offset carries the name `N configuration register` elsewhere, thus a whole-chip soft-reset meaning is unconfirmed. |
| +0xE0 | GPIO1 interrupt status `[hypothesis]` |
| +0xE4 | GPIO2 interrupt status `[hypothesis]` |
| +0xE8 | GPIO3 interrupt status `[hypothesis]` |
| +0xEC | GPIO4 interrupt status `[hypothesis]` |
| +0xF0 | GPIO1 interrupt mask, 1 = masked `[hypothesis]` |
| +0xF4 | GPIO2 interrupt mask `[hypothesis]` |
| +0xF8 | GPIO3 interrupt mask `[hypothesis]` |
| +0xFC | GPIO4 interrupt mask `[hypothesis]` |

The `+0xE0..+0xFC` block stays a hypothesis. It fits a clear-status and mask-all initialization during the early SYSCTRL setup, but no driver path in EBOOT uses interrupts.

### CPU Clock Formula

The PLL1 configuration register at SYSCTRL+0x04 encodes the clock as:

```
M         = 62 + PLL[5:0]
N         = 1 + PLL[20:17]
ASIC_DIV  = (PLL[8:6] == 0 ? 2 : 1 << PLL[8:6])

PLL1_CLK  = 4 MHz * M / N
ASIC_CLK  = PLL1_CLK / ASIC_DIV
CPU_CLK   = (PLL[15] ? PLL1_CLK : ASIC_CLK)
```

For `SYSCTRL+0x04 = 0x0000D000`, the raw multiplier and divisor fields are both zero, the ASIC divider field is zero, and bit 15 selects PLL1 directly for the CPU. PLL1 and the CPU are therefore at 248 MHz, and the ASIC clock, the MCI source, is at 124 MHz. The 266 MHz value in the v1.58.2 EBOOT banner is a fixed string. It does not describe this register setting. v1.88 prints the matching 248 MHz value.

## SPI Controller Register Usage

The current EBOOT code reaches the SPI hardware through two paths. The CH374 path calls `spi_init_controller(0)` and then uses the generic helpers `spi_transfer_tx`, `spi_transfer_txrx` and `spi_config_mode_clock`. The ENC28J60 path programs physical `0x20024000` directly inside `enc28j60_init`. The unused `spi_init_controller(1)` branch selects `0x20025000`, but no caller in this image exercises it.

The register offsets that the code shows directly:

| Offset | Width | Observed use                                              |
| ------ | ----- | --------------------------------------------------------- |
| +0x00  | 32    | Control / mode word                                       |
| +0x04  | 32    | Status                                                    |
| +0x0C  | 16    | Transfer count                                            |
| +0x10  | 32    | Cleared by the generic TX helper before write bursts      |
| +0x14  | 32    | Cleared by the generic TX/RX helper before the read phase |
| +0x18  | 32    | TX data port                                              |
| +0x1C  | 32    | RX data port                                              |
| +0x20  | 32    | Written to `0x00FFFFFF` by both init paths `[partial]`    |

On the ENC28J60 path, `enc28j60_init` computes a divider that keeps the SPI clock at 10 MHz or below, then programs `SPI_CTRL = (div << 8) | 0x52` and `SPI_CONFIG2 = 0x00FFFFFF`. For a 248 MHz CPU clock the code first computes `div = 11`, then raises it to `12`, which gives `248 / (2 * (12 + 1)) = 9.54 MHz`.

The ENC28J60 transfer code polls status bit `2` while it fills `+0x18`, status bit `6` while it drains `+0x1C`, and status bit `8` for transfer completion. The control flow of the driver supports those bit meanings directly.

The generic SPI helper that CH374 uses toggles control bits `0` and `1` around the TX and RX phase split, and it uses control bit `5` in `spi_cs_assert` and `spi_cs_deassert`. The exact names of those bits do not yet match the ENC28J60-side view, thus this document records only the operations that the code shows.

## DDR Runtime Layout

DDR SDRAM is 64 MB at physical `0x30000000..0x33FFFFFF`. The broader platform analysis and the working LCD configuration point to a wrap at the 64 MB boundary. From the EBOOT assembly alone, two facts are directly visible: the cached framebuffer clear at `0x87B00000`, and the LCD base register literal `0x07B00000`. The effective `0x33B00000` DMA source is an inference from that wrap behavior.

The default DDR runtime layout of EBOOT:

| Region | Address | Purpose |
| --- | --- | --- |
| EBOOT IRQ stack top | 0x30FFFF00 | IRQ-mode stack initialized early in relocation |
| EBOOT SVC stack top | 0x30036000 | SVC-mode stack for the main EBOOT code |
| IMG wrapper | 0x30037FD4 - 0x30037FFF | 44-byte IMG header copied from NAND by nboot |
| EBOOT code and data | 0x30038000+ | EBOOT `.text` / `.data` / `.bss` |
| NK load target | 0x30200000 (virt 0x80200000) | WinCE kernel loaded here by EBOOT before jump |
| Framebuffer | 0x33B00000 | Effective wrapped LCD DMA source on current hardware |
| Top of DDR | 0x33FFFFFF | End of the 64 MB window |

EBOOT writes the literal `0x07B00000` into the base register of the LCD controller. On the current 64 MB board this is the effective DMA source that we usually write as `0x33B00000`. See [lcd-driver.md](lcd-driver.md) for the distinction between the assembly facts and the address-wrap interpretation.

## Virtual Address Mapping

EBOOT uses a WinCE-style OEMAddressTable in the image, through `OALPAtoVA`. It maps the peripherals and DDR into two virtual regions:

- `0x8xxx_xxxx`: cached alias
- `0xAxxx_xxxx`: uncached alias, the same offsets as the cached one, on a different base

Every register access inside EBOOT uses the uncached alias, after a call to `OALPAtoVA(phys, cached=0)`. Examples:

| Physical   | Uncached Virtual | Cached Virtual | Region         |
| ---------- | ---------------- | -------------- | -------------- |
| 0x08000000 | 0xA8100000       | 0x88100000     | SYSCTRL        |
| 0x20010000 | 0xA8010000       | 0x88010000     | LCD controller |
| 0x20024000 | 0xA8024000       | 0x88024000     | SPI controller |
| 0x2002A000 | 0xA802A000       | 0x8802A000     | NAND sequencer |
| 0x30000000 | 0xA0000000       | 0x80000000     | DDR SDRAM      |
| 0x48000000 | 0xA8200000       | 0x88200000     | L2 SRAM        |

This document does not tabulate the base pairs of every region. The list above is the set that the current analysis confirms directly. `OALPAtoVA` is the OEM hook, and it returns the correct virtual address for a physical one.

The convention that the uncached DDR alias starts at `0xA0000000` matters for one runtime data structure. EBOOT keeps the active network state, the device IP and the subnet mask, in DDR at uncached virtual `0xA0020838`, which is DDR physical `0x30020838`.

## Global Variables of Interest

EBOOT uses a small set of fixed-address globals in `.data` and `.bss`. The important ones:

| Address | Contents |
| --- | --- |
| 0x80104A5C | ENC28J60 SPI virtual base pointer |
| 0x80104A60 | Cached ENC28J60 bank-select bits (`0x00`, `0x20`, `0x40`, `0x60`) |
| 0x80107768 | Generic SPI virtual base pointer used by the CH374 path |
| 0x80107798 | Selected generic SPI controller index (`0` selects `0x20024000`, `1` selects `0x20025000`) |
| 0x80106E14 | SYSCTRL virtual base pointer (filled by early init) |
| 0x80106E40 | First slot of the Ethernet HAL dispatch block |
| 0x80106E44 | Vtable: RX-ready helper |
| 0x80106E4C | Vtable: receive function |
| 0x80106E54 | Vtable: driver init function |
| 0x80106E58 | Vtable: send function |
| 0x80106E60 | Backend-private field zeroed by the Ethernet registration paths |
| 0x80106EA0 | Start of the in-RAM default PTB structure |
| 0x80106EB0 | Device IP address (u32 little-endian; default `0x0B00A8C0`) |
| 0x80106EB4 | Subnet mask (u32 little-endian; default `0x00FFFFFF`) |
| 0x80106EB8 | Gateway IP (u32 little-endian; default `0`) |
| 0x800F0140 | Runtime copy of the 57-entry alt-function dispatch table |
| 0x800F36B0 | ENC28J60 cached `next_packet_ptr` (see `ethernet-driver.md`) |
| 0x800F5128 | Fixed Ethernet RX frame buffer base passed to `OEMEthGetFrame` |
| 0x800F5134 | EtherType field inside that RX buffer (`0x800F5128 + 12`) |
| 0xA0020838 | Active device IP in the runtime network state (uncached DDR) |
| 0xA002083C | Active subnet mask in the runtime network state (uncached DDR) |

## Function Addresses

The EBOOT image holds about 700 functions. This table gives the 126 that the analysis names so far. A function that is absent from the table is not yet named. It is not absent from the image.

Most of these names are ours. The exceptions come from a contract outside this image. `OALPAtoVA`, `OALMSG`, `OEMEthGetFrame` and `OEMEthSendFrame` are WinCE OAL entry points. `EbootSendBootmeAndWaitForTftp`, `SendBootme` and `LoadNandBoot` come from the Ethernet Bootloader Common Library. `memset`, `memcpy` and `memcmp` are the standard C routines.

The addresses are the linked virtual addresses of the v1.88 build. EBOOT executes from physical `0x30038000`, thus the physical address of a function is its virtual address minus `0x50000000`.

| Function | Address |
| --- | --- |
| `gpio_set_value` | 0x80059EC4 |
| `oal_print_section_header` | 0x8005A244 |
| `display_init_tvout` | 0x8005A2E4 |
| `check_update_eboot_request` | 0x8005A368 |
| `jump_to_nk_kernel` | 0x8005A4C0 |
| `oem_early_init` | 0x8005A7FC |
| `power_on_reason_init` | 0x8005AA44 |
| `oem_platform_init` | 0x8005ABF4 |
| `OALPAtoVA` | 0x8005ADF0 |
| `oal_va_to_pa_for_launch` | 0x8005AE6C |
| `OALMSG` | 0x8005B004 |
| `nk_partition_load` | 0x8005B4A4 |
| `eboot_main` | 0x8005BC44 |
| `EbootSendBootmeAndWaitForTftp` | 0x8005C3B0 |
| `SendBootme` | 0x8005ECBC |
| `memset` | 0x8005EE68 |
| `memcpy` | 0x8005EEF0 |
| `memcmp` | 0x8005FC50 |
| `native_usb_host_init` | 0x800626B0 |
| `gpio_bank_config_write` | 0x800628BC |
| `gpio_bank_data_write` | 0x80062968 |
| `gpio_aux_config_write` | 0x80062A28 |
| `init_clock_register_pointers` | 0x80062D20 |
| `cpu_clock_get` | 0x80062D78 |
| `cpu_clock_get_vco` | 0x80062DF4 |
| `sysctrl_clock_gate` | 0x80062E50 |
| `delay_us_cpu_spin` | 0x80062E78 |
| `sysctrl_reset_pulse` | 0x80062ED0 |
| `pal_ioctl_get_spi_clock` | 0x80062F18 |
| `pal_ioctl_set_clock_gate` | 0x80062F40 |
| `reg_rmw` | 0x80062F7C |
| `sysctrl_altfunc_bit_set` | 0x80062F90 |
| `sysctrl_altfunc_bit_clear` | 0x80062FB8 |
| `sysctrl_clock_init` | 0x80062FE0 |
| `gpio_enable_alt` | 0x8006308C |
| `gpio_table_ready` | 0x8006386C |
| `eboot_handoff_to_launch_addr_mmu_off` | 0x80063A0C |
| `pal_ioctl` | 0x80063B14 |
| `fmd_mount` | 0x800647E8 |
| `ptb_build_default_in_ram` | 0x80064BA0 |
| `eboot_download_file_tftp` | 0x80065754 |
| `ptb_load_default_network_config` | 0x800660DC |
| `LoadNandBoot` | 0x800663E4 |
| `hw_phase1_step3` | 0x8006742C |
| `hw_phase1_init` | 0x800674AC |
| `hw_unknown_post_touchpad` | 0x80067548 |
| `oal_bootargs_init` | 0x800675D4 |
| `fmd_read_partition_table` | 0x800676FC |
| `fmd_init` | 0x80067764 |
| `fmd_get_partition_info` | 0x80067838 |
| `OEMEthGetFrame` | 0x80067B38 |
| `OEMEthSendFrame` | 0x80067B60 |
| `eth_register_bulverde_rndis` | 0x80067BD0 |
| `eth_register_enc28j60` | 0x80067CF4 |
| `disable_mmu_and_enter_phys_trampoline` | 0x80067EB4 |
| `invalidate_tlb_and_branch` | 0x80067ED4 |
| `nand_chip_select_cs` | 0x80068D90 |
| `nand_read_status` | 0x80068E28 |
| `nand_cmd_sub` | 0x80068E84 |
| `nand_write_addr_bytes_col_row` | 0x80068EEC |
| `nand_reset` | 0x80068FF0 |
| `nand_read_id` | 0x8006909C |
| `nand_init_chip` | 0x800691B4 |
| `nand_detect_device` | 0x80069728 |
| `nand_read_page` | 0x80069F78 |
| `nand_read_oob_or_ecc` | 0x8006A264 |
| `nand_verify_ecc_match` | 0x8006A61C |
| `nand_write_page` | 0x8006A9A0 |
| `nand_program_page` | 0x8006AC78 |
| `nand_erase_block` | 0x8006B0D0 |
| `fmd_read_sector` | 0x8006BA54 |
| `fmd_erase_block` | 0x8006BA84 |
| `fmd_get_block_status` | 0x8006BAAC |
| `fmd_write_sector` | 0x8006BAD4 |
| `fmd_driver_init` | 0x8006BAF0 |
| `l2_sram_alloc_dma_descriptors` | 0x8006C9A0 |
| `get_lcd_panel_power_pin` | 0x8006CD4C |
| `get_lcd_panel_reset_pin` | 0x8006CD54 |
| `fb_draw_char` | 0x8006CFBC |
| `console_newline` | 0x8006D09C |
| `console_backspace` | 0x8006D0F4 |
| `console_putchar` | 0x8006D12C |
| `console_printf_raw` | 0x8006D204 |
| `console_init_fb_params` | 0x8006D270 |
| `console_printf_dec` | 0x8006D344 |
| `fb_clear_5mb` | 0x8006D3C4 |
| `lcd_init` | 0x8006D3E4 |
| `pwm_set` | 0x8006D64C |
| `touchpad_init_1` | 0x8006D930 |
| `touchpad_init_3` | 0x8006D9DC |
| `delay_ms_alt` | 0x8006DAA8 |
| `touchpad_get_keycode` | 0x8006DE78 |
| `ch374_poll_hid_keycode` | 0x800701B8 |
| `maint_format_partition` | 0x80070474 |
| `maint_update_eboot` | 0x80070518 |
| `maint_update_xip` | 0x800705B0 |
| `maint_update_dispatch` | 0x80070708 |
| `maintenance_menu` | 0x8007079C |
| `maintenance_menu_entry` | 0x80070CD0 |
| `delay_ms` | 0x80071FC8 |
| `ch374_reg_write` | 0x80071FF0 |
| `ch374_reg_read` | 0x80072078 |
| `ch374_set_address` | 0x800720F8 |
| `ch374_read_buffer` | 0x8007218C |
| `ch374_init` | 0x80072228 |
| `ch374_register_setup_stage2` | 0x8007242C |
| `ch374_register_setup_stage1` | 0x80072458 |
| `ch374_read_hid_report` | 0x8007327C |
| `spi_init_controller` | 0x8007534C |
| `spi_transfer_tx` | 0x80075428 |
| `spi_cs_assert` | 0x80075544 |
| `spi_cs_deassert` | 0x80075560 |
| `spi_transfer_txrx` | 0x8007557C |
| `pal_set_clock_gate` | 0x800756E0 |
| `pal_get_spi_clock_freq` | 0x80075730 |
| `spi_config_mode_clock` | 0x80075778 |
| `spi_submit_command_rx` | 0x8007580C |
| `spi_submit_command` | 0x8007581C |
| `hw_phase1_step2` | 0x80076C08 |
| `enc28j60_wcr` | 0x80076DE4 |
| `enc28j60_rcr` | 0x80076E7C |
| `enc28j60_read_buffer_bulk` | 0x80076F60 |
| `enc28j60_bank_select` | 0x80077070 |
| `enc28j60_init` | 0x800772BC |
| `enc28j60_rx_poll` | 0x800777A8 |
| `enc28j60_tx_frame` | 0x80077958 |

Two names in this table are known to be wrong. `nand_read_id` issues `cmd 0x70` and returns a status byte, and `gpio_table_ready` validates an ID range rather than a readiness state. See [nand-driver.md](nand-driver.md) and [gpio-driver.md](gpio-driver.md).

## Unresolved

- SYSCTRL `+0x9C..+0xA8`, the GPIO aux registers. The bit-level semantics are unknown. The code shows a per-pin single-bit toggle, but whether the bits control a pull-up or pull-down, an input filter, the drive strength, or a second pinmux layer is not determined.
- SYSCTRL `+0xE0..+0xFC`. These are per-bank GPIO interrupt status and mask registers by hypothesis, from the initialization pattern alone. No EBOOT driver path confirms the function.
- SYSCTRL `+0xD4`. The bank-0 input-filter path in the GPIO driver uses it, through a 32-byte lookup table, and so does the diagnostic mode of the bootrom. It is probably a wake-source or input-filter enable register, but the bit meanings are unconfirmed.
- The vendor numbering of the SPI controller blocks. The current EBOOT code actively uses physical `0x20024000`, and `spi_init_controller(1)` names `0x20025000`, but the match with any external `SPI0`, `SPI1` or `SPI2` naming is unconfirmed.
- SPI `+0x20`. Both the generic SPI init path and the ENC28J60 init path write `0x00FFFFFF` here, but its purpose is unknown.
- The use of control bits `0`, `1` and `5` by the generic SPI helper, together with the fixed low-byte mode value `0x52` of the ENC28J60 driver. These do not yet form one register-level model.
- The full contents of the `OEMAddressTable`. This document lists only the entries seen through `OALPAtoVA` calls.
