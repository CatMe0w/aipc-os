# DM9000 Ethernet Driver

The Ethernet controller of the AIPC is a Davicom DM9000A in 8-bit parallel mode, designator `U9` on the board. The populated part is marked `DM9000AEP 0921S`. The library symbol on the schematic is named `DM9000B`, but its value field reads `DM9000A-8/16bit`, and the revision register of the chip agrees with the silkscreen. The controller does not connect to an AK7802 host-bus function. Software generates every host-side bus cycle through thirteen GPIO pins, while the interrupts and several packet-processing operations stay hardware-assisted inside the DM9000A.

The WinCE NK image drives it with `dm9000x.dll`, an NDIS miniport built from `lan9000.c`. This document records the board wiring, the bus protocol recovered from that module, and the hardware results that confirm both.

## Board Wiring

The controller sits on twelve pins of the `GPIO2` register window, plus one pin of `GPIO4`. The `Z_nSM*` and `HBI_*` net names on the schematic follow a static-memory-controller convention, but they do not describe the AK7802 functions on these pins. Pins 47 through 58 switch as one group between GPIO and the camera interface: `VISCLK`, `VIPCLK`, `VIHREF`, `VIVREF` and `VI_DATA[0:7]`. No shared-pin function gives these twelve pins a third alternative, and `DGPIO0` has no static-bus alternative. Sharepin bit 24 therefore selects camera operation or GPIO operation, and nothing else. The camera mapping does not line up with the `CMD`, `SD0..SD7`, `INT`, `IOR#` and `IOW#` signals of the DM9000, and `CS#` sits on the separate `DGPIO0`. That mapping therefore cannot run these bus cycles.

| Board net   | AK7802 pin | Physical pin | Bank/bit    | DM9000 pin    |
| ----------- | ---------- | ------------ | ----------- | ------------- |
| `HBI_D0`    | `GPIO48`   | 48           | `GPIO2[16]` | `SD0` (18)    |
| `HBI_D1`    | `GPIO49`   | 49           | `GPIO2[17]` | `SD1` (17)    |
| `HBI_D2`    | `GPIO50`   | 50           | `GPIO2[18]` | `SD2` (16)    |
| `HBI_D3`    | `GPIO51`   | 51           | `GPIO2[19]` | `SD3` (14)    |
| `HBI_D4`    | `GPIO52`   | 52           | `GPIO2[20]` | `SD4` (13)    |
| `HBI_D5`    | `GPIO53`   | 53           | `GPIO2[21]` | `SD5` (12)    |
| `HBI_D6`    | `GPIO54`   | 54           | `GPIO2[22]` | `SD6` (11)    |
| `HBI_D7`    | `GPIO55`   | 55           | `GPIO2[23]` | `SD7` (10)    |
| `EBI_ADDR3` | `GPIO47`   | 47           | `GPIO2[15]` | `CMD` (32)    |
| `RLAN_INT#` | `GPIO56`   | 56           | `GPIO2[24]` | `INT` (34)    |
| `Z_nSMOEN`  | `GPIO57`   | 57           | `GPIO2[25]` | `IOR#` (35)   |
| `Z_nSMWEN`  | `GPIO58`   | 58           | `GPIO2[26]` | `IOW#` (36)   |
| `Z_nSMCS0`  | `DGPIO0`   | 102          | `GPIO4[6]`  | `CS#` (37)    |
| `LAN_RST#`  | `TCK`      | 2            | `GPIO1[2]`  | `PWRST#` (40) |

The reference clock is a 25 MHz crystal. The magnetics are an H1102 that feeds a single RJ45 jack. The board has no EEPROM next to the controller, which fits the way the WinCE miniport takes its MAC address from `MacHigh` and `MacLow` in the registry instead of from the chip. `LAN_RST#` shares the JTAG `TCK` pin. No observed driver path uses it, because the WinCE miniport and EBOOT both reset the chip through the `NCR` software-reset bit.

## Pin Setup

