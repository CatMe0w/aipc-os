#include <stdint.h>

#ifndef USE_L2
#define USE_L2 0
#endif

#ifndef USE_L2_DMA
#define USE_L2_DMA 0
#endif

#ifndef TRANSFER_BLOCKS
#define TRANSFER_BLOCKS 8u
#endif

#ifndef DMA_EXPERIMENT_BASE
#define DMA_EXPERIMENT_BASE 15u
#endif

#ifndef USE_CMD23
#define USE_CMD23 0
#endif

#ifndef USE_HIGH_SPEED
#define USE_HIGH_SPEED 0
#endif

#ifndef USE_SCATTER_DMA
#define USE_SCATTER_DMA 0
#endif

#if USE_L2_DMA && !USE_L2
#error USE_L2_DMA requires USE_L2
#endif

#if USE_HIGH_SPEED && (!USE_L2_DMA || !USE_CMD23)
#error USE_HIGH_SPEED requires the final L2 DMA and CMD23 path
#endif

#if USE_SCATTER_DMA && !USE_L2_DMA
#error USE_SCATTER_DMA requires USE_L2_DMA
#endif

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define MCI(off) REG32(0x20020000u + (off))
#define OUT ((volatile uint32_t *)(uintptr_t)0x48001100u)
#define PARAM ((volatile uint32_t *)(uintptr_t)0x48001380u)
#define ORIGINAL ((volatile uint32_t *)(uintptr_t)0x30010000u)

#define MCI_CLOCK MCI(0x04)
#define MCI_ARGUMENT MCI(0x08)
#define MCI_COMMAND MCI(0x0C)
#define MCI_RESPONSE0 MCI(0x14)
#define MCI_DATATIMER MCI(0x24)
#define MCI_DATALENGTH MCI(0x28)
#define MCI_DATACTRL MCI(0x2C)
#define MCI_DATACNT MCI(0x30)
#define MCI_STATUS MCI(0x34)
#define MCI_MASK MCI(0x38)
#define MCI_DMACTRL MCI(0x3C)
#define MCI_FIFO MCI(0x40)

#if USE_L2
#define SYSCTRL(off) REG32(0x08000000u + (off))
#define L2(off) REG32(0x2002C000u + (off))
#define L2_DMAREQ L2(0x80)
#define L2_CONF1 L2(0x88)
#define L2_ASSIGN1 L2(0x90)
#define L2_STATUS1 L2(0xA0)
#define L2_BUFFER_ID 2u
#define L2_BUFFER ((volatile uint32_t *)(uintptr_t)0x48000400u)
#define L2_STATUS_BASE 64u
#if USE_L2_DMA
#define L2_DMA_ADDRESS L2(L2_BUFFER_ID * 4u)
#define L2_DMA_COUNT L2(0x40u + L2_BUFFER_ID * 4u)
#define L2_DMA_REQUEST (1u << (24u + L2_BUFFER_ID))
#define L2_DMA_REQUEST_MASK (0xFFFFu << 16)
#define L2_FRACTION_REQUEST (1u << 9)
#ifndef DMA_BUFFER_ADDR
#define DMA_BUFFER_ADDR 0x30012000u
#endif
#define DMA_BUFFER ((volatile uint32_t *)(uintptr_t)DMA_BUFFER_ADDR)
#define DMA_RECORD_BASE(phase) (104u + (phase) * 4u)
#endif
#endif

#define MCI_CPSM_ENABLE (1u << 0)
#define MCI_CPSM_CMD(cmd) (((cmd) & 0x3Fu) << 1)
#define MCI_CPSM_RESPONSE (1u << 7)
#define MCI_CPSM_WITHDATA (1u << 11)

#define MCI_DPSM_ENABLE (1u << 0)
#define MCI_DPSM_DIRECTION (1u << 1)
#define MCI_DPSM_BUSMODE(x) (((x) & 0x3u) << 3)
#define MCI_DPSM_BLOCKSIZE(size) (((size) & 0xFFFu) << 16)

#define MCI_DMA_BUFEN (1u << 0)
#define MCI_DMA_SIZE(words) (((words) & 0x7FFFu) << 17)

#define MCI_CLK_ENABLE (1u << 16)
#define MCI_FAIL_TRIGGER (1u << 19)
#define MCI_ENABLE (1u << 20)
#define MCI_CLOCK_20_67_MHZ (MCI_ENABLE | MCI_FAIL_TRIGGER | \
			      MCI_CLK_ENABLE | 0x0202u)
#ifndef HIGH_SPEED_CLOCK_DIV
#define HIGH_SPEED_CLOCK_DIV 0x0101u
#endif
#define MCI_CLOCK_HIGH_SPEED (MCI_ENABLE | MCI_FAIL_TRIGGER | \
			      MCI_CLK_ENABLE | HIGH_SPEED_CLOCK_DIV)

#define MCI_RESPCRCFAIL (1u << 0)
#define MCI_DATACRCFAIL (1u << 1)
#define MCI_RESPTIMEOUT (1u << 2)
#define MCI_DATATIMEOUT (1u << 3)
#define MCI_RESPEND (1u << 4)
#define MCI_DATAEND (1u << 6)
#define MCI_DATABLOCKEND (1u << 7)
#define MCI_STARTBIT_ERR (1u << 8)
#define MCI_TXACTIVE (1u << 10)
#define MCI_RXACTIVE (1u << 11)
#define MCI_FIFOFULL (1u << 12)
#define MCI_FIFOEMPTY (1u << 13)

