#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define SYSCTRL(off) REG32(0x08000000u + (off))
#define MCI(off) REG32(0x20020000u + (off))
#define L2(off) REG32(0x2002c000u + (off))
#define OUT ((volatile uint32_t *)(uintptr_t)0x48001100u)

#define SYSCTRL_INT_STATUS SYSCTRL(0xcc)

#define MCI_STATUS MCI(0x34)
#define MCI_MASK MCI(0x38)

#define L2_DMAREQ L2(0x80)
#define L2_BUFINTEN L2(0x9c)

#define MCI_RESPCRCFAIL (1u << 0)
#define MCI_DATACRCFAIL (1u << 1)
#define MCI_RESPTIMEOUT (1u << 2)
#define MCI_DATATIMEOUT (1u << 3)
#define MCI_RESPEND (1u << 4)
#define MCI_CMDSENT (1u << 5)
#define MCI_DATAEND (1u << 6)
#define MCI_STARTBITERR (1u << 8)

#define MCI_COMMAND_EVENTS (MCI_RESPCRCFAIL | MCI_RESPTIMEOUT | \
			    MCI_RESPEND | MCI_CMDSENT)
#define MCI_DATA_EVENTS (MCI_DATACRCFAIL | MCI_DATATIMEOUT | \
			 MCI_DATAEND | MCI_STARTBITERR)

#define L2_BUFFER_ID 2u
#define L2_BUFFER_REQUEST (1u << (24u + L2_BUFFER_ID))
#define L2_BUFFER_INTEN (1u << (9u + L2_BUFFER_ID))

#define MAIN_IRQ_L2 (1u << 10)
#define MAIN_IRQ_MCI (1u << 22)

#define RESULT_MAGIC 0x53445052u
#define PREPARED_EXPERIMENT 51u
#define WINDOW_EXPERIMENT 56u
#define RESULT_WORDS 32u
#define WAIT_LIMIT 8000000u

static int wait_source(uint32_t source, int asserted, uint32_t *pending)
{
	for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
		uint32_t value = SYSCTRL_INT_STATUS;

		if (!!(value & source) == !!asserted) {
			*pending = value;
			return 1;
		}
	}
	*pending = SYSCTRL_INT_STATUS;
	return 0;
}

static uint32_t sample_source(uint32_t source)
{
	uint32_t pending = 0;

	for (uint32_t i = 0; i < 1024u; i++) {
		pending = SYSCTRL_INT_STATUS;
		if (pending & source)
			break;
	}
	return pending;
}

static int observe_mci_window(uint32_t mask, uint32_t base)
{
	uint32_t pending;

	MCI_MASK = 0;
	if (!wait_source(MAIN_IRQ_MCI, 0, &pending))
		return 0;
	OUT[base] = MCI_STATUS;
	OUT[base + 1u] = pending;
	MCI_MASK = mask;
	pending = sample_source(MAIN_IRQ_MCI);
	OUT[base + 2u] = pending;
	OUT[base + 3u] = MCI_STATUS;
	MCI_MASK = 0;
	if (!wait_source(MAIN_IRQ_MCI, 0, &pending))
		return 0;
	OUT[base + 4u] = pending;
	return 1;
}

static int observe_l2_window(uint32_t base)
{
	uint32_t pending;

	L2_BUFINTEN &= ~L2_BUFFER_INTEN;
	if (!wait_source(MAIN_IRQ_L2, 0, &pending))
		return 0;
	OUT[base] = L2_DMAREQ;
	OUT[base + 1u] = pending;
	if (OUT[base] & L2_BUFFER_REQUEST)
		return 0;
	L2_BUFINTEN |= L2_BUFFER_INTEN;
	if (!wait_source(MAIN_IRQ_L2, 1, &pending))
		return 0;
	OUT[base + 2u] = pending;
	OUT[base + 3u] = L2_DMAREQ;
	L2_BUFINTEN &= ~L2_BUFFER_INTEN;
	if (!wait_source(MAIN_IRQ_L2, 0, &pending))
		return 0;
	OUT[base + 4u] = pending;
	return 1;
}

void stub_main(void)
{
	uint32_t prepared_magic = OUT[0];
	uint32_t prepared_experiment = OUT[2];
	uint32_t prepared_status = OUT[3];

	for (uint32_t i = 0; i < RESULT_WORDS; i++)
		OUT[i] = 0;
	OUT[0] = RESULT_MAGIC;
	OUT[1] = 1u;
	OUT[2] = WINDOW_EXPERIMENT;
	OUT[4] = 20u;

	if (prepared_magic != RESULT_MAGIC ||
	    prepared_experiment != PREPARED_EXPERIMENT ||
	    prepared_status != 0u) {
		OUT[3] = 0x80000101u;
		return;
	}

	OUT[5] = SYSCTRL_INT_STATUS;
	OUT[6] = MCI_STATUS;
	OUT[7] = L2_DMAREQ;
	OUT[8] = MCI_COMMAND_EVENTS;
	OUT[9] = MCI_DATA_EVENTS;
	OUT[10] = L2_BUFFER_INTEN;

	if (!observe_mci_window(MCI_COMMAND_EVENTS, 11u)) {
		OUT[3] = 0x80000201u;
		return;
	}
	if (!observe_mci_window(MCI_DATA_EVENTS, 16u)) {
		OUT[3] = 0x80000301u;
		return;
	}
	if (!observe_l2_window(21u)) {
		OUT[3] = 0x80000401u;
		return;
	}
}
