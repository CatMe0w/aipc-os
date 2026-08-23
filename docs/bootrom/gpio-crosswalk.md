# GPIO Crosswalk

Every GPIO on this SoC has one number, and that number is where the pin sits in the SYSCTRL GPIO register file:

```
number = bank * 32 + bit
```

Bank 0 is the `GPIO1` register window, bank 1 is `GPIO2`, bank 2 is `GPIO3` and bank 3 is `GPIO4`. To reach the registers of pin `n`, split it back with `bank = n / 32` and `bit = n % 32`, then use the offsets in [memory-map.md](memory-map.md). The bootrom, EBOOT, the WinCE OAL and our Linux gpiochip all count pins this way, thus `105` means the same pin in all four. Refer to a pin by its number, and to a signal by its net name.

The table below also writes the split out, as `GPIO4[9]`. That form is the arithmetic done for you, not a second name for the pin.

## The Number and the Schematic Name

The AK7802 has 70 GPIOs, and the schematic symbol names them in two families.

64 are shared with a peripheral function, and the symbol names them `GPIO0` to `GPIO79`. **For these the name and the number are the same number**: pin 9 is `GPIO9`, and there is nothing to convert. The index space is sparse, thus 16 of the 80 names do not exist. The gaps are 11 to 12, 16 to 23, 59 to 60 and 72 to 75, and they are the `Not found` rows below.