#define R1_STATUS_MASK 0xFFF9A000u
#define R1_READY_FOR_DATA (1u << 8)
#define R1_CURRENT_STATE(value) (((value) >> 9) & 0xFu)
#define R1_STATE_TRAN 4u

#define COMMAND_WAIT_LIMIT 500000u
#define DATA_WAIT_LIMIT 8000000u
#define READY_ATTEMPTS 2000u
#define RESULT_MAGIC 0x53445052u
#define WRITE_LBA_MAGIC 0x57524954u
#define RESULT_WORDS 160u
#define BLOCKS TRANSFER_BLOCKS
#define SECTOR_WORDS 128u
#define TOTAL_WORDS (BLOCKS * SECTOR_WORDS)
#define TOTAL_BYTES (TOTAL_WORDS * sizeof(uint32_t))
#ifndef DMA_CHUNK_BYTES
#define DMA_CHUNK_BYTES 4096u
#endif
#ifndef DMA_SEGMENT_BYTES
#define DMA_SEGMENT_BYTES DMA_CHUNK_BYTES
#endif
#ifndef DMA_SEGMENT_STRIDE
#define DMA_SEGMENT_STRIDE DMA_SEGMENT_BYTES
#endif
#if USE_SCATTER_DMA && DMA_SEGMENT_STRIDE < DMA_SEGMENT_BYTES
#error DMA_SEGMENT_STRIDE must include the data segment
#endif
#if USE_SCATTER_DMA && (DMA_SEGMENT_BYTES % 64u)
#error DMA segments must preserve the L2 DMA granularity
#endif
#if USE_SCATTER_DMA && ((TRANSFER_BLOCKS * 512u) % DMA_SEGMENT_BYTES)
#error transfer size must contain complete DMA segments
#endif
#define RECORD_BASE(phase) (16u + (phase) * 8u)
#define CMD23_RECORD_BASE(phase) (128u + (phase) * 2u)
#define HIGH_SPEED_CHECK_BASE 64u
#define HIGH_SPEED_SET_BASE 80u

enum send_result {
	SEND_OK = 0,
	SEND_SOFTWARE_TIMEOUT = 1,
	SEND_RESPONSE_TIMEOUT = 2,
	SEND_RESPONSE_CRC = 3,
	SEND_WRONG_EVENT = 4,
};

enum read_mode {
	READ_STORE_ORIGINAL = 0,
	READ_COMPARE_PATTERN = 1,
	READ_COMPARE_ORIGINAL = 2,
};

enum run_mode {
	RUN_FULL = 0,
	RUN_VERIFY = 1,
	RUN_CAPTURE = 2,
};

static void delay(uint32_t count)
{
	for (volatile uint32_t i = 0; i < count; i++)
		__asm__ volatile ("" : : : "memory");
}

static uint32_t wait_status(uint32_t mask)
{
	for (uint32_t i = 0; i < COMMAND_WAIT_LIMIT; i++) {
		uint32_t status = MCI_STATUS;

		if (status & mask)
			return status;
	}

	return MCI_STATUS;
}

static int send_command(uint32_t cmd, uint32_t arg, uint32_t flags,
			uint32_t *final_status, uint32_t *response)
{
	uint32_t event_mask = MCI_RESPCRCFAIL | MCI_RESPTIMEOUT | MCI_RESPEND;

	if (MCI_COMMAND & MCI_CPSM_ENABLE) {
		MCI_COMMAND = 0;
		delay(16u);
	}

	MCI_ARGUMENT = arg;
	MCI_COMMAND = MCI_CPSM_ENABLE | MCI_CPSM_CMD(cmd) | flags;
	uint32_t status = wait_status(event_mask);

	*final_status = status;
	*response = MCI_RESPONSE0;
	if (!(status & event_mask))
		return SEND_SOFTWARE_TIMEOUT;
	if (status & MCI_RESPTIMEOUT)
		return SEND_RESPONSE_TIMEOUT;
	if (status & MCI_RESPCRCFAIL)
		return SEND_RESPONSE_CRC;
	if (!(status & MCI_RESPEND))
		return SEND_WRONG_EVENT;
	return SEND_OK;
}

static void stop_data(void)
{
	MCI_COMMAND = 0;
	MCI_DATACTRL = 0;
	MCI_DATALENGTH = 0;
	MCI_DMACTRL = 0;
	MCI_MASK = 0;
}

static void fail(uint32_t phase, uint32_t reason)
{
	OUT[3] = 0x80000000u | (phase << 8) | reason;
	stop_data();
}

static uint32_t pattern_word(uint32_t lba, uint32_t index)
{
	return 0xA15C0000u ^ lba ^ index * 0x9E3779B9u;
}

#if USE_L2_DMA
static void drain_write_buffer(void)
{
	uint32_t zero = 0;

	__asm__ volatile ("mcr p15, 0, %0, c7, c10, 4"
			  : : "r" (zero) : "memory");
}

static uint32_t dma_ram_address(uint32_t base, uint32_t offset)
{
#if USE_SCATTER_DMA
	if (base == DMA_BUFFER_ADDR)
		return base + offset / DMA_SEGMENT_BYTES * DMA_SEGMENT_STRIDE +
			offset % DMA_SEGMENT_BYTES;
#endif
	return base + offset;
}

static uint32_t dma_transfer_bytes(uint32_t base, uint32_t offset)
{
	uint32_t bytes = TOTAL_BYTES - offset;
	(void)base;

	if (bytes > DMA_CHUNK_BYTES)
		bytes = DMA_CHUNK_BYTES;
#if USE_SCATTER_DMA
	if (base == DMA_BUFFER_ADDR) {
		uint32_t segment_bytes = DMA_SEGMENT_BYTES -
			(offset % DMA_SEGMENT_BYTES);

		if (bytes > segment_bytes)
			bytes = segment_bytes;
	}
#endif
	return bytes;
}

