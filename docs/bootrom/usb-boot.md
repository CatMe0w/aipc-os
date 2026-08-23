# USB Boot Mode

USB Boot is the main development and recovery interface of the AK7802 bootrom. A high level on DGPIO[2] before power-on selects it. It gives the host a protocol to write memory, read memory, and branch to any address.

## Entry

When `detect_boot_override()` returns 1, the bootrom calls `usbboot_main_loop()`, which:

1. Initializes three state structures. It zeroes `usb_ep0_reply_t`, zeroes `usbboot_cmd_state_t`, and zeroes the first three fields of `usbboot_tx_ctx_t`: `remaining`, `offset` and `active`.
2. Initializes the UART console for diagnostic output.
3. Initializes the USB hardware.
4. Prints `"\nAspen2_Usbboot>#"` on the UART.
5. Enters a loop with no exit. It polls SYSCTRL+0xCC bit 25 for a pending USB interrupt, then calls the USB IRQ dispatcher.

## USB Hardware Initialization

`usbboot_hw_init()` does the following:

1. Clear the low 3 bits of SYSCTRL+0x58, then set them to 6 (`0b110`). This enables the USB block.
2. Configure the L2 buffer assignment. Clear the low 6 bits of L2CTR_ASSIGN_REG1 (0x2002C090), then set bit 3, which gives 0x08 and assigns the USB data path.
3. Force full-speed mode: write 1 to USB+0x344.
4. Clear the USB POWER register: USB+0x01 = 0.

