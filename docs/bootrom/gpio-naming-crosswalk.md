# GPIO Naming Crosswalk

This note uses three naming layers:

- our bank/bit notation from reverse engineering
- the SoC pin name used in the schematic
- the actual board net connected to that pin

The table below is flattened one row per GPIO or DGPIO name visible on the
AK7802 QFP216 schematic symbol.

## Crosswalk Table

| AIPC OS naming           | SoC pin name in schematic | Actual board connection       |
| ------------------------ | ------------------------- | ----------------------------- |
| `GPIO1[0]`               | `GPIO0`                   | `TMS`                         |
| `GPIO1[1]`               | `GPIO1`                   | `TDI`                         |
| `GPIO1[2]`               | `GPIO2`                   | `TCK`                         |
| `GPIO1[3]`               | `GPIO3`                   | `TDO`                         |
| `GPIO1[4]`               | `GPIO4`                   | `RTCK`                        |
| `GPIO1[5]`               | `GPIO5`                   | `GPIO5 -> WIFI_PD`            |
| `GPIO1[6]`               | `GPIO6`                   | `GPIO6 -> W_SYS_RST`          |
| `GPIO1[7]`               | `GPIO7`                   | `GPIO7 -> SD_WP0`             |
| `GPIO1[8]`               | `GPIO8`                   | `DRVBUS`                      |
| `GPIO1[9]`               | `GPIO9`                   | `GPIO9 -> WLED_PWM`           |
| `GPIO1[10]`              | `GPIO10`                  | `GPIO10 -> WIFI_POWER`        |
| `GPIO1[11]`              | Not found                 | -                             |
| `GPIO1[12]`              | Not found                 | -                             |
| `GPIO1[13]`              | `GPIO13`                  | `GPIO3 -> SD_CD#`             |
| `GPIO1[14]`              | `GPIO14`                  | `AK_UARTTXD0 -> TOUCHPAD_CLK` |
| `GPIO1[15]`              | `GPIO15`                  | `AK_UARTRXD0 -> TOUCHPAD_DAT` |
| `GPIO1[16]`              | Not found                 | -                             |
| `GPIO1[17]`              | Not found                 | -                             |
| `GPIO1[18]`              | Not found                 | -                             |
| `GPIO1[19]`              | Not found                 | -                             |
| `GPIO1[20]`              | Not found                 | -                             |
| `GPIO1[21]`              | Not found                 | -                             |
| `GPIO1[23]`              | Not found                 | -                             |
| `GPIO1[23]`              | Not found                 | -                             |
| `GPIO1[24]`              | `GPIO24`                  | `WIFI_SDIO_D0`                |
| `GPIO1[25]`              | `GPIO25`                  | `WIFI_SDIO_D1`                |
| `GPIO1[26]`              | `GPIO26`                  | `WIFI_SDIO_D2`                |
| `GPIO1[27]`              | `GPIO[27]`                | `WIFI_SDIO_D3`                |
| `GPIO1[28]`              | `GPIO28`                  | `WIFI_SDIO_CLK`               |
| `GPIO1[29]`              | `GPIO29`                  | `WIFI_SDIO_CMD`               |
| `GPIO1[30]`              | `GPIO30`                  | `NFC_D0`                      |
| `GPIO1[31]`              | `GPIO31`                  | `NFC_D1`                      |
| `GPIO2[0]`               | `GPIO32`                  | `NFC_D2`                      |
| `GPIO2[1]`               | `GPIO33`                  | `NFC_D3`                      |
| `GPIO2[2]`               | `GPIO34`                  | `NFC_D4`                      |
| `GPIO2[3]`               | `GPIO35`                  | `NFC_D5`                      |
| `GPIO2[4]`               | `GPIO36`                  | `NFC_D6`                      |
| `GPIO2[5]`               | `GPIO37`                  | `NFC_D7`                      |
| `GPIO2[6]`               | `GPIO38`                  | `NFC_CE1`                     |
| `GPIO2[7]`               | `GPIO39`                  | `MCIO_CMD`                    |
| `GPIO2[8]`               | `GPIO40`                  | `MCIO_CLK`                    |
| `GPIO2[9]`               | `GPIO41`                  | `NFC_RB`                      |
| `GPIO2[10]`              | `GPIO42`                  | `NFC_CE0`                     |
| `GPIO2[11]`              | `GPIO43`                  | `NFC_RD`                      |
| `GPIO2[12]`              | `GPIO44`                  | `NFC_WE`                      |
| `GPIO2[13]`              | `GPIO45`                  | `NFC_CLE`                     |
| `GPIO2[14]`              | `GPIO46`                  | `NFC_ALE`                     |
| `GPIO2[15]`              | `GPIO47`                  | `GPIO47`                      |
| `GPIO2[16]`              | `GPIO48`                  | `GPIO48`                      |
| `GPIO2[17]`              | `GPIO49`                  | `GPIO49`                      |
| `GPIO2[18]`              | `GPIO50`                  | `GPIO50`                      |
| `GPIO2[19]`              | `GPIO51`                  | `GPIO51`                      |
| `GPIO2[20]`              | `GPIO52`                  | `GPIO52`                      |
| `GPIO2[21]`              | `GPIO53`                  | `GPIO53`                      |
| `GPIO2[22]`              | `GPIO54`                  | `GPIO54`                      |
| `GPIO2[23]`              | `GPIO55`                  | `GPIO55`                      |
| `GPIO2[24]`              | `GPIO56`                  | `GPIO56`                      |
| `GPIO2[25]`              | `GPIO57`                  | `GPIO57`                      |
| `GPIO2[26]`              | `GPIO58`                  | `GPIO58`                      |
| `GPIO2[27]`              | Not found                 | -                             |
| `GPIO2[28]`              | Not found                 | -                             |
| `GPIO2[29]`              | `GPIO61`                  | `LCD_D9`                      |
| `GPIO2[30]`              | `GPIO62`                  | `LCD_D10`                     |
| `GPIO2[31]`              | `GPIO63`                  | `LCD_D11`                     |
| `GPIO3[0]`               | `GPIO64`                  | `LCD_D13`                     |
| `GPIO3[1]`               | `GPIO65`                  | `LCD_D14`                     |
| `GPIO3[2]`               | `GPIO66`                  | `LCD_D15`                     |
| `GPIO3[3]`               | `GPIO67`                  | `LCD_D16`                     |
| `GPIO3[4]`               | `GPIO68`                  | `LCD_D17`                     |
| `GPIO3[5]`               | `GPIO69`                  | `LCD_D6`                      |
| `GPIO3[6]`               | `GPIO70`                  | `LCD_D12`                     |
| `GPIO3[7]`               | `GPIO71`                  | `SPI_INT#`                    |
| `GPIO3[8]`               | Not found                 | -                             |
| `GPIO3[9]`               | Not found                 | -                             |
| `GPIO3[10]`              | Not found                 | -                             |
| `GPIO3[11]`              | Not found                 | -                             |
| `GPIO3[12]`              | `GPIO76`                  | `SPI_CS`                      |
| `GPIO3[13]`              | `GPIO77`                  | `SPI_CLK`                     |
| `GPIO3[14]`              | `GPIO78`                  | `SPI_DOUT`                    |
| `GPIO3[15]`              | `GPIO79`                  | `SPI_DIN`                     |
| `GPIO4[6]`               | `DGPIO0`                  | `GPIO0`                       |
| `GPIO4[7]`               | `DGPIO1`                  | `GPIO1 -> AC_DET`             |
| `GPIO4[in 5], GPIO4[8]`  | `DGPIO2`                  | `USB_BOOT`                    |
| `GPIO4[in 6], GPIO4[9]`  | `DGPIO3`                  | `BOOT0 -> POWER_ON`           |
| `GPIO4[10] or GPIO4[11]` | `DGPIO19`                 | `GPIO19 -> SPI_CS#`           |
| `GPIO4[10] or GPIO4[11]` | `DGPIO28`                 | `GPIO28 -> USB_SLAEN`         |

## Notes

- `GPIO1`..`GPIO4` are our internal names for four 32-bit SYSCTRL register
  windows. They are not the names used by the schematic.
- `GPIO2[27]` and `GPIO2[28]` do not have named package pins in the
  QFP216 schematic pin tables; this matches the bootrom diagnostic mask
  `0xE7FFFFFF`.
- The ordering of `GPIO4[10]` and `GPIO4[11]` against `DGPIO19` and
  `DGPIO28` is still unresolved.
