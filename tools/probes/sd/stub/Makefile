CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -Wno-unused-parameter
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

STUBS    = sd_clock_probe sd_sharepin_probe sd_reg_probe sd_cd_probe sd_cmd0_probe sd_cmd0_v2_probe sd_cmd0_v3_probe sd_cmd0_v4_probe sd_reg_bitmap_probe sd_cmd_trace_probe sd_offset_scan sd_status_scan sd_cmd8_probe sd_gpio_probe sd_pull_cmd0 sd_cmd0_noshare sd_cmd0_mdat2 sd_base_scan sd_sdio_probe sd_ioctrl_probe sd_sta_capture sd_init_probe sd_init_v2_probe sd_full_init sd_full_init_v2 sd_acmd41_loop sd_cmd17_probe sd_cmd17_v2 sd_fifo_diag sd_noreset_probe sd_full_flow
BINS     = $(addsuffix .bin,$(STUBS))
ENTRY    = start.o

all: $(BINS)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.elf: %.o $(ENTRY)
	$(CC) $(LDFLAGS) -o $@ $(ENTRY) $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@ size: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
