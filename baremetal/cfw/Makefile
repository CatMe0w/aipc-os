BAREMETAL_LIB = ../lib
include $(BAREMETAL_LIB)/common.mk

OUT = build
NK_RESIDENT = residents/nk/build

# cfw leaves the UART to EBOOT and NK.
CFLAGS = $(ARCHFLAGS) -Os -fomit-frame-pointer -ffreestanding \
         -ffunction-sections -fdata-sections -fno-common -fno-builtin \
         -std=gnu11 -DLOG_BASE=0x30180000u -DLOG_NO_UART \
         -Wall -Wextra -Wno-unused-parameter \
         -I. -Inkpf -I$(LIB) $(DEPFLAGS)
LDFLAGS = -nostdlib -Wl,--gc-sections -Wl,--build-id=none \
          -Wl,--no-warn-rwx-segments \
          -Wl,--just-symbols=$(NK_RESIDENT)/nk.syms.elf

SRCS = core/header.c core/core.c core/printf.c core/resident.c \
       core/blobs.S eboot/entry.S eboot/eboot.c eboot/handoff.c \
       nkpf/nk.S nkpf/nkpf.c nkpf/romimage.c \
       components/power_hold.c components/battery_check_skip.c \
       components/rtc_stall_fix.c components/diag.c
LIB_SRCS = pf.c fbcon.c log.c
OBJS = $(addprefix $(OUT)/,$(addsuffix .o,$(basename $(SRCS)))) \
       $(addprefix $(OUT)/lib_,$(LIB_SRCS:.c=.o))

all: $(OUT)/cfw.bin

$(NK_RESIDENT)/nk.bin $(NK_RESIDENT)/nk.syms.elf: FORCE
	$(MAKE) -C residents/nk

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/core/blobs.o: $(NK_RESIDENT)/nk.bin

$(OUT)/lib_%.o: $(LIB)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/cfw.lds: cfw.ld
	@mkdir -p $(dir $@)
	$(CC) -E -P -x c -D__ASSEMBLER__ $(CFLAGS) -MT $@ -o $@ $<

$(OUT)/cfw.elf: $(OBJS) $(OUT)/cfw.lds $(NK_RESIDENT)/nk.syms.elf
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(OUT)/cfw.lds -o $@ $(OBJS) -lgcc

$(OUT)/cfw.bin: $(OUT)/cfw.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@ | tr -d ' ') bytes"

-include $(OBJS:.o=.d) $(OUT)/cfw.d

clean:
	rm -rf $(OUT)
	$(MAKE) -C residents/nk clean

FORCE:

.PHONY: all clean FORCE
