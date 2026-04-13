# Diagnostic Self-Test Mode

Diagnostic mode is a factory test path activated when both DGPIO[3] and
DGPIO[2] are held high during the boot override sampling (all 5 polls
must see both bits asserted). The bootrom sets rRTC_BOOTMOD = 0x05000000,
then enters `bootrom_diag_mode()`, which never returns.

## Overview

The diagnostic mode runs two categories of hardware self-tests:

1. **GPIO/Sharepin connectivity test** - verifies that all GPIO groups can
   be driven to all-ones and all-zeroes.
2. **RTC/USB indexed register window test** - writes and reads back test
   patterns across 6 register windows to verify the indexed sideband
   interface.

Results are signaled via two GPIO4 output pins used as status indicators.
After the tests complete, the bootrom enters an infinite idle loop.

## GPIO4 Test Output Pins

The diagnostic mode uses GPIO4 bits 10 and 11 (bit index = argument + 6,
where argument 4 -> bit 10, argument 5 -> bit 11) as test status indicators:

| Pin (bit) | Role           | Driven by  |
| --------- | -------------- | ---------- |
| GPIO4[10] | Busy indicator | Argument 4 |
| GPIO4[11] | Pass latch     | Argument 5 |

Pin driving functions:

- **Drive high**: clear direction bit in SYSCTRL+0x94, set output bit in
  SYSCTRL+0x98.
- **Drive low**: clear direction bit in SYSCTRL+0x94, clear output bit in
  SYSCTRL+0x98.

Similarly, GPIO4 bits 6 and 7 (arguments 0 and 1) are used as additional
status indicators during the GPIO/sharepin test phase.

## Initialization

`diag_init()`:

1. Drive GPIO4[6] low (clear arg 0).
2. Drive GPIO4[6] high (set arg 0) - signals test start.
3. Drive GPIO4[7] high (set arg 1) - pass indicator for GPIO test.
4. Run the GPIO/sharepin connectivity test.
5. Drive GPIO4[6] low - signals GPIO test phase complete.

## GPIO/Sharepin Connectivity Test

This test verifies that all four GPIO groups (GPIO1-GPIO4) can be driven
and read back correctly.

### Setup

1. Clear sharepin mux registers: SYSCTRL+0x74 = 0, SYSCTRL+0x78 = 0
   (switch all sharepins to GPIO mode).
2. Set I/O control: SYSCTRL+0xD4 |= 0x3FFFC (bits [17:2]) and
   SYSCTRL+0xD4 |= 0xC000000 (bits [27:26]).
3. Set GPIO1 direction register to 0: SYSCTRL+0x7C = 0.

### Drive All-Ones Test

Set all output registers to their maximum values:

| Register  | Value              | Notes                          |
| --------- | ------------------ | ------------------------------ |
| GPIO1 dir | 0                  | All outputs                    |
| GPIO1 out | 0xFFFFFFFF         |                                |
| GPIO2 dir | 0                  | All outputs                    |
| GPIO2 out | 0xE7FFFFFF         | Bits 28:27 excluded (reserved) |
| GPIO3 dir | 0                  | All outputs                    |
| GPIO3 out | 0xFFFFFFFF         |                                |
| GPIO4 dir | Low 3 bits cleared |                                |
| GPIO4 out | Low 3 bits set     |                                |

Then read back and verify:

| Input Register | Expected   | Mask         |
| -------------- | ---------- | ------------ |
| GPIO1 in       | 0xFFFFFFFF | full         |
| GPIO2 in       | 0xE7FFFFFF | & 0xE7FFFFFF |
| GPIO3 in       | 0xFFFFFFFF | full         |
| GPIO4 in       | 0x07       | & 0x07       |

Any mismatch drives GPIO4[7] low (fail) and returns.

### Drive All-Zeroes Test

Clear all output registers to 0 and verify all input registers read 0
(with the same masks). Any mismatch drives GPIO4[7] low (fail).

## RTC/USB Indexed Register Window Test

After GPIO tests, the main test function:

1. Drives GPIO4[10] low then high (busy pulse).
2. Drives GPIO4[11] high (pass latch preset).
3. Tests 6 register windows sequentially.
4. On any window failure, drives GPIO4[11] low (fail latch).
5. Drives GPIO4[10] low (busy cleared = test complete).

### Indexed Register Interface

The RTC/USB sideband is accessed through a 14-bit indexed register interface:

**Write** (`rtcusb_write_indexed14(window, value)`):

1. Clear SYSCTRL+0x50 bits [18:0] (shift right 19, shift left 19).
2. OR in: `window | (value & 0x3FFF) | 0x40000` (bit 18 = write strobe).
3. Poll SYSCTRL+0x4C bit 24 until set (transfer complete).

**Read** (`rtcusb_read_indexed14(window)`):

1. Clear SYSCTRL+0x50 bits [18:0].
2. OR in: `window | 0x60000` (bits 18:17 = read strobe).
3. Poll SYSCTRL+0x4C bit 24 until set.
4. Return SYSCTRL+0x54 & 0x3FFF (low 14 bits of the read-back register).

### Window Addresses

| Window | Hex     | Purpose [unverified] |
| ------ | ------- | -------------------- |
| 0      | 0x00000 | RTC window 0         |
| 1      | 0x04000 | RTC window 1         |
| 2      | 0x08000 | RTC window 2         |
| 3      | 0x0C000 | RTC window 3         |
| 4      | 0x10000 | USB window 0         |
| 5      | 0x14000 | USB window 1         |

### Test Pattern

Each window is tested with 4 write-read-verify cycles using complementary
bit patterns:

| Step | Write Value | Purpose                        |
| ---- | ----------- | ------------------------------ |
| 1    | 0x3FFF      | All-ones (within 14-bit range) |
| 2    | 0x0000      | All-zeroes                     |
| 3    | 0x1555      | Alternating bits (0101...)     |
| 4    | 0x2AAA      | Alternating bits (1010...)     |

### Per-Window Expected Masks

Not all bits in every window are writable. The test applies per-window
masks to the read-back value before comparison:

| Window  | All-1s mask | All-0s mask | 0x1555 mask | 0x2AAA mask |
| ------- | ----------- | ----------- | ----------- | ----------- |
| 0x00000 | 0xFFE       | 0xFFE       | 0x554       | 0xAAA       |
| 0x04000 | 0x3FFF      | 0x3FFF      | 0x1555      | 0x2AAA      |
| 0x08000 | 0xFFF       | 0xFFF       | 0x555       | 0xAAA       |
| 0x0C000 | 0x3FFF      | 0x3FFF      | 0x1555      | 0x2AAA      |
| 0x10000 | 0x1FFD      | 0x1FFD      | 0x1554      | 0xAA9       |
| 0x14000 | 0x3FDF      | 0x3FDF      | 0x1555      | 0x2A8A      |

These masks reflect read-only, reserved, or always-set/clear bits in each
register window. If any verification step fails, the function returns 0
immediately (fail), and the outer loop drives the fail indicator low.

## Post-Test Behavior

After all tests complete (or any test fails), the diagnostic mode enters
an infinite `while(1)` loop. The GPIO4 output pins retain their final state,
allowing external test equipment to read the pass/fail result.
