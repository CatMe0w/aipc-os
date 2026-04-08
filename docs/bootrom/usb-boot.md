# USB Boot Mode

USB Boot is the primary development and recovery interface of the AK7802
bootrom. It is activated by pulling DGPIO[2] high before power-on and provides
a host-driven protocol for writing to memory, reading from memory, and
branching to an arbitrary address.

## Entry

When `detect_boot_override()` returns 1, the bootrom calls
`usbboot_main_loop()`, which:

1. Initializes three state structures: zeroes `usb_ep0_reply_t`, zeroes
   `usbboot_cmd_state_t`, and zeroes the first three fields of
   `usbboot_tx_ctx_t` (`remaining`, `offset`, `active`).
2. Initializes the UART console (for diagnostic output).
3. Initializes the USB hardware.
4. Prints `"\nAspen2_Usbboot>#"` on UART.
5. Enters an infinite loop: poll SYSCTRL+0xCC bit 25 for USB interrupt
   pending, then call the USB IRQ dispatcher.

## USB Hardware Initialization

`usbboot_hw_init()` performs:

1. Clear SYSCTRL+0x58 low 3 bits, then set to 6 (enable USB block).
2. Configure L2 buffer assignment: L2CTR_ASSIGN_REG1 (0x2002C090)
   low 6 bits cleared, then bit 3 set (= 0x08, assigns USB data path).
3. Force full-speed mode: write 1 to USB+0x344.
4. Clear USB POWER register (USB+0x01 = 0).

## USB Device Enumeration

The device enumerates with the following identifiers:

| Field     | Value            |
| --------- | ---------------- |
| VID       | 0x0471           |
| PID       | 0x0666           |
| bcdUSB    | 0x0110 (USB 1.1) |
| bcdDevice | 0x0100           |
| Class     | 0xFF (vendor)    |
| Subclass  | 0xFF             |

### Device Descriptor (18 bytes)

Packed in ROM as 5 dwords at offset 0x4990:

| Offset | Field              | Value  |
| ------ | ------------------ | ------ |
| 0      | bLength            | 0x12   |
| 1      | bDescriptorType    | 1      |
| 2-3    | bcdUSB             | 0x0110 |
| 4      | bDeviceClass       | 0xFF   |
| 5      | bDeviceSubClass    | 0xFF   |
| 6      | bDeviceProtocol    | 0xFF   |
| 7      | bMaxPacketSize0    | 0x10   |
| 8-9    | idVendor           | 0x0471 |
| 10-11  | idProduct          | 0x0666 |
| 12-13  | bcdDevice          | 0x0100 |
| 14     | iManufacturer      | 0      |
| 15     | iProduct           | 0      |
| 16     | iSerialNumber      | 0      |
| 17     | bNumConfigurations | 1      |

### Configuration Descriptor (39 bytes total)

Composed of 5 concatenated descriptors:

**Configuration (9 bytes)**:
`09 02 27 00 01 01 00 C0 01`

- 1 interface, self-powered, 2 mA max current

**Interface (9 bytes)**:
`09 04 00 00 03 FF FF 00 00`

- 3 endpoints, vendor class

**Endpoint 1 - EP1 IN, interrupt (7 bytes)**:
`07 05 81 03 40 00 0A`

- Max packet 64, interval 10 ms
- Note: this endpoint is declared in the descriptor but not used by the
  boot protocol [unverified]

**Endpoint 2 - EP2 IN, bulk (7 bytes)**:
`07 05 82 02 40 00 00`

- Max packet 64, device-to-host (upload path)

**Endpoint 3 - EP3 OUT, bulk (7 bytes)**:
`07 05 03 02 40 00 00`

- Max packet 64, host-to-device (command and data path)

## Bus Reset Handling

On receiving a USB bus reset interrupt (INTRUSB bit 2):

1. Clear FADDR to 0 (un-addressed state).
2. Set POWER = 1 (resume from suspend).
3. Enable interrupt masks: INTRUSBE = 0xF7, INTRTX1E = 0x05 (EP0 + EP2),
   INTRRX1E = 0x0A (EP3).
4. Configure EP2 IN with max packet = 512 and TX mode.
5. Configure EP3 OUT with max packet = 512 and RX mode.
6. Reset INDEX to 0.

Note: Although max packet is programmed as 512 at the register level, the
actual USB 1.1 full-speed bus limits transfers to 64 bytes per packet.

## EP0 Control Transfers

The bootrom handles three standard USB requests on EP0:

| bRequest          | Code | Handling                                                                                                            |
| ----------------- | ---- | ------------------------------------------------------------------------------------------------------------------- |
| SET_ADDRESS       | 5    | Sends a zero-length status stage, waits for TX completion (up to 10000 polls), then writes the new address to FADDR |
| GET_DESCRIPTOR    | 6    | Returns device or configuration descriptor                                                                          |
| SET_CONFIGURATION | 9    | Sends a zero-length status stage                                                                                    |

EP0 data transfers use 16-byte chunks. For responses longer than 16 bytes,
the dispatcher sends chunks on successive EP0 TX interrupts until the full
response is delivered, then sends a final ZLP/status stage (CSR0 = 0x48).