static uint32_t dma_load_word(uint32_t word)
{
	volatile uint32_t *address = (volatile uint32_t *)(uintptr_t)
		dma_ram_address(DMA_BUFFER_ADDR, word * sizeof(uint32_t));

	return *address;
}

static void dma_store_word(uint32_t word, uint32_t value)
{
	volatile uint32_t *address = (volatile uint32_t *)(uintptr_t)
		dma_ram_address(DMA_BUFFER_ADDR, word * sizeof(uint32_t));

	*address = value;
}

#if USE_SCATTER_DMA
static uint32_t dma_guard_word(uint32_t segment, uint32_t word)
{
	return 0x51A70000u ^ segment * 0x9E37u ^ word;
}

static void fill_dma_guards(void)
{
	for (uint32_t segment = 0;
	     segment < TOTAL_BYTES / DMA_SEGMENT_BYTES; segment++) {
		volatile uint32_t *guard = (volatile uint32_t *)(uintptr_t)
			(DMA_BUFFER_ADDR + segment * DMA_SEGMENT_STRIDE +
			 DMA_SEGMENT_BYTES);

		for (uint32_t word = 0;
		     word < (DMA_SEGMENT_STRIDE - DMA_SEGMENT_BYTES) /
			    sizeof(uint32_t); word++)
			guard[word] = dma_guard_word(segment, word);
	}
	drain_write_buffer();
}

static int check_dma_guards(void)
{
	for (uint32_t segment = 0;
	     segment < TOTAL_BYTES / DMA_SEGMENT_BYTES; segment++) {
		volatile uint32_t *guard = (volatile uint32_t *)(uintptr_t)
			(DMA_BUFFER_ADDR + segment * DMA_SEGMENT_STRIDE +
			 DMA_SEGMENT_BYTES);

		for (uint32_t word = 0;
		     word < (DMA_SEGMENT_STRIDE - DMA_SEGMENT_BYTES) /
			    sizeof(uint32_t); word++) {
			uint32_t expected = dma_guard_word(segment, word);

			if (guard[word] != expected) {
				OUT[142] = segment;
				OUT[143] = word;
				OUT[144] = guard[word];
				OUT[145] = expected;
				return 0;
			}
		}
	}
	return 1;
}
#else
static void fill_dma_guards(void)
{
}

static int check_dma_guards(void)
{
	return 1;
}
#endif
#endif

#if USE_L2
static uint32_t l2_status(void)
{
	return (L2_STATUS1 >> (L2_BUFFER_ID * 4u)) & 0xFu;
}

static void l2_clear_status(void)
{
	L2_CONF1 |= 1u << (24u + L2_BUFFER_ID);
}

static void l2_configure(void)
{
	uint32_t request;

	SYSCTRL(0x0C) &= ~(1u << 3);
	request = L2_DMAREQ;
#if USE_L2_DMA
	request &= ~(L2_FRACTION_REQUEST | L2_DMA_REQUEST_MASK);
#endif
	L2_DMAREQ = request | 1u;
	L2_ASSIGN1 = (L2_ASSIGN1 & ~(7u << 12)) |
		(L2_BUFFER_ID << 12);
	L2_CONF1 = (L2_CONF1 & ~(1u << (8u + L2_BUFFER_ID))) |
		(1u << L2_BUFFER_ID) | (1u << (16u + L2_BUFFER_ID));
	l2_clear_status();
	OUT[56] = L2_CONF1;
	OUT[57] = L2_ASSIGN1;
	OUT[58] = L2_DMAREQ;
}

#if USE_L2_DMA
static void l2_dma_start(uint32_t ram_addr, uint32_t bytes,
			 int mem_to_buffer, int clear_status)
{
	uint32_t config;
	uint32_t request;

	if (clear_status)
		l2_clear_status();
	L2_DMA_ADDRESS = ram_addr;
	L2_DMA_COUNT = bytes / 64u;

	config = L2_CONF1;
	if (mem_to_buffer)
		config |= 1u << (8u + L2_BUFFER_ID);
	else
		config &= ~(1u << (8u + L2_BUFFER_ID));
	config |= (1u << L2_BUFFER_ID) |
		(1u << (16u + L2_BUFFER_ID));
	L2_CONF1 = config;

	request = L2_DMAREQ;
	request &= ~(L2_FRACTION_REQUEST | L2_DMA_REQUEST_MASK);
	L2_DMAREQ = request | 1u | L2_DMA_REQUEST;
}

static void record_dma(uint32_t phase)
{
	uint32_t base = DMA_RECORD_BASE(phase);

	OUT[base] = L2_DMA_ADDRESS;
	OUT[base + 1u] = L2_DMA_COUNT;
	OUT[base + 2u] = L2_DMAREQ;
	OUT[base + 3u] = L2_CONF1;
}
#endif
#endif

