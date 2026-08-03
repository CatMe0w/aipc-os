#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define SYSCTRL(off) REG32(0x08000000u + (off))
#define MCI(off) REG32(0x20020000u + (off))
#define L2(off) REG32(0x2002c000u + (off))
#define OUT ((volatile uint32_t *)(uintptr_t)0x48001100u)
#define PARAM ((volatile uint32_t *)(uintptr_t)0x48001380u)
#define ORIGINAL ((volatile uint32_t *)(uintptr_t)0x30010000u)
#define DMA_BUFFER ((volatile uint32_t *)(uintptr_t)0x30012000u)

#define SYSCTRL_INT_STATUS SYSCTRL(0xcc)

#define MCI_CLOCK MCI(0x04)
#define MCI_ARGUMENT MCI(0x08)
#define MCI_COMMAND MCI(0x0c)
#define MCI_RESPONSE0 MCI(0x14)
#define MCI_DATATIMER MCI(0x24)
#define MCI_DATALENGTH MCI(0x28)
#define MCI_DATACTRL MCI(0x2c)
#define MCI_DATACNT MCI(0x30)
#define MCI_STATUS MCI(0x34)
#define MCI_MASK MCI(0x38)
#define MCI_DMACTRL MCI(0x3c)

#define L2_DMA_ADDRESS L2(0x08)
#define L2_DMA_COUNT L2(0x48)
#define L2_DMAREQ L2(0x80)
#define L2_CONF1 L2(0x88)
#define L2_ASSIGN1 L2(0x90)
#define L2_BUFINTEN L2(0x9c)
#define L2_STATUS1 L2(0xa0)

#define MCI_CPSM_ENABLE (1u << 0)
#define MCI_CPSM_CMD(cmd) (((cmd) & 0x3fu) << 1)
#define MCI_CPSM_RESPONSE (1u << 7)
#define MCI_CPSM_WITHDATA (1u << 11)

#define MCI_DPSM_ENABLE (1u << 0)
#define MCI_DPSM_DIRECTION (1u << 1)
#define MCI_DPSM_BUSMODE(x) (((x) & 0x3u) << 3)
#define MCI_DPSM_BLOCKSIZE(size) (((size) & 0xfffu) << 16)

#define MCI_DMA_BUFEN (1u << 0)
#define MCI_DMA_SIZE(words) (((words) & 0x7fffu) << 17)

#define MCI_RESPCRCFAIL (1u << 0)
#define MCI_DATACRCFAIL (1u << 1)
#define MCI_RESPTIMEOUT (1u << 2)
#define MCI_DATATIMEOUT (1u << 3)
#define MCI_RESPEND (1u << 4)
#define MCI_DATAEND (1u << 6)
#define MCI_STARTBIT_ERR (1u << 8)

#define MCI_COMMAND_EVENTS (MCI_RESPCRCFAIL | MCI_RESPTIMEOUT | MCI_RESPEND)
#define MCI_DATA_EVENTS (MCI_DATACRCFAIL | MCI_DATATIMEOUT | \
			 MCI_DATAEND | MCI_STARTBIT_ERR)
#define MCI_DATA_ERRORS (MCI_DATACRCFAIL | MCI_DATATIMEOUT | \
			 MCI_STARTBIT_ERR)

#define L2_BUFFER_ID 2u
#define L2_DMAREQ_ENABLE (1u << 0)
#define L2_FRACTION_REQUEST (1u << 9)
#define L2_REQUEST_MASK (0xffffu << 16)
#define L2_BUFFER_REQUEST (1u << (24u + L2_BUFFER_ID))
#define L2_BUFFER_INTEN (1u << (9u + L2_BUFFER_ID))
#define L2_BUFFER_DIRECTION (1u << (8u + L2_BUFFER_ID))
#define L2_BUFFER_DMA_VALID (1u << L2_BUFFER_ID)
#define L2_BUFFER_VALID (1u << (16u + L2_BUFFER_ID))
#define L2_BUFFER_CLEAR (1u << (24u + L2_BUFFER_ID))

#define MAIN_IRQ_L2 (1u << 10)
#define MAIN_IRQ_MCI (1u << 22)

