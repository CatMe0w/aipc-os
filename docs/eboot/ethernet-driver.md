# Ethernet Driver

EBOOT talks to a Microchip ENC28J60 SPI Ethernet controller attached to
the AK7802's SPI2 bus. It uses a compact driver layer to implement the
standard WinCE OAL Ethernet HAL entry points (`OEMEthInit`,
`OEMEthSendFrame`, `OEMEthGetFrame`) and a state machine that broadcasts
`BOOTME` UDP packets to announce the device, then accepts a TFTP
download over the WinCE EDBG protocol.

This document covers:

1. SPI2 register usage from the ENC28J60 driver's perspective
2. The ENC28J60 register addressing scheme used internally
3. The driver function layer (init, control-register read/write, bank
   select, TX, RX)
4. The OEM Ethernet HAL vtable that dispatches to driver backends
5. The `BOOTME` + EDBG + TFTP download state machine
6. The hardcoded default network configuration

## SPI2 Usage

SPI2 lives at physical `0x20024000`. See
[memory-map.md](memory-map.md) for the full register map; this section
documents only the bit-level usage patterns that appear in the ENC28J60
driver.

The ENC28J60 driver performs one SPI transaction per control-register
access, with this pattern:

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

Bulk transfers (the buffer read and buffer write opcodes) hold the
transfer open and pump data 4 bytes at a time through `SPI_TXDATA` /
`SPI_RXDATA`, polling `SPI_STATUS` bit 2 (TX FIFO has space) or bit 6
(RX data available) to flow-control the burst. The 4-byte-wide
`SPI_TXDATA` port supports word-aligned bulk writes without per-byte
overhead.

SPI clock setup during init:

```
cpu_clk = cpu_clock_get()              // 248 MHz on typical config
div = cpu_clk / 20_000_000 - 1         // target 10 MHz
if (div < 2) div = 2
if (cpu_clk / (2 * (div + 1)) > 10_000_000) div++
SPI_CTRL = (div << 8) | 0x52
SPI_CONFIG2 = 0xFFFFFF                 // purpose unknown, written once
```

The `0x52` bits in the low byte are `bit 1 | bit 4 | bit 6` - their
exact mode-selection meaning is `[partial]`, but this value is the one
EBOOT uses for all ENC28J60 traffic and must be preserved.

The SPI clock is kept at or below 10 MHz even though the ENC28J60
datasheet allows up to 20 MHz. EBOOT's conservative choice is not
challenged here.

## ENC28J60 Register Addressing

The ENC28J60's physical register space is organized into 4 banks of up
to 32 registers each. Selecting a bank requires writing the `BSEL[1:0]`
bits in `ECON1` (bank 0 register `0x1F`). Reading MAC/MII registers
requires a dummy byte between the address and the data byte - pure
ethernet registers do not.

The EBOOT driver hides the bank-and-MII machinery behind an 8-bit
"caller-side register number" that encodes three fields:

```
bit 7:      MAC/MII register flag (1 = read needs dummy byte)
bits 6:5:   bank number (0..3)
bits 4:0:   5-bit register address within the bank
```

For example, caller-side `0xC2` decodes as MAC/MII flag set, bank 2,
register 2, which is `MACON3`. Writing `0xC2` with a value through the
driver does the bank switch to 2 and then sends a standard WCR
transaction for register 2.

The bank-switch helper extracts bits `6:5` of the caller-side register
number, compares against a software cache, and if different issues a
`BFC` / `BFS` pair against `ECON1` to reprogram `BSEL[1:0]`. The cached
bank value lives at `0x80106E60` in `.data`.

### SPI Opcode Prefixes

The ENC28J60 SPI protocol uses a 3-bit opcode in the top bits of the
first byte, followed by the 5-bit register address in the low bits. The
driver builds SPI bytes as `(reg_addr & 0x1F) | opcode_prefix`, where
`opcode_prefix` is one of:

| Prefix | Opcode name           | Description                  |
| ------ | --------------------- | ---------------------------- |
| 0x00   | RCR                   | Read Control Register        |
| 0x40   | WCR                   | Write Control Register       |
| 0x80   | BFS                   | Bit Field Set                |
| 0xA0   | BFC                   | Bit Field Clear              |
| 0x3A   | RBM                   | Read Buffer Memory (fixed)   |
| 0x7A   | WBM                   | Write Buffer Memory (fixed)  |
| 0xFF   | SRC                   | System Reset Command         |

RBM and WBM opcodes are fixed (they do not combine with a register
address); the `0x1A` in the low 5 bits is part of the opcode.

The driver exposes two entry points that wrap these:
`enc28j60_wcr(opcode_prefix, caller_side_reg, value)` and
`enc28j60_rcr(opcode_prefix, caller_side_reg)`. Most callers use
`opcode_prefix = 0x40` (WCR) for writes and `0x00` (RCR) for reads,
with occasional use of `0x80` (BFS) and `0xA0` (BFC) for bit
manipulation without read-modify-write.

## Driver Function Layer

### `enc28j60_init(mac_bytes)`

Full initialization of the SPI bus and the ENC28J60 chip. The function
is the INIT entry in the Ethernet HAL vtable. It always targets SPI2
and hard-codes the SPI2 base via `OALPAtoVA(0x20024000, 0)` regardless
of what the vtable registration passes as a `base` argument. The
sequence:

1. Set up the SPI2 clock divider, `SPI_CTRL`, and `SPI_CONFIG2` as
   described above.
2. Emit a raw SRC byte (`0xFF`) over SPI to software-reset the chip.
3. Poll `ESTAT.CLKRDY` (bank 0 register `0x1D` bit 0) until set.
4. Read `EREVID` (bank 3 register `0x12`) and print it via OALMSG.
5. Program the receive buffer range:
   - `ERXSTL:ERXSTH = 0x0600`  (RX buffer start)
   - `ERXNDL:ERXNDH = 0x1FFF`  (RX buffer end, end of ENC28J60 SRAM)
   - `ERXRDPTL:ERXRDPTH = 0x0600` (RX read pointer)
   - `ERDPTL:ERDPTH = 0x0000`
6. Cache the initial next-packet pointer `0x0600` in software at
   `0x800F36B0`.
7. Program the MAC registers (bank 2):
   - `MACON1 = 0x0D`   (MARXEN | TXPAUS | RXPAUS)
   - `MACON2 = 0x00`
   - `MACON3 = old | 0x32`  (TXCRCEN | PADCFG=01 | FRMLNEN)
   - `MABBIPG = 0x12`
   - `MAIPGL:MAIPGH = 0x12, 0x0C`
   - `MAMXFLL:MAMXFLH = 0xEE, 0x05` (1518 bytes max frame)
8. Write the 6-byte MAC address into `MAADR0..MAADR5` in reverse
   order (the chip stores MAADR with the first byte at the
   highest-numbered register).
9. Issue an MII write via `MIREGADR + MIWRL + MIWRH` to initialize
   the PHY (the specific PHY register written is `0x10`, value
   `0x0001`, which triggers a standard operation but is not
   independently documented here).
10. Poll `MIISTAT.BUSY` until the MII write completes.
11. Enable interrupts via `EIE` BFS (bits `INTIE | PKTIE = 0xC0`) and
    enable receive via `ECON1` BFS (bit `RXEN = 0x04`).

This is a textbook ENC28J60 bring-up sequence. Linux's mainline
`drivers/net/ethernet/microchip/enc28j60.c` performs substantially the
same steps in the same order.

### `enc28j60_wcr` / `enc28j60_rcr`

The two SPI transaction helpers described above. Both take a
caller-side register number and dispatch the bank switch + SPI
transaction. `enc28j60_rcr` handles the MAC/MII dummy-byte requirement
when bit 7 of the caller-side register number is set, by issuing an
extra dummy read cycle before reading the real byte.

