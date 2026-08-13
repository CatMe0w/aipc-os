# SDHC Driver

The WinCE NK image contains `sdhc_anyka.dll`, an SD/MMC/SDIO host-controller driver for the AK7802 MCI controller. This document records the behavior recovered from that module, together with the controller behavior that hardware confirms. [docs/aipc-os-original/faster-sd-driver.md](../aipc-os-original/faster-sd-driver.md) records our own driver on top of it.

The register names below are the established names for this MCI block, and they serve as a naming cross-check. The register offsets and the access patterns come from the WinCE module itself.

## Controller Instances

The driver accepts two MCI physical base addresses from the device registry value `port`:

| Physical base | Driver role                        |
| ------------- | ---------------------------------- |
| `0x20020000`  | Primary MCI/MMC controller path    |
| `0x20021000`  | Secondary MCI/SDIO controller path |

The MCI register block maps with size `0x44`. The driver also maps `0x08000074` with size `0x68` for the sharepin and pad-control registers, and `0x0800000C` with size `4` for a clock-gate register.

## MCI Register Layout

All offsets are relative to the mapped MCI base.

| Offset | Name | Observed use |
| --- | --- | --- |
| `+0x04` | `MCI_CLOCK` | Clock enable state and divider fields |
| `+0x08` | `MCI_ARGUMENT` | Command argument |
| `+0x0C` | `MCI_COMMAND` | Command opcode and response-control bits |
| `+0x14` | `MCI_RESPONSE0` | Short response word, or final word of long response |
| `+0x18` | `MCI_RESPONSE1` | Long response word |
| `+0x1C` | `MCI_RESPONSE2` | Long response word |
| `+0x20` | `MCI_RESPONSE3` | First word copied for long response |
| `+0x24` | `MCI_DATATIMER` | Data timeout register; the init and transfer paths write `0xFFFFFFFF` |
| `+0x28` | `MCI_DATALENGTH` | Transfer byte count |
| `+0x2C` | `MCI_DATACTRL` | Data path enable, direction, bus-mode and config bits, and block size |
| `+0x34` | `MCI_STATUS` | Command, data, FIFO and SDIO status bits |
| `+0x38` | `MCI_MASK` | Interrupt mask |
| `+0x3C` | `MCI_DMACTRL` | DMA buffer control |
| `+0x40` | `MCI_FIFO` | 32-bit FIFO data port |

The command register uses these bit fields:

| Field               | Name                              |
| ------------------- | --------------------------------- |
| bit 0               | `CPSM_ENABLE`                     |
| opcode contribution | written as `(opcode & 0x7F) << 1` |
| bit 7               | `CPSM_RESPONSE`                   |
| bit 8               | `CPSM_LONGRSP`                    |
| bit 10              | `CPSM_RSPCRC_NOCHK`               |
| bit 11              | `CPSM_WITHDATA`                   |

The data-control register uses these bit fields:

| Bits        | Name                                                 |
| ----------- | ---------------------------------------------------- |
| bit 0       | `DPSM_ENABLE`                                        |
| bit 1       | read direction                                       |
| bit 3       | config bit supplied through SD bus I/O control paths |
| bits 16..27 | block size                                           |

The status register uses the established names for the bits that this driver exercises:

| Bit | Mask      | Name               |
| --- | --------- | ------------------ |
| 0   | `0x00001` | `MCI_RESPCRCFAIL`  |
| 1   | `0x00002` | `MCI_DATACRCFAIL`  |
| 2   | `0x00004` | `MCI_RESPTIMEOUT`  |
| 3   | `0x00008` | `MCI_DATATIMEOUT`  |
| 4   | `0x00010` | `MCI_RESPEND`      |
| 5   | `0x00020` | `MCI_CMDSENT`      |
| 6   | `0x00040` | `MCI_DATAEND`      |
| 7   | `0x00080` | `MCI_DATABLOCKEND` |
| 10  | `0x00400` | `MCI_TXACTIVE`     |
| 11  | `0x00800` | `MCI_RXACTIVE`     |
| 12  | `0x01000` | `MCI_FIFOFULL`     |
| 13  | `0x02000` | `MCI_FIFOEMPTY`    |
| 17  | `0x20000` | `MCI_SDIOINT`      |

## Request View