#define R1_STATUS_MASK 0xfff9a000u
#define R1_READY_FOR_DATA (1u << 8)
#define R1_CURRENT_STATE(value) (((value) >> 9) & 0xfu)
#define R1_STATE_TRAN 4u

#define RESULT_MAGIC 0x53445052u
#define READ_LBA_MAGIC 0x52454144u
#define PREPARED_EXPERIMENT 51u
#define IRQ_EXPERIMENT 55u
#define RESULT_WORDS 64u
#define WAIT_LIMIT 8000000u
#define TRANSFER_BLOCKS 64u
#define TRANSFER_BYTES (TRANSFER_BLOCKS * 512u)
#define DMA_CHUNK_BYTES 8192u
#define DMA_CHUNKS (TRANSFER_BYTES / DMA_CHUNK_BYTES)
#define SECTOR_WORDS 128u

enum send_result {
	SEND_OK = 0,
	SEND_SOFTWARE_TIMEOUT = 1,
	SEND_RESPONSE_TIMEOUT = 2,
	SEND_RESPONSE_CRC = 3,
	SEND_WRONG_EVENT = 4,
	SEND_STALE_IRQ = 5,
};

static void delay(uint32_t count)
{
	for (volatile uint32_t i = 0; i < count; i++)
		__asm__ volatile ("" : : : "memory");
}

static int wait_main_set(uint32_t source, uint32_t *pending)
{
	for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
		uint32_t value = SYSCTRL_INT_STATUS;

		if (value & source) {
			*pending = value;
			return 1;
		}
	}
	*pending = SYSCTRL_INT_STATUS;
	return 0;
}

static int wait_main_clear(uint32_t source, uint32_t *pending)
{
	for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
		uint32_t value = SYSCTRL_INT_STATUS;

		if (!(value & source)) {
			*pending = value;
			return 1;
		}
	}
	*pending = SYSCTRL_INT_STATUS;
	return 0;
}

static void stop_data(void)
{
	uint32_t request;

	MCI_MASK = 0;
	L2_BUFINTEN &= ~L2_BUFFER_INTEN;
	MCI_COMMAND = 0;
	MCI_DATACTRL = 0;
	MCI_DATALENGTH = 0;
	MCI_DMACTRL = 0;
	request = L2_DMAREQ;
	if (request & L2_BUFFER_REQUEST) {
		request &= ~L2_BUFFER_REQUEST;
		request |= L2_DMAREQ_ENABLE;
		L2_DMAREQ = request;
		L2_DMA_COUNT = 0;
	}
}

static void fail(uint32_t phase, uint32_t reason)
{
	OUT[3] = 0x80000000u | (phase << 8) | reason;
	stop_data();
}

static int send_command_irq(uint32_t cmd, uint32_t arg, uint32_t flags,
			    uint32_t record_base)
{
	uint32_t pending;
	uint32_t status;
	uint32_t response;
	uint32_t clear_pending;

	MCI_MASK = 0;
	if (!wait_main_clear(MAIN_IRQ_MCI, &pending))
		return SEND_STALE_IRQ;
	if (MCI_COMMAND & MCI_CPSM_ENABLE) {
		MCI_COMMAND = 0;
		delay(16u);
	}

	MCI_ARGUMENT = arg;
	MCI_MASK = MCI_COMMAND_EVENTS;
	MCI_COMMAND = MCI_CPSM_ENABLE | MCI_CPSM_CMD(cmd) | flags;
	if (!wait_main_set(MAIN_IRQ_MCI, &pending)) {
		MCI_MASK = 0;
		return SEND_SOFTWARE_TIMEOUT;
	}

	status = MCI_STATUS;
	response = MCI_RESPONSE0;
	OUT[record_base] = pending;
	OUT[record_base + 1u] = status;
	OUT[record_base + 2u] = response;
	MCI_MASK = 0;
	wait_main_clear(MAIN_IRQ_MCI, &clear_pending);
	OUT[record_base + 3u] = clear_pending;

	if (status & MCI_RESPTIMEOUT)
		return SEND_RESPONSE_TIMEOUT;
	if (status & MCI_RESPCRCFAIL)
		return SEND_RESPONSE_CRC;
	if (!(status & MCI_RESPEND))
		return SEND_WRONG_EVENT;
	return SEND_OK;
}