static void setup_data(int read, int four_bit, uint32_t dma_addr)
{
#if USE_L2
	l2_configure();
#if USE_L2_DMA
	l2_dma_start(dma_ram_address(dma_addr, 0u),
		     dma_transfer_bytes(dma_addr, 0u),
		     !read, 1);
#else
	(void)dma_addr;
#endif
	MCI_DMACTRL = MCI_DMA_BUFEN | MCI_DMA_SIZE(SECTOR_WORDS);
#else
	(void)dma_addr;
	MCI_DMACTRL = 0;
#endif
	MCI_DATATIMER = 0xFFFFFFFFu;
	MCI_DATALENGTH = BLOCKS * 512u;
	MCI_DATACTRL = MCI_DPSM_BLOCKSIZE(512u) | MCI_DPSM_ENABLE |
		(read ? MCI_DPSM_DIRECTION : 0u) |
		(four_bit ? MCI_DPSM_BUSMODE(1u) : 0u);
}

static void observe_block_end(uint32_t status, uint32_t *previous,
			      uint32_t *blocks)
{
	if ((status & MCI_DATABLOCKEND) &&
	    !(*previous & MCI_DATABLOCKEND))
		(*blocks)++;
	*previous = status;
}

static void record_data(uint32_t phase, uint32_t command_status,
			uint32_t response, uint32_t observed,
			uint32_t blocks)
{
	uint32_t base = RECORD_BASE(phase);

	OUT[base] = command_status;
	OUT[base + 1u] = response;
	OUT[base + 2u] = observed;
	OUT[base + 3u] = MCI_DATACNT;
	OUT[base + 4u] = blocks;
#if USE_L2
	OUT[59u + phase] = l2_status();
#endif
}

static int wait_card_ready(uint32_t rca, uint32_t *ready_response)
{
	uint32_t status;
	uint32_t response = 0;

	for (uint32_t attempt = 0; attempt < READY_ATTEMPTS; attempt++) {
		int rc = send_command(13u, rca, MCI_CPSM_RESPONSE,
				      &status, &response);

		if (rc != SEND_OK || (response & R1_STATUS_MASK))
			return 0;
		if ((response & R1_READY_FOR_DATA) &&
		    R1_CURRENT_STATE(response) == R1_STATE_TRAN) {
			*ready_response = response;
			return 1;
		}
		delay(40000u);
	}

	return 0;
}

static int finish_multiblock(uint32_t phase, uint32_t rca)
{
	uint32_t base = RECORD_BASE(phase);
	uint32_t status;
	uint32_t response;
	uint32_t ready_response;

	stop_data();
	int rc = send_command(12u, 0u, MCI_CPSM_RESPONSE,
			      &status, &response);
	OUT[base + 5u] = status;
	OUT[base + 6u] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK))
		return 0;
	if (!wait_card_ready(rca, &ready_response))
		return 0;
	OUT[base + 7u] = ready_response;
	return 1;
}

#if USE_L2_DMA
static int set_block_count(uint32_t phase)
{
#if USE_CMD23
	uint32_t base = CMD23_RECORD_BASE(phase);
	uint32_t status;
	uint32_t response;
	int rc = send_command(23u, BLOCKS, MCI_CPSM_RESPONSE,
			      &status, &response);

	OUT[base] = status;
	OUT[base + 1u] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		fail(phase, rc != SEND_OK ? 80u + (uint32_t)rc : 85u);
		return 0;
	}
#else
	(void)phase;
#endif
	return 1;
}

static int finish_dma_multiblock(uint32_t phase, uint32_t rca,
				 int data_complete)
{
#if USE_CMD23
	if (data_complete) {
		uint32_t base = RECORD_BASE(phase);
		uint32_t ready_response;

		stop_data();
		OUT[base + 5u] = 0x4E4F5354u;
		OUT[base + 6u] = 0u;
		if (!wait_card_ready(rca, &ready_response))
			return 0;
		OUT[base + 7u] = ready_response;
		return 1;
	}
#else
	(void)data_complete;
#endif
	return finish_multiblock(phase, rca);
}

static int wait_l2_dma(uint32_t *observed, uint32_t *previous,
			uint32_t *blocks, uint32_t *final_status,
			uint32_t *final_request, uint32_t dma_addr,
			int mem_to_buffer, uint32_t *chunks)
{
	uint32_t submitted = dma_transfer_bytes(dma_addr, 0u);

	*chunks = 1u;
	for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
		uint32_t status = MCI_STATUS;
		uint32_t request = L2_DMAREQ;

		*observed |= status;
		observe_block_end(status, previous, blocks);
		*final_status = status;
		*final_request = request;
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR))
			return -1;
		if (!(request & L2_DMA_REQUEST)) {
			if (submitted < TOTAL_BYTES) {
				uint32_t bytes = dma_transfer_bytes(dma_addr,
								    submitted);

				l2_dma_start(dma_ram_address(dma_addr, submitted), bytes,
					     mem_to_buffer, 0);
				submitted += bytes;
				(*chunks)++;
				continue;
			}
			if (*observed & MCI_DATAEND)
				return 1;
		}
	}

	return 0;
}

static int compare_dma_read(enum read_mode mode, uint32_t lba)
{
	if (mode == READ_STORE_ORIGINAL)
		return 1;

	for (uint32_t word = 0; word < TOTAL_WORDS; word++) {
		uint32_t value = dma_load_word(word);
		uint32_t expected = mode == READ_COMPARE_PATTERN ?
			pattern_word(lba, word) : ORIGINAL[word];

		if (value != expected) {
			OUT[8] = word;
			OUT[9] = value;
			OUT[10] = expected;
			return 0;
		}
	}

	return 1;
}

