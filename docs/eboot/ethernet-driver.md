# Ethernet Driver

**The AIPC board carries no ENC28J60.** Its Ethernet controller is a Davicom DM9000A on a software-timed parallel bus. See [docs/nk/dm9000-driver.md](../nk/dm9000-driver.md). The code below is present in EBOOT and works as written, but nothing on this board answers it. We document it because it is the only ENC28J60 reference in this firmware, and because other code reuses the SPI2 usage patterns that it establishes.

Three independent points confirm this. The board schematic places a DM9000 at designator `U9`, with no second Ethernet part. The NK BINFS file table holds `dm9000x.dll` and no ENC28J60 driver. Cold-boot RAM captures from both units show `DM9000X` as a live NDIS miniport. The backend selection of EBOOT also never reaches the ENC28J60 path unless PTB field `+0x28` is non-zero.

EBOOT talks to a Microchip ENC28J60 SPI Ethernet controller on the SPI2 bus of the AK7802. It uses a compact driver layer, plus a small Ethernet dispatch block behind `OEMEthSendFrame` and `OEMEthGetFrame`. It then runs a `BOOTME`, EDBG and TFTP download state machine.

This document covers:

1. SPI2 register usage from the view of the ENC28J60 driver
2. The internal ENC28J60 register addressing scheme
3. The driver function layer: init, control-register read and write, bank select, TX and RX
4. The Ethernet HAL dispatch block that reaches the driver backends
5. The `BOOTME`, EDBG and TFTP download state machine
6. The hardcoded default network configuration

## SPI2 Usage

SPI2 lives at physical `0x20024000`. See [memory-map.md](memory-map.md) for the full register map. This section covers only the bit-level usage patterns in the ENC28J60 driver.

The ENC28J60 driver runs one SPI transaction per control-register access, in this pattern:

```
1. Clear SPI_CTRL bit 0      (CS asserted, active low)
2. Set   SPI_CTRL bit 1      (direction = write)
3. Set   SPI_CTRL bit 5      (transfer enable)
4. Write SPI_COUNT = 1       (single byte burst)
5. Write SPI_TXDATA = opcode_byte
6. Poll  SPI_COUNT == 0      (TX drained)
7. Poll  SPI_STATUS bit 8    (transfer complete)
8. Write SPI_COUNT = 1       (next single byte)
9. Write SPI_TXDATA = data_byte
10. Same drain/complete polling
11. Clear SPI_CTRL bit 5     (transfer end, CS deasserted)
```

A bulk transfer, the buffer read and buffer write opcodes, holds the transfer open and pumps data 4 bytes at a time through `SPI_TXDATA` and `SPI_RXDATA`. It polls `SPI_STATUS` bit 2, TX FIFO has space, or bit 6, RX data available, to flow-control the burst. The 4-byte-wide `SPI_TXDATA` port takes word-aligned bulk writes with no per-byte overhead.

SPI clock setup during init:

```
cpu_clk = cpu_clock_get()              // 248 MHz on typical config
div = cpu_clk / 20_000_000 - 1         // target 10 MHz
if (div < 2) div = 2
if (cpu_clk / (2 * (div + 1)) > 10_000_000) div++
SPI_CTRL = (div << 8) | 0x52
SPI_CONFIG2 = 0xFFFFFF                 // purpose unknown, written once
```

For a 248 MHz CPU clock the code first computes `div = 11`, then raises it to `12`, because `248 / (2 * (11 + 1)) = 10.33 MHz` still exceeds the 10 MHz target. The programmed SPI clock is therefore `248 / (2 * (12 + 1)) = 9.54 MHz`.

The `0x52` bits in the low byte are `bit 1 | bit 4 | bit 6`. Their exact mode-selection meaning is `[partial]`, but this is the value that EBOOT uses for all ENC28J60 traffic, and a port must keep it.

## ENC28J60 Register Addressing

The physical register space of the ENC28J60 has 4 banks of up to 32 registers each. A bank selection needs a write to the `BSEL[1:0]` bits in `ECON1`, bank 0 register `0x1F`. A read of a MAC or MII register needs a dummy byte between the address and the data byte. A pure Ethernet register does not.

The EBOOT driver hides the bank and MII machinery behind an 8-bit "caller-side register number" with three fields:

