CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

GDBSTUB  = ../../../gdbstub
LIB      = ../../../lib

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -std=gnu11 -I. -I$(GDBSTUB) -I$(LIB)
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

STUB_SRC = start.S resume.S bp.S step.c musb.c rsp.c trace.c
STUB_OBJ = $(addprefix stub_,$(addsuffix .o,$(basename $(STUB_SRC))))

LIB_SRC  = lcd.c mmu.c mmu_arm926.S
LIB_OBJ  = $(addprefix lib_,$(addsuffix .o,$(basename $(LIB_SRC))))

OWN_OBJ  = main.o lcd_timing.o

all: BOOT.BIN

stub_%.o: $(GDBSTUB)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

stub_%.o: $(GDBSTUB)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

lib_%.o: $(LIB)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

lib_%.o: $(LIB)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c -o $@ $<

lcd-timing.elf: $(STUB_OBJ) $(LIB_OBJ) $(OWN_OBJ) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(STUB_OBJ) $(LIB_OBJ) $(OWN_OBJ)

BOOT.BIN: lcd-timing.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes (base 0x33000000)"

clean:
	rm -f *.o *.elf BOOT.BIN

.PHONY: all clean