### `enc28j60_bank_select`

The bank-switch helper called before every register access. See the
*ENC28J60 Register Addressing* section above for behavior.

### `enc28j60_tx_frame(buf, len)`

Transmits one Ethernet frame. The sequence:

1. Program `EWRPTL:EWRPTH = 0x0000` (write pointer to start of TX
   buffer in ENC28J60 SRAM).
2. Program `ETXNDL:ETXNDH = len` (end of TX region).
3. Issue WBM (opcode `0x7A`) with first byte `0x00` (per-packet
   control byte: use MAC defaults).
4. Stream `len` bytes of frame data into `SPI_TXDATA`, polling
   `SPI_STATUS` bit 2 between words.
5. `ECON1` BFS with `TXRTS = 0x08` to request transmission.
6. Poll `ECON1.TXRTS` until the bit clears (TX done).
7. Read `ESTAT.TXABRT`. If set, read the transmit status vector,
   print a diagnostic, clear the abort flag via `ECON1` BFC, and
   retry.
8. On success, return 0 (one retry loop at this level; the HAL
   wrapper adds another four retries on top).

### `enc28j60_rx_poll(buf_out, len_out)`

Non-blocking frame receive. The sequence:

1. Check `EIR.PKTIF` (bank 0 register `0x1C` bit 6). If clear, return
   0 (no frame available).
2. Check `EPKTCNT` (bank 1 register `0x19`). If zero, return 0.
3. Program `ERDPT` to the cached next-packet pointer `0x800F36B0`.
4. Use RBM (opcode `0x3A`) to read 6 bytes: 2 bytes next-packet
   pointer followed by a 4-byte status vector.
5. Update the cached next-packet pointer.
6. Extract the byte count from the status vector.
7. Use RBM again to read the frame payload into `buf_out`.
8. Program `ERXRDPT` to the new read pointer.
9. `ECON2` BFS with `PKTDEC = 0x40` to decrement the packet count.
10. Return frame length (byte count minus 4-byte FCS) in `*len_out`.

### `enc28j60_read_buffer_bulk`

The inner helper used by `rx_poll` to pull bytes out of ENC28J60 SRAM
4 at a time through `SPI_RXDATA`, with the RX FIFO flow control
described in *SPI2 Usage* above.

## Ethernet HAL Vtable

EBOOT maintains a 9-slot Ethernet HAL vtable in `.bss` at
`0x80106E40..0x80106E60`. The slots are:

| Offset from 0x80106E40 | Purpose                                    |
| ----------------------- | ------------------------------------------ |
| +0x00                   | `[unknown]`                                |
| +0x04                   | RX-ready helper (polled during main loop)  |
| +0x08                   | always 0                                   |
| +0x0C                   | RECV function (dispatched by OEMEthGetFrame) |
| +0x10                   | miscellaneous helper                       |
| +0x14                   | INIT function (called on driver bringup)   |
| +0x18                   | SEND function (dispatched by OEMEthSendFrame) |
| +0x1C                   | miscellaneous helper                       |
| +0x20                   | cached ENC28J60 bank number                |

The vtable is populated by a **driver registration function** that
takes a MAC address pointer and a state struct. EBOOT contains two
driver registration functions, only one of which is expected to
succeed at a time:

### ENC28J60 Registration (primary on AIPC)

Populates the vtable with the ENC28J60 driver's RECV, SEND, and INIT
entries, and writes `0x20020000` (SPI0 physical base) into a state
struct field as a bus-handle placeholder. The actual SPI base used by
`enc28j60_init` is then hardcoded to SPI2 `0x20024000` inside the init
function, so the state struct value is not consulted for register
access.

### Bulverde RNDIS Registration (fallback, unused on AIPC)

Attempts to initialize a USB RNDIS Ethernet adapter attached to the
SoC's MUSB USB host controller at physical `0x70000000`. On AIPC, no
USB Ethernet dongle is attached, and the init path fails with the
OALMSG string `"ERROR: Failed to initialize Bulverde Rndis USB
Ethernet controller."`. The driver registration function then returns
failure and the ENC28J60 path is tried instead.

