CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump

GDBSTUB  = ../../../gdbstub
COMMON   = .

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -std=gnu11 -I. -I$(COMMON) -I$(GDBSTUB)
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

# Everything from the GDB stub except its main.c, which this payload replaces.
STUB_SRC = start.S resume.S bp.S step.c musb.c rsp.c trace.c
STUB_OBJ = $(addprefix stub_,$(addsuffix .o,$(basename $(STUB_SRC))))

PROBE   ?= restart
OWN_OBJ  = main.o $(PROBE)_core.o


all: BOOT-$(PROBE).BIN

stub_%.o: $(GDBSTUB)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

stub_%.o: $(GDBSTUB)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROBE).elf: $(STUB_OBJ) $(OWN_OBJ) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(STUB_OBJ) $(OWN_OBJ)

BOOT-$(PROBE).BIN: $(PROBE).elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes (base 0x33000000)"

clean:
	rm -f *.o *.elf BOOT-*.BIN

.PHONY: all clean
