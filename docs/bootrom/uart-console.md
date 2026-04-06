# AP2-BIOS UART Console

The bootrom contains an interactive UART console referred to as "AP2-BIOS".
It is entered either as a forced boot mode (DGPIO[3]=1, DGPIO[2]=0), or as
the fallback when no valid boot image is found on SPI or NAND flash.

## Entry

The entry function initializes the UART hardware (sharepin mux, L2 buffer
assignment, baud rate configuration at UART base+0x00 = 0x20026000), then
enters the console loop.

The UART initialization sets:

- SYSCTRL+0x78 bit 9 (sharepin UART TX/RX enable)
- L2CTR_DMAFRAC bits [29:28] (UART L2 buffer path)
- UART+0x00 = 807405133 (0x30200A4D) - baud rate and frame config
- UART+0x0C = 0

## Console Loop

The console displays the prompt:

```
AP2-BIOS>#
```

It then reads characters one at a time from UART. Input is packed into a
16-byte (4-word) buffer using the L2 buffer RX path. The console supports:

- **Printable characters**: echoed back and appended to the command buffer
- **Backspace (0x08)**: erases the last character (with visual BS-SPACE-BS
  echo) and clears the corresponding bits in the packed buffer
- **Enter (0x0D)**: dispatches the accumulated command, or reprints the
  prompt if the buffer is empty
- **Overflow**: if the input exceeds 14 characters, prints `"too much!\n"`
  and restarts the prompt

## Command Table

The console has 4 built-in commands, matched by exact string comparison
against the command buffer:

| Command    | Description                                                  |
| ---------- | ------------------------------------------------------------ |
| `download` | Receive a binary file over UART into a specified RAM address |
| `go`       | Prompt for an address and branch to it                       |
| `dump`     | Display a memory range as 32-bit hex words                   |
| `setvalue` | Write a 32-bit value to a specified address                  |

If the input does not match any command, the console prints `"Err Comm\n"`.

### `go`

Prompts: `"Input addr(0x30000000):"`

Reads a hexadecimal address from UART (default: 0x30000000 if Enter is pressed
with no input). Branches to the given address as a function call and does not
return to the console on success.

### `download`

Prompts: `"Input down addr(0x30000000):"`

1. Reads a hex destination address (default: 0x30000000).
2. Prints the address and `"Select your file:"`.
3. Reads 4 bytes as a length header, subtracts 6, yielding the payload byte
   count.
4. Receives `payload_count + 2` bytes into the destination buffer. The last
   2 bytes of the received data are the expected checksum.
5. Extracts the 16-bit checksum from the tail of the received data, handling
   the 4 possible byte-alignment cases within the packed 32-bit words.
6. Computes a running 16-bit sum over the payload bytes.
7. Prints `"Down OK!\n"` or `"Down faild!\n"` [sic] based on whether the
   computed checksum matches.

### `dump`

Prompts:

- `"Input start addr(0x40000000):"`
- `"Input end addr(0x40000000):"`

Reads 32-bit words from the start address through the end address (inclusive)
and prints them in a tabular format with 4 words per line:

```
   Adress      0        4         8            c
0xADDRESS:  0xVALUE  0xVALUE  0xVALUE  0xVALUE
```

### `setvalue`

Prompts:

- `"Input addr(0xfffffff0):"`
- `"Input value(0xfffffff0):"`

Writes the 32-bit value to the specified address, reads it back, and prints
both the address and the read-back value for verification.

## Hex Input Parser

All address/value prompts use a shared hex input routine. It:

1. Reads characters from UART, echoing each one.
2. Converts ASCII hex digits (0-9, a-f, A-F) to 4-bit nibbles using a
   lookup function that returns 0xFF for invalid characters.
3. Packs up to 8 nibbles into a 32-bit value (MSB-first).
4. On Enter with no input, returns the default value shown in the prompt.
5. On Enter with valid input, returns the parsed value.
6. Returns failure (0) if any invalid hex digit was entered or if more than
   8 digits were entered.
7. Supports backspace to erase the last entered digit.

## UART I/O Internals

### Transmit (`uart_putc`)

1. Sets L2CTR_UARTBUF_CFG bit 16 (TX buffer enable).
2. Writes the character byte to the L2 buffer TX slot at 0x48000E14.
3. Clears the L2 fractional count at 0x48000F8C.
4. Sets UART+0x00 bit 28 (TX start) and UART+0x04 bits for TX trigger.
5. Polls UART+0x08 bits [12:0] until the count reaches 0 (TX complete).

### Receive (`uart_get_rx_word`)

1. Sets UART+0x00 bit 23 (RX enable).
2. Polls UART+0x04 bit 30 until RX data is available.
3. Reads the L2 buffer index from UART+0x08 bits [17:13] to locate the
   RX data in L2 SRAM.
4. Reads one 32-bit word from the L2 buffer page.
5. Checks UART+0x04 bit 2 (fractional flag): if set, reads the fractional
   byte count from UART+0x08 bits [23:22] and masks the word accordingly.
   Otherwise, all 4 bytes are valid.
6. Returns the number of valid bytes (1-4) and the packed data word.

### String Output (`uart_puts`)

Iterates over the input string 4 bytes at a time (reading as 32-bit words
for efficiency on the ARM bus), outputting each non-zero byte via `uart_putc`
until a NUL terminator is reached.

### Packed Byte Receive (`uart_recv_packed_bytes`)

A higher-level receive function that accumulates UART data into a 32-bit word
array. Handles cross-word boundary packing when a single `uart_get_rx_word`
call returns bytes that span two output words. If `exact_len` is nonzero,
loops until the specified byte count is reached; otherwise returns after one
chunk.