static int read_blocks(uint32_t phase, uint32_t command_arg, uint32_t lba,
		       uint32_t rca, int four_bit, enum read_mode mode)
{
	uint32_t dma_addr = mode == READ_STORE_ORIGINAL ?
		(uint32_t)(uintptr_t)ORIGINAL : DMA_BUFFER_ADDR;
	uint32_t command_status;
	uint32_t response;
	uint32_t observed;
	uint32_t previous;
	uint32_t blocks = 0;
	uint32_t status = 0;
	uint32_t request = 0;
	uint32_t chunks = 0;

	if (!set_block_count(phase))
		return 0;
	setup_data(1, four_bit, dma_addr);
	int rc = send_command(18u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		record_data(phase, command_status, response,
			    command_status, blocks);
		record_dma(phase);
		fail(phase, rc != SEND_OK ? (uint32_t)rc : 60u);
		return 0;
	}

	observed = command_status;
	previous = command_status;
	rc = wait_l2_dma(&observed, &previous, &blocks, &status, &request,
			 dma_addr, 0, &chunks);
	uint32_t remaining = MCI_DATACNT;
	uint32_t buffered = l2_status();
	record_data(phase, command_status, response, observed, blocks);
	record_dma(phase);
	OUT[100u + phase] = chunks;
	int data_complete = rc == 1 && (observed & MCI_DATAEND) != 0u &&
		remaining == 0u && buffered == 0u &&
		(request & L2_DMA_REQUEST) == 0u;
	if (!finish_dma_multiblock(phase, rca, data_complete)) {
		fail(phase, 61u);
		return 0;
	}
	if (rc != 1) {
		fail(phase, rc < 0 ? 62u : 63u);
		return 0;
	}
	if ((observed & MCI_DATAEND) == 0u || remaining != 0u ||
	    buffered != 0u || (request & L2_DMA_REQUEST)) {
		fail(phase, 64u);
		return 0;
	}
	if (!compare_dma_read(mode, lba)) {
		fail(phase, 65u);
		return 0;
	}
	if (!check_dma_guards()) {
		fail(phase, 66u);
		return 0;
	}
	return 1;
}

static int write_blocks(uint32_t phase, uint32_t command_arg, uint32_t lba,
			uint32_t rca, int four_bit, int restore)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed;
	uint32_t previous;
	uint32_t blocks = 0;
	uint32_t status = 0;
	uint32_t request = 0;
	uint32_t chunks = 0;

	for (uint32_t word = 0; word < TOTAL_WORDS; word++)
		dma_store_word(word, restore ? ORIGINAL[word] :
			       pattern_word(lba, word));
	drain_write_buffer();

	if (!set_block_count(phase))
		return 0;
	int rc = send_command(25u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		record_data(phase, command_status, response,
			    command_status, blocks);
		fail(phase, rc != SEND_OK ? (uint32_t)rc : 70u);
		return 0;
	}

	setup_data(0, four_bit, DMA_BUFFER_ADDR);
	observed = command_status;
	previous = command_status;
	rc = wait_l2_dma(&observed, &previous, &blocks, &status, &request,
			 DMA_BUFFER_ADDR, 1, &chunks);
	uint32_t remaining = MCI_DATACNT;
	uint32_t buffered = l2_status();
	record_data(phase, command_status, response, observed, blocks);
	record_dma(phase);
	OUT[100u + phase] = chunks;
	int data_complete = rc == 1 && (observed & MCI_DATAEND) != 0u &&
		remaining == 0u && buffered == 0u &&
		(request & L2_DMA_REQUEST) == 0u;
	if (!finish_dma_multiblock(phase, rca, data_complete)) {
		fail(phase, 71u);
		return 0;
	}
	if (rc != 1) {
		fail(phase, rc < 0 ? 72u : 73u);
		return 0;
	}
	if ((observed & MCI_DATAEND) == 0u || remaining != 0u ||
	    buffered != 0u || (request & L2_DMA_REQUEST)) {
		fail(phase, 74u);
		return 0;
	}
	if (!check_dma_guards()) {
		fail(phase, 75u);
		return 0;
	}
	return 1;
}
#elif USE_L2
static int wait_l2_block(uint32_t *observed, uint32_t *previous,
			 uint32_t *blocks, uint32_t *final_status)
{
	for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
		uint32_t before = *blocks;
		uint32_t status = MCI_STATUS;

		*observed |= status;
		observe_block_end(status, previous, blocks);
		*final_status = status;
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR))
			return -1;
		if (*blocks != before)
			return 1;
		if (status & MCI_DATAEND)
			return 0;
	}

	return 0;
}

static void consume_read_word(enum read_mode mode, uint32_t lba,
			      uint32_t word, uint32_t value,
			      uint32_t *mismatch)
{
	if (mode == READ_STORE_ORIGINAL) {
		ORIGINAL[word] = value;
	} else {
		uint32_t expected = mode == READ_COMPARE_PATTERN ?
			pattern_word(lba, word) : ORIGINAL[word];

		if (!*mismatch && value != expected) {
			*mismatch = 1u;
			OUT[8] = word;
			OUT[9] = value;
			OUT[10] = expected;
		}
	}
}

