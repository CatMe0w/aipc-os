#include <stdint.h>

#ifndef PIO_WITHDATA
#error PIO_WITHDATA must select the command-register variant
#endif

#ifndef PIO_L2
#define PIO_L2 0
#endif

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define SYSCTRL(off) REG32(0x08000000u + (off))
#define MCI(off) REG32(0x20020000u + (off))
#define L2(off) REG32(0x2002C000u + (off))
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

#define L2_DMAREQ L2(0x80)
#define L2_CONF1 L2(0x88)
#define L2_ASSIGN1 L2(0x90)
#define L2_STATUS1 L2(0xA0)

#define MCI_CLK_ENABLE (1u << 16)
#define MCI_FAIL_TRIGGER (1u << 19)
#define MCI_ENABLE (1u << 20)

#define MCI_CPSM_ENABLE (1u << 0)
#define MCI_CPSM_CMD(cmd) (((cmd) & 0x3Fu) << 1)
#define MCI_CPSM_RESPONSE (1u << 7)
#define MCI_CPSM_LONGRSP (1u << 8)
#define MCI_CPSM_WITHDATA (1u << 11)

#define MCI_DPSM_ENABLE (1u << 0)
#define MCI_DPSM_DIRECTION (1u << 1)
#define MCI_DPSM_BLOCKSIZE(size) (((size) & 0xFFFu) << 16)

#define MCI_DMA_BUFEN (1u << 0)
#define MCI_DMA_SIZE(words) (((words) & 0x7FFFu) << 17)

#define MCI_RESPCRCFAIL (1u << 0)
#define MCI_DATACRCFAIL (1u << 1)
#define MCI_RESPTIMEOUT (1u << 2)
#define MCI_DATATIMEOUT (1u << 3)
#define MCI_RESPEND (1u << 4)
#define MCI_CMDSENT (1u << 5)
#define MCI_DATAEND (1u << 6)
#define MCI_DATABLOCKEND (1u << 7)
#define MCI_STARTBIT_ERR (1u << 8)
#define MCI_RXACTIVE (1u << 11)
#define MCI_FIFOFULL (1u << 12)

#define COMMAND_WAIT_LIMIT 500000u
#define DATA_WAIT_LIMIT 2000000u
#define RESULT_WORDS 160u
#define DATA_BASE 32u
#define SECTOR_WORDS 128u
#define L2_BUFFER_ID 6u
#define L2_BUFFER ((volatile uint32_t *)(uintptr_t)0x48000C00u)
#define READ_LBA_MAGIC 0x52454144u

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

static uint32_t wait_status(uint32_t mask, uint32_t *polls)
{
    for (uint32_t i = 0; i < COMMAND_WAIT_LIMIT; i++) {
        uint32_t status = MCI_STATUS;
        if (status & mask) {
            *polls = i + 1u;
            return status;
        }
    }

    *polls = COMMAND_WAIT_LIMIT;
    return MCI_STATUS;
}

static int send_command(uint32_t cmd, uint32_t arg, uint32_t flags,
                        int crc_required, uint32_t *final_status,
                        uint32_t *response)
{
    uint32_t command = MCI_CPSM_ENABLE | MCI_CPSM_CMD(cmd) | flags;
    uint32_t event_mask = flags & MCI_CPSM_RESPONSE
        ? MCI_RESPCRCFAIL | MCI_RESPTIMEOUT | MCI_RESPEND
        : MCI_RESPTIMEOUT | MCI_CMDSENT;
    uint32_t polls;

    if (MCI_COMMAND & MCI_CPSM_ENABLE) {
        MCI_COMMAND = 0;
        delay(16u);
    }

    MCI_ARGUMENT = arg;
    MCI_COMMAND = command;
    uint32_t status = wait_status(event_mask, &polls);
    uint32_t resp = MCI_RESPONSE0;

    *final_status = status;
    *response = resp;

    if (!(status & event_mask))
        return SEND_SOFTWARE_TIMEOUT;
    if (status & MCI_RESPTIMEOUT)
        return SEND_RESPONSE_TIMEOUT;
    if ((flags & MCI_CPSM_RESPONSE) && crc_required &&
        (status & MCI_RESPCRCFAIL))
        return SEND_RESPONSE_CRC;
    if (flags & MCI_CPSM_RESPONSE) {
        /* R3 has no CRC field, so RESPCRCFAIL is its terminal event. */
        if (!(status & (MCI_RESPEND | MCI_RESPCRCFAIL)))
            return SEND_WRONG_EVENT;
    } else if (!(status & MCI_CMDSENT)) {
        return SEND_WRONG_EVENT;
    }

    return SEND_OK;
}

