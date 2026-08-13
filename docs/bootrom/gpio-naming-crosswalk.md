# GPIO Naming Crosswalk

This note uses three naming layers:

- our bank and bit notation from reverse engineering
- the SoC pin name in the schematic
- the board net on that pin

The table below has one row per GPIO or DGPIO name on the AK7802 QFP216 schematic symbol.

## Crosswalk Table

| AIPC OS naming | SoC pin name in schematic | Actual board connection |
| --- | --- | --- |
| `GPIO1[0]` | `GPIO0` | `TMS` |
| `GPIO1[1]` | `GPIO1` | `TDI` |
| `GPIO1[2]` | `GPIO2` | `TCK -> LAN_RST#` |
| `GPIO1[3]` | `GPIO3` | `TDO` |
| `GPIO1[4]` | `GPIO4` | `RTCK` |
| `GPIO1[5]` | `GPIO5` | `GPIO5 -> WIFI_PD` |
| `GPIO1[6]` | `GPIO6` | `GPIO6 -> W_SYS_RST` |
| `GPIO1[7]` | `GPIO7` | `GPIO7 -> SD_WP0` |
| `GPIO1[8]` | `GPIO8` | `DRVBUS` |
| `GPIO1[9]` | `GPIO9` | `GPIO9 -> WLED_PWM` |
| `GPIO1[10]` | `GPIO10` | `GPIO10 -> WIFI_POWER` |
| `GPIO1[11]` | Not found | - |
| `GPIO1[12]` | Not found | - |
| `GPIO1[13]` | `GPIO13` | `GPIO3 -> SD_CD#` |
| `GPIO1[14]` | `GPIO14` | `AK_UARTTXD0 -> TOUCHPAD_CLK` |
| `GPIO1[15]` | `GPIO15` | `AK_UARTRXD0 -> TOUCHPAD_DAT` |
| `GPIO1[16]` | Not found | - |
| `GPIO1[17]` | Not found | - |
| `GPIO1[18]` | Not found | - |
| `GPIO1[19]` | Not found | - |
| `GPIO1[20]` | Not found | - |
| `GPIO1[21]` | Not found | - |
| `GPIO1[23]` | Not found | - |
| `GPIO1[23]` | Not found | - |
| `GPIO1[24]` | `GPIO24` | `WIFI_SDIO_D0` |
| `GPIO1[25]` | `GPIO25` | `WIFI_SDIO_D1` |
| `GPIO1[26]` | `GPIO26` | `WIFI_SDIO_D2` |
| `GPIO1[27]` | `GPIO[27]` | `WIFI_SDIO_D3` |
| `GPIO1[28]` | `GPIO28` | `WIFI_SDIO_CLK` |
| `GPIO1[29]` | `GPIO29` | `WIFI_SDIO_CMD` |
| `GPIO1[30]` | `GPIO30` | `NFC_D0` |
| `GPIO1[31]` | `GPIO31` | `NFC_D1` |
| `GPIO2[0]` | `GPIO32` | `NFC_D2` |
| `GPIO2[1]` | `GPIO33` | `NFC_D3` |
| `GPIO2[2]` | `GPIO34` | `NFC_D4` |
| `GPIO2[3]` | `GPIO35` | `NFC_D5` |
| `GPIO2[4]` | `GPIO36` | `NFC_D6` |
| `GPIO2[5]` | `GPIO37` | `NFC_D7` |
| `GPIO2[6]` | `GPIO38` | `NFC_CE1` |
| `GPIO2[7]` | `GPIO39` | `MCIO_CMD` |
| `GPIO2[8]` | `GPIO40` | `MCIO_CLK` |
| `GPIO2[9]` | `GPIO41` | `NFC_RB` |
| `GPIO2[10]` | `GPIO42` | `NFC_CE0` |
| `GPIO2[11]` | `GPIO43` | `NFC_RD` |
| `GPIO2[12]` | `GPIO44` | `NFC_WE` |
| `GPIO2[13]` | `GPIO45` | `NFC_CLE` |
| `GPIO2[14]` | `GPIO46` | `NFC_ALE` |
| `GPIO2[15]` | `GPIO47` | `EBI_ADDR3` |
| `GPIO2[16]` | `GPIO48` | `HBI_D0` |
| `GPIO2[17]` | `GPIO49` | `HBI_D1` |
| `GPIO2[18]` | `GPIO50` | `HBI_D2` |
| `GPIO2[19]` | `GPIO51` | `HBI_D3` |
| `GPIO2[20]` | `GPIO52` | `HBI_D4` |
| `GPIO2[21]` | `GPIO53` | `HBI_D5` |
| `GPIO2[22]` | `GPIO54` | `HBI_D6` |
| `GPIO2[23]` | `GPIO55` | `HBI_D7` |
| `GPIO2[24]` | `GPIO56` | `RLAN_INT#` |
| `GPIO2[25]` | `GPIO57` | `Z_nSMOEN` |
| `GPIO2[26]` | `GPIO58` | `Z_nSMWEN` |
| `GPIO2[27]` | Not found | - |
| `GPIO2[28]` | Not found | - |
| `GPIO2[29]` | `GPIO61` | `LCD_D9` |
| `GPIO2[30]` | `GPIO62` | `LCD_D10` |
| `GPIO2[31]` | `GPIO63` | `LCD_D11` |
| `GPIO3[0]` | `GPIO64` | `LCD_D13` |
| `GPIO3[1]` | `GPIO65` | `LCD_D14` |
| `GPIO3[2]` | `GPIO66` | `LCD_D15` |
| `GPIO3[3]` | `GPIO67` | `LCD_D16` |
| `GPIO3[4]` | `GPIO68` | `LCD_D17` |
| `GPIO3[5]` | `GPIO69` | `LCD_D6` |
| `GPIO3[6]` | `GPIO70` | `LCD_D12` |
| `GPIO3[7]` | `GPIO71` | `SPI_INT#` |
| `GPIO3[8]` | Not found | - |
| `GPIO3[9]` | Not found | - |
| `GPIO3[10]` | Not found | - |
| `GPIO3[11]` | Not found | - |
| `GPIO3[12]` | `GPIO76` | `SPI_CS` |
| `GPIO3[13]` | `GPIO77` | `SPI_CLK` |
| `GPIO3[14]` | `GPIO78` | `SPI_DOUT` |
| `GPIO3[15]` | `GPIO79` | `SPI_DIN` |
| `GPIO4[6]` | `DGPIO0` | `GPIO0 -> Z_nSMCS0` |
| `GPIO4[7]` | `DGPIO1` | `GPIO1 -> AC_DET` |
| `GPIO4[in 5], GPIO4[8]` | `DGPIO2` | `USB_BOOT` |
| `GPIO4[in 6], GPIO4[9]` | `DGPIO3` | `BOOT0 -> POWER_ON` |
| `GPIO4[10] or GPIO4[11]` | `DGPIO19` | `GPIO19 -> SPI_CS#` |
| `GPIO4[10] or GPIO4[11]` | `DGPIO28` | `GPIO28 -> USB_SLAEN` |

## Notes

- `GPIO1` to `GPIO4` are our names for four 32-bit SYSCTRL register windows. The schematic does not use these names.
- `GPIO2[27]` and `GPIO2[28]` have no named package pin in the QFP216 schematic pin tables. This matches the bootrom diagnostic mask `0xE7FFFFFF`.
- The order of `GPIO4[10]` and `GPIO4[11]` against `DGPIO19` and `DGPIO28` is still unresolved.
- `GPIO2[15]` through `GPIO2[26]`, together with `GPIO4[6]`, form the parallel bus of the DM9000A Ethernet controller. `GPIO1[2]` also serves as its reset line. The `Z_nSM*` and `HBI_*` net names come from a static-memory-bus convention, but no shared-pin function of the AK7802 maps such a bus to these pins. They are ordinary GPIOs under software control. See [docs/nk/dm9000-driver.md](../nk/dm9000-driver.md).