static int read_blocks(uint32_t phase, uint32_t command_arg, uint32_t lba,
		       uint32_t rca, int four_bit, enum read_mode mode)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed;
	uint32_t previous;
	uint32_t blocks = 0;
	uint32_t status = 0;
	uint32_t mismatch = 0;

	setup_data(1, four_bit, 0u);
	int rc = send_command(18u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		record_data(phase, command_status, response,
			    command_status, blocks);
		fail(phase, rc != SEND_OK ? (uint32_t)rc : 40u);
		return 0;
	}

	observed = command_status;
	previous = command_status;
	for (uint32_t block = 0; block < BLOCKS; block++) {
		rc = wait_l2_block(&observed, &previous, &blocks, &status);
		OUT[L2_STATUS_BASE + phase * BLOCKS + block] = l2_status();
		if (rc != 1 || l2_status() != 8u) {
			record_data(phase, command_status, response,
				    observed, blocks);
			finish_multiblock(phase, rca);
			fail(phase, rc < 0 ? 41u : 42u);
			return 0;
		}

		for (uint32_t word = 0; word < SECTOR_WORDS; word++) {
			uint32_t index = block * SECTOR_WORDS + word;
			uint32_t value = L2_BUFFER[word];

			consume_read_word(mode, lba, index, value, &mismatch);
		}
	}

	for (uint32_t poll = 0;
	     !(observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		status = MCI_STATUS;
		observed |= status;
		observe_block_end(status, &previous, &blocks);
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR))
			break;
	}
	uint32_t remaining = MCI_DATACNT;
	uint32_t buffered = l2_status();
	record_data(phase, command_status, response, observed, blocks);
	if (!finish_multiblock(phase, rca)) {
		fail(phase, 43u);
		return 0;
	}
	if (observed & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			MCI_STARTBIT_ERR)) {
		fail(phase, 44u);
		return 0;
	}
	if ((observed & MCI_DATAEND) == 0u || remaining != 0u || buffered != 0u ||
	    blocks != BLOCKS) {
		fail(phase, 45u);
		return 0;
	}
	if (mismatch) {
		fail(phase, 46u);
		return 0;
	}
	return 1;
}

static int fill_l2_block(uint32_t lba, uint32_t block, int restore,
			 uint32_t *observed, uint32_t *status)
{
	for (uint32_t chunk = 0; chunk < 8u; chunk++) {
		for (uint32_t poll = 0; l2_status() == 8u; poll++) {
			if (poll == DATA_WAIT_LIMIT)
				return 0;
			*status = MCI_STATUS;
			*observed |= *status;
			if (*status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
				       MCI_STARTBIT_ERR | MCI_DATAEND))
				return 0;
		}

		for (uint32_t word = 0; word < 16u; word++) {
			uint32_t offset = chunk * 16u + word;
			uint32_t index = block * SECTOR_WORDS + offset;

			L2_BUFFER[offset] = restore ? ORIGINAL[index] :
				pattern_word(lba, index);
		}
	}

	return 1;
}

static int write_blocks(uint32_t phase, uint32_t command_arg, uint32_t lba,
			uint32_t rca, int four_bit, int restore)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed;
	uint32_t previous;
	uint32_t blocks = 0;
	uint32_t status = 0;

	int rc = send_command(25u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		record_data(phase, command_status, response,
			    command_status, blocks);
		fail(phase, rc != SEND_OK ? (uint32_t)rc : 50u);
		return 0;
	}

	setup_data(0, four_bit, 0u);
	observed = command_status;
	previous = command_status;
	for (uint32_t block = 0; block < BLOCKS; block++) {
		l2_clear_status();
		if (!fill_l2_block(lba, block, restore, &observed, &status)) {
			record_data(phase, command_status, response,
				    observed, blocks);
			finish_multiblock(phase, rca);
			fail(phase, 51u);
			return 0;
		}
		rc = wait_l2_block(&observed, &previous, &blocks, &status);
		OUT[L2_STATUS_BASE + phase * BLOCKS + block] = l2_status();
		if (rc != 1) {
			record_data(phase, command_status, response,
				    observed, blocks);
			finish_multiblock(phase, rca);
			fail(phase, rc < 0 ? 52u : 53u);
			return 0;
		}
	}

	for (uint32_t poll = 0;
	     !(observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		status = MCI_STATUS;
		observed |= status;
		observe_block_end(status, &previous, &blocks);
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR))
			break;
	}
	uint32_t remaining = MCI_DATACNT;
	uint32_t buffered = l2_status();
	record_data(phase, command_status, response, observed, blocks);
	if (!finish_multiblock(phase, rca)) {
		fail(phase, 54u);
		return 0;
	}
	if (observed & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			MCI_STARTBIT_ERR)) {
		fail(phase, 55u);
		return 0;
	}
	if ((observed & MCI_DATAEND) == 0u || remaining != 0u || buffered != 0u ||
	    blocks != BLOCKS) {
		fail(phase, 56u);
		return 0;
	}
	return 1;
}
#else
static int read_blocks(uint32_t phase, uint32_t command_arg, uint32_t lba,
		       uint32_t rca, int four_bit, enum read_mode mode)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed;
	uint32_t previous;
	uint32_t blocks = 0;
	uint32_t status = 0;
	uint32_t mismatch = 0;

	setup_data(1, four_bit, 0u);
	int rc = send_command(18u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		record_data(phase, command_status, response,
			    command_status, blocks);
		fail(phase, rc != SEND_OK ? (uint32_t)rc : 40u);
		return 0;
	}

	observed = command_status;
	previous = command_status;
	for (uint32_t word = 0; word < TOTAL_WORDS; word++) {
		int ready = 0;

		for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
			status = MCI_STATUS;
			observed |= status;
			observe_block_end(status, &previous, &blocks);
			if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
				      MCI_STARTBIT_ERR))
				break;
			if ((status & (MCI_FIFOFULL | MCI_RXACTIVE)) ==
			    (MCI_FIFOFULL | MCI_RXACTIVE)) {
				ready = 1;
				break;
			}
			if (status & MCI_DATAEND)
				break;
		}
		if (!ready) {
			record_data(phase, command_status, response,
				    observed, blocks);
			finish_multiblock(phase, rca);
			fail(phase, status & MCI_DATAEND ? 41u : 42u);
			return 0;
		}

		uint32_t value = MCI_FIFO;
		if (mode == READ_STORE_ORIGINAL) {
			ORIGINAL[word] = value;
		} else {
			uint32_t expected = mode == READ_COMPARE_PATTERN ?
				pattern_word(lba, word) : ORIGINAL[word];

			if (!mismatch && value != expected) {
				mismatch = 1u;
				OUT[8] = word;
				OUT[9] = value;
				OUT[10] = expected;
			}
		}
	}

	for (uint32_t poll = 0;
	     !(observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		status = MCI_STATUS;
		observed |= status;
		observe_block_end(status, &previous, &blocks);
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR))
			break;
	}
	uint32_t remaining = MCI_DATACNT;
	record_data(phase, command_status, response, observed, blocks);
	if (!finish_multiblock(phase, rca)) {
		fail(phase, 43u);
		return 0;
	}
	if (observed & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			MCI_STARTBIT_ERR)) {
		fail(phase, 44u);
		return 0;
	}
	if ((observed & MCI_DATAEND) == 0u || remaining != 0u ||
	    blocks != BLOCKS) {
		fail(phase, 45u);
		return 0;
	}
	if (mismatch) {
		fail(phase, 46u);
		return 0;
	}
	return 1;
}