This path is mentioned here for completeness and as a warning that the
`0x70000000` base in EBOOT's ethernet code is **not** a second
independent Ethernet controller - it is the SoC's own USB host
controller, which EBOOT's ethernet HAL would have driven a RNDIS
dongle through if one were present.

## `OEMEthSendFrame` and `OEMEthGetFrame`

These are the two standard WinCE OAL entry points, and in EBOOT they
are thin wrappers over the HAL vtable:

```
OEMEthGetFrame(): call vtable RECV slot, return as 16-bit frame length
OEMEthSendFrame(buf, len):
    for retry in 1..4:
        if vtable SEND slot succeeds: return 1
        OALMSG("INFO: OEMEthSendFrame: retrying send (%u)", retry)
    return 0
```

`OEMEthSendFrame` adds four retries on top of the one-retry inner loop
inside `enc28j60_tx_frame`, giving up to 5 total TX attempts per
logical call.

`OEMEthGetFrame` is non-blocking: callers are responsible for looping
and polling. The download state machine polls it directly.

## Download State Machine

The EBOOT download path is a single state machine that:

1. Broadcasts `BOOTME` UDP packets on a fixed interval.
2. Polls incoming Ethernet frames through `OEMEthGetFrame`.
3. Dispatches on EtherType: `0x0806` (ARP) or `0x0800` (IP).
4. Once a UDP packet matching the EDBG port arrives, hands off to the
   TFTP state machine to receive the kernel image.
5. Returns when the TFTP download completes.

Pseudo-code of the main loop:

```c
int EbootSendBootmeAndWaitForTftp(nic_state, ...) {
    int next_bootme = now() - 3;
    int bootme_count = 0;

    while (!tftp_started_flag) {
        // Send BOOTME every 3 seconds, up to 40 retries
        if (bootme_count < 40 && now() - next_bootme >= 3) {
            ++bootme_count;
            next_bootme += 3;
            SendBootme(nic_state, ...);
        }

        uint16_t pkt_len = OEMEthGetFrame();
        if (!pkt_len) continue;

        uint16_t ethertype = ntohs(*(uint16_t *)(rx_buf + 12));

        if (ethertype == 0x0800) {          // IP
            if (ProcessIP(...) == 0) {
                if (ProcessUDP(...) == 0) {
                    TftpStateMachine(...);
                }
            }
        } else if (ethertype == 0x0806) {   // ARP
            if (ProcessARP(...) == 3) {
                OALMSG("Some other station has IP Address: ... Aborting");
                return 0;
            }
        }
    }
    return 1;
}
```

The RX frame buffer that `enc28j60_rx_poll` writes into is at
`0x800F5134` in `.data`. The state machine reads EtherType and
subsequent protocol fields from that buffer directly.

### TFTP / EDBG Port

The TFTP opener uses UDP port `0xD403 = 54275` for both source and
destination:

```
sub_8005BF50(src_addr, 0xD403, 0xD403, filename);
```

This is **not standard TFTP on port 69**. Port `0xD403` is the WinCE
EDBG-over-TFTP convention used between Platform Builder and EBOOT.
Using a private port lets the Platform Builder-initiated flow coexist
with other TFTP traffic and makes wire captures easy to filter.

### EDBG Commands

OALMSG strings confirm that EBOOT implements at least two EDBG
command handlers:

```
Got EDBG_CMD_CONFIG, flags: 0x%X
Got EDBG_CMD_JUMPIMG
```

`EDBG_CMD_CONFIG` receives a configuration word from the host;
`EDBG_CMD_JUMPIMG` tells EBOOT to jump to the loaded image. The full
set of EDBG commands supported by EBOOT is not enumerated in this
document; the two above are the ones observed in the string table.

### BOOTME Broadcast