Before any bus cycle, bits 0 and 24 of `SYSCTRL + 0x78`, sharepin mux register 1, must be clear. This is the only direct hardware access that the WinCE miniport makes on its own. It maps four bytes at physical `0x08000078` and masks the register with `0xFEFFFFFE`. On a device out of reset the register reads `0x00000203`, thus bit 0 is the only bit that this changes.

The two bits cover different pins. Bit 24 muxes pins 47 through 58, the data bus together with `CMD`, `IOR#` and `IOW#`, and it is already clear at reset. Bit 0 muxes pins 0 through 3, the JTAG group, and `LAN_RST#` sits on `TCK`. A clear of bit 0 is therefore about the reset line, not the bus. A driver that leaves the reset pin unused does not need it.

Direction and idle levels are then:

| Pins | Direction | Idle level |
| --- | --- | --- |
| `CMD`, `IOR#`, `IOW#`, `CS#` | output | high |
| `SD0`..`SD7` | output, flipped to input for read cycles | - |
| `INT` | input | - |

## Bus Protocol

The DM9000 uses a two-port interface. `CMD` low selects the index port, and `CMD` high selects the data port. The rising edge of `IOW#` latches the data, and the data is present while `IOR#` is low.

An index cycle selects the register number for the data cycle that follows:

```
SD0..SD7 -> output
CS#  = 0
CMD  = 0
IOW# = 0
drive SD0..SD7 = reg
IOW# = 1
CS#  = 1
```

A data read, which can stream several bytes without a release of `CS#`:

```
SD0..SD7 -> input
CS# = 0
CMD = 1
per byte:  IOR# = 0;  sample SD0..SD7;  IOR# = 1
CS# = 1
```

A data write, in the same shape:

```
SD0..SD7 -> output
CS# = 0
CMD = 1
per byte:  IOW# = 0;  drive SD0..SD7 = value;  IOW# = 1
CS# = 1
```

In both write directions the data lines go out only after the strobe is already low. The rising edge does the latching, thus the order is harmless at software-generated speeds.

A single register read is an index cycle, then a one-byte data read. A single register write is an index cycle, then a one-byte data write. The PHY registers come through `EPCR`, `EPAR`, `EPDRL` and `EPDRH` at register numbers `0x0B`..`0x0E`, exactly as in a memory-mapped DM9000 driver.

## Hardware Acceleration Boundary

The `INT` connection is real and useful. It signals receive, transmit and link-status events, thus the CPU can wait instead of poll the controller. It does not transfer packet data. After a receive interrupt, the host must still select `MRCMD` and generate one `IOR#` cycle for every byte that it reads from the data port. Transmission likewise uses `MWCMD`, then one `IOW#` cycle per byte.

The DM9000 documentation calls its internal-SRAM access mechanism DMA. `MRCMD` and `MWCMD` select streaming ports whose internal address advances on its own, and the controller prefetches reads from its 16 KiB packet SRAM. This is internal to the DM9000 and is not host bus-master DMA. The chip has no DMA request pin, no acknowledge pin and no host-address pins, thus it cannot move a frame directly to or from AK7802 RAM.

The AK7802 DMA accelerator cannot supply the missing host cycles. Its channels connect external RAM to assigned on-chip functional blocks, at 16-bit or 32-bit width. The available L2 device assignment covers the USB, NAND, MMC/SD, SDIO, SPI and audio paths, with no GPIO request path. That DMA engine therefore cannot reach the 8-bit DM9000 port on this board.

Other DM9000A hardware offloads remain available. The controller holds the 10/100 MAC and PHY, the receive and transmit packet SRAM, the CRC and padding logic, flow control, and IP/TCP/UDP checksum generation and checking. Only the host bus cycles are software. See [docs/aipc-os-original/ethernet-driver.md](../aipc-os-original/ethernet-driver.md) for what our own driver does with this boundary.

### Index Readback

The index port is also readable, through the data-read sequence with `CMD` held low. The WinCE miniport never runs this cycle, because it only writes the index port. A driver that saves and restores the index register across an interrupt needs it.

