# USB Boot Mode

USB Boot is the primary development and recovery interface of the AK7802
bootrom. It is activated by pulling DGPIO[2] high before power-on and provides
a host-driven protocol for writing to memory, reading from memory, and
branching to an arbitrary address.

## Entry

When `detect_boot_override()` returns 1, the bootrom calls
`usbboot_main_loop()`, which:

1. Zeroes out three state structures: `usb_ep0_reply_t`, `usbboot_tx_ctx_t`,
   and `usbboot_cmd_state_t`.
2. Initializes the UART console (for diagnostic output).
3. Initializes the USB hardware.
4. Prints `"\nAspen2_Usbboot>#"` on UART.
5. Enters an infinite loop: poll SYSCTRL+0xCC bit 25 for USB interrupt
   pending, then call the USB IRQ dispatcher.

## USB Hardware Initialization

`usbboot_hw_init()` performs:

1. Clear SYSCTRL+0x58 low 3 bits, then set to 6 (enable USB block).
2. Configure L2 buffer assignment: L2CTR_ASSIGN_REG1 low 6 bits cleared,
   then bit 3 set (assigns L2 buffer 1 to the USB data path).
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
| 0x1F   | WRITE32        | addr = target, arg0 = value           | Write a 32-bit value to `addr`; reads back and prints the result on UART                             |
| 0x7F   | UPLOAD_BEGIN   | addr = source, arg0 = byte count      | Begin uploading `arg0` bytes from `addr` to the host via EP2 IN                                      |
| 0x9F   | EXECUTE        | addr = branch target                  | Clear EP3 RXCSR1 and branch to `addr`; does not return                                               |

Any unrecognized opcode resets the command state to idle (NONE).

### Download Data Flow

After a DOWNLOAD_BEGIN command, the device enters a download state. Each
subsequent EP3 OUT packet that does not match the command frame signature is
treated as raw payload data. The data bytes are written sequentially to
`cmd_state.addr + cmd_state.progress`, and `progress` is incremented by the
USB RX byte count of each packet. A DOWNLOAD_DONE command (or any new command
frame) ends the session.

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
| 0x00 | opcode | Current command state (enum) |
| 0x04 | addr | Target/source address |
| 0x08 | arg0 | Byte count or value |
| 0x0C | progress | Bytes transferred so far (download only) |

**`usbboot_tx_ctx_t`** (12 bytes):
| Offset | Field | Description |
|--------|-----------|------------------------------------------|
| 0x00 | active | 1 = upload in progress |
| 0x04 | base_addr | Source address for upload |
| 0x08 | offset | Current offset from base_addr |
| 0x0C | remaining | Bytes remaining to send |

Note: the `offset` and `remaining` fields together track the upload progress.
`offset` is reset to 0 after each complete transfer.

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