The driver treats the SD bus request object as an opaque structure with fixed offsets. The command and data paths use these offsets directly:

| Offset | Meaning |
| --- | --- |
| `+0x0C` | Completion mode/status word. Bit 31 selects the asynchronous completion path. |
| `+0x10` | Transfer type: `0` read, `1` write, `2` command-only. |
| `+0x14` | Command opcode byte. |
| `+0x18` | Command argument. |
| `+0x1C` | Response type value, range `0..8` in observed paths. |
| `+0x20..+0x2F` | 16-byte response output buffer. |
| `+0x3C` | Block count. |
| `+0x40` | Block size. The driver multiplies it by the block count and encodes it into `MCI_DATACTRL[27:16]`. |
| `+0x48` | Normal transfer buffer pointer. |
| `+0x54` | Alternate-buffer flag word. Bit 16 selects an alternate pointer format. |
| `+0x58` | Process permission value, passed to `SetProcPermissions` around user-buffer access. |

With `req+0x54 bit16` set, the driver treats `req+0x48` as a pointer to a small descriptor. The first word becomes the alternate transfer buffer pointer, and `req+0x48 + 8` goes to a second related pointer. With the bit clear, `req+0x48` is the transfer buffer itself.

## Initialization

The controller initialization sequence takes these hardware-relevant steps:

1. Map the sharepin area at physical `0x08000074`, size `0x68`.
2. Map the MCI register block that registry `port` selects, size `0x44`.
3. Map physical `0x0800000C`, size `4`.
4. If registry `DMAChannel` is not `-1`, allocate a `0x8000` byte DMA buffer through `CEDDK_60(0x8000, 0x204, ...)`.
5. Enable the controller clock through `KernelIoControl(0x1012020, ...)`. The argument is `4` for `0x20020000` and `0x100` for `0x20021000`.
6. Configure the sharepin and pad-control registers.
7. Write `MCI_CLOCK = 0x0019FFFF`, then call the clock-setting routine for `100000` Hz.
8. Sleep for 100 ms.
9. Write `MCI_DATALENGTH = 512`.
10. Write `MCI_DATATIMER = 0xFFFFFFFF`.
11. Create the first GPIO event, named from a `GPIO_%d` string, and register it with the GPIO driver.
12. Create a second GPIO event or an MCI interrupt event, which depends on registry `SdioIntMode`.
13. Initialize the L2 DMA function table through `CEDDK_62`, and call table entry 0 with slot value `4` or `5` and argument `1`.

The slot value for the DMA table is `4` for physical base `0x20020000`, and `5` for `0x20021000`.

## Sharepin Setup

The sharepin setup branches on the MCI physical base.

For `0x20020000`, the driver calls a helper that returns the constant `9` in this module. The `result != 10` branch is therefore the active branch for this image:

```
*(sharepin + 0x28) &= 0x3FFFFFFF
*(sharepin + 0x2C) &= ~0x1C0
*(sharepin + 0x60) |= 1
```

For any other accepted MCI physical base, `0x20021000` included, the driver writes:

```
*(sharepin + 0x28) = (*(sharepin + 0x28) & ~0x3F000000) | 0x0F000000
*(sharepin + 0x60) |= 0x07C03C02
```

The inactive `result == 10` branch under the `0x20020000` path also exists in the binary, but the helper of this module cannot return 10.

## Clock Programming

The clock routine first queries the MCI source clock through `KernelIoControl(0x1012010, ...)`. It clamps the requested frequency to half the source clock at most, then computes a divider:

```
divider = source_clock / requested_clock - 2
```

The divider clamps to `0..510`. If the first computed actual frequency would exceed the requested frequency, the routine increments the divider once and computes again. The register value keeps the existing high 16 bits of `MCI_CLOCK`, then writes:

```
low_div  = divider - (divider >> 1)
high_div = divider >> 1
MCI_CLOCK[15:0] = (high_div << 8) | low_div
```

If bit 16 was set before the divider update, the routine sets bit 16 again after the write. The computed actual frequency goes into the host context, and the routine returns it.

## Command Encoding

Before it sends a command, the driver clears `MCI_COMMAND` and writes `MCI_ARGUMENT`. It then builds the command register from `response_type` and `opcode`.

Every successful encoding sets `CPSM_ENABLE` and includes `(opcode & 0x7F) << 1`.

