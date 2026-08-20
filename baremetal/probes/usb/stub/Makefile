CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -Wno-unused-parameter
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

STUBS    = phy_baseline sideband sideband_rw

all: $(addsuffix .bin,$(STUBS))

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.elf: %.o start.o
	$(CC) $(LDFLAGS) -o $@ start.o $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
