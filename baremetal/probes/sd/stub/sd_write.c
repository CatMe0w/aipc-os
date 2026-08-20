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
#define DATA_BASE 32u
#define SECTOR_WORDS 128u
#define RESULT_MAGIC 0x53445052u
#define WRITE_LBA_MAGIC 0x57524954u

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

static void stop_transfer(void)
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
    stop_transfer();
}

static uint32_t pattern_word(uint32_t lba, uint32_t index)
{
    return 0xA15C0000u ^ lba ^ index * 0x9E3779B9u;
}

static void setup_data(int read)
{
    MCI_DMACTRL = 0;
    MCI_DATATIMER = 0xFFFFFFFFu;
    MCI_DATALENGTH = 512u;
    MCI_DATACTRL = MCI_DPSM_BLOCKSIZE(512u) | MCI_DPSM_ENABLE |
        (read ? MCI_DPSM_DIRECTION : 0u);
}

static void record_transfer(uint32_t base, uint32_t command_status,
                            uint32_t response, uint32_t observed,
                            uint32_t final_value)
{
    OUT[base] = command_status;
    OUT[base + 1u] = response;
    OUT[base + 2u] = observed;
    OUT[base + 3u] = final_value;
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
            OUT[10] = response;
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

    OUT[10] = response;
    fail(phase, 36u);
    return 0;
}

static int write_sector(uint32_t phase, uint32_t record_base,
                        uint32_t command_arg, uint32_t lba,
                        uint32_t rca, int restore)
{
    uint32_t command_status;
    uint32_t response;
    uint32_t observed;
    uint32_t status = 0;

    int rc = send_command(24u, command_arg,
                          MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
                          &command_status, &response);
    if (rc != SEND_OK || (response & R1_STATUS_MASK)) {
        record_transfer(record_base, command_status, response,
                        command_status, status);
        fail(phase, rc != SEND_OK ? (uint32_t)rc : 40u);
        return 0;
    }

    setup_data(0);
    observed = command_status;
    for (uint32_t i = 0; i < SECTOR_WORDS; i++) {
        int ready = 0;
        for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
            status = MCI_STATUS;
            observed |= status;
            if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
                          MCI_STARTBIT_ERR)) {
                record_transfer(record_base, command_status, response,
                                observed, status);
                fail(phase, 41u);
                return 0;
            }
            if ((status & (MCI_FIFOEMPTY | MCI_TXACTIVE)) ==
                (MCI_FIFOEMPTY | MCI_TXACTIVE)) {
                ready = 1;
                break;
            }
            if (status & MCI_DATAEND)
                break;
        }
        if (!ready) {
            record_transfer(record_base, command_status, response,
                            observed, status);
            fail(phase, 42u);
            return 0;
        }
        MCI_FIFO = restore ? OUT[DATA_BASE + i] : pattern_word(lba, i);
    }

    int done = 0;
    for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
        status = MCI_STATUS;
        observed |= status;
        if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
                      MCI_STARTBIT_ERR)) {
            record_transfer(record_base, command_status, response,
                            observed, status);
            fail(phase, 43u);
            return 0;
        }
        if ((observed & (MCI_DATAEND | MCI_DATABLOCKEND)) ==
            (MCI_DATAEND | MCI_DATABLOCKEND)) {
            done = 1;
            break;
        }
    }
    if (!done) {
        record_transfer(record_base, command_status, response,
                        observed, status);
        fail(phase, observed & MCI_DATAEND ? 45u : 44u);
        return 0;
    }

    stop_transfer();
    uint32_t ready_response;
    if (!wait_card_ready(phase, rca, &ready_response))
        return 0;
    record_transfer(record_base, command_status, response, observed,
                    ready_response);
    return 1;
}

static int verify_sector(uint32_t phase, uint32_t record_base,
                         uint32_t command_arg, uint32_t lba,
                         int original)
{
    uint32_t command_status;
    uint32_t response;
    uint32_t observed;
    uint32_t status = 0;

    setup_data(1);
    int rc = send_command(17u, command_arg,
                          MCI_CPSM_RESPONSE | MCI_CPSM_WITHDATA,
                          &command_status, &response);
    if (rc != SEND_OK) {
        record_transfer(record_base, command_status, response,
                        command_status, status);
        fail(phase, (uint32_t)rc);
        return 0;
    }

    observed = command_status;
    for (uint32_t i = 0; i < SECTOR_WORDS; i++) {
        int ready = 0;
        for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
            status = MCI_STATUS;
            observed |= status;
            if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
                          MCI_STARTBIT_ERR)) {
                record_transfer(record_base, command_status, response,
                                observed, status);
                fail(phase, 50u);
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
            record_transfer(record_base, command_status, response,
                            observed, status);
            fail(phase, 51u);
            return 0;
        }

        uint32_t value = MCI_FIFO;
        uint32_t expected = original ? OUT[DATA_BASE + i] :
            pattern_word(lba, i);
        if (value != expected) {
            OUT[8] = i;
            OUT[9] = value;
            OUT[10] = expected;
            record_transfer(record_base, command_status, response,
                            observed, status);
            fail(phase, 52u);
            return 0;
        }
    }

    int done = 0;
    for (uint32_t poll = 0; poll < DATA_WAIT_LIMIT; poll++) {
        status = MCI_STATUS;
        observed |= status;
        if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT |
                      MCI_STARTBIT_ERR)) {
            record_transfer(record_base, command_status, response,
                            observed, status);
            fail(phase, 53u);
            return 0;
        }
        if ((observed & (MCI_DATAEND | MCI_DATABLOCKEND)) ==
            (MCI_DATAEND | MCI_DATABLOCKEND)) {
            done = 1;
            break;
        }
    }

    record_transfer(record_base, command_status, response, observed, status);
    if (!done) {
        fail(phase, observed & MCI_DATAEND ? 55u : 54u);
        return 0;
    }
    stop_transfer();
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

    OUT[1] = 2u;
    OUT[2] = 6u;
    OUT[3] = 0;
    OUT[4] = SECTOR_WORDS;
    OUT[5] = lba;
    OUT[6] = ocr;
    OUT[7] = rca;
    for (uint32_t i = 8u; i < DATA_BASE; i++)
        OUT[i] = 0;

    if (prepared_magic != RESULT_MAGIC || prepared_experiment != 4u ||
        prepared_status != 0u || arm != WRITE_LBA_MAGIC || lba == 0u ||
        prepared_lba != lba || !rca) {
        fail(0u, 1u);
        return;
    }

    uint32_t command_arg;
    if (ocr & (1u << 30)) {
        command_arg = lba;
    } else {
        if (lba > 0x007FFFFFu) {
            fail(0u, 2u);
            return;
        }
        command_arg = lba * 512u;
    }

    uint32_t ready_response;
    if (!wait_card_ready(1u, rca, &ready_response))
        return;
    OUT[8] = ready_response;

    uint32_t original_checksum = 0;
    uint32_t pattern_checksum = 0;
    for (uint32_t i = 0; i < SECTOR_WORDS; i++) {
        original_checksum ^= OUT[DATA_BASE + i];
        pattern_checksum ^= pattern_word(lba, i);
    }
    OUT[9] = original_checksum;
    OUT[10] = pattern_checksum;

    if (!write_sector(2u, 11u, command_arg, lba, rca, 0))
        return;
    if (!verify_sector(3u, 15u, command_arg, lba, 0))
        return;
    if (!write_sector(4u, 19u, command_arg, lba, rca, 1))
        return;
    if (!verify_sector(5u, 23u, command_arg, lba, 1))
        return;
}