```
bit 7:      MAC/MII register flag (1 = read needs dummy byte)
bits 6:5:   bank number (0..3)
bits 4:0:   5-bit register address within the bank
```

For example, the caller-side value `0xC2` decodes as the MAC/MII flag set, bank 2, register 2, which is `MACON3`. A write of `0xC2` with a value through the driver switches to bank 2 and then sends a standard WCR transaction for register 2.

The bank-switch helper extracts bits `6:5` of the caller-side register number and compares them against a software cache. On a difference it issues a `BFC` and `BFS` pair against `ECON1` to reprogram `BSEL[1:0]`. The cached bank-select bits live at `0x80104A60` in `.data`, as the masked values `0x00`, `0x20`, `0x40` or `0x60`.

### SPI Opcode Prefixes

The ENC28J60 SPI protocol uses a 3-bit opcode in the top bits of the first byte, then the 5-bit register address in the low bits. The driver builds an SPI byte as `(reg_addr & 0x1F) | opcode_prefix`, where `opcode_prefix` is one of:

| Prefix | Opcode name | Description                 |
| ------ | ----------- | --------------------------- |
| 0x00   | RCR         | Read Control Register       |
| 0x40   | WCR         | Write Control Register      |
| 0x80   | BFS         | Bit Field Set               |
| 0xA0   | BFC         | Bit Field Clear             |
| 0x3A   | RBM         | Read Buffer Memory (fixed)  |
| 0x7A   | WBM         | Write Buffer Memory (fixed) |
| 0xFF   | SRC         | System Reset Command        |

The RBM and WBM opcodes are fixed. They do not combine with a register address, and the `0x1A` in the low 5 bits is part of the opcode.

The driver exposes two entry points around these: `enc28j60_wcr(opcode_prefix, caller_side_reg, value)` and `enc28j60_rcr(opcode_prefix, caller_side_reg)`. Most callers use `opcode_prefix = 0x40` (WCR) for a write and `0x00` (RCR) for a read. Some use `0x80` (BFS) and `0xA0` (BFC) for bit manipulation without a read-modify-write.

## Driver Function Layer

### `enc28j60_init(mac_bytes)`

Full initialization of the SPI bus and the ENC28J60 chip. This function is the INIT entry in the Ethernet HAL dispatch block. It always targets SPI2 and hard-codes the SPI2 base through `OALPAtoVA(0x20024000, 0)`, whatever the vtable registration passes as a `base` argument. The sequence:

1. Set up the SPI2 clock divider, `SPI_CTRL` and `SPI_CONFIG2`, as above.
2. Emit a raw SRC byte (`0xFF`) over SPI to software-reset the chip.
3. Poll `ESTAT.CLKRDY`, bank 0 register `0x1D` bit 0, until it sets.
4. Read `EREVID`, bank 3 register `0x12`, and print it through OALMSG.
5. Program the receive buffer range:
   - `ERXSTL:ERXSTH = 0x0600` (RX buffer start)
   - `ERXNDL:ERXNDH = 0x1FFF` (RX buffer end, end of ENC28J60 SRAM)
   - `ERXRDPTL:ERXRDPTH = 0x0600` (RX read pointer)
   - `ERDPTL:ERDPTH = 0x0000`
6. Cache the initial next-packet pointer `0x0600` in software at `0x800F36B0`.
7. Program the MAC registers, bank 2:
   - `MACON1 = 0x0D` (MARXEN | TXPAUS | RXPAUS)
   - `MACON2 = 0x00`
   - `MACON3 = old | 0x32` (TXCRCEN | PADCFG=01 | FRMLNEN)
   - initial `MABBIPG = 0x12`
   - `MAIPGL:MAIPGH = 0x12, 0x0C`
   - `MAMXFLL:MAMXFLH = 0xEE, 0x05` (1518 bytes max frame)
