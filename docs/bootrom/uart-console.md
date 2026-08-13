# AP2-BIOS UART Console

The bootrom carries an interactive UART console with the name "AP2-BIOS". It starts as a forced boot mode (DGPIO[3]=1, DGPIO[2]=0). It also starts as the fallback when neither SPI nor NAND flash holds a valid boot image.

## Entry

The entry function (`enter_ap2_bios_console`) initializes the UART hardware: the sharepin mux, the L2 buffer assignment, and the baud rate at the UART base +0x00 = 0x20026000. It then enters the console loop.

The UART initialization sets:

- SYSCTRL+0x78 bit 9 (sharepin UART TX/RX enable)
- L2CTR_DMA_PATH_CFG (0x2002C084) bits [29:28] (UART L2 buffer path)
- UART+0x00 = 807405133 (0x3020064D), the baud rate and frame config
- UART+0x0C = 0

## Console Loop

The console shows this prompt:

```
AP2-BIOS>#
```

It then reads one character at a time from the UART. The input goes into a packed 16-byte buffer, 4 words, through the L2 buffer RX path. The console handles:

- **Printable characters**: echoed back and appended to the command buffer
- **Backspace (0x08)**: erases the last character, with a BS-SPACE-BS echo, and clears the matching bits in the packed buffer
- **Enter (0x0D)**: dispatches the command in the buffer, or reprints the prompt when the buffer is empty
- **Overflow**: an input longer than 14 characters prints `"too much!\n"` and restarts the prompt

## Command Table

The console has 4 built-in commands. It matches each one against the command buffer by exact string comparison.

| Command    | Function       | Description                                                  |
| ---------- | -------------- | ------------------------------------------------------------ |
| `go`       | `cmd_go`       | Prompt for an address and branch to it                       |
| `download` | `cmd_download` | Receive a binary file over UART into a given RAM address     |
| `dump`     | `cmd_dump`     | Show a memory range as 32-bit hex words                      |
| `setvalue` | `cmd_setvalue` | Write a 32-bit value to a given address                      |

For input that matches no command, the console prints `"Err Comm\n"`.

### `go`

Prompt: `"Input addr(0x30000000):"`

Reads a hexadecimal address from the UART. Enter with no input gives the default, 0x30000000. The console branches to the address as a function call. If the target returns, control comes back to the console.

### `download`

Prompt: `"Input down addr(0x30000000):"`

1. Reads a hex destination address. The default is 0x30000000.
2. Prints the address and `"Select your file:"`.
3. Reads 4 bytes as a length header and subtracts 6. The result is the payload byte count.
4. Receives `payload_count + 2` bytes into the destination buffer. The last 2 bytes of the received data are the expected checksum.
5. Extracts the 16-bit checksum from the tail of the received data. It handles the 4 possible byte alignments inside the packed 32-bit words.
6. Computes a running 16-bit sum over the payload bytes.
7. Prints `"Down OK!\n"` when the two checksums agree, or `"Down faild!\n"` [sic] when they disagree.

### `dump`

Prompts:

- `"Input start addr(0x40000000):"`
- `"Input end addr(0x40000000):"`

The start address prompt shows `0x40000000`, but the default that the console passes to the hex input parser is **0x30000000**, the DDR base. Enter with no input therefore gives a start address of 0x30000000, not the 0x40000000 of the prompt. The end address default does match its prompt at 0x40000000.

The console reads 32-bit words from the start address through the end address, inclusive, and prints 4 words per line:

```
   Adress      0        4         8            c
0xADDRESS:  0xVALUE  0xVALUE  0xVALUE  0xVALUE
```

### `setvalue`

Prompts:

- `"Input addr(0xfffffff0):"`
- `"Input value(0xfffffff0):"`

Writes the 32-bit value to the given address, reads it back, and prints the address together with the read-back value.

## Hex Input Parser (`uart_prompt_hex32`)

All address and value prompts share one hex input routine. It:

1. Reads characters from the UART and echoes each one.
2. Converts an ASCII hex digit (0-9, a-f, A-F) into a 4-bit nibble. The lookup function returns 0xFF for an invalid character.
3. Packs up to 8 nibbles into a 32-bit value, MSB first.
4. Returns the default value from the prompt on Enter with no input.
5. Returns the parsed value on Enter with valid input.
6. Returns failure, 0, after an invalid hex digit or after more than 8 digits.
7. Accepts backspace to erase the last digit.

## UART I/O Internals

### Transmit (`uart_putc`)

1. Sets L2CTR_BUF8_15_CFG (0x2002C08C) bit 16. This enables the UART TX-side L2 path.
2. Writes the character word to `L2_UART_TX_PORT` at 0x48001000.
3. Clears `L2_UART_TX_FRAC_PORT` at 0x4800103C.
4. Sets UART+0x00 bit 28 (TX start) and the TX trigger bits in UART+0x04.
5. Polls UART+0x08 bits [12:0] until the count reaches 0. This means TX complete.

### Receive (`uart_get_rx_word`)

1. Sets UART+0x00 bit 23 (RX enable).
2. Polls UART+0x04 bit 30 until RX data arrives.
3. Reads the L2 buffer index from UART+0x08 bits [17:13] to locate the RX data in L2 SRAM. For `idx != 0` the address is `L2_UART_RX_PAGE_BASE + idx*4` = `0x4800107C + idx*4`. For `idx == 0` the bootrom reads `L2_UART_RX_PAGE0` at 0x480010FC instead.
4. Reads one 32-bit word from the selected L2 UART RX page.
5. Checks UART+0x04 bit 2, the fractional flag. When it is set, the routine reads the fractional byte count from UART+0x08 bits [24:23] and masks the word. Otherwise all 4 bytes are valid.
6. Returns the number of valid bytes, 1 to 4, and the packed data word.

### String Output (`uart_puts`)

Steps over the input string 4 bytes at a time, as 32-bit words for speed on the ARM bus. It sends each non-zero byte with `uart_putc` until it reaches the NUL terminator.

### Packed Byte Receive (`uart_recv_packed_bytes`)

A higher-level receive function. It accumulates UART data into a 32-bit word array and handles the case where one `uart_get_rx_word` call returns bytes that span two output words. With a non-zero `exact_len` it loops until it reaches that byte count. Otherwise it returns after one chunk.
