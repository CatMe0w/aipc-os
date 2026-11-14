CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -Wno-unused-parameter
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

STUBS    = nand_id nand_copy
BINS     = $(addsuffix .bin, $(STUBS))
ENTRY    = start.o

all: $(BINS)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.elf: %.o $(ENTRY)
	$(CC) $(LDFLAGS) -o $@ $(ENTRY) $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@ size: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