8. Write the 6-byte MAC address into `MAADR0..MAADR5` in reverse order. The chip stores MAADR with the first byte in the highest-numbered register.
9. Issue an MII write with `MIREGADR = 0x10`, `MIWRL = 0x00`, `MIWRH = 0x01`.
10. Poll `MIISTAT.BUSY` until the MII write completes.
11. Call a duplex helper. It reads a PHY register through the MII path, sets `MACON3.FULDPX` to match, and rewrites `MABBIPG` to `0x15` for full duplex or `0x12` for half duplex.
12. Enable the interrupts through an `EIE` BFS, bits `INTIE | PKTIE = 0xC0`, and set `ECON1.RXEN` through a BFS of `0x04`.

### `enc28j60_wcr` / `enc28j60_rcr`

The two SPI transaction helpers above. Both take a caller-side register number and run the bank switch plus the SPI transaction. `enc28j60_rcr` handles the MAC and MII dummy-byte requirement when bit 7 of the caller-side register number is set. It issues an extra dummy read cycle before it reads the real byte.

### `enc28j60_bank_select`

The bank-switch helper before every register access. See _ENC28J60 Register Addressing_ above for the behavior.

### `enc28j60_tx_frame(buf, len)`

Transmits one Ethernet frame. The sequence:

1. Program `EWRPTL:EWRPTH = 0x0000`, the write pointer to the start of the TX buffer in ENC28J60 SRAM.
2. Program `ETXNDL:ETXNDH = len`, the end of the TX region.
3. Issue WBM, opcode `0x7A`, with the first byte `0x00`. That per-packet control byte means "use the MAC defaults".
4. Stream `len` bytes of frame data into `SPI_TXDATA`, and poll `SPI_STATUS` bit 2 between words.
5. Issue an `ECON1` BFS with `TXRTS = 0x08` to request the transmission.
6. Poll `ECON1.TXRTS` until the bit clears, which means TX done.
7. Read `ESTAT.TXABRT`. If it is set, program `ERDPT = len + 1`, read the 8-byte transmit status vector, print a diagnostic, clear `ESTAT` bits `0x12` through `BFC`, and restart the transmit sequence.
8. On success, return 0. This function has no visible bounded retry count. It keeps looping on the `TXABRT` path until a transmit completes without that flag.

### `enc28j60_rx_poll(buf_out, len_out)`

Non-blocking frame receive. The sequence:

1. Check `EIR.PKTIF`, bank 0 register `0x1C` bit 6. If it is clear, return 0, because no frame is available.
2. Check `EPKTCNT`, bank 1 register `0x19`. If it is zero, return 0.
3. Program `ERDPT` to the cached next-packet pointer at `0x800F36B0`.
4. Use RBM, opcode `0x3A`, to read 6 bytes: 2 bytes of next-packet pointer, 2 bytes of byte count, and 2 bytes of receive status.
5. Update the cached next-packet pointer.
6. Extract the byte count from the middle 2 bytes of that header.
7. Use RBM again to read the frame payload into `buf_out`.
8. Program `ERXRDPT` to the new next-packet pointer value.
9. Issue an `ECON2` BFS with `PKTDEC = 0x40` to decrement the packet count.
10. Store and return the received length. For a byte count greater than 4, subtract the 4-byte FCS. Otherwise use the raw byte count.

### `enc28j60_read_buffer_bulk`

The inner helper that `enc28j60_rx_poll` uses to pull bytes out of ENC28J60 SRAM, 4 at a time through `SPI_RXDATA`, with the RX FIFO flow control from _SPI2 Usage_ above.

## Ethernet HAL Dispatch Block

EBOOT keeps a small Ethernet HAL dispatch block in `.bss` at `0x80106E40..0x80106E60`. The registration paths write these slots:

| Offset from 0x80106E40 | Purpose |
| --- | --- |
| +0x00 | `[unknown]` |
| +0x04 | backend-specific helper; only the RNDIS path fills it in audited code |
| +0x08 | always 0 |
| +0x0C | RECV function (dispatched by OEMEthGetFrame) |
| +0x10 | miscellaneous helper |
| +0x14 | INIT function (called on driver bringup) |
| +0x18 | SEND function (dispatched by OEMEthSendFrame) |
| +0x1C | miscellaneous helper |
| +0x20 | backend-private field, zero-initialized by both registration paths |

A **driver registration function** fills the dispatch block. It takes a MAC address pointer and a state struct. `fmd_read_partition_table` does **not** probe both backends and fall back on its own. It reads BOOTARGS `0xA0020844`, copied there from PTB header field `+0x28`, and selects exactly one registration path:

