BAREMETAL_LIB = ../../../lib
include $(BAREMETAL_LIB)/common.mk

# The eboot resident runs inside EBOOT at its EBOOT VA, so lib/ builds with
# the register addresses of EBOOT. cfw links against the symbols in EXPORTS
# through eboot.syms.elf, and carries eboot.bin.
OUT = build
EXPORTS = eboot_menu_read_byte eboot_menu_get_string eboot_menu_get_char \
          eboot_menu_get_integer eboot_menu_ask_yes_no eboot_menu_secs \
          eboot_menu_putc

comma = ,

CFLAGS = $(ARCHFLAGS) -Os -fomit-frame-pointer -ffreestanding \
         -ffunction-sections -fdata-sections -fno-common -fno-builtin \
         -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
         -include mmio_va.h -I. -I../.. -I$(LIB) $(DEPFLAGS)
LDFLAGS = -nostdlib -Wl,--gc-sections -Wl,--build-id=none \
          -Wl,--no-warn-rwx-segments \
          $(addprefix -Wl$(comma)--undefined=,$(EXPORTS))

OBJS = $(addprefix $(OUT)/,menu_input.o lib_ch374.o lib_timer.o)

all: $(OUT)/eboot.bin $(OUT)/eboot.syms.elf

$(OUT):
	mkdir -p $@

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/lib_%.o: $(LIB)/%.c | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/eboot.lds: eboot.ld | $(OUT)
	$(CC) -E -P -x c -D__ASSEMBLER__ $(CFLAGS) -MT $@ -o $@ $<

$(OUT)/eboot.elf: $(OBJS) $(OUT)/eboot.lds
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(OUT)/eboot.lds -o $@ $(OBJS) -lgcc

$(OUT)/eboot.bin: $(OUT)/eboot.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@ | tr -d ' ') bytes"

$(OUT)/eboot.syms.elf: $(OUT)/eboot.elf
	$(OBJCOPY) --extract-symbol --strip-all $(addprefix -K ,$(EXPORTS)) $< $@

-include $(wildcard $(OUT)/*.d)

clean:
	rm -rf $(OUT)

.PHONY: all clean
