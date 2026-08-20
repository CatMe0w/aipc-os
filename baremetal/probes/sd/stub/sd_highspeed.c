#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define MCI(off) REG32(0x20020000u + (off))
#define OUT ((volatile uint32_t *)(uintptr_t)0x48001100u)
#define PARAM ((volatile uint32_t *)(uintptr_t)0x48001380u)
#define ORIGINAL ((volatile uint32_t *)(uintptr_t)0x30010000u)

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
#define MCI_FIFO MCI(0x40)

#define MCI_CPSM_ENABLE (1u << 0)
#define MCI_CPSM_CMD(cmd) (((cmd) & 0x3fu) << 1)
#define MCI_CPSM_RESPONSE (1u << 7)
#define MCI_CPSM_WITHDATA (1u << 11)

#define MCI_DPSM_ENABLE (1u << 0)
#define MCI_DPSM_DIRECTION (1u << 1)
#define MCI_DPSM_BUSMODE(x) (((x) & 0x3u) << 3)
#define MCI_DPSM_BLOCKSIZE(size) (((size) & 0xfffu) << 16)

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
#ifndef HIGH_SPEED_EXPERIMENT
#define HIGH_SPEED_EXPERIMENT 41u
#endif

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

#define R1_STATUS_MASK 0xfff9a000u
#define R1_READY_FOR_DATA (1u << 8)
#define R1_CURRENT_STATE(value) (((value) >> 9) & 0xfu)
#define R1_STATE_TRAN 4u

#define RESULT_MAGIC 0x53445052u
#define READ_LBA_MAGIC 0x52454144u
#define RESULT_WORDS 160u
#define COMMAND_WAIT_LIMIT 500000u
#define DATA_WAIT_LIMIT 2000000u
#define READY_ATTEMPTS 2000u
#define SWITCH_WORDS 16u
#define SECTOR_WORDS 128u
#define CHECK_STATUS_BASE 32u
#define SET_STATUS_BASE 48u

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
	uint32_t events = MCI_RESPCRCFAIL | MCI_RESPTIMEOUT | MCI_RESPEND;

	if (MCI_COMMAND & MCI_CPSM_ENABLE) {
		MCI_COMMAND = 0;
		delay(16u);
	}
	MCI_ARGUMENT = arg;
	MCI_COMMAND = MCI_CPSM_ENABLE | MCI_CPSM_CMD(cmd) | flags;
	uint32_t status = wait_status(events);

	*final_status = status;
	*response = MCI_RESPONSE0;
	if (!(status & events))
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

static int wait_card_ready(uint32_t rca, uint32_t *final_status,
			   uint32_t *ready_response)
{
	uint32_t status = 0;
	uint32_t response = 0;

	for (uint32_t attempt = 0; attempt < READY_ATTEMPTS; attempt++) {
		int rc = send_command(13u, rca, MCI_CPSM_RESPONSE,
				      &status, &response);

		if (rc != SEND_OK || (response & R1_STATUS_MASK))
			return 0;
		if ((response & R1_READY_FOR_DATA) &&
		    R1_CURRENT_STATE(response) == R1_STATE_TRAN) {
			*final_status = status;
			*ready_response = response;
			return 1;
		}
		delay(40000u);
	}

	return 0;
}

static int set_four_bit_bus(uint32_t rca)
{
	uint32_t status;
	uint32_t response;
	int rc = send_command(55u, rca, MCI_CPSM_RESPONSE,
			      &status, &response);

	OUT[8] = status;
	OUT[9] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK) ||
	    !(response & (1u << 5)))
		return 0;
	rc = send_command(6u, 2u, MCI_CPSM_RESPONSE, &status, &response);
	OUT[10] = status;
	OUT[11] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK))
		return 0;

	MCI_CLOCK = MCI_CLOCK_20_67_MHZ;
	delay(40000u);
	OUT[24] = MCI_CLOCK;
	return 1;
}

static int read_data_words(uint32_t words, volatile uint32_t *destination,
			   uint32_t *observed, uint32_t *words_read)
{
	uint32_t status = MCI_STATUS;

	*observed = status;
	*words_read = 0;
	for (uint32_t word = 0; word < words; word++) {
		int ready = 0;

		for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
			status = MCI_STATUS;
			*observed |= status;
			if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
				      MCI_STARTBIT_ERR))
				return 0;
			if ((status & (MCI_FIFOFULL | MCI_RXACTIVE)) ==
			    (MCI_FIFOFULL | MCI_RXACTIVE)) {
				ready = 1;
				break;
			}
			if (status & MCI_DATAEND)
				break;
		}
		if (!ready)
			return 0;
		destination[word] = MCI_FIFO;
		(*words_read)++;
	}

	for (uint32_t poll = 0;
	     !(*observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		status = MCI_STATUS;
		*observed |= status;
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR))
			return 0;
	}

	return (*observed & (MCI_DATAEND | MCI_DATABLOCKEND)) ==
		(MCI_DATAEND | MCI_DATABLOCKEND) && MCI_DATACNT == 0u;
}

static uint32_t status_byte(volatile uint32_t *status, uint32_t index)
{
	return (status[index / 4u] >> ((index % 4u) * 8u)) & 0xffu;
}