- `0`: Bulverde RNDIS / `AKUSB`
- non-zero: `ENC28J60`

If the selected registration path fails, `fmd_read_partition_table` returns failure. The same call path never retries the other backend.

### ENC28J60 Registration (selected when the transport is non-zero)

It fills the dispatch block with the RECV, SEND and INIT entries of the ENC28J60 driver, and writes `0x20024000` into two state-struct fields, at offsets `+0x0C` and `+0x10`. `enc28j60_init` then calls `OALPAtoVA(0x20024000, 0)` on its own and uses the resulting SPI2 virtual base for register access.

### Bulverde RNDIS Registration (selected when the transport is zero)

It tries to initialize a USB RNDIS Ethernet adapter on the MUSB USB host controller of the SoC, at physical `0x70000000`. On the AIPC no USB Ethernet dongle exists, and the init path fails with the OALMSG string `"ERROR: Failed to initialize Bulverde Rndis USB Ethernet controller."`. When this backend is selected and that init fails, `fmd_read_partition_table` fails. The same control flow makes no automatic retry on the ENC28J60 path.

This path appears here for completeness, and as a warning. The `0x70000000` base in the Ethernet code of EBOOT is **not** a second independent Ethernet controller. It is the USB host controller of the SoC, and the Ethernet HAL of EBOOT would have driven an RNDIS dongle through it, if one existed.

## `OEMEthSendFrame` and `OEMEthGetFrame`

These are the two standard WinCE OAL entry points. In EBOOT they are thin wrappers over the dispatch block:

```
OEMEthGetFrame(buf, len_ptr):
    return (uint16_t)vtable RECV slot(buf, len_ptr)

OEMEthSendFrame(buf, len):
    for retry in 1..4:
        if vtable SEND slot(buf, len) == 0: return 1
        OALMSG("INFO: OEMEthSendFrame: retrying send (%u)", retry)
    return 0
```

`OEMEthSendFrame` retries up to 4 times when the SEND slot returns a non-zero failure code. The ENC28J60 backend, `enc28j60_tx_frame`, returns `0` on success and handles `TXABRT` with an internal retry, thus the wrapper-level retries normally do not run on that path.

`OEMEthGetFrame` does not touch `R0` or `R1`. Callers pass a receive buffer and a length pointer straight through to the RECV slot. In `EbootSendBootmeAndWaitForTftp` those are `0x800F5128` and a stack `uint16_t` set to `0x05F0`.

`OEMEthGetFrame` is non-blocking. The caller must loop and poll. The download state machine polls it directly.

## Download State Machine

The download path of EBOOT is one state machine. `check_update_eboot_request()` reaches it on the KITL and network-download path, after the PTB and boot-menu logic decides to stay in EBOOT. The normal flash `NK` boot path does not enter this loop.

The machine:

1. Broadcasts `BOOTME` UDP packets at a fixed interval.
2. Polls incoming Ethernet frames through `OEMEthGetFrame`.
3. Dispatches on the EtherType: `0x0806` (ARP) or `0x0800` (IP).
4. For an IP frame, runs the IP parser helper, then the EDBG helper, then the TFTP receive helper if the EDBG helper does not consume the packet.
5. Returns when the exit flag at `0x800F5718` becomes non-zero.

Pseudo-code of the main loop:

```c
int EbootSendBootmeAndWaitForTftp(nic_state, ...) {
    int next_bootme = now() - 3;
    int bootme_count = 0;
    uint8_t *rx_buf = (uint8_t *)0x800F5128;
    uint16_t pkt_len;

    while (!MEMORY[0x800F5718]) {
        // Send BOOTME every 3 seconds, up to 40 retries
        if (bootme_count < 40 && now() - next_bootme >= 3) {
            ++bootme_count;
            next_bootme += 3;
            SendBootme(nic_state, ...);
        }

        pkt_len = 0x05F0;
        if (!OEMEthGetFrame(rx_buf, &pkt_len))
            continue;

        uint16_t ethertype = ntohs(*(uint16_t *)(rx_buf + 12));
        if (ethertype == 0x0806) {   // ARP
            if (sub_8005D700(...) == 3)
                return 0;
        } else if (ethertype == 0x0800) {   // IP
            if (!sub_8005DC00(...)) {
                if (!sub_8005EB20(...))
                    sub_8005D310(...);
            }
        }
    }
    return 1;
}
```