static void l2_configure(void)
{
	uint32_t request;
	uint32_t config;

	SYSCTRL(0x0c) &= ~(1u << 3);
	request = L2_DMAREQ;
	request &= ~(L2_FRACTION_REQUEST | L2_REQUEST_MASK);
	L2_DMAREQ = request | L2_DMAREQ_ENABLE;
	L2_ASSIGN1 = (L2_ASSIGN1 & ~(7u << 12)) |
		(L2_BUFFER_ID << 12);
	config = L2_CONF1;
	config &= ~L2_BUFFER_DIRECTION;
	config |= L2_BUFFER_DMA_VALID | L2_BUFFER_VALID;
	L2_CONF1 = config | L2_BUFFER_CLEAR;
	L2_BUFINTEN &= ~L2_BUFFER_INTEN;
}

static int l2_start_dma(uint32_t address)
{
	uint32_t pending;
	uint32_t config;
	uint32_t request;

	L2_BUFINTEN &= ~L2_BUFFER_INTEN;
	if (!wait_main_clear(MAIN_IRQ_L2, &pending))
		return 0;
	L2_DMA_ADDRESS = address;
	L2_DMA_COUNT = DMA_CHUNK_BYTES / 64u;
	config = L2_CONF1;
	config &= ~L2_BUFFER_DIRECTION;
	config |= L2_BUFFER_DMA_VALID | L2_BUFFER_VALID;
	L2_CONF1 = config;
	L2_BUFINTEN |= L2_BUFFER_INTEN;
	request = L2_DMAREQ;
	request &= ~(L2_FRACTION_REQUEST | L2_REQUEST_MASK);
	L2_DMAREQ = request | L2_DMAREQ_ENABLE | L2_BUFFER_REQUEST;
	return 1;
}

static int wait_l2_irq(uint32_t chunk)
{
	uint32_t pending;
	uint32_t request;
	uint32_t clear_pending;

	if (!wait_main_set(MAIN_IRQ_L2, &pending))
		return 0;
	request = L2_DMAREQ;
	L2_BUFINTEN &= ~L2_BUFFER_INTEN;
	wait_main_clear(MAIN_IRQ_L2, &clear_pending);
	if (chunk == 0u) {
		OUT[20] = pending;
		OUT[21] = request;
		OUT[22] = clear_pending;
	} else if (chunk == DMA_CHUNKS - 1u) {
		OUT[23] = pending;
		OUT[24] = request;
		OUT[25] = clear_pending;
	}
	return !(request & L2_BUFFER_REQUEST) &&
		!(clear_pending & MAIN_IRQ_L2);
}

static int wait_data_irq(void)
{
	uint32_t pending;
	uint32_t status;
	uint32_t clear_pending;

	if (!wait_main_set(MAIN_IRQ_MCI, &pending))
		return 0;
	status = MCI_STATUS;
	MCI_MASK = 0;
	wait_main_clear(MAIN_IRQ_MCI, &clear_pending);
	OUT[27] = pending;
	OUT[28] = status;
	OUT[29] = MCI_DATACNT;
	OUT[30] = clear_pending;
	return (status & MCI_DATAEND) && !(status & MCI_DATA_ERRORS) &&
		MCI_DATACNT == 0u && !(clear_pending & MAIN_IRQ_MCI);
}

static int compare_first_sector(void)
{
	OUT[34] = 0xffffffffu;
	for (uint32_t word = 0; word < SECTOR_WORDS; word++) {
		if (DMA_BUFFER[word] != ORIGINAL[word]) {
			OUT[34] = word;
			OUT[35] = DMA_BUFFER[word];
			OUT[36] = ORIGINAL[word];
			return 0;
		}
	}
	return 1;
}

