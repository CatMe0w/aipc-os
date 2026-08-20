#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define MCI(off) REG32(0x20020000u + (off))
#define OUT ((volatile uint32_t *)(uintptr_t)0x48001100u)
#define PARAM ((volatile uint32_t *)(uintptr_t)0x48001380u)

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
#define MCI_DPSM_BLOCKSIZE(size) (((size) & 0xFFFu) << 16)

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
#define MULTI_WORDS (2u * SECTOR_WORDS)

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

static void setup_read(uint32_t bytes)
{
	MCI_DMACTRL = 0;
	MCI_DATATIMER = 0xFFFFFFFFu;
	MCI_DATALENGTH = bytes;
	MCI_DATACTRL = MCI_DPSM_BLOCKSIZE(512u) |
		MCI_DPSM_DIRECTION | MCI_DPSM_ENABLE;
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
			OUT[29] = response;
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

	OUT[29] = response;
	fail(phase, 36u);
	return 0;
}

static void observe_block_end(uint32_t status, uint32_t words,
			      uint32_t *previous, uint32_t *edges)
{
	if ((status & MCI_DATABLOCKEND) &&
	    !(*previous & MCI_DATABLOCKEND)) {
		if (*edges < 2u)
			OUT[14u + *edges] = words;
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
	OUT[19] = status;
	OUT[20] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		fail(phase, rc != SEND_OK ? (uint32_t)rc : 40u);
		return 0;
	}
	return 1;
}

static int read_two_blocks(uint32_t command_arg)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed = 0;
	uint32_t block_observed[2] = { 0, 0 };
	uint32_t previous = 0;
	uint32_t edges = 0;
	uint32_t status = 0;
	uint32_t mismatch_phase = 0;

	setup_read(1024u);
	int rc = send_command(18u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	OUT[9] = command_status;
	OUT[10] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		fail(2u, rc != SEND_OK ? (uint32_t)rc : 40u);
		return 0;
	}

	observed = command_status;
	previous = command_status;
	for (uint32_t word = 0; word < MULTI_WORDS; word++) {
		int ready = 0;

		for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
			status = MCI_STATUS;
			observed |= status;
			block_observed[word < SECTOR_WORDS ? 0u : 1u] |= status;
			observe_block_end(status, word, &previous, &edges);
			if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
				      MCI_STARTBIT_ERR)) {
				OUT[18] = status;
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
			OUT[18] = status;
			stop_multiblock(3u);
			fail(3u, status & MCI_DATAEND ? 42u : 43u);
			return 0;
		}

		uint32_t value = MCI_FIFO;
		if (word < SECTOR_WORDS) {
			uint32_t expected = OUT[DATA_BASE + word];

			if (!mismatch_phase && value != expected) {
				mismatch_phase = 1u;
				OUT[27] = mismatch_phase;
				OUT[28] = word;
				OUT[29] = value;
				OUT[30] = expected;
			}
		} else {
			OUT[DATA_BASE + word - SECTOR_WORDS] = value;
		}
		OUT[25] = word + 1u;
	}

	for (uint32_t poll = 0;
	     !(observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		status = MCI_STATUS;
		observed |= status;
		block_observed[1] |= status;
		observe_block_end(status, MULTI_WORDS, &previous, &edges);
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR)) {
			OUT[18] = status;
			stop_multiblock(3u);
			fail(3u, 44u);
			return 0;
		}
	}

	OUT[11] = block_observed[0];
	OUT[12] = block_observed[1];
	OUT[13] = edges;
	OUT[16] = observed;
	OUT[17] = MCI_DATACNT;
	OUT[18] = status;
	if (!(observed & MCI_DATAEND)) {
		stop_multiblock(3u);
		fail(3u, 45u);
		return 0;
	}
	if (!(observed & MCI_DATABLOCKEND)) {
		stop_multiblock(3u);
		fail(3u, 46u);
		return 0;
	}
	if (!stop_multiblock(4u))
		return 0;
	if (mismatch_phase) {
		fail(3u, 47u);
		return 0;
	}
	return 1;
}

static int verify_second_block(uint32_t command_arg)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed;
	uint32_t status = 0;
	uint32_t mismatch = 0;

	setup_read(512u);
	int rc = send_command(17u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	OUT[22] = command_status;
	OUT[23] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		fail(6u, rc != SEND_OK ? (uint32_t)rc : 40u);
		return 0;
	}

	observed = command_status;
	for (uint32_t word = 0; word < SECTOR_WORDS; word++) {
		int ready = 0;

		for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
			status = MCI_STATUS;
			observed |= status;
			if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
				      MCI_STARTBIT_ERR)) {
				fail(7u, 50u);
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
			fail(7u, status & MCI_DATAEND ? 51u : 52u);
			return 0;
		}

		uint32_t value = MCI_FIFO;
		uint32_t expected = OUT[DATA_BASE + word];
		if (!mismatch && value != expected) {
			mismatch = 1u;
			OUT[27] = 2u;
			OUT[28] = word;
			OUT[29] = value;
			OUT[30] = expected;
		}
		OUT[26] = word + 1u;
	}

	for (uint32_t poll = 0;
	     !(observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		status = MCI_STATUS;
		observed |= status;
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR)) {
			fail(7u, 53u);
			return 0;
		}
	}
	OUT[24] = observed;
	stop_data();
	if (!(observed & MCI_DATAEND) ||
	    !(observed & MCI_DATABLOCKEND)) {
		fail(7u, 54u);
		return 0;
	}
	if (mismatch) {
		fail(7u, 55u);
		return 0;
	}
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
	uint32_t lba = PARAM[1];

	for (uint32_t i = 0; i < DATA_BASE; i++)
		OUT[i] = 0;
	OUT[0] = RESULT_MAGIC;
	OUT[1] = 1u;
	OUT[2] = 7u;
	OUT[4] = SECTOR_WORDS;
	OUT[5] = lba;
	OUT[6] = ocr;
	OUT[7] = rca;
	OUT[14] = 0xFFFFFFFFu;
	OUT[15] = 0xFFFFFFFFu;

	if (prepared_magic != RESULT_MAGIC || prepared_experiment != 4u ||
	    prepared_status != 0u || arm != READ_LBA_MAGIC ||
	    prepared_lba != lba || !rca) {
		fail(0u, 1u);
		return;
	}

	uint32_t command_arg;
	if (ocr & (1u << 30)) {
		command_arg = lba;
	} else {
		if (lba > 0x007FFFFEu) {
			fail(0u, 2u);
			return;
		}
		command_arg = lba * 512u;
	}

	uint32_t ready_response;
	if (!wait_card_ready(1u, rca, &ready_response))
		return;
	OUT[8] = ready_response;

	if (!read_two_blocks(command_arg))
		return;
	if (!wait_card_ready(5u, rca, &ready_response))
		return;
	OUT[21] = ready_response;

	uint32_t next_arg = command_arg +
		((ocr & (1u << 30)) ? 1u : 512u);
	if (!verify_second_block(next_arg))
		return;
	if (!wait_card_ready(8u, rca, &ready_response))
		return;
	OUT[31] = ready_response;
}