A floating data bus can return whatever went onto it last and imitate a working readback, thus the check drove an index, read a register whose contents differ from it, and only then read the index port. Across index values `0x28` through `0x2C` the readback returned the index every time and the register contents never. An idle bus with all eight data pins as inputs reads `0x00`.

### Interrupt Polarity

The `INT` output is active high. Neither the WinCE miniport nor a mainline Linux driver programs the interrupt control register of the chip, thus the pin keeps its power-on polarity. The `RLAN_INT#` net name on the schematic does not describe the signal as the board runs it. A consumer that configures the receiving GPIO for a low level sees the idle state as permanently asserted and takes a continuous interrupt.

Hardware established this. A receiver configured active low counted about two million interrupts with no traffic, and no frame ever completed transmission. Active high leaves the counter quiet and the interface works.

## WinCE Driver Structure

`dm9000x.dll` toggles no pin itself. Each pin operation is a `KernelIoControl` into the OAL, with OEM control codes in the `FILE_DEVICE_HAL` space:

| Control code | Input | Meaning |
| --- | --- | --- |
| `0x010120D0` | `{pin, direction}` | Set one pin's direction; 1 is input |
| `0x010120D4` | `{pin, level}` | Set one pin's output level |
| `0x010120E0` | `{56, 1}` | Interrupt enable for the `INT` pin `[unverified]` |
| `0x010120E4` | `{pin, value}` | Per-pin attribute set at init `[unverified]` |
| `0x010120E8` | `{pin, 0}` | Per-pin attribute set at init `[unverified]` |
| `0x010120FC` | `{48, 8, mask}` | Direction for the eight data pins; `0x00` all output, `0xFF` all input |
| `0x01012100` | `{48, 8}` | Sample the eight data pins |
| `0x01012104` | `{48, 8, value}` | Drive the eight data pins |

The driver caches the current data-bus direction and reprograms it only on a change. The register accessors take a critical section around the index and data cycles.

The initialization pin table lives in the `.data` of the module. It holds thirteen sixteen-byte entries, for pins 102, 47, 48 through 56, 57 and 58. After it walks the table, the driver drives `CS#`, `CMD`, `IOR#` and `IOW#` high.

### Registry Configuration

`HKLM\Comm\DM9000X1\Parms` carries the miniport parameters. On the v1.88 unit:

| Value               | Setting      |
| ------------------- | ------------ |
| `IoBaseAddress`     | 0            |
| `SysIntr`           | 0            |
| `InterruptNumber`   | 0            |
| `TxDMAMode`         | 0            |
| `RxDMAMode`         | 0            |
| `FlowControl`       | 0            |
| `LinkAvailableMode` | `0x7F`       |
| `MacHigh`           | `0x0070`     |
| `MacLow`            | `0x0F117003` |

An `IoBaseAddress` of zero fits the absence of any memory window. The driver reads the value but never uses it as a base address.

## Hardware Confirmation

The sequence above works with direct SYSCTRL writes, through `SYSCTRL + 0x84`, `+0x88` and `+0xC0` for the `GPIO2` pins and `SYSCTRL + 0x94` and `+0x98` for `CS#`. A read of registers `0x28`..`0x2C` returns:

| Register      | Value    |
| ------------- | -------- |
| `VIDL`:`VIDH` | `0x0A46` |
| `PIDL`:`PIDH` | `0x9000` |
| `CHIPR`       | `0x19`   |

`0x0A46` and `0x9000` are the Davicom DM9000 signature. `0x19` is the DM9000A revision, where `0x1A` would mean a DM9000B. The same values come back after an `NCR` software reset. The controller is powered and responsive in USB boot mode, before any of the vendor firmware runs.

## Unresolved

- The meaning of control codes `0x010120E4` and `0x010120E8`. In the initialization table their operands are 1 for `CS#`, `IOR#` and `IOW#`, and 0 everywhere else. This fits a pull-up or pad-drive setting, but nothing tests it.
- Whether the bus also works with bit 0 of `SYSCTRL + 0x78` left set. That bit muxes the JTAG pins rather than the bus, but no experiment varies it.
