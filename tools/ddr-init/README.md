# aipc-ddr-init

Initialize DDR on AIPC from USB boot mode.

This tool exists to replace slow host-side `poke` loops with one short stub
upload and one `EXECUTE`. That is the path we need for later cold-boot memory
dump work: get DDR online quickly, return to the bootrom, then keep using the
normal USB upload/download commands.

Two stubs are provided, matching the DDR init sequences extracted from nboot:

- `1.58.2`
- `1.88`

The only sequence differences are the extra UART write in `1.88` and the final
DDR refresh timing register value. See [`docs/nboot/boot-flow.md`](../../docs/nboot/boot-flow.md).

## Building the stubs

Requires `arm-none-eabi-gcc`.

```sh
cd stub/
make
```

This produces:

- `ddr_init_v1_58_2.bin`
- `ddr_init_v1_88.bin`

## Running

```sh
uv run aipc-ddr-init --firmware 1.58.2
# or
uv run aipc-ddr-init --firmware 1.88
```

The device must already be in USB boot mode (`DGPIO[2]` high at power-on).

Options:

- `--stub PATH` overrides the compiled stub binary
- `--addr ADDR` overrides the L2 SRAM upload address (default: `0x48000240`)

## How it works

1. Upload the selected stub to `0x48000240`.
2. Execute it with the bootrom `EXECUTE` command.
3. The stub replays the DDR init register script directly on-chip and includes
   an exact inline clone of the bootrom `delay_ticks()` helper between
   timing-sensitive steps.
4. The stub returns to the bootrom, so USB boot mode resumes immediately.

## Memory layout during EXECUTE

| Address range             | Size    | Usage                         |
| ------------------------- | ------- | ----------------------------- |
| `0x48000200 - 0x4800023F` | 64 B    | EP3 RX DMA window; not used   |
| `0x48000240 - 0x48000E6F` | ~3.1 KB | Stub code + literal pool      |
| `0x48000E70 - 0x48000FFC` | 396 B   | Bootrom stack area            |
| `0x48001100 - 0x4800157F` | 1152 B  | Safe stack area               |