## USB IRQ Dispatcher

The main loop polls SYSCTRL+0xCC bit 25, then calls `usb_irq_dispatch()`:

1. **Reset** (INTRUSB bit 2): call `usb_handle_bus_reset()`.
2. **EP0** (INTRTX1 bit 0): handle setup/status/data stages.
3. **EP2 IN** (INTRTX1 bit 2): if TX is complete and more data remains in
   `usbboot_tx_ctx_t`, send the next 64-byte chunk. On underrun or stall,
   log and clear. When remaining = 0 and `active` flag = 1, send a final
   ZLP and clear the active flag.
4. **EP3 OUT** (INTRRX1 bit 3): call `handle_usbboot_packet()` to parse the
   received data, then clear RXCSR1 bit 0.

## Boot Protocol

### Command Frame Format (64 bytes, EP3 OUT)

```
Offset  Size  Field         Value / Description
0x00    28    sync_pad      All bytes = 0x60
0x1C    2     (reserved)
0x1E    2     header_magic  0x0052 (little-endian)
0x20    17    (reserved)
0x31    1     opcode        See opcode table below
0x32    4     addr          Target address (little-endian u32)
0x36    4     arg0          First argument (little-endian u32)
0x3A    4     arg1          Second argument (little-endian u32)
0x3E    2     tail_magic    0x1413 (little-endian)
```

A received 64-byte packet is recognized as a command frame only when **both**
magic values match **and** the first 28 bytes are all 0x60. Any packet that
fails either check is treated as data (during an active download session).

### Opcodes

| Opcode | Name           | Fields Used                           | Description                                                                                          |
| ------ | -------------- | ------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| 0x3F   | DOWNLOAD_BEGIN | addr = destination, arg0 = byte count | Begin a download session; subsequent non-command packets are written sequentially starting at `addr` |
| 0x3C   | DOWNLOAD_DONE  | (none)                                | End the download session; resets progress counter                                                    |
| 0x1F   | WRITE32        | addr = target, arg1 = value           | Write a 32-bit value to `addr`; reads back and prints the result on UART                             |
| 0x7F   | UPLOAD_BEGIN   | addr = source, arg0 = byte count      | Begin uploading `arg0` bytes from `addr` to the host via EP2 IN                                      |
| 0x9F   | EXECUTE        | addr = branch target                  | Clear EP3 RXCSR1 bit 0 (`RXPKTRDY`) and call `addr` as a function (see below)                        |

Any unrecognized opcode resets the command state to idle (NONE).

### Download Data Flow

After a DOWNLOAD_BEGIN command, the device enters a download state. Each
subsequent EP3 OUT packet that does not match the command frame signature is
treated as raw payload data. The data bytes are written sequentially to
`cmd_state.addr + cmd_state.progress`, and `progress` is incremented by the
USB RX byte count of each packet. A DOWNLOAD_DONE command (or any new command
frame) ends the session.

### EP3 Receive Path and L2BUF_01

Every USB EP3 OUT packet (whether command frame or raw data) is first written
by the USB hardware into L2BUF_01 at 0x48000200 via DMA. The bootrom then
copies the data from L2BUF_01 into a local stack variable (`cmd_pkt`) before
parsing. This means each incoming USB packet overwrites 0x48000200–0x4800023F
regardless of the current protocol state.

**Consequence for downloads to 0x48000200**: When DOWNLOAD_BEGIN targets
address 0x48000200, each subsequent data packet and the DOWNLOAD_DONE /
EXECUTE command frames all overwrite the first 64 bytes. After the final
EXECUTE command, 0x48000200–0x4800023F contains the EXECUTE command frame
(starting with 28 bytes of 0x60 sync_pad), not the intended payload. Code
uploaded to L2 buffer for execution must be loaded at **0x48000240 or later**
to avoid this corruption.

### Upload Data Flow

After an UPLOAD_BEGIN command, the device begins streaming data from the
specified memory address through EP2 IN. Data is sent in 64-byte chunks.
The final chunk uses the exact remaining byte count. After the last chunk,
a zero-length packet is sent if the `active` flag is still set, signaling
transfer completion.

### State Structures

**`usbboot_cmd_state_t`** (16 bytes):
| Offset | Field | Description |
|--------|----------|------------------------------------------|
| 0x00 | mode | Download-active discriminator / last non-download marker |
| 0x04 | addr | Target/source address |
| 0x08 | arg0 | Byte count or value |
| 0x0C | progress | Bytes transferred so far (download only) |

Note: only `mode == 0x3F` is tested later to mean "download active". The
bootrom stores `0x1F` for both WRITE32 and EXECUTE, so this field is not a
strict copy of the last opcode.

**`usbboot_tx_ctx_t`** (16 bytes):
| Offset | Field | Description |
|--------|-----------|------------------------------------------|
| 0x00 | remaining | Bytes remaining to send |
| 0x04 | offset | Current offset from `base_addr` |
| 0x08 | active | 1 = upload in progress |
| 0x0C | base_addr | Source address for upload |