static void stop_transfer(void)
{
    MCI_COMMAND = 0;
    MCI_DATACTRL = 0;
    MCI_DATALENGTH = 0;
    MCI_DMACTRL = 0;
}

static void fail(uint32_t phase, uint32_t reason)
{
    OUT[3] = 0x80000000u | (phase << 8) | reason;
    stop_transfer();
}

static int require_command(uint32_t phase, uint32_t cmd, uint32_t arg,
                           uint32_t flags, int crc_required,
                           uint32_t *status, uint32_t *response)
{
    int rc = send_command(cmd, arg, flags, crc_required, status, response);
    if (rc != SEND_OK) {
        fail(phase, (uint32_t)rc);
        return 0;
    }
    return 1;
}

static int initialize_card(uint32_t *ocr, uint32_t *rca,
                           uint32_t *attempts)
{
    uint32_t status;
    uint32_t response;

    if (!require_command(1u, 0u, 0u, 0u, 0, &status, &response))
        return 0;
    delay(40000u);

    if (!require_command(2u, 8u, 0x000001AAu, MCI_CPSM_RESPONSE, 1,
                         &status, &response))
        return 0;
    if (response != 0x000001AAu) {
        fail(2u, 5u);
        return 0;
    }

    int rc = send_command(5u, 0u, MCI_CPSM_RESPONSE, 0,
                          &status, &response);
    if (rc != SEND_RESPONSE_TIMEOUT) {
        fail(3u, rc == SEND_OK ? 6u : (uint32_t)rc);
        return 0;
    }

    if (!require_command(4u, 55u, 0u, MCI_CPSM_RESPONSE, 1,
                         &status, &response))
        return 0;
    if (!(response & (1u << 5))) {
        fail(4u, 7u);
        return 0;
    }
    if (!require_command(5u, 41u, 0u, MCI_CPSM_RESPONSE, 0,
                         &status, &response))
        return 0;

    uint32_t selected_ocr = response & 0x00300000u;
    if (!selected_ocr) {
        fail(5u, 8u);
        return 0;
    }

    delay(40000u);
    if (!require_command(6u, 0u, 0u, 0u, 0, &status, &response))
        return 0;
    delay(40000u);

    if (!require_command(7u, 8u, 0x000001AAu, MCI_CPSM_RESPONSE, 1,
                         &status, &response))
        return 0;
    if (response != 0x000001AAu) {
        fail(7u, 5u);
        return 0;
    }

    uint32_t final_ocr = 0;
    uint32_t attempt;
    for (attempt = 1u; attempt <= 100u; attempt++) {
        if (!require_command(8u, 55u, 0u, MCI_CPSM_RESPONSE, 1,
                             &status, &response))
            return 0;
        if (!(response & (1u << 5))) {
            fail(8u, 7u);
            return 0;
        }
        if (!require_command(9u, 41u, selected_ocr | (1u << 30),
                             MCI_CPSM_RESPONSE, 0, &status, &response))
            return 0;

        final_ocr = response;
        if (final_ocr & 0x80000000u)
            break;
        delay(400000u);
    }
    if (!(final_ocr & 0x80000000u)) {
        fail(9u, 9u);
        return 0;
    }

    if (!require_command(10u, 2u, 0u,
                         MCI_CPSM_RESPONSE | MCI_CPSM_LONGRSP, 1,
                         &status, &response))
        return 0;
    if (!require_command(11u, 3u, 0u, MCI_CPSM_RESPONSE, 1,
                         &status, &response))
        return 0;
    uint32_t card_rca = response & 0xFFFF0000u;
    if (!card_rca) {
        fail(11u, 10u);
        return 0;
    }

    if (!require_command(12u, 9u, card_rca,
                         MCI_CPSM_RESPONSE | MCI_CPSM_LONGRSP, 1,
                         &status, &response))
        return 0;
    if (!require_command(13u, 7u, card_rca, MCI_CPSM_RESPONSE, 1,
                         &status, &response))
        return 0;
    delay(40000u);
    if (!require_command(14u, 13u, card_rca, MCI_CPSM_RESPONSE, 1,
                         &status, &response))
        return 0;
    if (((response >> 9) & 0xFu) != 4u) {
        fail(14u, 11u);
        return 0;
    }

    *ocr = final_ocr;
    *rca = card_rca;
    *attempts = attempt;
    return 1;
}

