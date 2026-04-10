# aipc-coldboot-dump

Dump AIPC DDR contents after forcing the machine into USB boot mode during a
cold-boot attack workflow.

## Running

```sh
uv run aipc-coldboot-dump --firmware 1.58.2 -o coldboot.bin
```

The command waits for the AK7802 to enumerate in USB boot mode.

Options:

- `--firmware 1.58.2|1.88` selects the DDR init sequence
- `-o, --output PATH` selects the dump file
- `--stub PATH` overrides the DDR init stub binary

## Workflow

1. On the PC, start `aipc-coldboot-dump` first and let it wait for USB boot.
2. Boot the netbook normally into Windows CE.
   > You can leave the 9V DC adapter disconnected and only connect the rear USB
   > port to the PC. In this USB-only setup, plugging in USB should immediately
   > auto-boot the machine into Windows CE.
3. Connect `DL_JUMP` or `USB_BOOT`.
4. Immediately unplug and replug the USB cable once so the machine reboots.
5. At that point the dump should start automatically.

## Dump range

| Field  | Value        |
| ------ | ------------ |
| Start  | `0x30000000` |
| Length | `0x04000000` |
| End    | `0x33FFFFFF` |