static int write_blocks(uint32_t phase, uint32_t command_arg, uint32_t lba,
			uint32_t rca, int four_bit, int restore)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed;
	uint32_t previous;
	uint32_t blocks = 0;
	uint32_t status = 0;

	int rc = send_command(25u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		record_data(phase, command_status, response,
			    command_status, blocks);
		fail(phase, rc != SEND_OK ? (uint32_t)rc : 50u);
		return 0;
	}

	setup_data(0, four_bit, 0u);
	observed = command_status;
	previous = command_status;
	for (uint32_t word = 0; word < TOTAL_WORDS; word++) {
		int ready = 0;

		for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
			status = MCI_STATUS;
			observed |= status;
			observe_block_end(status, &previous, &blocks);
			if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
				      MCI_STARTBIT_ERR))
				break;
			if ((status & (MCI_FIFOEMPTY | MCI_TXACTIVE)) ==
			    (MCI_FIFOEMPTY | MCI_TXACTIVE)) {
				ready = 1;
				break;
			}
			if (status & MCI_DATAEND)
				break;
		}
		if (!ready) {
			record_data(phase, command_status, response,
				    observed, blocks);
			finish_multiblock(phase, rca);
			fail(phase, status & MCI_DATAEND ? 51u : 52u);
			return 0;
		}

		MCI_FIFO = restore ? ORIGINAL[word] : pattern_word(lba, word);
	}

	for (uint32_t poll = 0;
	     !(observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		status = MCI_STATUS;
		observed |= status;
		observe_block_end(status, &previous, &blocks);
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR))
			break;
	}
	uint32_t remaining = MCI_DATACNT;
	record_data(phase, command_status, response, observed, blocks);
	if (!finish_multiblock(phase, rca)) {
		fail(phase, 53u);
		return 0;
	}
	if (observed & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			MCI_STARTBIT_ERR)) {
		fail(phase, 54u);
		return 0;
	}
	if ((observed & MCI_DATAEND) == 0u || remaining != 0u ||
	    blocks != BLOCKS) {
		fail(phase, 55u);
		return 0;
	}
	return 1;
}
#endif

static int set_four_bit_bus(uint32_t rca)
{
	uint32_t status;
	uint32_t response;

	int rc = send_command(55u, rca, MCI_CPSM_RESPONSE,
			      &status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK) ||
	    !(response & (1u << 5)))
		return 0;
	rc = send_command(6u, 2u, MCI_CPSM_RESPONSE, &status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK))
		return 0;

	MCI_CLOCK = MCI_CLOCK_20_67_MHZ;
	delay(40000u);
	OUT[13] = response;
	OUT[14] = MCI_CLOCK;
	return 1;
}

#if USE_HIGH_SPEED
static uint32_t switch_status_byte(volatile uint32_t *status, uint32_t index)
{
	return (status[index / 4u] >> ((index % 4u) * 8u)) & 0xFFu;
}

static int read_switch_status(uint32_t arg, volatile uint32_t *destination,
			      uint32_t record_base)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed = 0;
	uint32_t words = 0;

	stop_data();
	MCI_DATATIMER = 0xFFFFFFFFu;
	MCI_DATALENGTH = 64u;
	MCI_DATACTRL = MCI_DPSM_BLOCKSIZE(64u) | MCI_DPSM_BUSMODE(1u) |
		MCI_DPSM_DIRECTION | MCI_DPSM_ENABLE;
	int rc = send_command(6u, arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	OUT[record_base] = command_status;
	OUT[record_base + 1u] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		stop_data();
		return 0;
	}

	for (uint32_t word = 0; word < 16u; word++) {
		int ready = 0;

		for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
			uint32_t status = MCI_STATUS;

			observed |= status;
			if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
				      MCI_STARTBIT_ERR))
				break;
			if ((status & (MCI_FIFOFULL | MCI_RXACTIVE)) ==
			    (MCI_FIFOFULL | MCI_RXACTIVE)) {
				ready = 1;
				break;
			}
			if (status & MCI_DATAEND)
				break;
		}
		if (!ready)
			break;
		destination[word] = MCI_FIFO;
		words++;
	}

	for (uint32_t poll = 0;
	     !(observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		uint32_t status = MCI_STATUS;

		observed |= status;
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR))
			break;
	}
	OUT[record_base + 2u] = observed;
	OUT[record_base + 3u] = MCI_DATACNT;
	stop_data();
	return words == 16u &&
		(observed & (MCI_DATAEND | MCI_DATABLOCKEND)) ==
		(MCI_DATAEND | MCI_DATABLOCKEND) &&
		!(observed & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR)) && OUT[record_base + 3u] == 0u;
}