`SendBootme` constructs a BOOTME UDP packet containing the device's
MAC address, current IP, CPU/platform identifier strings, and
version metadata. It is broadcast to `255.255.255.255` on a WinCE
EDBG UDP port. Platform Builder on the host listens for these
broadcasts to discover devices that are waiting for a download.

The packet format is the standard WinCE BOOTME structure and is not
documented here.

### DHCP

A full DHCP client implementation (`ProcessDHCP`, `SendDHCP`,
`DHCPFindOption`, `EbootDHCPRetransmit`, all of the standard WinCE
OAL DHCP helpers) is present in EBOOT's string table and callable,
but it is not activated by default. EBOOT defaults to static IP
configuration and only enters the DHCP path when the user selects a
"use DHCP" option from the maintenance menu.

## Default Network Configuration

EBOOT hardcodes the factory default network parameters inside a
function that writes **compile-time constants** directly into the
in-RAM PTB header:

```
IP address   = 0x0B00A8C0 little-endian = 192.168.0.11
subnet mask  = 0x00FFFFFF little-endian = 255.255.255.0
gateway      = 0                          (disabled)
```

These values live at offsets `+0x10`, `+0x14`, and `+0x18` of the
in-RAM PTB header at `0x80106EA0` (see
[partition-format.md](partition-format.md) for the header layout).
The values are not read from NAND, from `config.txt`, from an
upstream server, or from any runtime configuration source. They are
baked into the EBOOT binary as literal immediates.

When dumping the `PTB` block from a fresh unit's NAND, the same
`c0 a8 00 0b ff ff ff 00` byte sequence is observed in the header's
network config fields. This is **not** because NAND stores the
configuration; it is because the factory programming tool copies
EBOOT's in-RAM default PTB to NAND when flashing, and EBOOT's default
has these values baked in. Two machines with unchanged configuration
will show the same bytes for the same reason.

### Runtime Network State

`eboot_download_file_tftp` copies the IP and mask out of the in-RAM
PTB header into a runtime network state area at virtual
`0xA0020838..0xA002083C` (DDR uncached):

```
MEMORY[0xA0020838] = MEMORY[0x80106EB0]   // active IP
MEMORY[0xA002083C] = MEMORY[0x80106EB4]   // active subnet mask
```

That uncached virtual address maps to DDR physical `0x30020838`,
which is well inside EBOOT's working memory and does not conflict
with the framebuffer (`0x33B00000`) or the NK load address
(`0x30200000`).

The full contents of this runtime network state structure (beyond the
IP and mask at the first two slots) are not tabulated; a MAC address
and BOOTME sequence number are likely present in adjacent slots but
have not been independently identified.

## Unresolved

- `SPI_CONFIG2` at SPI2 `+0x20` is written to `0xFFFFFF` once during
  init and never touched again. Purpose unknown.
- The `0x52` bit pattern in SPI2 `SPI_CTRL` during init: bit 1 is
  direction, bit 4 and bit 6 are mode selectors with unknown specific
  meaning. The value is known to work for ENC28J60 traffic and has
  not been varied experimentally.
- The ENC28J60's `INT` output pin is wired to an AK7802 GPIO somewhere
  on the board, because EBOOT enables `PKTIE | INTIE` in the chip's
  EIE register during init. EBOOT is polling-mode and does not
  consume the interrupt, but a Linux driver would. The GPIO pin that
  receives `INT` is not identified in this documentation.
- Similarly, the ENC28J60 `RST` pin (if connected) is not identified.
  EBOOT relies on the software SRC reset over SPI.
- The full BOOTME packet format emitted by `SendBootme` is not
  tabulated here.
- The full set of EDBG commands supported on the RX path is not
  enumerated; only `CONFIG` and `JUMPIMG` are confirmed from string
  references.
- The runtime network state structure at `0xA0020830..` is only
  partially understood. Fields beyond IP and mask are not named.
- The MII write performed during init (MIREGADR = `0x10`, value =
  `0x0001`) is referenced but the specific PHY register and what it
  programs are not identified.
