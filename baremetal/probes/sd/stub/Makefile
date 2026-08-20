CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

CFLAGS   = -mcpu=arm926ej-s -marm -Os -fomit-frame-pointer -ffreestanding -nostdlib \
           -nostartfiles -Wall -Wextra -Werror
LDFLAGS  = -T linker.ld -nostdlib -Wl,--gc-sections

STUBS    = sd_baseline sd_init sd_pio_nodata sd_pio_withdata sd_l2 sd_write \
	   sd_highspeed \
	   sd_multiread sd_multiread8 sd_multiwrite8 sd_l2_multiwrite8 \
	   sd_l2_dma_multiwrite8 sd_l2_dma_multiwrite64 \
	   sd_l2_dma_multiwrite128 sd_l2_dma_multiwrite64_chunk8 \
	   sd_l2_dma_multiwrite64_chunk16 sd_l2_dma_cmd23_multiwrite64 \
	   sd_l2_dma_cmd23_multiwrite128_chunk8 \
	   sd_l2_dma_cmd23_multiwrite256_chunk8 \
	   sd_l2_dma_cmd23_multiwrite512_chunk8 \
	   sd_l2_dma_cmd23_highspeed_multiwrite256_chunk8 \
	   sd_l2_dma_cmd23_highspeed_multiwrite1024_chunk8 \
	   sd_highspeed_41mhz \
	   sd_irq sd_irq_window \
	   sd_l2_dma_cmd23_highspeed41_multiwrite1024_chunk8 \
	   sd_l2_dma_cmd23_highspeed_sg_multiwrite128_chunk8
BINS     = $(addsuffix .bin,$(STUBS))
ENTRY    = start.o

all: $(BINS)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

sd_pio_nodata.o: sd_pio.c
	$(CC) $(CFLAGS) -DPIO_WITHDATA=0 -c -o $@ $<

sd_pio_withdata.o: sd_pio.c
	$(CC) $(CFLAGS) -DPIO_WITHDATA=1 -c -o $@ $<

sd_l2.o: sd_pio.c
	$(CC) $(CFLAGS) -DPIO_WITHDATA=1 -DPIO_L2=1 -c -o $@ $<

sd_highspeed_41mhz.o: sd_highspeed.c
	$(CC) $(CFLAGS) -DHIGH_SPEED_CLOCK_DIV=0x0001u \
		-DHIGH_SPEED_EXPERIMENT=51u -c -o $@ $<

sd_l2_multiwrite8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -c -o $@ $<

sd_l2_dma_multiwrite8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 -c -o $@ $<

sd_l2_dma_multiwrite64.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 \
		-DTRANSFER_BLOCKS=64u -DDMA_BUFFER_ADDR=0x30018000u \
		-DDMA_EXPERIMENT_BASE=17u -c -o $@ $<

sd_l2_dma_multiwrite128.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 \
		-DTRANSFER_BLOCKS=128u -DDMA_BUFFER_ADDR=0x30020000u \
		-DDMA_EXPERIMENT_BASE=20u -c -o $@ $<

sd_l2_dma_multiwrite64_chunk8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 \
		-DTRANSFER_BLOCKS=64u -DDMA_BUFFER_ADDR=0x30018000u \
		-DDMA_CHUNK_BYTES=8192u -DDMA_EXPERIMENT_BASE=23u -c -o $@ $<

sd_l2_dma_multiwrite64_chunk16.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 \
		-DTRANSFER_BLOCKS=64u -DDMA_BUFFER_ADDR=0x30018000u \
		-DDMA_CHUNK_BYTES=16384u -DDMA_EXPERIMENT_BASE=26u -c -o $@ $<

sd_l2_dma_cmd23_multiwrite64.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 -DUSE_CMD23=1 \
		-DTRANSFER_BLOCKS=64u -DDMA_BUFFER_ADDR=0x30018000u \
		-DDMA_EXPERIMENT_BASE=29u -c -o $@ $<

sd_l2_dma_cmd23_multiwrite128_chunk8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 -DUSE_CMD23=1 \
		-DTRANSFER_BLOCKS=128u -DDMA_BUFFER_ADDR=0x30020000u \
		-DDMA_CHUNK_BYTES=8192u -DDMA_EXPERIMENT_BASE=32u -c -o $@ $<

sd_l2_dma_cmd23_multiwrite256_chunk8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 -DUSE_CMD23=1 \
		-DTRANSFER_BLOCKS=256u -DDMA_BUFFER_ADDR=0x30030000u \
		-DDMA_CHUNK_BYTES=8192u -DDMA_EXPERIMENT_BASE=35u -c -o $@ $<

sd_l2_dma_cmd23_multiwrite512_chunk8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 -DUSE_CMD23=1 \
		-DTRANSFER_BLOCKS=512u -DDMA_BUFFER_ADDR=0x30050000u \
		-DDMA_CHUNK_BYTES=8192u -DDMA_EXPERIMENT_BASE=38u -c -o $@ $<

