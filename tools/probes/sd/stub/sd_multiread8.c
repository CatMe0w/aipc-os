#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define MCI(off) REG32(0x20020000u + (off))
#define OUT ((volatile uint32_t *)(uintptr_t)0x48001100u)
#define PARAM ((volatile uint32_t *)(uintptr_t)0x48001380u)

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

#define MCI_CPSM_ENABLE (1u << 0)
#define MCI_CPSM_CMD(cmd) (((cmd) & 0x3Fu) << 1)
#define MCI_CPSM_RESPONSE (1u << 7)
#define MCI_CPSM_WITHDATA (1u << 11)

#define MCI_DPSM_ENABLE (1u << 0)
#define MCI_DPSM_DIRECTION (1u << 1)
#define MCI_DPSM_BUSMODE(x) (((x) & 0x3u) << 3)
#define MCI_DPSM_BLOCKSIZE(size) (((size) & 0xFFFu) << 16)

#define MCI_CLK_ENABLE (1u << 16)
#define MCI_FAIL_TRIGGER (1u << 19)
#define MCI_ENABLE (1u << 20)
#define MCI_CLOCK_20_67_MHZ (MCI_ENABLE | MCI_FAIL_TRIGGER | \
			      MCI_CLK_ENABLE | 0x0202u)

#define MCI_RESPCRCFAIL (1u << 0)
#define MCI_DATACRCFAIL (1u << 1)
#define MCI_RESPTIMEOUT (1u << 2)
#define MCI_DATATIMEOUT (1u << 3)
#define MCI_RESPEND (1u << 4)
#define MCI_DATAEND (1u << 6)
#define MCI_DATABLOCKEND (1u << 7)
#define MCI_STARTBIT_ERR (1u << 8)
#define MCI_RXACTIVE (1u << 11)
#define MCI_FIFOFULL (1u << 12)

#define R1_STATUS_MASK 0xFFF9A000u
#define R1_READY_FOR_DATA (1u << 8)
#define R1_CURRENT_STATE(value) (((value) >> 9) & 0xFu)
#define R1_STATE_TRAN 4u

#define COMMAND_WAIT_LIMIT 500000u
#define DATA_WAIT_LIMIT 8000000u
#define READY_ATTEMPTS 2000u
#define RESULT_MAGIC 0x53445052u
#define READ_LBA_MAGIC 0x52454144u
#define DATA_BASE 32u
#define SECTOR_WORDS 128u
#define BLOCKS 8u
#define TOTAL_WORDS (BLOCKS * SECTOR_WORDS)

enum send_result {
	SEND_OK = 0,
	SEND_SOFTWARE_TIMEOUT = 1,
	SEND_RESPONSE_TIMEOUT = 2,
	SEND_RESPONSE_CRC = 3,
	SEND_WRONG_EVENT = 4,
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

static int wait_card_ready(uint32_t phase, uint32_t rca,
			   uint32_t *ready_response)
{
	uint32_t status;
	uint32_t response = 0;

	for (uint32_t attempt = 0; attempt < READY_ATTEMPTS; attempt++) {
		int rc = send_command(13u, rca, MCI_CPSM_RESPONSE,
				      &status, &response);

		if (rc != SEND_OK) {
			fail(phase, 30u + (uint32_t)rc);
			return 0;
		}
		if (response & R1_STATUS_MASK) {
			OUT[30] = response;
			fail(phase, 35u);
			return 0;
		}
		if ((response & R1_READY_FOR_DATA) &&
		    R1_CURRENT_STATE(response) == R1_STATE_TRAN) {
			*ready_response = response;
			return 1;
		}
		delay(40000u);
	}

	OUT[30] = response;
	fail(phase, 36u);
	return 0;
}

static void observe_block_end(uint32_t status, uint32_t words,
			      uint32_t *previous, uint32_t *edges)
{
	if ((status & MCI_DATABLOCKEND) &&
	    !(*previous & MCI_DATABLOCKEND)) {
		if (*edges < BLOCKS)
			OUT[15u + *edges] = words;
		(*edges)++;
	}
	*previous = status;
}

static int stop_multiblock(uint32_t phase)
{
	uint32_t status;
	uint32_t response;

	stop_data();
	int rc = send_command(12u, 0u, MCI_CPSM_RESPONSE,
			      &status, &response);
	OUT[25] = status;
	OUT[26] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		fail(phase, rc != SEND_OK ? (uint32_t)rc : 40u);
		return 0;
	}
	return 1;
}

