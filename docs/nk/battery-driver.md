# Battery Driver

This document describes how the v1.58.2 WinCE firmware measures the battery and reports its state. Two modules do the work. `ak_adc.dll` samples the battery voltage, and `battdrvr.dll` turns the sample into the status that WinCE shows. EBOOT also reads the battery once before it loads NK.

`battdrvr.dll` is the standard WinCE battery MDD and a vendor PDD in one module. Addresses in this document are for that module rebuilt at base `0x10000000`, and for `ak_adc.dll` at base `0x82A05000`. See [Module Rebuild](module-rebuild.md).

The correctness of the battery circuit and its driver is in doubt. See [Unresolved](#unresolved).

## Connections

| Signal | SoC input | Circuit |
| --- | --- | --- |
| `BAT_AD` | ADC1 `AD5`, package pin 152 | `VBAT` through `R128` 30K 1% and `R129` 10K 1%, then `R49` 1K. `C176` 1 uF to ground after `R49` |
| `AC_DET` | GPIO103 (`DGPIO1`), package pin 208 | The junction of `R132` 300K from the DC jack and `R136` 100K to ground, through `R75` 100R |

The divider on `BAT_AD` is 1/4. `BAT_AD` measures `VBAT` before `Q4`, which is the battery terminal and the charger output. The system rail is after `Q4`.

GPIO103 reads through the shifted input bit of GPIO bank 4. See [the GPIO crosswalk](../bootrom/gpio-crosswalk.md#the-read-bit-is-not-the-write-bit).

Because the charger output and the battery terminal are the same node, the ADC reads the charger output when no battery is connected. The ADC therefore cannot detect whether a battery is present.

## ADC1 Setup

Two IOCTL calls from the driver initialization (`0x10003048`) to `ADC1:` configure ADC1. EBOOT runs the same register sequence in its boot-time battery check. No other module in the image opens `ADC1:`.

| Register | Change | Effect |
| --- | --- | --- |
| `SYSCTRL+0x08` | Clear bit 22, then set it | Pulses the ADC1 reset |
| `SYSCTRL+0x08` | Clear bit 29 | Powers up ADC1 |
| `SYSCTRL+0x08` | OR `0xA` | Sets bit 3, the ADC1 clock enable, and bit 1 of the divider field [2:0]. From the reset value 0, the clock is 12 MHz / 3 |
| `SYSCTRL+0x60` | OR `0x320` | Sets 800 in the bit cycle field [15:0] |
| `SYSCTRL+0x5C` | Clear bit 0 | Powers the analog reference |
| `SYSCTRL+0x64` | Set bit 8 | Enables ADC1 |
| `SYSCTRL+0x64` | Set bit 11 | Selects `AD5`, see below |

The driver does not write the hold field `SYSCTRL+0x60` [31:16] or `SYSCTRL+0x64` [7:0].

## Sampling

`ak_adc.dll` reads `AD5` in its handler for IOCTL `0x0101226C` with channel 2 (`0x82A0628C`). Channel 2 is the `AD5` input. The channel number does not match the input number.

1. Clear `SYSCTRL+0x5C` bit 29.
2. Wait 12 ms.
3. Set `SYSCTRL+0x64` bit 11.
4. Read `SYSCTRL+0x70` [9:0]. Keep a non-zero value and wait 1 ms. For a zero value, wait 2 ms. Stop after 100 zero values and fail.
5. Continue until there are five non-zero values, then divide their sum by 5.
6. Set `SYSCTRL+0x5C` bit 29 and clear `SYSCTRL+0x64` bit 11.

Bit 29 clear and bit 11 set select `AD5`.

`ak_adc.dll` then applies a cache. It returns the new average only if it differs from the cached value by 50 or less, or if the cache holds its initial value `0x400`. Otherwise it returns the cached value and does not update the cache. A step of more than 50, for example a battery connected while the system runs, therefore freezes the reported voltage until the next boot.

The failure exit in step 4 skips step 6 and leaves ADC1 in the `AD5` mode. The IOCTL handler still returns success, and `battdrvr.dll` does not examine the result.

## Voltage Level

`battdrvr.dll` converts the average `x` into a level `q` (`0x100032AC`):

```
q = 2 * (333 * x / 1024)      (integer division)
```

`q` is always even. One step of `q` is 20 mV of battery voltage, which assumes a 3.33 V ADC reference and the 1/4 divider. WinCE puts `q` itself in the `BatteryVoltage` field, although WinCE defines that field in millivolts.

## Capacity Curve

`0x10003470` maps `q` to a percentage with one linear segment per range. Each segment computes `(q - start) * 100 / divisor / 10 + base`, with two integer divisions.

| Range of `q` | `start` | `divisor` | `base` | Upper bound |
| --- | ---: | ---: | ---: | ---: |
| `q <= 300` | | | 0% | 6.00 V |
| `300 < q <= 345` | 300 | 45 | 0 | 6.90 V |
| `345 < q <= 368` | 345 | 23 | 5 | 7.36 V |
| `368 < q <= 374` | 368 | 6 | 10 | 7.48 V |
| `374 < q <= 377` | 374 | 3 | 20 | 7.54 V |
| `377 < q <= 379` | 377 | 2 | 30 | 7.58 V |
| `379 < q <= 382` | 379 | 3 | 40 | 7.64 V |
| `382 < q <= 387` | 382 | 5 | 50 | 7.74 V |
| `387 < q <= 392` | 387 | 5 | 60 | 7.84 V |
| `392 < q <= 398` | 392 | 6 | 70 | 7.96 V |
| `398 < q <= 406` | 398 | 8 | 80 | 8.12 V |
| `406 < q <= 420` | 406 | 14 | 90 | 8.40 V |
| `q > 420` | | | 100% | |

The curve is not monotonic. The first segment ends at 10%, but the second starts at 5%, so `q` = 344 gives 9% and 346 gives 5%. The second segment ends at 15%, but the third starts at 10%, so `q` = 368 gives 15% and 370 gives 13%. Every other segment ends at the base of the next one.

## Status

The PDD status function (`0x10002E20`) fills `SYSTEM_POWER_STATUS_EX2` as follows.

| Field | Value |
| --- | --- |
| `ACLineStatus` | 1 if GPIO103 reads 1, else 0 |
| `BatteryFlag` | 8 (charging) if GPIO103 reads 1. Else 1 (high) at 50% or more, 2 (low) from 11% to 49%, 4 (critical) at 10% or less |
| `BatteryLifePercent` | The curve result |
| `BatteryVoltage` | `q` |
| `BatteryChemistry` | 4 (lithium ion) |
| `BatteryCurrent`, `BatteryAverageCurrent`, `BatteryAverageInterval`, `BatteryTemperature` | Constants 100, 600, 10 and 30 |
| `BatteryLifeTime`, `BatteryFullLifeTime` | Constants 100 and 120 |
| `BackupBatteryFlag` | `0xFF` |

The presence callback always returns 1. The branch that prints `battery has been romoved` and sets the flags to `0xFF` does not run.

GPIO103 comes from OAL IOCTL `0x010120D8`, which returns the input level. The driver compares the result with 1. No other signal sets the charging flag, and nothing sets a full state.

## Polling

The MDD thread waits on its event with a timeout of `PollInterval` ms. The registry does not set `PollInterval`, thus the default 3000 ms applies. The thread priority is also a default, 249. After each timeout the thread calls the PDD status function and compares the result with the previous status. If any byte differs, it stores the new status and calls `PowerPolicyNotify(PPN_POWERCHANGE, 0)`.

## Low-Battery Actions

Only EBOOT acts on a low battery. The WinCE path sends an event that nothing receives.

EBOOT (v1.58.2 `0x80059DF0`) runs one check before it loads NK:

1. If GPIO103 reads 1, continue the boot.
2. Read `AD5` once with the setup above and calculate `q`. EBOOT prints `voltage = <q>`.
3. If `q` is more than 345 (6.90 V), continue the boot.
4. Read again. If `q` is still 345 or less, print `Battery low,please insert adapter!!!`.
5. Read GPIO103 three times, one second apart. If it reads 1, continue the boot.
6. Print `Power off !!!`, wait 1 s, and write GPIO105 (`POWER_ON`) low.

EBOOT prints the `voltage` message through its debug output. In the stock image, `OEMWriteDebugByte` is an empty function, thus this message does not show on the screen. The `Battery low` and `Power off` messages show on the screen.

A USB host keeps the device powered after step 6, see [USB Back-Power](power-management.md#usb-back-power).

In WinCE, the capacity function calls `0x100033E8` when GPIO103 reads 0. That function reads `q` five more times. If four or more reads give 342 (6.84 V) or less, it creates `LowBattEvent`, waits 1 s, and signals the event. It also has an entry check against 342, but the caller passes `q & 0xFF`, so the entry check always passes. `ak_pwrctl.dll` stores the `LowBattEvent` handle at index 4 of its wait array but waits on four handles, so it never receives the event. Its own check against `PowerOffBatteryPercent` (default 6) sets a result that its dispatch ignores. WinCE therefore does not shut down on a low battery.

## Unresolved

The correctness of the battery circuit and its driver is in doubt. This document records what the firmware does. Do not use it as a reference for a correct design. The driver derives the charging state from `AC_DET` alone. The schematic routes the charger status output `CHARGE_STAT` only to the indicator connector `J7` and shows no SoC pin for it. The board power path has other defects, for example the USB VBUS that feeds the main 5 V rail with no switch. See [USB Back-Power](power-management.md#usb-back-power).

On the v1.58.2 device, `AC_DET` also reads high when a battery is connected and the DC adapter is not. No other device was examined. The schematic shows no path from `VBAT` to `AC_DET`, thus the level can also be an electrical fault of that device. Because of this level, the v1.58.2 device reports AC online and charging in every state with a battery, as the battery-only rows in [Appendix: Linux Driver](#appendix-linux-driver) show. The levels under Linux were:

| Power sources | `AC_DET` |
| --- | ---: |
| USB, no battery | 0 |
| DC adapter, no battery | 1 |
| USB and battery | 1 |
| Battery only | 1 |
| Battery only, after a cold boot from battery | 1 |

A measurement of the DC jack node and the `R132`/`R136` junction in the battery-only state would separate a back-feed into the DC input from an input bias at the SoC pin.

## Appendix: Linux Driver

No reliable battery policy is available for this board. The schematic shows no SoC input for the charger status, and the ADC cannot detect whether a battery is present. The Linux driver therefore reproduces the original firmware as closely as possible. It uses the setup, the sampling, the voltage level, the capacity curve and the flag rules above, and polls every 3000 ms.

The driver differs from the original firmware in four points:

- It does not apply the 50-step cache of `ak_adc.dll`. A connected or removed battery shows in the next poll.
- A failed sample restores the `AD5` selection bits and keeps the previous status.
- It does not report the constant current, temperature and time fields.
- It reports `q * 20` mV as the battery voltage. WinCE reports `q`. On the v1.58.2 device, `x * 13320 / 1024` mV was within 0.6% of a multimeter on the battery terminal at four points from 7.79 V to 8.16 V. The meter and the ADC samples were not synchronized. The integer division in `q` removes up to 20 mV more.
