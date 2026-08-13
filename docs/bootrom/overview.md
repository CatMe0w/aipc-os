# AK7802 Bootrom Overview

The AK7802 bootrom is a mask ROM program in the SoC. It is 0x4A60 bytes, about 19 KB. When the ARM926EJ-S starts to execute from boot ROM, it starts at address `0x00000000`. The instruction there is the reset vector, and it branches immediately to the main bootrom entry at `0x00000020`. The bootrom then selects a boot source, loads the first-stage payload, and gives control to it.

## Confirm the Bootrom Image

The bootrom is mask ROM and is not writable. To confirm that your device holds the same bootrom image that these documents describe, read the ROM in USB boot mode:

```bash
uv run ak7802-usbboot read --addr 0x0 --len 0x4a60 bootrom.bin
```

Expected SHA-256: 76f87330b9e2d1d2804acc5489cf085389db8452f1c503c8fcf6a8c0305e0ce9

If the hash is different, do not assume that the offsets and the behavior here apply to your device.

## Exception Vector Table

The ROM starts at address `0x00000000` with the standard ARM exception vector table. The first fetch on this entry path is the instruction at `0x00000000`. That reset vector jumps immediately to `bootrom_entry`. The bytes between `0x00000000` and `0x00000020` are not dead space. They are the rest of the exception vector table. The other vectors (Undefined Instruction, SVC, Prefetch Abort, Data Abort, IRQ, FIQ) point into DDR from `0x30000004` to `0x3000001C`. A loaded program can therefore install its own handlers.

| Vector           | Mechanism     | Target     |
| ---------------- | ------------- | ---------- |
| Reset            | Branch        | 0x00000020 |
| Undefined Instr. | MOV PC, imm   | 0x30000004 |
| SVC              | MOV PC, imm   | 0x30000008 |
| Prefetch Abort   | MOV PC, imm   | 0x3000000C |
| Data Abort       | LDR PC, [lit] | 0x30000010 |
| Reserved         | LDR PC, [lit] | 0x30000014 |
| IRQ              | LDR PC, [lit] | 0x30000018 |
| FIQ              | LDR PC, [lit] | 0x3000001C |

## External Reset Pin

`#RST` is an external active-low reset input. It resets all internal states except the RTC module. This is the behavior of the external reset pin only. It is not evidence that a normal cold start enters the bootrom through `#RST`.

## Boot Paths

The bootrom has four operating modes. At entry it samples the DGPIO[3:2] strap pins to select one (see [boot-flow.md](boot-flow.md)):

| DGPIO[3] | DGPIO[2] | Mode | Description |
| --- | --- | --- | --- |
| 0 | 0 | Normal boot | Probe SPI, then NAND, then fall back to the UART console |
| 0 | 1 | USB Boot | Enter the USB download, upload and execute loop |
| 1 | 0 | AP2-BIOS console | Enter the UART interactive console directly |
| 1 | 1 | Diagnostic self-test | Run the GPIO and RTC/USB register tests, then hang |

## Memory Regions Used

| Address    | Size   | Description                          |
| ---------- | ------ | ------------------------------------ |
| 0x00000000 | 0x4A60 | Bootrom code and read-only data      |
| 0x08000000 | -      | System control registers (SYSCTRL)   |
| 0x20024000 | -      | SPI controller registers             |
| 0x20026000 | -      | UART controller registers            |
| 0x2002A000 | -      | NAND Flash sequencer registers       |
| 0x2002B000 | -      | NAND Flash ECC/DMA registers         |
| 0x48000000 | 0x157F | L2 buffer SRAM                       |
| 0x70000000 | -      | USB controller registers (MUSBMHDRC) |
| 0x30000000 | -      | DDR SDRAM base (external memory)     |

See [memory-map.md](memory-map.md) for the register-level breakdown.

## Stage Progression Marker

The bootrom writes a stage code to the RTC boot-mode register at SYSCTRL+0x54 as it moves through each phase. The code shows how far the boot went after a failure:

| Value      | Phase                             |
| ---------- | --------------------------------- |
| 0x01000000 | USB Boot mode entered             |
| 0x02000000 | AP2-BIOS console mode entered     |
| 0x03000000 | SPI boot probe started            |
| 0x04000000 | NAND boot probe started           |
| 0x05000000 | Diagnostic self-test mode entered |

## Execution Handoff

When the bootrom finds a valid boot image, it jumps to one of two fixed addresses. The image type selects the address:

- **0x48000200** (L2 buffer) for image type 8, for small in-place payloads
- **0x30000000** (DDR base) for image type 6, for payloads that need DDR setup from the register init script in the image header

The bootrom does not return. Every path either jumps to a loaded payload or loops forever (diagnostic mode and UART console).

## Function Addresses

The function names in these documents are our names from the reverse engineering. The mask ROM holds no symbols. The ROM sits at address 0, thus each address below is both the ROM offset and the address where the function runs.

| Function | Address |
| --- | --- |
| `bootrom_entry` | 0x0020 |
| `strlen` | 0x0120 |
| `strcmp_l2_string` | 0x0170 |
| `copy_u32s` | 0x021C |
| `cmd_go` | 0x02A4 |
| `cmd_download` | 0x0300 |
| `cmd_dump` | 0x058C |
| `cmd_setvalue` | 0x06E0 |
| `dispatch_ap2_command` | 0x07AC |
| `ap2_bios_console` | 0x0860 |
| `enter_ap2_bios_console` | 0x0A80 |
| `nf_is_supported_page_chunk` | 0x0A98 |
| `probe_flash_boot_source` | 0x0AE0 |
| `probe_spi_boot_source` | 0x0D68 |
| `delay_ticks` | 0x0FA8 |
| `detect_boot_override` | 0x103C |
| `apply_reg_init_script` | 0x1188 |
| `bootrom_diag_mode` | 0x1260 |
| `run_rtcusb_selftest` | 0x1278 |
| `rtcusb_test_window` | 0x1398 |
| `rtcusb_write_indexed14` | 0x17D0 |
| `rtcusb_read_indexed14` | 0x187C |
| `gpio4_drive_testbit_high` | 0x1928 |
| `gpio4_drive_testbit_low` | 0x199C |
| `gpio_mux_selftest` | 0x1A14 |
| `diag_init` | 0x1CA4 |
| `hex_digit_to_nibble` | 0x1CD8 |
| `uart_console_init` | 0x1D74 |
| `uart_recv_packed_bytes` | 0x1DF0 |
| `uart_get_rx_word` | 0x2028 |
| `uart_putc` | 0x2184 |
| `uart_puts` | 0x2240 |
| `uart_prompt_hex32` | 0x22CC |
| `uart_put_hex32` | 0x24B4 |
| `nf_seq_is_done` | 0x2604 |
| `nf_boot_hw_init` | 0x2648 |
| `nf_set_boot_timings` | 0x277C |
| `nf_delay_ticks` | 0x27D8 |
| `copy_l2buf0_words` | 0x28A8 |
| `nf_issue_probe_sequence` | 0x293C |
| `nf_read_chunk_to_buf` | 0x2C3C |
| `nf_load_payload` | 0x2D5C |
| `spi_write_byte` | 0x2E48 |
| `spi_read_word` | 0x2EB0 |
| `spi_boot_configure` | 0x2F50 |
| `spi_boot_read` | 0x2FC8 |
| `usbboot_main_loop` | 0x3110 |
| `usbboot_hw_init` | 0x318C |
| `usb_handle_bus_reset` | 0x3238 |
| `usb_bulk_in_send_next_chunk` | 0x32B8 |
| `usb_handle_set_address` | 0x3530 |
| `usb_handle_get_descriptor` | 0x35CC |
| `usb_irq_dispatch` | 0x3A54 |
| `handle_usbboot_packet` | 0x3CE4 |
| `usb_handle_setup_request` | 0x4218 |
| `usb_ep0_send_status` | 0x4344 |
| `usb_ep0_send_next_chunk` | 0x4370 |
| `usb_configure_endpoint_maxpacket` | 0x464C |

The reset vector at address 0 is a four-byte branch to `bootrom_entry`. It carries no name of its own here.