The fixed RX frame buffer base for `OEMEthGetFrame` is `0x800F5128` in `.data`. The state machine then reads the EtherType from `0x800F5134`, which is `rx_buf + 12`.

### TFTP / EDBG Port

The TFTP opener uses UDP port `0xD403 = 54275` for both the source and the destination:

```
sub_8005BF50(src_addr, 0xD403, 0xD403, filename);
```

This call site uses `0xD403`, not UDP port 69.

### EDBG Commands

`sub_8005EB20` first checks that the payload starts with `"EDBG"` and that byte 4 is `0xFF`. Byte 7 currently decodes into two command values:

| Byte 7 | Meaning   |
| ------ | --------- |
| 0x02   | `JUMPIMG` |
| 0x03   | `CONFIG`  |

`JUMPIMG` sets the exit flag that the outer wait loop consumes. `CONFIG` logs `flags:0x%X` from byte 8 and replies through `sub_8005D988`.

### BOOTME Broadcast

`SendBootme` builds an `EDBG`-tagged BOOTME packet, writes the broadcast destination `255.255.255.255`, sets UDP port `0xD403`, and sends it through `sub_8005D988`. The success path logs `Sent BOOTME to %s`.

### DHCP

DHCP-related helpers and strings exist in EBOOT, but this document covers the default static-IP path below.

## Default Network Configuration

EBOOT hardcodes the factory default network parameters inside a function that writes **compile-time constants** straight into the in-RAM PTB header:

```
IP address   = 0x0B00A8C0 little-endian = 192.168.0.11
subnet mask  = 0x00FFFFFF little-endian = 255.255.255.0
gateway      = 0                          (disabled)
```

These literal immediates are the **factory-reset defaults**. `ptb_load_default_network_config` writes them into PTB offsets `+0x10`, `+0x14` and `+0x18` at `0x80106EA0`. See [partition-format.md](partition-format.md) for the header layout. If EBOOT later reloads a saved PTB snapshot from `CFG`, the active IP, mask and gateway values can come from that persisted PTB state instead.

### Runtime Network State

`eboot_download_file_tftp` copies the IP and the mask out of the in-RAM PTB header into a runtime network state area at virtual `0xA0020838..0xA002083C`, uncached DDR:

```
MEMORY[0xA0020838] = MEMORY[0x80106EB0]   // active IP
MEMORY[0xA002083C] = MEMORY[0x80106EB4]   // active subnet mask
```

That uncached virtual address maps to DDR physical `0x30020838`. It sits well inside the working memory of EBOOT, and it conflicts with neither the framebuffer (`0x33B00000`) nor the NK load address (`0x30200000`).

This document does not name the fields after the first two slots.

## Unresolved

- `SPI_CONFIG2` at SPI2 `+0x20`. Init writes `0xFFFFFF` once and never touches it again. The purpose is unknown.
- The `0x52` bit pattern in SPI2 `SPI_CTRL` during init. Bit 1 is the direction. Bit 4 and bit 6 are mode selectors with an unknown specific meaning. The value works for ENC28J60 traffic, and no experiment varies it.
- The `INT` output pin of the ENC28J60 goes to an AK7802 GPIO somewhere on the board, because EBOOT enables `PKTIE | INTIE` in the EIE register of the chip during init. EBOOT runs in polling mode and does not consume the interrupt, but a Linux driver would. This documentation does not identify the GPIO pin that receives `INT`.
- The `RST` pin of the ENC28J60, if it is connected, is also unidentified. EBOOT relies on the software SRC reset over SPI.
- The full BOOTME packet format that `SendBootme` emits is not tabulated here.
- The full set of EDBG commands on the RX path is not enumerated. `sub_8005EB20` decodes only command bytes `0x02` (`JUMPIMG`) and `0x03` (`CONFIG`).
- The runtime network state structure at `0xA0020830..` is only partly understood. The fields after the IP and the mask have no names.
- The MII write during init, `MIREGADR = 0x10` with value `0x0001`. The specific PHY register and what it programs are unidentified.
