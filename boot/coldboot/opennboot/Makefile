CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -Wno-unused-parameter -std=c11
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections -Wl,--build-id=none \
           -Wl,--no-warn-rwx-segments

OBJS     = start.o main.o log.o soc.o ecc.o nand.o sd.o fat.o

HOSTCC      ?= cc
HOSTCFLAGS   = -Wall -Wextra -std=c11 -O1 -g

all: opennboot.bin stubs bootbin test

stubs:
	$(MAKE) -C tools/stub CROSS=$(CROSS)

bootbin:
	$(MAKE) -C test/bootbin CROSS=$(CROSS)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

opennboot.elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

opennboot.bin: opennboot.elf
	$(OBJCOPY) -O binary $< $@
	@size=$$(wc -c < $@); \
	 echo "$@ size: $$size bytes ($$(( ($$size + 2047) / 2048 )) pages of 63)"

debug:
	rm -f $(OBJS) opennboot.elf opennboot.bin
	$(MAKE) EXTRA_CFLAGS=-DONB_SD_DEBUG

test: test/test_ecc
	./test/test_ecc

test/test_ecc: test/test_ecc.c ecc.c ecc.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ test/test_ecc.c ecc.c

clean:
	rm -f *.o *.elf *.bin test/test_ecc
	$(MAKE) -C tools/stub clean
	$(MAKE) -C test/bootbin clean

.PHONY: all clean test stubs
