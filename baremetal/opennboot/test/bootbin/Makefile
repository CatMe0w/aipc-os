CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -ffreestanding -nostdlib -nostartfiles -Wall
LDFLAGS  = -T linker.ld -nostdlib -Wl,--build-id=none -Wl,--no-warn-rwx-segments

all: BOOT.BIN

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

stub.elf: start.o linker.ld
	$(CC) $(LDFLAGS) -o $@ start.o

stub.bin: stub.elf
	$(OBJCOPY) -O binary $< $@

BOOT.BIN: stub.bin pack.py
	uv run --no-project pack.py

clean:
	rm -f *.o *.elf stub.bin BOOT.BIN

.PHONY: all clean