static int read_switch_status(uint32_t arg, volatile uint32_t *destination,
			      uint32_t record_base)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed = 0;
	uint32_t words_read = 0;

	stop_data();
	MCI_DATATIMER = 0xffffffffu;
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

	int data_ok = read_data_words(SWITCH_WORDS, destination,
				      &observed, &words_read);
	OUT[record_base + 2u] = observed;
	OUT[record_base + 3u] = MCI_DATACNT;
	OUT[record_base + 4u] = words_read;
	stop_data();
	return data_ok;
}

static int compare_sector_at_high_speed(uint32_t command_arg)
{
	uint32_t command_status;
	uint32_t response;
	uint32_t observed = 0;
	uint32_t words_read = 0;
	uint32_t mismatch = 0xffffffffu;
	uint32_t actual = 0;
	uint32_t expected = 0;

	stop_data();
	MCI_DATATIMER = 0xffffffffu;
	MCI_DATALENGTH = 512u;
	MCI_DATACTRL = MCI_DPSM_BLOCKSIZE(512u) | MCI_DPSM_BUSMODE(1u) |
		MCI_DPSM_DIRECTION | MCI_DPSM_ENABLE;
	int rc = send_command(17u, command_arg,
			      MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
			      &command_status, &response);
	OUT[28] = command_status;
	OUT[29] = response;
	if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
		stop_data();
		return 0;
	}

	uint32_t previous = command_status;
	observed = command_status;
	for (uint32_t word = 0; word < SECTOR_WORDS; word++) {
		int ready = 0;
		uint32_t status = previous;

		for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
			status = MCI_STATUS;
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
		uint32_t value = MCI_FIFO;

		if (mismatch == 0xffffffffu && value != ORIGINAL[word]) {
			mismatch = word;
			actual = value;
			expected = ORIGINAL[word];
		}
		words_read++;
		previous = status;
	}

	for (uint32_t poll = 0;
	     !(observed & MCI_DATAEND) && poll < DATA_WAIT_LIMIT; poll++) {
		uint32_t status = MCI_STATUS;

		observed |= status;
		if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR))
			break;
	}
	OUT[30] = observed;
	OUT[31] = MCI_DATACNT;
	OUT[64] = words_read;
	OUT[65] = mismatch;
	OUT[66] = actual;
	OUT[67] = expected;
	OUT[68] = MCI_STATUS;
	OUT[69] = MCI_DATACTRL;
	int ok = words_read == SECTOR_WORDS && mismatch == 0xffffffffu &&
		(observed & (MCI_DATAEND | MCI_DATABLOCKEND)) ==
		(MCI_DATAEND | MCI_DATABLOCKEND) && MCI_DATACNT == 0u &&
		!(observed & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
			      MCI_STARTBIT_ERR));
	stop_data();
	return ok;
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
	uint32_t four_bit = PARAM[3];

	for (uint32_t word = 0; word < SECTOR_WORDS; word++)
		ORIGINAL[word] = OUT[32u + word];
	for (uint32_t i = 0; i < RESULT_WORDS; i++)
		OUT[i] = 0;
	OUT[0] = RESULT_MAGIC;
	OUT[1] = 1u;
	OUT[2] = HIGH_SPEED_EXPERIMENT;
	OUT[4] = 64u;
	OUT[5] = lba;
	OUT[6] = ocr;
	OUT[7] = rca;
	OUT[65] = 0xffffffffu;

	if (prepared_magic != RESULT_MAGIC || prepared_experiment != 4u ||
	    prepared_status != 0u || arm != READ_LBA_MAGIC || lba == 0u ||
	    prepared_lba != lba || !rca || four_bit != 1u) {
		fail(1u, 1u);
		return;
	}

	uint32_t command_arg;
	if (ocr & (1u << 30)) {
		command_arg = lba;
	} else {
		if (lba > 0x007fffffu) {
			fail(1u, 2u);
			return;
		}
		command_arg = lba * 512u;
	}

	uint32_t status;
	uint32_t response;
	if (!wait_card_ready(rca, &status, &response)) {
		fail(2u, 1u);
		return;
	}
	OUT[26] = status;
	OUT[27] = response;
	if (!set_four_bit_bus(rca)) {
		fail(3u, 1u);
		return;
	}

	if (!read_switch_status(0x00fffff0u, &OUT[CHECK_STATUS_BASE], 12u)) {
		fail(4u, 1u);
		return;
	}
	OUT[17] = status_byte(&OUT[CHECK_STATUS_BASE], 13u);
	if (!(OUT[17] & 0x02u)) {
		fail(5u, 1u);
		return;
	}

	if (!read_switch_status(0x80fffff1u, &OUT[SET_STATUS_BASE], 18u)) {
		fail(6u, 1u);
		return;
	}
	OUT[23] = status_byte(&OUT[SET_STATUS_BASE], 16u);
	if ((OUT[23] & 0x0fu) != 1u) {
		fail(7u, 1u);
		return;
	}

	MCI_CLOCK = MCI_CLOCK_HIGH_SPEED;
	delay(40000u);
	OUT[25] = MCI_CLOCK;
	if (!wait_card_ready(rca, &status, &response)) {
		fail(8u, 1u);
		return;
	}
	OUT[26] = status;
	OUT[27] = response;
	if (!compare_sector_at_high_speed(command_arg)) {
		fail(9u, 1u);
		return;
	}
}
