# RTC Poll Stall

On the v1.58.2 device, each OAL timer tick polls an RTC ready bit that stays clear, so the CPU stays in the interrupt handler and no thread runs again. On screen, WinCE stops after `BackLight Initializing... OK` and does not reach the desktop.

The ready bit stays clear because the RTC module has no clock on this board. See [RTC Clock Source](power-management.md#rtc-clock-source).

---

Addresses in this document are NK virtual addresses in the v1.58.2 image unless the text gives a v1.88 address.

## Enable Path

`IOCTL_HAL_INIT_RTC` (`0x01012A30`) starts the RTC poll. The RTC driver `ak_rtc.dll` sends it from `RTC_Init` in `device.exe`. The handler `OALIoCtlHalInitRTC` (`0x80240080`) sets two flags at its end, with no condition:

| Flag | Address |
| --- | --- |
| RTC init done | `0x80629408` |
| BOOTARGS RTC present | `0xA0020848` |

EBOOT sets the BOOTARGS flag to 0. No other NK code writes either flag, so the RTC poll starts at the first timer tick after the IOCTL returns.

## RTC Poll Function

`OEMInterruptHandler` (`0x8023E118`) handles the system tick (status bit 18 of `SYSCTRL+0x4C`). It reloads the timer and then calls the RTC poll function, `0x80240364` (v1.88: `0x8023F11C`). The timer reload value is `0x2EE0`, which is 1 ms at 12 MHz.

When both flags are nonzero, each call advances an RTC read state machine by one step. The state is a u16 at `0x80629402`. The state machine has three branches, which `0x8062940C` and `0x80629410` select. The default branch, with both values 0, is the branch that runs on this device:

| State | Action |
| --- | --- |
| 1 | Writes `(SYSCTRL+0x50 & 0xFFF80000) \| 0x64000` to issue a read command |
| 2, 4, `0x1F6`, `0x1F8` | Polls bit 24 of `SYSCTRL+0x4C`, the RTC ready bit, up to `0x3A98` (15000) times. On timeout, returns and does not advance the state |
| Other | No wait. After state 999, the state goes back to 0 |

In the branch that `0x80629410` selects, states 1 and 2 wait with no limit.

## Interrupt Livelock

In state 2 the ready bit stays clear, so every 1 ms tick runs the full 15000 polls and the state stays at 2. The handler reloads the timer before it polls, so when the polls take more than 1 ms, the next tick is already pending when the handler returns. The CPU then enters the interrupt handler again at the same instruction boundary, and no thread gets CPU time.

The poll function does not write `RTC_CONF` (`SYSCTRL+0x50`) back, so the read command stays pending. In a bare-metal test, the ready bit stayed clear for 10000000 polls after a read command, and a write of 0 to `RTC_CONF` cleared the pending read at once.

Two independent measurements during the stall gave about 114 interrupts per second, which is about 8.8 ms for each tick. The kernel millisecond counter (`0x80629234`) advances by 1 for each tick. In dump 1 below, the interrupt count was 20480 at 20022 kernel milliseconds. Kernel time therefore runs about 8.8 times slower than wall time, and all kernel timeouts increase by the same factor.

States 0 and 1 have no wait. Thus after the IOCTL returns, about 2 ticks run at normal speed. In this interval `device.exe` completes the rest of `RTC_Init` and starts the `RTC1:` interface notification, and then it stops at the position where the livelock starts.

The evidence from two memory dumps of a stalled device agrees:

| Value | Dump 1 | Dump 2 |
| --- | --- | --- |
| State (`0x80629402`) | 2 | 2 |
| RTC init done (`0x80629408`) | 1 | 1 |
| Branch select (`0x8062940C` / `0x80629410`) | 0 / 0 | 0 / 0 |
| Kernel milliseconds (`0x80629234`) | `0x4E36` | `0x5E8F` |

## Fix

Change the first instruction of the RTC poll function to `bx lr`. This stops the poll in the tick. With this change, the v1.58.2 device boots to the WinCE desktop on every boot in the tests.

The state machine writes its results to the words from `0x806293D8` to `0x806293E4`. In both dumps these words are 0, so the poll gives no valid data on this board. The RTC read in `OALGetRealTime` and `OALSetRealTime` runs in thread context with a bounded wait and stays unchanged by the fix. Each of these calls takes about 17 ms on this board.

A different fix is to stop `OALIoCtlHalInitRTC` from setting the BOOTARGS flag, which agrees with the EBOOT setting. Five other locations in the OAL read that flag, for example `0x8024084C`, `0x80240AB4` and `0x80241048`. This fix therefore also changes how the OAL gets the time.

## Unresolved

- In earlier months, the same device usually reached the desktop. On 9 V DC input the stall occurred on few boots. Later the success rate decreased gradually, and then the stall occurred on almost every boot with any power source. Because the RTC module has no clock, the ready bit should never come, and the reason for the earlier successful boots is unknown.
- The effect of the fix on a board with a fitted RTC crystal is unknown.
