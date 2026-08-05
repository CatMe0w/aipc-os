CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -DPROBE_TIM_A=$(TIM_A)u -DPROBE_TIM_B=$(TIM_B)u -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -Werror
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

PAGE    ?= 128
ITERS   ?= 400
TIM_A   ?= 0x00030230
TIM_B   ?= 0x00040203
STUBS    = nf_col_raw nf_col_ecc nf_ecc_snapshot nf_ecc_retry \
           nf_wr_status nf_wr_erase nf_wr_program
BINS     = $(addsuffix .bin,$(STUBS))
ENTRY    = start.o

all: $(BINS)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

nf_col_raw.o: nf_read.c
	$(CC) $(CFLAGS) -DPROBE_MODE=0 -DPROBE_PAGE=$(PAGE)u -c -o $@ $<

nf_col_ecc.o: nf_read.c
	$(CC) $(CFLAGS) -DPROBE_MODE=1 -DPROBE_PAGE=$(PAGE)u -c -o $@ $<

nf_ecc_snapshot.o: nf_read.c
	$(CC) $(CFLAGS) -DPROBE_MODE=2 -DPROBE_PAGE=$(PAGE)u -c -o $@ $<

nf_ecc_retry.o: nf_read.c
	$(CC) $(CFLAGS) -DPROBE_MODE=3 -DPROBE_PAGE=$(PAGE)u -DPROBE_ITERS=$(ITERS)u -c -o $@ $<

BLOCK   ?= 1
PPB     ?= 64

nf_wr_status.o: nf_write.c
	$(CC) $(CFLAGS) -DPROBE_MODE=0 -DPROBE_BLOCK=$(BLOCK)u -DPROBE_PPB=$(PPB)u -c -o $@ $<

nf_wr_erase.o: nf_write.c
	$(CC) $(CFLAGS) -DPROBE_MODE=1 -DPROBE_BLOCK=$(BLOCK)u -DPROBE_PPB=$(PPB)u -c -o $@ $<

nf_wr_program.o: nf_write.c
	$(CC) $(CFLAGS) -DPROBE_MODE=2 -DPROBE_BLOCK=$(BLOCK)u -DPROBE_PPB=$(PPB)u -c -o $@ $<

nf_wr_%.elf: nf_wr_%.o $(ENTRY)
	$(CC) -T linker_write.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

%.elf: %.o $(ENTRY)
	$(CC) $(LDFLAGS) -o $@ $(ENTRY) $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@ size: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