#if !PIO_L2
static void record_data_state(uint32_t words_read, uint32_t status,
                              uint32_t observed, uint32_t total_polls,
                              uint32_t max_polls)
{
    OUT[15] = status;
    OUT[16] = observed;
    OUT[17] = total_polls;
    OUT[18] = max_polls;
    OUT[19] = words_read;
    OUT[20] = MCI_DATACNT;
    OUT[21] = MCI_DATALENGTH;
    OUT[22] = MCI_DATACTRL;
    OUT[23] = MCI_DMACTRL;
    OUT[24] = MCI_COMMAND;
    OUT[25] = MCI_STATUS;
}
#endif

#if PIO_L2
static uint32_t l2_status(void)
{
    return (L2_STATUS1 >> (L2_BUFFER_ID * 4u)) & 0xFu;
}

static void record_l2_state(uint32_t status, uint32_t observed,
                            uint32_t polls)
{
    OUT[20] = status;
    OUT[21] = observed;
    OUT[22] = polls;
    OUT[23] = l2_status();
    OUT[25] = MCI_DATACNT;
    OUT[26] = MCI_DATALENGTH;
    OUT[27] = MCI_DATACTRL;
    OUT[28] = MCI_DMACTRL;
    OUT[29] = MCI_COMMAND;
    OUT[30] = MCI_STATUS;
}
#endif