6 are "dedicated", and the symbol names them `DGPIO0`, `DGPIO1`, `DGPIO2`, `DGPIO3`, `DGPIO19` and `DGPIO28`. Those names are not positions. These six are therefore the only pins on the part where the name and the number differ. They occupy 102 to 107, in the order the names run. [The Dedicated Pins in Detail](#the-dedicated-pins-in-detail) has all six, and they need their own table for two other reasons as well.

The worst pair to mix up is `DGPIO3`, pin 105, which carries `POWER_ON`, against `GPIO3`, pin 3, which carries the power-key sense. Both are power signals, and the names differ by one letter.

## Small Integers That Are Not Pin Numbers

#### A board net named `GPIOn`

The board designer reused that shape for net labels, and the labels do not follow the schematic pin names. The net `GPIO0` is on pin 102, and the net `GPIO3` is on pin 13. Read the last column of the table as net labels only.

#### An alt function ID (EBOOT)

EBOOT routes pads with `gpio_enable_alt(id)`, where `id` runs 0 to 56 and has no relation to a pin number. See [docs/eboot/gpio-driver.md](../eboot/gpio-driver.md).

## Crosswalk Table

| Pin | Register bit | SoC pin name in schematic | Actual board connection |
| --- | --- | --- | --- |
| 0 | `GPIO1[0]` | `GPIO0` | `TMS` |
| 1 | `GPIO1[1]` | `GPIO1` | `TDI` |
| 2 | `GPIO1[2]` | `GPIO2` | `TCK -> LAN_RST#` |
| 3 | `GPIO1[3]` | `GPIO3` | `TDO -> POWER_KEY` |
| 4 | `GPIO1[4]` | `GPIO4` | `RTCK -> KEY_L` |
| 5 | `GPIO1[5]` | `GPIO5` | `GPIO5 -> WIFI_PD` |
| 6 | `GPIO1[6]` | `GPIO6` | `GPIO6 -> W_SYS_RST` |
| 7 | `GPIO1[7]` | `GPIO7` | `GPIO7 -> SD_WP0` |
| 8 | `GPIO1[8]` | `GPIO8` | `DRVBUS` |
| 9 | `GPIO1[9]` | `GPIO9` | `GPIO9 -> WLED_PWM` |
| 10 | `GPIO1[10]` | `GPIO10` | `GPIO10 -> WIFI_POWER` |
| 11 | `GPIO1[11]` | Not found | - |
| 12 | `GPIO1[12]` | Not found | - |
| 13 | `GPIO1[13]` | `GPIO13` | `GPIO3 -> SD_CD#` |
| 14 | `GPIO1[14]` | `GPIO14` | `AK_UARTTXD0 -> TOUCHPAD_CLK` |
| 15 | `GPIO1[15]` | `GPIO15` | `AK_UARTRXD0 -> TOUCHPAD_DAT` |
| 16 | `GPIO1[16]` | Not found | - |
| 17 | `GPIO1[17]` | Not found | - |
| 18 | `GPIO1[18]` | Not found | - |
| 19 | `GPIO1[19]` | Not found | - |
| 20 | `GPIO1[20]` | Not found | - |
| 21 | `GPIO1[21]` | Not found | - |
| 22 | `GPIO1[22]` | Not found | - |
| 23 | `GPIO1[23]` | Not found | - |
| 24 | `GPIO1[24]` | `GPIO24` | `WIFI_SDIO_D0` |
| 25 | `GPIO1[25]` | `GPIO25` | `WIFI_SDIO_D1` |
| 26 | `GPIO1[26]` | `GPIO26` | `WIFI_SDIO_D2` |
| 27 | `GPIO1[27]` | `GPIO[27]` | `WIFI_SDIO_D3` |
| 28 | `GPIO1[28]` | `GPIO28` | `WIFI_SDIO_CLK` |
| 29 | `GPIO1[29]` | `GPIO29` | `WIFI_SDIO_CMD` |
| 30 | `GPIO1[30]` | `GPIO30` | `NFC_D0` |
| 31 | `GPIO1[31]` | `GPIO31` | `NFC_D1` |
| 32 | `GPIO2[0]` | `GPIO32` | `NFC_D2` |
| 33 | `GPIO2[1]` | `GPIO33` | `NFC_D3` |
| 34 | `GPIO2[2]` | `GPIO34` | `NFC_D4` |
| 35 | `GPIO2[3]` | `GPIO35` | `NFC_D5` |
| 36 | `GPIO2[4]` | `GPIO36` | `NFC_D6` |
| 37 | `GPIO2[5]` | `GPIO37` | `NFC_D7` |
| 38 | `GPIO2[6]` | `GPIO38` | `NFC_CE1` |
| 39 | `GPIO2[7]` | `GPIO39` | `MCIO_CMD` |
| 40 | `GPIO2[8]` | `GPIO40` | `MCIO_CLK` |
| 41 | `GPIO2[9]` | `GPIO41` | `NFC_RB` |
| 42 | `GPIO2[10]` | `GPIO42` | `NFC_CE0` |
| 43 | `GPIO2[11]` | `GPIO43` | `NFC_RD` |
| 44 | `GPIO2[12]` | `GPIO44` | `NFC_WE` |
| 45 | `GPIO2[13]` | `GPIO45` | `NFC_CLE` |
| 46 | `GPIO2[14]` | `GPIO46` | `NFC_ALE` |
| 47 | `GPIO2[15]` | `GPIO47` | `EBI_ADDR3` |
| 48 | `GPIO2[16]` | `GPIO48` | `HBI_D0` |
| 49 | `GPIO2[17]` | `GPIO49` | `HBI_D1` |
| 50 | `GPIO2[18]` | `GPIO50` | `HBI_D2` |
| 51 | `GPIO2[19]` | `GPIO51` | `HBI_D3` |
| 52 | `GPIO2[20]` | `GPIO52` | `HBI_D4` |
| 53 | `GPIO2[21]` | `GPIO53` | `HBI_D5` |
| 54 | `GPIO2[22]` | `GPIO54` | `HBI_D6` |
| 55 | `GPIO2[23]` | `GPIO55` | `HBI_D7` |
| 56 | `GPIO2[24]` | `GPIO56` | `RLAN_INT#` |
| 57 | `GPIO2[25]` | `GPIO57` | `Z_nSMOEN` |
| 58 | `GPIO2[26]` | `GPIO58` | `Z_nSMWEN` |
| 59 | `GPIO2[27]` | Not found | - |
| 60 | `GPIO2[28]` | Not found | - |
| 61 | `GPIO2[29]` | `GPIO61` | `LCD_D9` |
| 62 | `GPIO2[30]` | `GPIO62` | `LCD_D10` |
| 63 | `GPIO2[31]` | `GPIO63` | `LCD_D11` |
| 64 | `GPIO3[0]` | `GPIO64` | `LCD_D13` |
| 65 | `GPIO3[1]` | `GPIO65` | `LCD_D14` |
| 66 | `GPIO3[2]` | `GPIO66` | `LCD_D15` |
| 67 | `GPIO3[3]` | `GPIO67` | `LCD_D16` |
| 68 | `GPIO3[4]` | `GPIO68` | `LCD_D17` |
| 69 | `GPIO3[5]` | `GPIO69` | `LCD_D6` |
| 70 | `GPIO3[6]` | `GPIO70` | `LCD_D12` |
| 71 | `GPIO3[7]` | `GPIO71` | `SPI_INT#` |
| 72 | `GPIO3[8]` | Not found | - |
| 73 | `GPIO3[9]` | Not found | - |
| 74 | `GPIO3[10]` | Not found | - |
| 75 | `GPIO3[11]` | Not found | - |
| 76 | `GPIO3[12]` | `GPIO76` | `SPI_CS` |
| 77 | `GPIO3[13]` | `GPIO77` | `SPI_CLK` |
| 78 | `GPIO3[14]` | `GPIO78` | `SPI_DOUT` |
| 79 | `GPIO3[15]` | `GPIO79` | `SPI_DIN` |
| 102 | `GPIO4[6]` | `DGPIO0` | `GPIO0 -> Z_nSMCS0` |
| 103 | `GPIO4[7]` | `DGPIO1` | `GPIO1 -> AC_DET` |
| 104 | `GPIO4[8]` | `DGPIO2` | `USB_BOOT` |
| 105 | `GPIO4[9]` | `DGPIO3` | `BOOT0 -> POWER_ON` |
| 106 or 107 | `GPIO4[10]` or `[11]` | `DGPIO19` | `GPIO19 -> SPI_CS#` |
| 106 or 107 | `GPIO4[10]` or `[11]` | `DGPIO28` | `GPIO28 -> USB_SLAEN` |

## The Dedicated Pins in Detail

The last six rows of the table are hard to read, because the dedicated window is the one place where every column has an exception. This table gives the same six pins with one column per fact:

| Pin | Out bit | In bit | SoC pin | Package pin | Net at the pin | Net at the far end |
| --- | --- | --- | --- | --- | --- | --- |
| 102 | `GPIO4[6]` | `GPIO4[in 3]` | `DGPIO0` | 206 | `GPIO0` | `Z_nSMCS0` |
| 103 | `GPIO4[7]` | `GPIO4[in 4]` | `DGPIO1` | 208 | `GPIO1` | `AC_DET` |
| 104 | `GPIO4[8]` | `GPIO4[in 5]` | `DGPIO2` | 210 | `USB_BOOT` | - |
| 105 | `GPIO4[9]` | `GPIO4[in 6]` | `DGPIO3` | 51 | `BOOT0` | `POWER_ON` |
| 106 or 107 | `GPIO4[10]` or `[11]` | `GPIO4[in 7]` or `[in 8]` | `DGPIO19` | 55 | `GPIO19` | `SPI_CS#` |
| 106 or 107 | `GPIO4[10]` or `[11]` | `GPIO4[in 7]` or `[in 8]` | `DGPIO28` | 53 | `GPIO28` | `USB_SLAEN` |

---

Four things in this table are easy to get wrong:

#### The DGPIO index is not the bit number

The six pins take output bits 6 to 11 in the order that their names run, `0, 1, 2, 3, 19, 28`. `DGPIO19` is not at bit 19. Output bits 0 to 5 have no pin.

#### The read bit is not the write bit

In this window only, the input bit is the output bit minus 3. Hardware confirms the rule on `DGPIO3`, and the read helper of EBOOT applies it to every pin from 99 up. The input bits of the other four rows come from the rule. No test reads them.

#### The net names in the last two columns are net labels

The net `GPIO0` is on pin `DGPIO0`, not on the SoC pin `GPIO0`, which is package pin 41 and carries `TMS`. The net `GPIO28` is worse, because a SoC pin `GPIO28` also exists, at `GPIO1[28]`, and it carries `WIFI_SDIO_CLK`.

#### `BOOT0` and `POWER_ON` are one wire with two roles

At reset the bootrom samples it as a boot strap. After software makes it an output it becomes the power hold. The rename in the schematic marks the change of role, not a change of wire.

## Notes

- Every `Not found` row is a gap in the sparse shared index space, not a pin that this document failed to trace. `GPIO2[27]` and `GPIO2[28]`, the gap at `GPIO59` and `GPIO60`, also match the bootrom diagnostic mask `0xE7FFFFFF`.
- The JTAG group is pins 0 to 3. Bit 0 of `SYSCTRL + 0x78` muxes it. The board uses two of the four for other signals, thus the second name in the table. `TDO` carries the power-key sense, and `TCK` carries `LAN_RST#`. See [docs/nk/power-management.md](../nk/power-management.md).
- Each pad in the group carries three functions, and the schematic symbol names all three. The package pins are `TMS/GPIO0/PCM_SYNC` = 41, `TDI/GPIO1/PCM_DATA_R` = 42, `TDO/GPIO3/PCM_DATA_T` = 43, `RTCK/GPIO4` = 44, `TCLK/GPIO2/PCM_CLK` = 45. The GPIO index does not follow the pin number in this group. `#TRST` is pin 36 and is not a GPIO.
- The PCM function and the power-key sense share one pad. A driver that enables PCM takes `GPIO3` away from the key.
- The order of `GPIO4[10]` and `GPIO4[11]` against `DGPIO19` and `DGPIO28` is still unresolved. Bits 6 to 9 hold `DGPIO0` to `DGPIO3` in index order, thus `GPIO4[10]` = `DGPIO19` and `GPIO4[11]` = `DGPIO28` is the consistent extension. No test confirms it. To confirm, drive one bit and watch `SPI_CS#` against `USB_SLAEN`.
- `GPIO2[15]` through `GPIO2[26]`, together with `GPIO4[6]`, form the parallel bus of the DM9000A Ethernet controller. `GPIO1[2]` also serves as its reset line. The `Z_nSM*` and `HBI_*` net names come from a static-memory-bus convention, but no shared-pin function of the AK7802 maps such a bus to these pins. They are ordinary GPIOs under software control. See [docs/nk/dm9000-driver.md](../nk/dm9000-driver.md).
