CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump

GDBSTUB  = ../../../gdbstub
COMMON   = ../stub

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -std=gnu11 -I. -I$(COMMON) -I$(GDBSTUB)
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

# Everything from the GDB stub except its main.c, which this payload replaces.
STUB_SRC = start.S resume.S bp.S step.c musb.c rsp.c trace.c
STUB_OBJ = $(addprefix stub_,$(addsuffix .o,$(basename $(STUB_SRC))))

PROBE   ?= dqs
OWN_OBJ  = main.o $(PROBE)_core.o l2call.o

# ASIC3X_CONTROL=1 adds the control point, which moves the real ASIC clock.
# Without it the instrument stays unvalidated and the decoder says so.
ASIC3X_CONTROL ?= 0
CFLAGS  += -DASIC3X_CONTROL=$(ASIC3X_CONTROL)

all: BOOT.BIN

stub_%.o: $(GDBSTUB)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

stub_%.o: $(GDBSTUB)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c $(wildcard *.h) $(COMMON)/cpufreq_common.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROBE).elf: $(STUB_OBJ) $(OWN_OBJ) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(STUB_OBJ) $(OWN_OBJ)
	@$(MAKE) --no-print-directory check-l2 ELF=$@

# The worker is copied out of DDR into L2 SRAM. A call that leaves the copied
# section would jump back to DDR while DDR may be unusable.
check-l2:
	@lo=$$($(OBJDUMP) -h $(ELF) | awk '/\.l2text/ {print strtonum("0x" $$4)}'); \
	  sz=$$($(OBJDUMP) -h $(ELF) | awk '/\.l2text/ {print strtonum("0x" $$3)}'); \
	  bad=$$($(OBJDUMP) -d --section=.l2text $(ELF) \
	    | awk -v lo=$$lo -v hi=$$((lo+sz)) \
	        '/\<bl\>|\<blx\>/ { for (i=1;i<=NF;i++) if ($$i=="bl"||$$i=="blx") { \
	           t=strtonum("0x" $$(i+1)); if (t<lo||t>=hi) print $$0 } }'); \
	  test -z "$$bad" || { echo "ERROR: .l2text calls out of section: $$bad"; exit 1; }
	@$(OBJDUMP) -d --section=.l2text $(ELF) | grep -oE '@ [0-9a-f]{8}' | sort -u \
	  | awk '{print strtonum("0x" $$2)}' > /tmp/l2lit.$$$$; \
	  lo=$$($(OBJDUMP) -h $(ELF) | awk '/\.l2text/ {print strtonum("0x" $$4)}'); \
	  sz=$$($(OBJDUMP) -h $(ELF) | awk '/\.l2text/ {print strtonum("0x" $$3)}'); \
	  bad=$$(awk -v lo=$$lo -v hi=$$((lo+sz)) '$$1 < lo || $$1 >= hi' /tmp/l2lit.$$$$); \
	  rm -f /tmp/l2lit.$$$$; \
	  test -z "$$bad" || { echo "ERROR: .l2text literal outside section: $$bad"; exit 1; }
	@echo "check-l2: self contained"

BOOT.BIN: $(PROBE).elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes (base 0x33000000)"

clean:
	rm -f *.o *.elf BOOT.BIN

.PHONY: all clean check-l2