void stub_main(void)
{
	uint32_t prepared_magic = OUT[0];
	uint32_t prepared_experiment = OUT[2];
	uint32_t prepared_status = OUT[3];
	uint32_t prepared_lba = OUT[5];
	uint32_t ocr = OUT[6];
	uint32_t rca = OUT[7];
	uint32_t arm = PARAM[0];
	uint32_t lba = PARAM[1];
	uint32_t four_bit = PARAM[3];
	uint32_t command_arg;
	uint32_t response;
	int rc;

	for (uint32_t i = 0; i < RESULT_WORDS; i++)
		OUT[i] = 0;
	OUT[0] = RESULT_MAGIC;
	OUT[1] = 1u;
	OUT[2] = IRQ_EXPERIMENT;
	OUT[4] = 48u;
	OUT[5] = lba;
	OUT[6] = ocr;
	OUT[7] = rca;
	OUT[8] = SYSCTRL_INT_STATUS;
	OUT[9] = MCI_CLOCK;
	OUT[46] = MAIN_IRQ_MCI;
	OUT[47] = MAIN_IRQ_L2;

	if (prepared_magic != RESULT_MAGIC ||
	    prepared_experiment != PREPARED_EXPERIMENT ||
	    prepared_status != 0u || prepared_lba != lba ||
	    arm != READ_LBA_MAGIC || !lba || !rca || four_bit != 1u ||
	    MCI_CLOCK != 0x00190001u) {
		fail(1u, 1u);
		return;
	}

	command_arg = (ocr & (1u << 30)) ? lba : lba * 512u;
	rc = send_command_irq(13u, rca, MCI_CPSM_RESPONSE, 10u);
	response = OUT[12];
	if (rc != SEND_OK || (response & R1_STATUS_MASK) ||
	    !(response & R1_READY_FOR_DATA) ||
	    R1_CURRENT_STATE(response) != R1_STATE_TRAN) {
		fail(2u, (uint32_t)rc + 1u);
		return;
	}

	rc = send_command_irq(23u, TRANSFER_BLOCKS, MCI_CPSM_RESPONSE, 14u);
	if (rc != SEND_OK || (OUT[16] & R1_STATUS_MASK)) {
		fail(3u, (uint32_t)rc + 1u);
		return;
	}

	l2_configure();
	if (!l2_start_dma((uint32_t)(uintptr_t)DMA_BUFFER)) {
		fail(4u, 1u);
		return;
	}
	MCI_DMACTRL = MCI_DMA_BUFEN | MCI_DMA_SIZE(SECTOR_WORDS);
	MCI_DATATIMER = 0xffffffffu;
	MCI_DATALENGTH = TRANSFER_BYTES;
	MCI_DATACTRL = MCI_DPSM_BLOCKSIZE(512u) |
		MCI_DPSM_BUSMODE(1u) | MCI_DPSM_DIRECTION | MCI_DPSM_ENABLE;
	rc = send_command_irq(18u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA, 17u);
	if (rc != SEND_OK || (OUT[19] & R1_STATUS_MASK)) {
		fail(5u, (uint32_t)rc + 1u);
		return;
	}

	MCI_MASK = MCI_DATA_EVENTS;
	for (uint32_t chunk = 0; chunk < DMA_CHUNKS; chunk++) {
		if (!wait_l2_irq(chunk)) {
			fail(6u, chunk + 1u);
			return;
		}
		if (chunk + 1u < DMA_CHUNKS &&
		    !l2_start_dma((uint32_t)(uintptr_t)DMA_BUFFER +
				 chunk * DMA_CHUNK_BYTES + DMA_CHUNK_BYTES)) {
			fail(7u, chunk + 1u);
			return;
		}
	}
	OUT[26] = DMA_CHUNKS;
	if (!wait_data_irq()) {
		fail(8u, 1u);
		return;
	}
	if (!compare_first_sector()) {
		fail(9u, 1u);
		return;
	}

	OUT[37] = L2_CONF1;
	OUT[38] = L2_ASSIGN1;
	OUT[39] = L2_DMA_COUNT;
	OUT[43] = MCI_CLOCK;
	OUT[44] = (L2_STATUS1 >> (L2_BUFFER_ID * 4u)) & 0xfu;
	OUT[45] = L2_DMAREQ;
	stop_data();
	rc = send_command_irq(13u, rca, MCI_CPSM_RESPONSE, 40u);
	response = OUT[42];
	if (rc != SEND_OK || (response & R1_STATUS_MASK) ||
	    !(response & R1_READY_FOR_DATA) ||
	    R1_CURRENT_STATE(response) != R1_STATE_TRAN) {
		fail(10u, (uint32_t)rc + 1u);
		return;
	}
	OUT[33] = SYSCTRL_INT_STATUS;
}
