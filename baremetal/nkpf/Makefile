BAREMETAL_LIB = ../lib
include $(BAREMETAL_LIB)/common.mk

# DIAG=1 adds the diagnostic hooks and carries the resident image.
DIAG ?= 0
ifeq ($(DIAG),1)
OUT = build-diag
else
OUT = build
endif

# nkpf leaves the UART to EBOOT and NK.
CFLAGS = $(ARCHFLAGS) -Os -fomit-frame-pointer -ffreestanding \
         -ffunction-sections -fdata-sections \
         -fno-common -fno-builtin -std=gnu11 -DLOG_BASE=0x30180000u -DLOG_NO_UART \
         -Wall -Wextra -Wno-unused-parameter \
         -I. -I$(LIB)
LDFLAGS = -nostdlib -Wl,--gc-sections -Wl,--build-id=none -Wl,--no-warn-rwx-segments

NKPF_OBJS = entry.o main.o printf.o romimage.o rtc_stall_fix.o \
            lib_pf.o lib_fbcon.o lib_log.o
RES_OBJS  = res_start.o res_resident.o res_fbcon.o

ifeq ($(DIAG),1)
CFLAGS += -DNKPF_DIAG
NKPF_OBJS += diag.o resident_blob.o
endif

all: $(OUT)/nkpf.bin

$(OUT):
	mkdir -p $@

$(OUT)/%.o: %.c layout.h nkpf.h $(LIB)/pf.h $(LIB)/fbcon.h | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/diag.o: resident/resident.h

$(OUT)/%.o: %.S layout.h | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/lib_%.o: $(LIB)/%.c $(LIB)/pf.h $(LIB)/fbcon.h $(LIB)/log.h | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/nkpf.elf: $(addprefix $(OUT)/,$(NKPF_OBJS)) nkpf.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -T nkpf.ld -o $@ $(addprefix $(OUT)/,$(NKPF_OBJS)) -lgcc

RES_CFLAGS = $(CFLAGS) -Iresident

$(OUT)/res_%.o: resident/%.S resident/resident.h | $(OUT)
	$(CC) $(RES_CFLAGS) -c -o $@ $<

$(OUT)/res_%.o: resident/%.c resident/resident.h layout.h $(LIB)/fbcon.h | $(OUT)
	$(CC) $(RES_CFLAGS) -c -o $@ $<

$(OUT)/res_fbcon.o: $(LIB)/fbcon.c | $(OUT)
	$(CC) $(RES_CFLAGS) -c -o $@ $<

$(OUT)/resident.elf: $(addprefix $(OUT)/,$(RES_OBJS)) resident/resident.ld
	$(CC) $(RES_CFLAGS) $(LDFLAGS) -T resident/resident.ld -o $@ $(addprefix $(OUT)/,$(RES_OBJS)) -lgcc

$(OUT)/resident_blob.o: resident_blob.S $(OUT)/resident.bin | $(OUT)
	$(CC) $(CFLAGS) -DRESIDENT_BIN='"$(OUT)/resident.bin"' -c -o $@ $<

$(OUT)/%.bin: $(OUT)/%.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(wc -c < $@ | tr -d ' ') bytes"

clean:
	rm -rf build build-diag

.PHONY: all clean