static int enable_high_speed(uint32_t rca)
{
	uint32_t ready_response;

	if (!read_switch_status(0x00FFFFF0u, &OUT[HIGH_SPEED_CHECK_BASE],
				96u))
		return 0;
	OUT[138] = switch_status_byte(&OUT[HIGH_SPEED_CHECK_BASE], 13u);
	if (!(OUT[138] & 0x02u))
		return 0;

	if (!read_switch_status(0x80FFFFF1u, &OUT[HIGH_SPEED_SET_BASE],
				124u))
		return 0;
	OUT[139] = switch_status_byte(&OUT[HIGH_SPEED_SET_BASE], 16u);
	if ((OUT[139] & 0x0Fu) != 1u)
		return 0;

	MCI_CLOCK = MCI_CLOCK_HIGH_SPEED;
	delay(40000u);
	OUT[140] = MCI_CLOCK;
	if (!wait_card_ready(rca, &ready_response))
		return 0;
	OUT[141] = ready_response;
	return 1;
}
#endif

void stub_main(void)
{
	uint32_t prepared_magic = OUT[0];
	uint32_t prepared_experiment = OUT[2];
	uint32_t prepared_status = OUT[3];
	uint32_t ocr = OUT[6];
	uint32_t rca = OUT[7];
	uint32_t prepared_lba = OUT[9];
	uint32_t arm = PARAM[0];
	uint32_t lba = PARAM[1];
	uint32_t four_bit = PARAM[3];
	uint32_t run_mode = PARAM[4];

	for (uint32_t i = 0; i < RESULT_WORDS; i++)
		OUT[i] = 0;
	OUT[0] = RESULT_MAGIC;
	OUT[1] = USE_L2_DMA ? 3u : (USE_L2 ? 2u : 1u);
	OUT[2] = USE_L2_DMA ? DMA_EXPERIMENT_BASE + run_mode :
		(USE_L2 ? (run_mode == RUN_VERIFY ? 14u : 13u) :
		 (run_mode == RUN_VERIFY ? 12u : (four_bit ? 11u : 10u)));
	OUT[4] = BLOCKS;
	OUT[5] = lba;
	OUT[6] = ocr;
	OUT[7] = rca;
	OUT[8] = 0xFFFFFFFFu;

	if (prepared_magic != RESULT_MAGIC || prepared_experiment != 4u ||
	    prepared_status != 0u || arm != WRITE_LBA_MAGIC || lba == 0u ||
	    prepared_lba != lba || !rca || four_bit > 1u ||
	    run_mode > RUN_CAPTURE || (!USE_L2_DMA && run_mode == RUN_CAPTURE)) {
		fail(0u, 1u);
		return;
	}

	uint32_t command_arg;
	if (ocr & (1u << 30)) {
		command_arg = lba;
	} else {
		if (lba > 0x00800000u - BLOCKS) {
			fail(0u, 2u);
			return;
		}
		command_arg = lba * 512u;
	}

	uint32_t ready_response;
	if (!wait_card_ready(rca, &ready_response)) {
		fail(1u, 1u);
		return;
	}
	OUT[15] = ready_response;
	if (four_bit && !set_four_bit_bus(rca)) {
		fail(1u, 2u);
		return;
	}
#if USE_HIGH_SPEED
	if (!four_bit || !enable_high_speed(rca)) {
		fail(1u, 3u);
		return;
	}
#endif
#if USE_L2_DMA
	fill_dma_guards();
#endif

	if (run_mode == RUN_CAPTURE) {
		if (!read_blocks(0u, command_arg, lba, rca, four_bit,
				 READ_STORE_ORIGINAL))
			return;
		return;
	}

	if (run_mode == RUN_VERIFY) {
		if (!read_blocks(4u, command_arg, lba, rca, four_bit,
				 READ_COMPARE_ORIGINAL))
			return;
		return;
	}

	if (!read_blocks(0u, command_arg, lba, rca, four_bit,
			 READ_STORE_ORIGINAL))
		return;

	uint32_t original_checksum = 0;
	uint32_t pattern_checksum = 0;
	for (uint32_t word = 0; word < TOTAL_WORDS; word++) {
		original_checksum ^= ORIGINAL[word];
		pattern_checksum ^= pattern_word(lba, word);
	}
	OUT[11] = original_checksum;
	OUT[12] = pattern_checksum;

	uint32_t first_error = 0;
	if (!write_blocks(1u, command_arg, lba, rca, four_bit, 0)) {
		first_error = OUT[3];
	} else if (!read_blocks(2u, command_arg, lba, rca, four_bit,
				READ_COMPARE_PATTERN)) {
		first_error = OUT[3];
	}

	OUT[3] = 0;
	if (!write_blocks(3u, command_arg, lba, rca, four_bit, 1))
		return;
	if (!read_blocks(4u, command_arg, lba, rca, four_bit,
			 READ_COMPARE_ORIGINAL))
		return;
	OUT[15] = 0x52535452u;
	if (first_error)
		OUT[3] = first_error;
}