void stub_main(void)
{
    uint32_t sector = PARAM[0] == READ_LBA_MAGIC ? PARAM[1] : 0u;

    for (uint32_t i = 0; i < RESULT_WORDS; i++)
        OUT[i] = 0;

    OUT[0] = 0x53445052u;
    OUT[1] = 1u;
    OUT[2] = PIO_L2 ? 5u : (PIO_WITHDATA ? 4u : 3u);
    OUT[4] = SECTOR_WORDS;
    OUT[5] = PIO_L2 ? L2_BUFFER_ID : PIO_WITHDATA;

    SYSCTRL(0x0C) &= ~((1u << 2) | (PIO_L2 ? 1u << 3 : 0u) |
                       (1u << 18));
    SYSCTRL(0x78) |= 1u << 29;
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    SYSCTRL(0x9C) &= ~0xC0000000u;
    SYSCTRL(0xA0) &= ~0x000001C0u;
    SYSCTRL(0xD4) |= 1u;

    MCI_CLOCK = 0;
    delay(40000u);
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL_TRIGGER;
    MCI_MASK = 0;
    MCI_COMMAND = 0;
    MCI_DATACTRL = 0;
    MCI_DATALENGTH = 0;
    MCI_DMACTRL = 0;
    MCI_DATATIMER = 0xFFFFFFFFu;
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL_TRIGGER | MCI_CLK_ENABLE | 0xFFFFu;
    delay(400000u);

    uint32_t ocr;
    uint32_t rca;
    uint32_t attempts;
    if (!initialize_card(&ocr, &rca, &attempts))
        return;

    OUT[6] = ocr;
    OUT[7] = rca;
    OUT[8] = attempts;

    if (!(ocr & (1u << 30))) {
        uint32_t status;
        uint32_t response;
        if (!require_command(15u, 16u, 512u, MCI_CPSM_RESPONSE, 1,
                             &status, &response))
            return;
    }

    uint32_t datactrl = MCI_DPSM_BLOCKSIZE(512u) |
        MCI_DPSM_DIRECTION | MCI_DPSM_ENABLE;
    uint32_t command = MCI_CPSM_ENABLE | MCI_CPSM_CMD(17u) |
        MCI_CPSM_RESPONSE;
    if (PIO_WITHDATA)
        command |= MCI_CPSM_WITHDATA;

    OUT[9] = sector;
    OUT[10] = command;
    OUT[13] = datactrl;

#if PIO_L2
    OUT[14] = L2_CONF1;
    OUT[15] = L2_ASSIGN1;
    OUT[16] = l2_status();

    L2_DMAREQ |= 1u;
    L2_ASSIGN1 = (L2_ASSIGN1 & ~(7u << 12)) | (L2_BUFFER_ID << 12);
    L2_CONF1 = (L2_CONF1 & ~(1u << (8u + L2_BUFFER_ID))) |
        (1u << L2_BUFFER_ID) | (1u << (16u + L2_BUFFER_ID)) |
        (1u << (24u + L2_BUFFER_ID));

    OUT[17] = L2_CONF1;
    OUT[18] = L2_ASSIGN1;
    OUT[19] = L2_DMAREQ;
    MCI_DMACTRL = MCI_DMA_BUFEN | MCI_DMA_SIZE(SECTOR_WORDS);
#else
    MCI_DMACTRL = 0;
#endif
    MCI_DATATIMER = 0xFFFFFFFFu;
    MCI_DATALENGTH = 512u;
    MCI_DATACTRL = datactrl;

    uint32_t command_status;
    uint32_t command_response;
    if (!(ocr & (1u << 30)) && sector > 0x007FFFFFu) {
        fail(15u, 12u);
        return;
    }
    uint32_t command_arg = ocr & (1u << 30) ? sector : sector * 512u;
    int rc = send_command(17u, command_arg,
                          MCI_CPSM_RESPONSE |
                          (PIO_WITHDATA ? MCI_CPSM_WITHDATA : 0u),
                          1, &command_status, &command_response);
    OUT[11] = command_status;
    OUT[12] = command_response;
    if (rc != SEND_OK) {
        fail(16u, (uint32_t)rc);
        return;
    }

    uint32_t observed = command_status;
    const uint32_t data_errors = MCI_DATACRCFAIL | MCI_DATATIMEOUT |
        MCI_STARTBIT_ERR;

#if PIO_L2
    uint32_t last_status = command_status;
    uint32_t data_polls = 0;
    int data_done = 0;

    while (data_polls < DATA_WAIT_LIMIT) {
        data_polls++;
        last_status = MCI_STATUS;
        observed |= last_status;
        if (last_status & data_errors) {
            record_l2_state(last_status, observed, data_polls);
            fail(17u, 1u);
            return;
        }
        if (last_status & MCI_DATAEND) {
            data_done = 1;
            break;
        }
    }

    record_l2_state(last_status, observed, data_polls);
    if (!data_done) {
        fail(17u, 3u);
        return;
    }
    if (!(observed & MCI_DATABLOCKEND)) {
        fail(17u, 4u);
        return;
    }
    if (l2_status() != 8u) {
        fail(17u, 5u);
        return;
    }

    /* Buffer 6 must not overlap this binary or the bootrom return stack. */
    for (uint32_t word = 0; word < SECTOR_WORDS; word++)
        OUT[DATA_BASE + word] = L2_BUFFER[word];

    OUT[24] = l2_status();
    if (OUT[24] != 0u) {
        fail(18u, 5u);
        return;
    }

    stop_transfer();
    return;
#else
    uint32_t total_polls = 0;
    uint32_t max_polls = 0;
    uint32_t last_status = command_status;
    uint32_t words_read = 0;

    for (uint32_t word = 0; word < SECTOR_WORDS; word++) {
        uint32_t polls = 0;
        int ready = 0;

        while (polls < DATA_WAIT_LIMIT) {
            polls++;
            last_status = MCI_STATUS;
            observed |= last_status;
            if (last_status & data_errors) {
                record_data_state(words_read, last_status, observed,
                                  total_polls + polls, max_polls);
                fail(17u, 1u);
                return;
            }
            if ((last_status & (MCI_FIFOFULL | MCI_RXACTIVE)) ==
                (MCI_FIFOFULL | MCI_RXACTIVE)) {
                ready = 1;
                break;
            }
            if (last_status & MCI_DATAEND)
                break;
        }

        total_polls += polls;
        if (polls > max_polls)
            max_polls = polls;
        if (!ready) {
            record_data_state(words_read, last_status, observed,
                              total_polls, max_polls);
            fail(17u, last_status & MCI_DATAEND ? 2u : 3u);
            return;
        }

        if (word == 0u)
            OUT[14] = last_status;
        OUT[DATA_BASE + word] = MCI_FIFO;
        words_read++;
    }

    int data_done = 0;
    uint32_t done_polls = 0;
    while (done_polls < DATA_WAIT_LIMIT) {
        done_polls++;
        last_status = MCI_STATUS;
        observed |= last_status;
        if (last_status & data_errors) {
            record_data_state(words_read, last_status, observed,
                              total_polls + done_polls, max_polls);
            fail(18u, 1u);
            return;
        }
        if (last_status & MCI_DATAEND) {
            total_polls += done_polls;
            if (done_polls > max_polls)
                max_polls = done_polls;
            data_done = 1;
            break;
        }
    }
    if (!data_done)
        total_polls += done_polls;

    record_data_state(words_read, last_status, observed,
                      total_polls, max_polls);
    if (!data_done) {
        fail(18u, 3u);
        return;
    }
    if (!(observed & MCI_DATABLOCKEND)) {
        fail(18u, 4u);
        return;
    }

    stop_transfer();
#endif
}
