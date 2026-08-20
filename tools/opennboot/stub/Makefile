CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding \
           -nostdlib -nostartfiles -Wall -Wextra -Werror

#   0x30110000  nf_read  code      0x30113000  nf_read  stack top / command block
#   0x30114000  nf_write code      0x30117000  nf_write stack top / command block
#   0x30120000  block buffer, up to 128 pages of 2112 bytes
READ_BASE   = 0x30110000
READ_LIMIT  = 0x30112000
READ_STACK  = 0x30113000
WRITE_BASE  = 0x30114000
WRITE_LIMIT = 0x30116000
WRITE_STACK = 0x30117000

ldflags = -T linker.ld -nostdlib -Wl,--gc-sections \
          -Wl,--defsym,STUB_BASE=$(1) -Wl,--defsym,STUB_LIMIT=$(2) \
          -Wl,--defsym,STUB_STACK=$(3)

all: nf_read.bin nf_write.bin

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c nf_common.h
	$(CC) $(CFLAGS) -c -o $@ $<

nf_read.elf: start.o nf_read.o nf_common.o linker.ld
	$(CC) $(call ldflags,$(READ_BASE),$(READ_LIMIT),$(READ_STACK)) \
	      -o $@ start.o nf_read.o nf_common.o

nf_write.elf: start.o nf_write.o nf_common.o linker.ld
	$(CC) $(call ldflags,$(WRITE_BASE),$(WRITE_LIMIT),$(WRITE_STACK)) \
	      -o $@ start.o nf_write.o nf_common.o

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@ size: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
