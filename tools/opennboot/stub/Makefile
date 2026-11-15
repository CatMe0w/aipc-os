CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

LIB      = ../../../baremetal/lib

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding \
           -nostdlib -nostartfiles -Wall -Wextra -Werror \
           -ffunction-sections -fdata-sections -I$(LIB)

#   0x30110000  nf_block0 code    0x30113000  nf_block0 stack top / command block
#   0x30118000  nf_backup code    0x3011B000  nf_backup stack top / command block
#   0x3011C000  nf_restore code   0x3011F000  nf_restore stack top / command block
#   0x31000000  buffers, see stub.h
BLOCK0_BASE   = 0x30110000
BLOCK0_LIMIT  = 0x30112000
BLOCK0_STACK  = 0x30113000
BACKUP_BASE   = 0x30118000
BACKUP_LIMIT  = 0x3011A000
BACKUP_STACK  = 0x3011B000
RESTORE_BASE  = 0x3011C000
RESTORE_LIMIT = 0x3011E000
RESTORE_STACK = 0x3011F000

ldflags = -T linker.ld -nostdlib -Wl,--gc-sections \
          -Wl,--defsym,STUB_BASE=$(1) -Wl,--defsym,STUB_LIMIT=$(2) \
          -Wl,--defsym,STUB_STACK=$(3)

all: nf_block0.bin nf_backup.bin nf_restore.bin

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c stub.h $(LIB)/nand.h $(LIB)/block0.h
	$(CC) $(CFLAGS) -c -o $@ $<

nand.o: $(LIB)/nand.c $(LIB)/nand.h
	$(CC) $(CFLAGS) -c -o $@ $<

block0.o: $(LIB)/block0.c $(LIB)/block0.h $(LIB)/nand.h
	$(CC) $(CFLAGS) -c -o $@ $<

nf_block0.elf: start.o nf_block0.o block0.o nand.o linker.ld
	$(CC) $(call ldflags,$(BLOCK0_BASE),$(BLOCK0_LIMIT),$(BLOCK0_STACK)) \
	      -o $@ start.o nf_block0.o block0.o nand.o

nf_backup.elf: start.o nf_backup.o nand.o linker.ld
	$(CC) $(call ldflags,$(BACKUP_BASE),$(BACKUP_LIMIT),$(BACKUP_STACK)) \
	      -o $@ start.o nf_backup.o nand.o

nf_restore.elf: start.o nf_restore.o nand.o linker.ld
	$(CC) $(call ldflags,$(RESTORE_BASE),$(RESTORE_LIMIT),$(RESTORE_STACK)) \
	      -o $@ start.o nf_restore.o nand.o

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@ size: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