static int read_blocks(uint32_t command_arg, uint32_t target, int four_bit)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed;
	uint32_t previous;
	uint32_t edges = 0;
	uint32_t status = 0;
	uint32_t mismatch = 0;

	MCI_DMACTRL = 0;
	MCI_DATATIMER = 0xFFFFFFFFu;
	MCI_DATALENGTH = BLOCKS * 512u;
	MCI_DATACTRL = MCI_DPSM_BLOCKSIZE(512u) |
		MCI_DPSM_DIRECTION | MCI_DPSM_ENABLE |
		(four_bit ? MCI_DPSM_BUSMODE(1u) : 0u);

	int rc = send_command(18u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	OUT[11] = command_status;
	OUT[12] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		fail(2u, rc != SEND_OK ? (uint32_t)rc : 40u);
		return 0;
	}

	observed = command_status;
	previous = command_status;
	for (uint32_t word = 0; word < TOTAL_WORDS; word++) {
		int ready = 0;

		for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
			status = MCI_STATUS;
			observed |= status;
			observe_block_end(status, word, &previous, &edges);
			if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
				      MCI_STARTBIT_ERR)) {
				OUT[24] = status;
				stop_multiblock(3u);
				fail(3u, 41u);
				return 0;
			}
			if ((status & (MCI_FIFOFULL | MCI_RXACTIVE)) ==
			    (MCI_FIFOFULL | MCI_RXACTIVE)) {
				ready = 1;
				break;
			}
			if (status & MCI_DATAEND)
				break;
		}
		if (!ready) {
			OUT[24] = status;
			stop_multiblock(3u);
			fail(3u, status & MCI_DATAEND ? 42u : 43u);
			return 0;
		}

		uint32_t value = MCI_FIFO;
		if (word / SECTOR_WORDS == target) {
			uint32_t index = word % SECTOR_WORDS;
			uint32_t expected = OUT[DATA_BASE + index];

			if (!mismatch && value != expected) {
				mismatch = 1u;
				OUT[29] = index;
				OUT[30] = value;
				OUT[31] = expected;
			}
		}
		OUT[28] = word + 1u;
	}

	for (uint32_t poll = 0;
	     !(observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		status = MCI_STATUS;
		observed |= status;
		observe_block_end(status, TOTAL_WORDS, &previous, &edges);
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR)) {
			OUT[24] = status;
			stop_multiblock(3u);
			fail(3u, 44u);
			return 0;
		}
	}

	OUT[13] = observed;
	OUT[14] = edges;
	OUT[23] = MCI_DATACNT;
	OUT[24] = status;
	if (!(observed & MCI_DATAEND)) {
		stop_multiblock(3u);
		fail(3u, 45u);
		return 0;
	}
	if (!stop_multiblock(4u))
		return 0;
	if (mismatch) {
		fail(3u, 46u);
		return 0;
	}
	if (edges != BLOCKS) {
		fail(3u, 47u);
		return 0;
	}
	for (uint32_t block = 0; block < BLOCKS; block++) {
		if (OUT[15u + block] != (block + 1u) * SECTOR_WORDS) {
			fail(3u, 48u);
			return 0;
		}
	}
	return 1;
}

static int set_four_bit_bus(uint32_t rca)
{
	uint32_t status;
	uint32_t response;

	int rc = send_command(55u, rca, MCI_CPSM_RESPONSE,
			      &status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK) ||
	    !(response & (1u << 5))) {
		fail(2u, rc != SEND_OK ? 50u + (uint32_t)rc : 55u);
		return 0;
	}
	rc = send_command(6u, 2u, MCI_CPSM_RESPONSE, &status, &response);
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		fail(2u, rc != SEND_OK ? 60u + (uint32_t)rc : 65u);
		return 0;
	}

	MCI_CLOCK = MCI_CLOCK_20_67_MHZ;
	delay(40000u);
	OUT[30] = response;
	OUT[31] = MCI_CLOCK;
	return 1;
}

void stub_main(void)
{
	uint32_t prepared_magic = OUT[0];
	uint32_t prepared_experiment = OUT[2];
	uint32_t prepared_status = OUT[3];
	uint32_t ocr = OUT[6];
	uint32_t rca = OUT[7];
	uint32_t prepared_lba = OUT[9];
	uint32_t arm = PARAM[0];
	uint32_t reference_lba = PARAM[1];
	uint32_t target = PARAM[2];
	uint32_t four_bit = PARAM[3];
	uint32_t start_lba = reference_lba - target;

	for (uint32_t i = 0; i < DATA_BASE; i++)
		OUT[i] = 0;
	OUT[0] = RESULT_MAGIC;
	OUT[1] = 1u;
	OUT[2] = four_bit ? 9u : 8u;
	OUT[4] = SECTOR_WORDS;
	OUT[5] = start_lba;
	OUT[6] = reference_lba;
	OUT[7] = target;
	OUT[8] = ocr;
	OUT[9] = rca;
	OUT[29] = 0xFFFFFFFFu;
	for (uint32_t i = 0; i < BLOCKS; i++)
		OUT[15u + i] = 0xFFFFFFFFu;

	if (prepared_magic != RESULT_MAGIC || prepared_experiment != 4u ||
	    prepared_status != 0u || arm != READ_LBA_MAGIC ||
	    prepared_lba != reference_lba || !rca || target >= BLOCKS ||
	    reference_lba < target || four_bit > 1u) {
		fail(0u, 1u);
		return;
	}

	uint32_t command_arg;
	if (ocr & (1u << 30)) {
		command_arg = start_lba;
	} else {
		if (start_lba > 0x007FFFF8u) {
			fail(0u, 2u);
			return;
		}
		command_arg = start_lba * 512u;
	}

	uint32_t ready_response;
	if (!wait_card_ready(1u, rca, &ready_response))
		return;
	OUT[10] = ready_response;
	if (four_bit) {
		if (!set_four_bit_bus(rca))
			return;
		if (!wait_card_ready(1u, rca, &ready_response))
			return;
		OUT[10] = ready_response;
	}
	if (!read_blocks(command_arg, target, four_bit))
		return;
	if (!wait_card_ready(5u, rca, &ready_response))
		return;
	OUT[27] = ready_response;
}