Note: `offset` and `remaining` track upload progress. `base_addr` is written by
UPLOAD_BEGIN before first use; `usbboot_main_loop` only zeros the first three
fields at startup.

### EXECUTE Mechanism

The EXECUTE handler uses a manual function-call sequence, not a tail branch:

```
4154  MOV  LR, PC                    ; LR = 0x415C (return address)
4158  LDR  PC, [R11,#-0xC+var_70]    ; PC = exec_addr
415c  B    locret_41F8               ; reached if stub returns
```

Since LR is set to the instruction after the branch, the called code **can
return** to the bootrom by executing `MOV PC, LR` (or any equivalent). If
the stub returns, execution continues at `handle_usbboot_packet`'s epilogue,
and the bootrom USB command loop resumes normally.

The stub inherits the bootrom's stack pointer. At the point of the EXECUTE
call, SP is inside the bootrom's call chain (`usbboot_main_loop` →
`usb_irq_dispatch` → `handle_usbboot_packet`), residing in L2 buffer SRAM
around 0x48000E70. The initial SP set at `bootrom_entry` is **0x48000FFC**
(for USB boot mode) or **0x4800157C** (before SPI/NF boot probing).

## Bulk IN Transfer Details

`usb_bulk_in_send_next_chunk()` handles the L2 buffer staging for EP2:

1. Select EP2 via INDEX register.
2. Compute source pointer: `base_addr + offset`.
3. If remaining > 64: copy 64 bytes from source to L2BUF_00 (0x48000000),
   writing to USB FIFO EP2 after each word. Set EP2_TX_COUNT = 64. Trigger
   pre-read. Set TXCSR1 bit 0 (TX ready). Decrement remaining by 64,
   increment offset by 64.
4. If remaining <= 64 and nonzero: same as above but with exact remaining
   count. Reset offset and remaining to 0 after send.
5. If remaining = 0: send ZLP (TXCSR1 = 1 with no data). Reset state.

The write-forbid register (USB+0x338) is toggled to gate L2 buffer writes
during the staging process.

## Bootrom Errata

### SET_CONFIGURATION does not reset data toggles

The USB 2.0 spec (§9.1.1.5) requires all endpoint data toggles to be reset to
DATA0 when the device processes a SET_CONFIGURATION request. The AK7802 bootrom
does not do this — the SET_CONFIGURATION handler (opcode 9 in
`usb_handle_setup_request`) only sends a status-stage ZLP and returns. Neither
`usb_handle_bus_reset` nor `usb_configure_endpoint_maxpacket` writes the
ClrDataTog bits (TXCSR1 bit 6 / RXCSR1 bit 7).

**Consequence**: If the host resets its own data toggles (e.g. a new process
calls `set_configuration()`), the host-side toggles return to DATA0 while the
device-side toggles remain at whatever value they held from the previous
session. The resulting toggle mismatch causes the device to ACK but silently
discard the next bulk OUT packet. From the host's perspective the device stops
responding entirely after the first successful session.

**Workaround**: Issue a USB bus reset (`usb.core.Device.reset()`) before
`set_configuration()`. A bus reset resets data toggles at the hardware level
on both sides.

### WRITE32 reads value from arg1, not arg0

The WRITE32 opcode (0x1F) documentation and intuition suggest the value should
be passed in `arg0` (packet offset 0x36). However, the bootrom extracts the
write value from `arg1` (packet offset 0x3A). The extraction uses the same
split-halfword pattern as the address field: low 16 bits from bytes 0x3A–0x3B,
high 16 bits from bytes 0x3C–0x3D.

Passing the value in `arg0` results in writing 0 to the target address (since
`arg1` defaults to 0).

### EXECUTE return path can drop an EP3 OUT packet

`usb_irq_dispatch()` snapshots EP3 `RXCSR1` before calling
`handle_usbboot_packet()`, then unconditionally writes that **pre-saved**
snapshot back with bit 0 cleared after the handler returns:

```
3cc0  STRB  R2, [R3]   ; INDEX = 3
3cd4  STRB  R3, [R2]   ; RXCSR1 = saved_rxcsr1 & 0xFE
```

The EXECUTE path inside `handle_usbboot_packet()` also clears `RXCSR1.bit0`
before jumping to the uploaded stub:

```
412c  STRB  R2, [R3]   ; INDEX = 3
4148  STRB  R3, [R2]   ; RXCSR1 &= ~1
4158  LDR   PC, [R11,#-0xC+var_70]
```

If the stub returns to the bootrom and a new EP3 OUT packet arrived while the
stub was running, the hardware can set `RXPKTRDY` in the meantime. The stale
write at `0x3CD4` then clears that newly-set bit without calling
`handle_usbboot_packet()` again, so the just-arrived packet is dropped.

This is a real lost-packet window in the return path, not just a naming
artifact.

**Workaround**: if an EXECUTE stub will return, the host must not send the next
EP3 OUT packet until it knows the stub has already finished. A fixed delay can
work only if it safely exceeds the stub's worst-case runtime; an out-of-band
completion signal (UART/GPIO/other side effect) is more reliable. A stub that
never returns avoids this specific issue entirely.