| Response type | Special opcodes `1`, `5`, `41` | Other opcodes         |
| ------------- | ------------------------------ | --------------------- |
| `0`           | `0x401 \| (cmd << 1)`          | `0x001 \| (cmd << 1)` |
| `1..2`        | `0x481 \| (cmd << 1)`          | `0x081 \| (cmd << 1)` |
| `3`           | `0x581 \| (cmd << 1)`          | `0x181 \| (cmd << 1)` |
| `4..8`        | `0x481 \| (cmd << 1)`          | `0x081 \| (cmd << 1)` |

For CMD17 with a normal response type, the command register value is `0xA3`:

```
CPSM_ENABLE | CPSM_RESPONSE | (17 << 1)
```

The driver does not set `CPSM_WITHDATA` for CMD17. `MCI_DATACTRL` controls the data phase.

On the `0x20021000` controller, CMD5 sets an internal flag that suppresses the interrupt-wait path in the status waiter, and it starts a worker thread. CMD0 clears that flag and stops the worker thread.

## Response Copying

The response reader waits for `MCI_STATUS bit4` (`MCI_RESPEND`). It calls the status waiter with mask `0x10`. On success it copies the response bytes into `req+0x20..+0x2F`.

For response types `1..2` it writes:

```
resp[0] = opcode
resp[1..4] = MCI_RESPONSE0, least significant byte first
```

For response type `3` it writes a 16-byte long response in this order:

```
MCI_RESPONSE3, MCI_RESPONSE2, MCI_RESPONSE1, MCI_RESPONSE0
```

Each register goes out least significant byte first.

For response types `4..5` it writes:

```
resp[0] = 0x3F
resp[1..4] = MCI_RESPONSE0, least significant byte first
resp[5] = 0xFF
```

For response types `6..8` it writes:

```
resp[0] = opcode
resp[1..4] = MCI_RESPONSE0, least significant byte first
resp[5] = MCI_RESPONSE1[31:24]
```

## Status Waiting

The status waiter accepts a mask, an assert level, a `use_cache` flag and a `use_irq` flag. It reads `MCI_STATUS`, unless `use_cache` is set. In that case it starts from the host status cache.

If status bit17 is set, the driver stores a flag in the host context and signals the event that the SDIO/MCI interrupt wait path uses. This is not a generic error.

The hard error checks are:

| Status condition                      | Return value |
| ------------------------------------- | ------------ |
| bit0 or bit1 set                      | `0xC000000D` |
| bit2 set                              | `0xC0000014` |
| bit3 set                              | `0xC0000015` |
| bus/controller callback returns false | `0xC0000011` |
| elapsed time reaches 1000 ms          | `0xC000000D` |

After the error checks, the function tests the requested mask. With `assert_level == 1` it succeeds when any requested bit is set. With `assert_level == 0` it succeeds when the requested bits are clear.

With `use_irq` set and the CMD5 worker flag clear, the driver writes:

```
MCI_MASK = mask | 0x204F
WaitForSingleObject(event, 1000)
MCI_MASK = 0
InterruptDone(mci_sysintr)
```

With `use_irq` clear, and the requested condition not met, the function refreshes the status once and returns `1`. The FIFO PIO loops treat this as a pending state and continue from their own timeout loop.

Before it returns, the function stores the last status word in the host status cache.

## PIO Data Path

The common PIO setup path writes:

```
MCI_DMACTRL = 0
MCI_DATATIMER = 0xFFFFFFFF
```

For a read request (`req+0x10 == 0`) it also writes:

```
total = req_block_count * req_block_size
MCI_DATALENGTH = total
MCI_DATACTRL = ((datactrl_prefix | (req_block_size << 13)) << 3) | 3
```

The final `| 3` sets the data-path enable and the read direction. The block size shifts by 13 first, then the whole value shifts by 3, thus the block size reaches `MCI_DATACTRL[27:16]`.

For a write request (`req+0x10 == 1`) the setup path only records the write direction. After the command response, the completion path writes:

```
MCI_DMACTRL = 0
MCI_DATATIMER = 0xFFFFFFFF
MCI_DATALENGTH = total
MCI_DATACTRL = ((datactrl_prefix | (req_block_size << 13)) << 3) | 1
```

The final `| 1` enables the data path without the read direction.

### FIFO Reads