Steps 3 and 4 are what hold the device at full speed. The controller itself can run at high speed, and neither write is a hardware limit. See [High and Full Speed](#high-and-full-speed).

## High and Full Speed

The USB controller is a high-speed core. Full speed is a choice that the bootrom makes, not a limit of the part. Two registers decide it, and they do different jobs.

`POWER` (USB+0x01) drives the speed negotiation. Bit 5 enables the chirp handshake during a bus reset. Bit 4 is a read-only record of the result: it reads 1 after a reset negotiates high speed. A write of 0xF0 reads back 0xA0. Bit 5 and bit 7 are therefore writable, bit 4 is status, and bit 6, the usual soft-connect bit, does not exist here. That last point explains why a host sees the bootrom without any write to bit 6. The D+ pull-up is not under software control, and the device side cannot signal a disconnect. A clear of bit 5, followed by a host port reset, brings the link up at full speed. A set of bit 5 brings it up at high speed.

`USB+0x344` sizes the data path. Only bits 0 and 1 of the low byte exist. A write of 0xFF to that byte reads back 0x03. Bit 0, held set, confines transfers to 64 bytes even after the link negotiates high speed. A device that leaves the bit set and chirps successfully therefore ends up with a 480 Mbit/s link that mangles any packet larger than 64 bytes. Bit 1 is status. It reads 1 while the link runs at full speed. The upper half of the register carries further status bits for the same transitions.

Neither register latches at reset in the way the write order suggests. A set of bit 0 of `USB+0x344` on a running controller does not suppress the next chirp, because that job belongs to bit 5 of `POWER`. Both writes are necessary. A bring-up sequence must do them together, and in opposite order for the two speeds.

The endpoint packet sizes follow from the speed. The bootrom programs both bulk endpoints to 512 at the register level in either case, and the L2 windows match that size. Buffers 0 and 1 are 512 bytes each, thus one high-speed bulk packet fills a window exactly. The EP0 window at 0x48001500 is 64 bytes, the size that USB 2.0 mandates for `bMaxPacketSize0` at high speed. The EP0 byte-count register at USB+0x330 is a 7-bit field, because a write of 0x3FF reads back 0x7F, thus it holds 64 but nothing larger.

## USB Device Enumeration

The device enumerates with these identifiers:

| Field     | Value            |
| --------- | ---------------- |
| VID       | 0x0471           |
| PID       | 0x0666           |
| bcdUSB    | 0x0110 (USB 1.1) |
| bcdDevice | 0x0100           |
| Class     | 0xFF (vendor)    |
| Subclass  | 0xFF             |

### Device Descriptor (18 bytes)

The ROM packs it as 5 dwords at offset 0x4990:

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

Five descriptors, one after the other:

**Configuration (9 bytes)**: `09 02 27 00 01 01 00 C0 01`

- 1 interface, self-powered, 2 mA max current

**Interface (9 bytes)**: `09 04 00 00 03 FF FF 00 00`

- 3 endpoints, vendor class

**Endpoint 1 - EP1 IN, interrupt (7 bytes)**: `07 05 81 03 40 00 0A`

- Max packet 64, interval 10 ms
- The descriptor declares this endpoint, but the boot protocol does not use it [unverified]

**Endpoint 2 - EP2 IN, bulk (7 bytes)**: `07 05 82 02 40 00 00`

- Max packet 64, device to host, the upload path

**Endpoint 3 - EP3 OUT, bulk (7 bytes)**: `07 05 03 02 40 00 00`

- Max packet 64, host to device, the command and data path

## Bus Reset Handling

On a USB bus reset interrupt (INTRUSB bit 2), the bootrom:

1. Clears FADDR to 0, the un-addressed state.
2. Sets POWER = 1 to resume from suspend.
3. Enables the interrupt masks: INTRUSBE = 0xF7, INTRTX1E = 0x05 (EP0 and EP2), INTRRX1E = 0x0A (EP1 and EP3).
4. Configures EP2 IN with max packet 512 and TX mode.
5. Configures EP3 OUT with max packet 512 and RX mode.
6. Resets INDEX to 0.

The registers hold a max packet of 512, but the USB 1.1 full-speed bus still limits each packet to 64 bytes.

## EP0 Control Transfers

The bootrom handles three standard USB requests on EP0:

| bRequest | Code | Handling |
| --- | --- | --- |
| SET_ADDRESS | 5 | Sends a zero-length status stage, waits for TX completion for up to 10000 polls, then writes the new address to FADDR |
| GET_DESCRIPTOR | 6 | Returns the device descriptor or the configuration descriptor |
| SET_CONFIGURATION | 9 | Sends a zero-length status stage |

EP0 data transfers use 16-byte chunks. For a response longer than 16 bytes, the dispatcher sends one chunk per EP0 TX interrupt until the full response is out. It then sends a final ZLP status stage (CSR0 = 0x48).

### EP0 Transmit Path

No EP0 payload passes through the FIFO port. Each packet stages in the L2 window at 0x48001500, where the incoming SETUP packet also lands. The write to the FIFO port at 0x70000020 only advances the hardware pointer. The staged region always takes a full 16 bytes, however short the packet is, and `EP0_TX_COUNT` bounds what reaches the wire.

```
USB_FORBID_WRITE     |= 1          gate L2 writes for EP0
                                   non-final packet: 4 word writes to 0x48001500,
                                   each followed by a word write to the FIFO port
                                   final packet: one byte write to the FIFO port per
                                   payload byte, then 4 word writes to 0x48001500
USB_EP0_TX_COUNT      = length     0x70000330, mirrored into both halfwords on read
USB_START_PRE_READ   |= 1
CSR0                  = 0x02       TxPktRdy
CSR0                  = 0x08       DataEnd, final packet only, a second write
USB_FORBID_WRITE     &= ~1
```

The FIFO port writes must add up to the payload length in the access width in use. A 16-byte packet takes four word writes. A short final packet takes one byte write per payload byte.

The final packet takes two separate stores to CSR0, `0x02` then `0x08`, at 0x4590 and 0x45A0. A write of `0x08` alone stages the packet but never puts it on the wire. The host then times the transfer out, and the core reports SETUPEND on the next EP0 interrupt. A decompiler that does not treat the register as volatile drops the first store, because the second one overwrites it immediately.

The zero-length status stage is a single write of `0x48` (ServicedRxPktRdy | DataEnd), with none of the staging above.

## USB IRQ Dispatcher

The main loop polls SYSCTRL+0xCC bit 25, then calls `usb_irq_dispatch()`:

1. **Reset** (INTRUSB bit 2): call `usb_handle_bus_reset()`.
2. **EP0** (INTRTX1 bit 0): handle the setup, status and data stages.
3. **EP2 IN** (INTRTX1 bit 2): if TX is complete and `usbboot_tx_ctx_t` still holds data, send the next 64-byte chunk. On an underrun or a stall, log it and clear it. When remaining = 0 and the `active` flag = 1, send a final ZLP and clear the flag.
4. **EP3 OUT** (INTRRX1 bit 3): call `handle_usbboot_packet()` to parse the received data, then clear RXCSR1 bit 0.

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

A received packet counts as a command frame only when **both** magic values match **and** the first 28 bytes, 7 words, are all 0x60. A packet that fails either check counts as data, during an active download session.

The ROM does not check that the received byte count (`RXCOUNT`) equals 64. The 64-byte frame size is a host-side rule, and the ROM relies only on the magic values and the sync pad. A shorter packet whose copied words happen to pass both checks therefore parses as a command. Its opcode, address and argument fields then come from whatever the stack `cmd_pkt` buffer already held.

### Opcodes

| Opcode | Name | Fields Used | Description |
| --- | --- | --- | --- |
| 0x3F | DOWNLOAD_BEGIN | addr = destination, arg0 = byte count | Start a download session. Later non-command packets go to `addr`, one after the other. |
| 0x3C | DOWNLOAD_DONE | (none) | End the download session and reset the progress counter |
| 0x1F | WRITE32 | addr = target, arg1 = value | Write a 32-bit value to `addr`, read it back, and print the result on the UART |
| 0x7F | UPLOAD_BEGIN | addr = source, arg0 = byte count | Start an upload of `arg0` bytes from `addr` to the host through EP2 IN |
| 0x9F | EXECUTE | addr = branch target | Clear EP3 RXCSR1 bit 0 (`RXPKTRDY`) and call `addr` as a function (see below) |

Any other opcode resets the command state to idle (NONE).

### Download Data Flow

A DOWNLOAD_BEGIN command puts the device into the download state. Each later EP3 OUT packet that does not match the command frame signature counts as raw payload data. The data goes to `cmd_state.addr + cmd_state.progress` through 32-bit word stores (`STR`), in a loop that adds 4 to a byte index while the index is less than `rx_count`. The `progress` counter then advances by the exact USB RX byte count. A DOWNLOAD_DONE command, or any new command frame, ends the session.

**Alignment caveat**: each iteration writes a full 32-bit word. A packet whose byte count is not a multiple of 4 therefore makes the last store overwrite up to 3 bytes past the received data. Those extra bytes hold whatever the matching word of the stack `cmd_pkt` buffer held. The `progress` counter still advances by the exact `rx_count`, thus later packets overwrite the extra bytes. No packet follows the last one before DOWNLOAD_DONE. The 1 to 3 trailing bytes at the end of the downloaded region therefore keep the stale `cmd_pkt` content.

### EP3 Receive Path and L2BUF_01

The USB hardware writes every EP3 OUT packet, command frame or raw data, into L2BUF_01 at 0x48000200 by DMA. The bootrom then copies the data from L2BUF_01 into a local stack variable (`cmd_pkt`) before it parses the packet. Every incoming USB packet therefore overwrites 0x48000200-0x4800023F, whatever the protocol state is.

**Consequence for a download to 0x48000200**: when DOWNLOAD_BEGIN targets address 0x48000200, every later data packet overwrites the first 64 bytes. The DOWNLOAD_DONE and EXECUTE command frames overwrite them too. After the EXECUTE command, 0x48000200-0x4800023F holds the EXECUTE command frame, which starts with 28 bytes of 0x60 sync_pad, not the intended payload. Load code for execution from the L2 buffer at **0x48000240 or higher** to avoid this corruption.

### Upload Data Flow

An UPLOAD_BEGIN command starts a stream from the given memory address through EP2 IN. The data goes out in 64-byte chunks. The final chunk, with remaining <= 64, sets `EP2_TX_COUNT` to the exact remaining byte count, but it stages only `remaining >> 2` whole words into L2BUF_00. After the last chunk, the bootrom sends a zero-length packet if the `active` flag is still set, which signals the end of the transfer.

**Alignment caveat**: if the total upload length is not a multiple of 4, the TX count of the terminal packet includes the fractional bytes. The copy from the source into the L2 staging buffer moves only `floor(remaining / 4)` words. The `remaining & 3` tail bytes that reach the host are stale content from an earlier transfer, still present in L2BUF_00. They are not data from the requested source address.

### State Structures

**`usbboot_cmd_state_t`** (16 bytes):

| Offset | Field | Description |
| --- | --- | --- |
| 0x00 | mode | Download-active discriminator, or last non-download marker |
| 0x04 | addr | Target or source address |
| 0x08 | arg0 | Byte count or value |
| 0x0C | progress | Bytes transferred so far, download only |

The bootrom tests only `mode == 0x3F` later, and that value means an active download. It stores `0x1F` for WRITE32 and for EXECUTE both, thus this field is not a strict copy of the last opcode.

**`usbboot_tx_ctx_t`** (16 bytes):

| Offset | Field | Description |
| --- | --- | --- |
| 0x00 | remaining | Bytes remaining to send |
| 0x04 | offset | Current offset from `base_addr` |
| 0x08 | active | 1 = upload in progress |
| 0x0C | base_addr | Source address of the upload |

`offset` and `remaining` track the upload progress. UPLOAD_BEGIN writes `base_addr` before the first use, because `usbboot_main_loop` zeroes only the first three fields at startup.

### EXECUTE Mechanism

The EXECUTE handler uses a manual function-call sequence, not a tail branch:

```
4154  MOV  LR, PC                    ; LR = 0x415C (return address)
4158  LDR  PC, [R11,#-0xC+var_70]    ; PC = exec_addr
415c  B    locret_41F8               ; reached if stub returns
```

LR holds the instruction after the branch, thus the called code **can return** to the bootrom with `MOV PC, LR` or an equivalent. When the stub returns, execution continues in the epilogue of `handle_usbboot_packet`, and the USB command loop of the bootrom continues as normal.

The stub inherits the stack pointer of the bootrom. At the EXECUTE call, SP is inside the bootrom call chain (`usbboot_main_loop` -> `usb_irq_dispatch` -> `handle_usbboot_packet`), in L2 buffer SRAM around 0x48000E70. The initial SP at `bootrom_entry` is **0x48000FFC** for USB boot mode, or **0x4800157C** before the SPI and NF boot probes.

## Bulk IN Transfer Details

`usb_bulk_in_send_next_chunk()` handles the L2 buffer staging for EP2:

1. Select EP2 through the INDEX register.
2. Compute the source pointer: `base_addr + offset`.
3. If remaining > 64: copy 64 bytes from the source to L2BUF_00 (0x48000000) and write to USB FIFO EP2 after each word. Set EP2_TX_COUNT = 64. Trigger the pre-read. Set TXCSR1 bit 0 (TX ready). Subtract 64 from remaining and add 64 to offset.
4. If remaining <= 64 and non-zero: copy `remaining >> 2` whole words from the source to L2BUF_00, at word granularity only. Set EP2_TX_COUNT to the exact `remaining` value. Reset offset and remaining to 0 after the send.
5. If remaining = 0: send a ZLP (TXCSR1 = 1 with no data). Reset the state.

The write-forbid register (USB+0x338) toggles to gate L2 buffer writes during the staging.

The rule about the FIFO port writes holds for EP2 as well, and `EP2_TX_COUNT` does not override it. Word writes that stage a length which is not a whole number of words round the packet up to the next word boundary. The trailing bytes then carry whatever the staging window held, even with `EP2_TX_COUNT` set to the exact length. One byte write to the FIFO port per payload byte gives the exact length instead. The measurement requested every reply length from 5 to 136 bytes and compared what arrived at the host. Word staging returned `ceil(len/4)*4` bytes, and byte staging returned `len` bytes. The bootrom never meets this case, because its own bulk transfers are whole words.

## Bootrom Errata

### EP0 MaxPacketSize incompatible with the XHCI initial endpoint context

The device descriptor declares `bMaxPacketSize0 = 16`, and the bootrom sends 16-byte packets on EP0 from the first data transaction. The XHCI specification (4.3) requires the host controller to set the MaxPacketSize of the Default Control Endpoint to **8** for a full-speed device. The host cannot learn the real max packet size until it reads byte 7 of the device descriptor and updates the endpoint context. The first packet already breaks the 8-byte assumption.

A 16-byte EP0 DATA packet on an endpoint configured for 8 raises a **babble error** in the XHCI hardware. The device sent more data than the endpoint allows per transaction. The host then aborts the enumeration at once. It reports the device descriptor as invalid and tears the device down before it registers the device for driver matching.

The real defect is that `usb_handle_get_descriptor` ignores `wLength`. A declared `bMaxPacketSize0 = 16` is legal and workable by itself. A host that asks for an 8-byte descriptor fragment must get an 8-byte short packet, which stays inside the 8-byte assumption. Only then does the host learn the real max packet size and enlarge the endpoint context. The bootrom answers that fragment read with a full 16 bytes instead. A replacement device that declares 16 and honors `wLength` enumerates on macOS, which we confirmed on hardware.

Before `SET_ADDRESS` sets `reply->status` to 1, the handler also returns only **16 of the 18** device descriptor bytes. It drops `iSerialNumber` and `bNumConfigurations`, thus the host sees a truncated descriptor even when it tolerates the oversized packet.

Linux and Windows XHCI implementations tolerate this in practice. They likely use a larger initial max packet size, or they recover from the babble during the first enumeration. The macOS XHCI driver keeps strictly to the specification and fails.

**Affected platforms**: macOS, confirmed on Apple Silicon with USB-C, macOS 26.4. Linux and Windows are not affected.

**Workaround**: Use [openNBOOT](../../baremetal/opennboot) with [gdbstub](../../baremetal/gdbstub) as a replacement. For USB boot, use a Linux or Windows host. 

### SET_CONFIGURATION does not reset the data toggles

The USB 2.0 specification (9.1.1.5) requires the device to reset all endpoint data toggles to DATA0 when it processes a SET_CONFIGURATION request. The AK7802 bootrom does not do this. The SET_CONFIGURATION handler, opcode 9 in `usb_handle_setup_request`, only sends a status-stage ZLP and returns. Neither `usb_handle_bus_reset` nor `usb_configure_endpoint_maxpacket` writes the ClrDataTog bits, TXCSR1 bit 6 and RXCSR1 bit 7.

**Consequence**: the host can reset its own data toggles, for example when a new process calls `set_configuration()`. The host-side toggles then return to DATA0 while the device-side toggles keep the value from the previous session. The device then ACKs the next bulk OUT packet but discards it without a message. From the host side the device stops responding after the first successful session.

**Workaround**: issue a USB bus reset (`usb.core.Device.reset()`) before `set_configuration()`. A bus reset resets the data toggles in hardware on both sides.

### WRITE32 reads the value from arg1, not arg0

The name of the WRITE32 opcode (0x1F) suggests that the value belongs in `arg0`, at packet offset 0x36. The bootrom instead extracts the write value from `arg1`, at packet offset 0x3A. The extraction uses the same split-halfword pattern as the address field. The low 16 bits come from bytes 0x3A-0x3B, and the high 16 bits from bytes 0x3C-0x3D.

A value in `arg0` therefore writes 0 to the target address, because `arg1` defaults to 0.

### The EXECUTE return path can drop an EP3 OUT packet

`usb_irq_dispatch()` snapshots the EP3 `RXCSR1` before it calls `handle_usbboot_packet()`. After the handler returns, it writes that **pre-saved** snapshot back with bit 0 cleared, in every case:

```
3cc0  STRB  R2, [R3]   ; INDEX = 3
3cd4  STRB  R3, [R2]   ; RXCSR1 = saved_rxcsr1 & 0xFE
```

The EXECUTE path inside `handle_usbboot_packet()` also clears `RXCSR1.bit0` before it jumps to the uploaded stub:

```
412c  STRB  R2, [R3]   ; INDEX = 3
4148  STRB  R3, [R2]   ; RXCSR1 &= ~1
4158  LDR   PC, [R11,#-0xC+var_70]
```

If the stub returns to the bootrom, and a new EP3 OUT packet arrived while the stub ran, the hardware can set `RXPKTRDY` in the meantime. The stale write at `0x3CD4` then clears that new bit without another call to `handle_usbboot_packet()`, thus the packet that just arrived is lost.

This is a real lost-packet window in the return path, not a naming artifact.

**Workaround**: if an EXECUTE stub returns, the host must not send the next EP3 OUT packet until it knows that the stub finished. A fixed delay works only when it safely exceeds the worst-case runtime of the stub. An out-of-band completion signal, over UART or GPIO or another side effect, is more reliable. A stub that never returns avoids this issue completely.
