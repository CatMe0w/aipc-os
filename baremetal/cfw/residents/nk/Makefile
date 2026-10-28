BAREMETAL_LIB = ../../../lib
include $(BAREMETAL_LIB)/common.mk

# The nk resident runs inside WinCE at its NK VA. cfw links against the
# symbols in EXPORTS through nk.syms.elf, and carries nk.bin.
OUT = build
EXPORTS = irq_hook irq_orig ioctl_hook ioctl_resume

CFLAGS = $(ARCHFLAGS) -Os -fomit-frame-pointer -ffreestanding \
         -ffunction-sections -fdata-sections -fno-common -fno-builtin \
         -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
         -I. -I../.. -I$(LIB) $(DEPFLAGS)
LDFLAGS = -nostdlib -Wl,--gc-sections -Wl,--build-id=none \
          -Wl,--no-warn-rwx-segments

OBJS = $(addprefix $(OUT)/,hooks.o diag.o lib_fbcon.o)

all: $(OUT)/nk.bin $(OUT)/nk.syms.elf

$(OUT):
	mkdir -p $@

$(OUT)/%.o: %.S | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/lib_%.o: $(LIB)/%.c | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/nk.lds: nk.ld | $(OUT)
	$(CC) -E -P -x c -D__ASSEMBLER__ $(CFLAGS) -MT $@ -o $@ $<

$(OUT)/nk.elf: $(OBJS) $(OUT)/nk.lds
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(OUT)/nk.lds -o $@ $(OBJS) -lgcc

$(OUT)/nk.bin: $(OUT)/nk.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@ | tr -d ' ') bytes"

$(OUT)/nk.syms.elf: $(OUT)/nk.elf
	$(OBJCOPY) --extract-symbol --strip-all $(addprefix -K ,$(EXPORTS)) $< $@

-include $(wildcard $(OUT)/*.d)

clean:
	rm -rf $(OUT)

.PHONY: all clean