The FIFO read loop first selects the destination buffer. With `req+0x54 bit16` set it uses the alternate buffer pointer from the host context. Otherwise it uses `req+0x48`.

For each FIFO word:

1. If at least four bytes remain, wait for status mask `0x1000` (`MCI_FIFOFULL`).
2. If fewer than four bytes remain, wait for status mask `0x40` (`MCI_DATAEND`).
3. Read one 32-bit word from `MCI_FIFO`.
4. Copy one to four bytes from that word, least significant byte first.

The loop returns `0xC0000015` if its own elapsed time reaches 1000 ms.

### FIFO Writes

The FIFO write loop uses the same choice between the normal buffer and the alternate buffer. It writes 32-bit FIFO words assembled from the source bytes. The loop checks the host active flag before each write, and it returns `0xC0000011` if that flag is zero.

## DMA Path Boundary

The driver can choose a DMA path when all of these conditions hold:

- registry `DMAChannel` is not `-1`
- the selected transfer buffer is 4-byte aligned
- `req+0x40` is 4-byte aligned
- the total byte count is at least `0x200`
- the total byte count is at most `0x8000`
- the total byte count is 64-byte aligned

The DLL gets a function table through `CEDDK_62`. This document gives no stable names to the table entries. The observed uses are:

| Entry offset | Observed use |
| --- | --- |
| `+0x00` | Called during init with slot value `4` or `5` and argument `1`. |
| `+0x04` | Called during cleanup with slot value `4` or `5`. |
| `+0x08` | Called during one DMA transfer direction with a per-chunk descriptor. |
| `+0x0C` | Called during the other DMA transfer direction with a per-chunk descriptor. |
| `+0x14` | Called during DMA setup after a write of `MCI_DMACTRL = 0x01000001`. |
| `+0x18` | Called in the DMA chunk loops. Its return value controls a wait and retry loop before a chunk goes out. |

The per-chunk loops split a transfer into chunks of `0x200` bytes at most.

## Error Recovery

The bus-submit path retries a request up to five times when the completion returns `0xC000000D`. Before each retry it writes:

```
MCI_COMMAND = 0
MCI_DATACTRL = 0
MCI_DATALENGTH = 0
MCI_DMACTRL = 0x01000001
```

When the completion returns `0xC0000011`, the driver does the same register cleanup and then sleeps for 300 ms. After all five retries fail, the driver sends CMD0 and sleeps for 100 ms.

## Verified Controller Behavior

Cold-boot tests on the AK7802 confirm the behavior below. It bounds what any driver on this controller can do.

**Enumeration.** CMD5 always ends with `MCI_RESPTIMEOUT`. The controller needs no reset after that, and CMD55 with ACMD41 completes normally. Identification, card selection and transfer-state verification follow.

**Response word order.** `MCI_RESPONSE0..3` maps directly to the Linux `resp[0..3]`. A reversal corrupts both the CID and the CSD. The reverse copy order of the WinCE byte buffer is an API layout detail. Do not reuse it as the response-word order.

**Inner FIFO.** A read takes each word only while `MCI_FIFOFULL` and `MCI_RXACTIVE` are both set. A write takes each word only while `MCI_FIFOEMPTY` and `MCI_TXACTIVE` are both set. One 512-byte block moves exactly 128 words and ends with `MCI_DATAEND` and `MCI_DATABLOCKEND`.

**Command encoding.** CMD17 works with `0x000000A3` and with `0x000008A3`, thus the controller accepts `CPSM_WITHDATA`. A write needs CMD24 before the write data path goes on.

**Multi-block.** CMD18 and CMD25 give one `MCI_DATABLOCKEND` per block. An unbounded transfer needs CMD12 to stop. CMD23 bounds a transfer and removes that need, but CMD12 is still necessary to abort a bounded transfer that fails before its declared block count. CMD13 must return the ready transfer state before the next request.

**L2 DMA path.** The DDR address goes to L2 offset `+0x08` and the operation list to `+0x48`. `L2_CONF1[10]` selects the direction and `L2_DMAREQ[26]` requests the common buffer. `MCI_DMACTRL` stays `0x01000001`. The final `L2_CONF1` is `0x00040004` for a read and `0x00040404` for a write. A transfer is complete only when the request bit clears, `MCI_DATAEND` appears, and `MCI_DATACNT` reaches zero. Common buffers 2 and 6 both work, thus the routing is generic. The operation list can also point at discontiguous DDR segments, and it touches nothing between them.