sd_l2_dma_cmd23_highspeed_multiwrite256_chunk8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 -DUSE_CMD23=1 \
		-DUSE_HIGH_SPEED=1 -DTRANSFER_BLOCKS=256u \
		-DDMA_BUFFER_ADDR=0x30030000u -DDMA_CHUNK_BYTES=8192u \
		-DDMA_EXPERIMENT_BASE=42u -c -o $@ $<

sd_l2_dma_cmd23_highspeed_multiwrite1024_chunk8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 -DUSE_CMD23=1 \
		-DUSE_HIGH_SPEED=1 -DTRANSFER_BLOCKS=1024u \
		-DDMA_BUFFER_ADDR=0x30090000u -DDMA_CHUNK_BYTES=8192u \
		-DDMA_EXPERIMENT_BASE=45u -c -o $@ $<

sd_l2_dma_cmd23_highspeed41_multiwrite1024_chunk8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 -DUSE_CMD23=1 \
		-DUSE_HIGH_SPEED=1 -DHIGH_SPEED_CLOCK_DIV=0x0001u \
		-DTRANSFER_BLOCKS=1024u -DDMA_BUFFER_ADDR=0x30090000u \
		-DDMA_CHUNK_BYTES=8192u -DDMA_EXPERIMENT_BASE=52u -c -o $@ $<

sd_l2_dma_cmd23_highspeed_sg_multiwrite128_chunk8.o: sd_multiwrite8.c
	$(CC) $(CFLAGS) -DUSE_L2=1 -DUSE_L2_DMA=1 -DUSE_CMD23=1 \
		-DUSE_HIGH_SPEED=1 -DUSE_SCATTER_DMA=1 -DTRANSFER_BLOCKS=128u \
		-DDMA_BUFFER_ADDR=0x30020000u -DDMA_CHUNK_BYTES=8192u \
		-DDMA_SEGMENT_BYTES=4096u -DDMA_SEGMENT_STRIDE=8192u \
		-DDMA_EXPERIMENT_BASE=48u -c -o $@ $<

%.elf: %.o $(ENTRY)
	$(CC) $(LDFLAGS) -o $@ $(ENTRY) $<

sd_l2_multiwrite8.elf: sd_l2_multiwrite8.o $(ENTRY)
	$(CC) -T linker-ddr.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_multiwrite8.elf: sd_l2_dma_multiwrite8.o $(ENTRY)
	$(CC) -T linker-ddr.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_multiwrite64.elf: sd_l2_dma_multiwrite64.o $(ENTRY)
	$(CC) -T linker-ddr.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_multiwrite128.elf: sd_l2_dma_multiwrite128.o $(ENTRY)
	$(CC) -T linker-ddr64.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_multiwrite64_chunk8.elf: sd_l2_dma_multiwrite64_chunk8.o $(ENTRY)
	$(CC) -T linker-ddr.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_multiwrite64_chunk16.elf: sd_l2_dma_multiwrite64_chunk16.o $(ENTRY)
	$(CC) -T linker-ddr.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_cmd23_multiwrite64.elf: sd_l2_dma_cmd23_multiwrite64.o $(ENTRY)
	$(CC) -T linker-ddr.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_cmd23_multiwrite128_chunk8.elf: sd_l2_dma_cmd23_multiwrite128_chunk8.o $(ENTRY)
	$(CC) -T linker-ddr64.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_cmd23_multiwrite256_chunk8.elf: sd_l2_dma_cmd23_multiwrite256_chunk8.o $(ENTRY)
	$(CC) -T linker-ddr128.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_cmd23_multiwrite512_chunk8.elf: sd_l2_dma_cmd23_multiwrite512_chunk8.o $(ENTRY)
	$(CC) -T linker-ddr256.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_cmd23_highspeed_multiwrite256_chunk8.elf: sd_l2_dma_cmd23_highspeed_multiwrite256_chunk8.o $(ENTRY)
	$(CC) -T linker-ddr128.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_cmd23_highspeed_multiwrite1024_chunk8.elf: sd_l2_dma_cmd23_highspeed_multiwrite1024_chunk8.o $(ENTRY)
	$(CC) -T linker-ddr512.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_cmd23_highspeed41_multiwrite1024_chunk8.elf: sd_l2_dma_cmd23_highspeed41_multiwrite1024_chunk8.o $(ENTRY)
	$(CC) -T linker-ddr512.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_l2_dma_cmd23_highspeed_sg_multiwrite128_chunk8.elf: sd_l2_dma_cmd23_highspeed_sg_multiwrite128_chunk8.o $(ENTRY)
	$(CC) -T linker-ddr-sg64.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_irq.elf: sd_irq.o $(ENTRY)
	$(CC) -T linker-ddr.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

sd_irq_window.elf: sd_irq_window.o $(ENTRY)
	$(CC) -T linker-ddr.ld -nostdlib -Wl,--gc-sections -o $@ $(ENTRY) $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@ size: $$(wc -c < $@) bytes"

sd_l2.bin: sd_l2.elf
	$(OBJCOPY) -O binary $< $@
	@test "$$(wc -c < $@)" -le 2496 || \
		{ echo "sd_l2.bin overlaps L2 buffer 6"; rm -f $@; exit 1; }
	@echo "$@ size: $$(wc -c < $@) bytes"

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
