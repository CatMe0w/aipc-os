# Diagnostic Self-Test Mode

Diagnostic mode is a factory test path. It starts when DGPIO[3] and DGPIO[2] both stay high through the boot override sampling. All 5 polls must see both bits asserted. The bootrom sets SYSCTRL+0x54 = 0x05000000, then enters `bootrom_diag_mode()`, which never returns.

## Overview

Diagnostic mode runs two groups of hardware self-tests:

1. **GPIO/sharepin connectivity test**: makes sure that all GPIO groups can drive all-ones and all-zeroes.
2. **RTC/USB indexed register window test**: writes and reads back test patterns across 6 register windows to make sure that the indexed sideband interface works.

Two GPIO4 output pins carry the results. After the tests, the bootrom enters an idle loop that never ends.

## GPIO4 Test Output Pins

Diagnostic mode uses GPIO4 bits 10 and 11 as test status indicators. The bit index is the argument plus 6, thus argument 4 drives bit 10 and argument 5 drives bit 11.

| Pin (bit) | Role           | Driven by  |
| --------- | -------------- | ---------- |
| GPIO4[10] | Busy indicator | Argument 4 |
| GPIO4[11] | Pass latch     | Argument 5 |

The pin driving functions work as follows:

- **Drive high**: clear the direction bit in SYSCTRL+0x94, set the output bit in SYSCTRL+0x98.
- **Drive low**: clear the direction bit in SYSCTRL+0x94, clear the output bit in SYSCTRL+0x98.

GPIO4 bits 6 and 7, arguments 0 and 1, are more status indicators for the GPIO/sharepin test phase.

## Initialization (`diag_init`)

1. Drive GPIO4[6] low (clear arg 0).
2. Drive GPIO4[6] high (set arg 0). This signals the test start.
3. Drive GPIO4[7] high (set arg 1). This is the pass indicator of the GPIO test.
4. Run the GPIO/sharepin connectivity test.
5. Drive GPIO4[6] low. This signals the end of the GPIO test phase.

## GPIO/Sharepin Connectivity Test (`gpio_mux_selftest`)

This test makes sure that all four GPIO groups, GPIO1 to GPIO4, drive and read back correctly.

### Setup

1. Clear the sharepin mux registers: SYSCTRL+0x74 = 0, SYSCTRL+0x78 = 0. This switches all sharepins to GPIO mode.
2. Set the I/O control: SYSCTRL+0xD4 |= 0x3FFFC (bits [17:2]) and SYSCTRL+0xD4 |= 0xC000000 (bits [27:26]).
3. Set the GPIO1 direction register to 0: SYSCTRL+0x7C = 0.

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

Then read back and compare:

| Input Register | Expected   | Mask         |
| -------------- | ---------- | ------------ |
| GPIO1 in       | 0xFFFFFFFF | full         |
| GPIO2 in       | 0xE7FFFFFF | & 0xE7FFFFFF |
| GPIO3 in       | 0xFFFFFFFF | full         |
| GPIO4 in       | 0x07       | & 0x07       |

Any mismatch drives GPIO4[7] low, which means fail, and returns.

### Drive All-Zeroes Test

Clear all output registers to 0 and make sure that all input registers read 0, with the same masks. Any mismatch drives GPIO4[7] low, which means fail.

## RTC/USB Indexed Register Window Test

After the GPIO tests, the main test function (`run_rtcusb_selftest`):

1. Drives GPIO4[10] low, then high. This is the busy pulse.
2. Drives GPIO4[11] high. This presets the pass latch.
3. Tests the 6 register windows in order.
4. Drives GPIO4[11] low on any window failure. This is the fail latch.
5. Drives GPIO4[10] low. Busy clear means that the test is complete.

### Indexed Register Interface

A 14-bit indexed register interface reaches the RTC/USB sideband.

**Write** (`rtcusb_write_indexed14(window, value)`):

1. Clear SYSCTRL+0x50 bits [18:0] (shift right 19, shift left 19).
2. OR in `window | (value & 0x3FFF) | 0x40000`, where bit 18 is the write strobe.
3. Poll SYSCTRL+0x4C bit 24 until it sets. This means transfer complete.

**Read** (`rtcusb_read_indexed14(window)`):

1. Clear SYSCTRL+0x50 bits [18:0].
2. OR in `window | 0x60000`, where bits 18:17 are the read strobe.
3. Poll SYSCTRL+0x4C bit 24 until it sets.
4. Return SYSCTRL+0x54 & 0x3FFF, the low 14 bits of the read-back register.

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

Each window gets 4 write-read-compare cycles with complementary bit patterns:

| Step | Write Value | Purpose                        |
| ---- | ----------- | ------------------------------ |
| 1    | 0x3FFF      | All-ones (within 14-bit range) |
| 2    | 0x0000      | All-zeroes                     |
| 3    | 0x1555      | Alternating bits (0101...)     |
| 4    | 0x2AAA      | Alternating bits (1010...)     |

### Per-Window Expected Masks

Not every bit in every window is writable. The test applies a per-window mask to the read-back value before the comparison:

| Window  | All-1s mask | All-0s mask | 0x1555 mask | 0x2AAA mask |
| ------- | ----------- | ----------- | ----------- | ----------- |
| 0x00000 | 0xFFE       | 0xFFE       | 0x554       | 0xAAA       |
| 0x04000 | 0x3FFF      | 0x3FFF      | 0x1555      | 0x2AAA      |
| 0x08000 | 0xFFF       | 0xFFF       | 0x555       | 0xAAA       |
| 0x0C000 | 0x3FFF      | 0x3FFF      | 0x1555      | 0x2AAA      |
| 0x10000 | 0x1FFD      | 0x1FFD      | 0x1554      | 0xAA9       |
| 0x14000 | 0x3FDF      | 0x3FDF      | 0x1555      | 0x2A8A      |

These masks come from the read-only, reserved, and always-set or always-clear bits of each register window. On any failed comparison the function returns 0 immediately, and the outer loop drives the fail indicator low.

## Post-Test Behavior

After all tests complete, or after any test fails, diagnostic mode enters a `while(1)` loop. The GPIO4 output pins hold their final state, thus external test equipment can read the pass or fail result.