**Chunk size limit.** 8 KiB, an operation count of 128, is the largest chunk that completes. An operation count of 256 stops after the first 512 MCI bytes, with `MCI_DATACNT` at `0x00007e00`, a common buffer count of 8, and `L2_DMAREQ[26]` still set. The 8-bit operation-count field explains the limit. A larger request must split into 8 KiB chunks, and one CMD18 or CMD25 can stay open across them.

**Clock.** The `MCI_CLOCK` values `0x00190202`, `0x00190101` and `0x00190001` give 20.67 MHz, 31 MHz and 41.33 MHz from the 124 MHz input clock. CMD6 check mode returns switch status byte 13 as `0x03`, and set mode selects function 1 and returns byte 16 as `0x01`. Data read at 31 MHz and at 41.33 MHz matches the 20.67 MHz reference word for word.

**Interrupt routing.** MCI command and data events assert main interrupt source 22. Completion on common buffer 2 asserts source 10, which `L2_BUFINTEN[11]` enables. Each source withdraws independently.

**Interrupt enable order.** With `L2_DMAREQ` clear, an enable of `L2_BUFINTEN[11]` asserts source 10 at once, and the main pending value goes from `0x02001000` to `0x02001400`. Software must therefore set the buffer request first and enable the completion interrupt second, with local IRQ delivery excluded across the two writes. The other order opens a false-completion window. [docs/aipc-os-original/faster-sd-driver.md](../aipc-os-original/faster-sd-driver.md) records what happens to a driver that gets this wrong.

Nothing here covers hot removal, frequencies above 41.33 MHz, or deliberate data-error recovery.

## First Transfer After Initialization

The first data transfer after card initialization always fails, and every transfer after it succeeds. `sd_init` returns clean, the first CMD17 gets a valid response, no data arrives, and `MCI_STATUS` ends at `0x00002008`, that is `MCI_FIFOEMPTY` with `MCI_DATATIMEOUT`. The next identical read reaches `0x000120C0`, `MCI_DATAEND` with `MCI_DATABLOCKEND`, and returns correct data.

None of the obvious causes explains it. A clear of `MCI_CMD` before the transfer, a settling delay of tens of milliseconds, a complete stop of UART output, an earlier NAND sharepin and L2 bring-up, an earlier L2 buffer binding, and the maximum data timeout all leave the failure in place. A register snapshot taken immediately before a failing transfer and immediately before a working one is identical in `MCI_DATALEN`, `MCI_DATACTRL`, `MCI_DMACTRL`, `MCI_CLOCK` and the whole L2 binding, and differs only in the sticky status bits and the interrupt mask. The register file does not show the cause. Two independent bare-metal loaders reproduce it on the same hardware.

A driver under a retrying block layer does not need a warm-up or a dummy transfer. It can report the timeout as an ordinary error and let the block layer reissue the request through `mrq->cmd->retries`. Code with no layer above it to retry has to retry the transfer itself, and one reissue is enough.

Exactly one transfer fails. With retries off completely, the transfer after initialization returns `MCI_DATATIMEOUT`, and several hundred consecutive single-attempt block reads then succeed with no failure at all.

A poorly seated card produces a similar-looking but different failure, and it is worth telling the two apart. Enumeration completes normally, because the command line alone carries it, but every data transfer times out however many times it goes out again. The first-transfer behavior costs one retry. A seating problem costs all of them.

## Cleanup

Cleanup sets the host stop flag, disables the driver interrupt path, disables the two interrupt values in the host context, and signals the request and interrupt events. It then closes the worker threads and the event handles.

If a DMA buffer exists, cleanup passes its virtual address to `FreePhysMem`. It then calls the DMA table cleanup entry if the table exists, unmaps the sharepin, MCI and clock-gate mappings, and deletes the host critical section.

## Unresolved

- The exact meaning of each `CEDDK_62` function-table entry. `sdhc_anyka.dll` alone does not give all of them.
- The SDIO and card-detect helper state around host offsets `0x340..0x34C`. This document records it as observed GPIO helper state only, not as a board-level signal meaning.
- The inactive `SDHC_GetControllerId() == 10` branch exists, but the implementation of this module cannot reach it.
