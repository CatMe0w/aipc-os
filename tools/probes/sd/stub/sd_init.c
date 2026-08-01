#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define SYSCTRL(off) REG32(0x08000000u + (off))
#define MCI(off) REG32(0x20020000u + (off))
#define OUT ((volatile uint32_t *)(uintptr_t)0x48001100u)

#define MCI_CLOCK MCI(0x04)
#define MCI_ARGUMENT MCI(0x08)
#define MCI_COMMAND MCI(0x0C)
#define MCI_RESPONSE0 MCI(0x14)
#define MCI_RESPONSE1 MCI(0x18)
#define MCI_RESPONSE2 MCI(0x1C)
#define MCI_RESPONSE3 MCI(0x20)
#define MCI_DATATIMER MCI(0x24)
#define MCI_DATALENGTH MCI(0x28)
#define MCI_DATACTRL MCI(0x2C)
#define MCI_STATUS MCI(0x34)
#define MCI_MASK MCI(0x38)
#define MCI_DMACTRL MCI(0x3C)

#define MCI_CLK_ENABLE (1u << 16)
#define MCI_FAIL_TRIGGER (1u << 19)
#define MCI_ENABLE (1u << 20)

#define MCI_CPSM_ENABLE (1u << 0)
#define MCI_CPSM_CMD(cmd) (((cmd) & 0x3Fu) << 1)
#define MCI_CPSM_RESPONSE (1u << 7)
#define MCI_CPSM_LONGRSP (1u << 8)

#define MCI_RESPCRCFAIL (1u << 0)
#define MCI_RESPTIMEOUT (1u << 2)
#define MCI_RESPEND (1u << 4)
#define MCI_CMDSENT (1u << 5)

#define TRACE_BASE 8u
#define TRACE_WORDS 9u
#define TRACE_COUNT 16u
#define RESULT_WORDS (TRACE_BASE + TRACE_WORDS * TRACE_COUNT)
#define WAIT_LIMIT 500000u

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

static volatile uint32_t *trace_slot(uint32_t slot)
{
    return &OUT[TRACE_BASE + slot * TRACE_WORDS];
}

static uint32_t wait_status(uint32_t mask, uint32_t *polls)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        uint32_t status = MCI_STATUS;
        if (status & mask) {
            *polls = i + 1u;
            return status;
        }
    }

    *polls = WAIT_LIMIT;
    return MCI_STATUS;
}

static int send_command(uint32_t slot, uint32_t cmd, uint32_t arg,
                        uint32_t flags, int crc_required)
{
    volatile uint32_t *trace = trace_slot(slot);
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

    trace[0] = cmd;
    trace[1] = arg;
    trace[2] = command;
    trace[3] = status;
    trace[4] = polls;
    trace[5] = MCI_RESPONSE0;
    trace[6] = MCI_RESPONSE1;
    trace[7] = MCI_RESPONSE2;
    trace[8] = MCI_RESPONSE3;

    if (!(status & event_mask))
        return SEND_SOFTWARE_TIMEOUT;
    if (status & MCI_RESPTIMEOUT)
        return SEND_RESPONSE_TIMEOUT;
    if ((flags & MCI_CPSM_RESPONSE) && crc_required &&
        (status & MCI_RESPCRCFAIL))
        return SEND_RESPONSE_CRC;
    if (flags & MCI_CPSM_RESPONSE) {
        /* A CRC failure completes an R3 response because R3 has no CRC field. */
        if (!(status & (MCI_RESPEND | MCI_RESPCRCFAIL)))
            return SEND_WRONG_EVENT;
    } else if (!(status & MCI_CMDSENT)) {
        return SEND_WRONG_EVENT;
    }

    return SEND_OK;
}

static uint32_t response0(uint32_t slot)
{
    return trace_slot(slot)[5];
}

static void fail(uint32_t slot, uint32_t reason)
{
    OUT[3] = 0x80000000u | (slot << 8) | reason;
}

void stub_main(void)
{
    for (uint32_t i = 0; i < RESULT_WORDS; i++)
        OUT[i] = 0;

    OUT[0] = 0x53445052u;
    OUT[1] = 1u;
    OUT[2] = 2u;
    OUT[4] = TRACE_WORDS * TRACE_COUNT;

    SYSCTRL(0x0C) &= ~((1u << 2) | (1u << 18));
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

    int rc = send_command(0u, 0u, 0u, 0u, 0);
    if (rc != SEND_OK) {
        fail(0u, (uint32_t)rc);
        return;
    }
    delay(40000u);

    rc = send_command(1u, 8u, 0x000001AAu, MCI_CPSM_RESPONSE, 1);
    if (rc != SEND_OK || response0(1u) != 0x000001AAu) {
        fail(1u, rc != SEND_OK ? (uint32_t)rc : 5u);
        return;
    }

    rc = send_command(2u, 5u, 0u, MCI_CPSM_RESPONSE, 0);
    if (rc != SEND_RESPONSE_TIMEOUT) {
        fail(2u, rc == SEND_OK ? 6u : (uint32_t)rc);
        return;
    }

    rc = send_command(3u, 55u, 0u, MCI_CPSM_RESPONSE, 1);
    if (rc != SEND_OK || !(response0(3u) & (1u << 5))) {
        fail(3u, rc != SEND_OK ? (uint32_t)rc : 7u);
        return;
    }
    rc = send_command(4u, 41u, 0u, MCI_CPSM_RESPONSE, 0);
    if (rc != SEND_OK) {
        fail(4u, (uint32_t)rc);
        return;
    }

    uint32_t probe_ocr = response0(4u);
    OUT[6] = probe_ocr;
    uint32_t selected_ocr = probe_ocr & 0x00300000u;
    if (!selected_ocr) {
        fail(4u, 8u);
        return;
    }

    delay(40000u);
    rc = send_command(5u, 0u, 0u, 0u, 0);
    if (rc != SEND_OK) {
        fail(5u, (uint32_t)rc);
        return;
    }
    delay(40000u);

    rc = send_command(6u, 8u, 0x000001AAu, MCI_CPSM_RESPONSE, 1);
    if (rc != SEND_OK || response0(6u) != 0x000001AAu) {
        fail(6u, rc != SEND_OK ? (uint32_t)rc : 5u);
        return;
    }

    uint32_t acmd41_arg = selected_ocr | (1u << 30);
    uint32_t final_ocr = 0;
    for (uint32_t attempt = 1u; attempt <= 100u; attempt++) {
        uint32_t app_slot = attempt == 1u ? 7u : 9u;
        uint32_t op_slot = attempt == 1u ? 8u : 10u;

        rc = send_command(app_slot, 55u, 0u, MCI_CPSM_RESPONSE, 1);
        if (rc != SEND_OK || !(response0(app_slot) & (1u << 5))) {
            fail(app_slot, rc != SEND_OK ? (uint32_t)rc : 7u);
            return;
        }
        rc = send_command(op_slot, 41u, acmd41_arg, MCI_CPSM_RESPONSE, 0);
        if (rc != SEND_OK) {
            fail(op_slot, (uint32_t)rc);
            return;
        }

        final_ocr = response0(op_slot);
        OUT[5] = attempt;
        OUT[7] = final_ocr;
        if (final_ocr & 0x80000000u)
            break;
        delay(400000u);
    }
    if (!(final_ocr & 0x80000000u)) {
        fail(10u, 9u);
        return;
    }

    rc = send_command(11u, 2u, 0u,
                      MCI_CPSM_RESPONSE | MCI_CPSM_LONGRSP, 1);
    if (rc != SEND_OK) {
        fail(11u, (uint32_t)rc);
        return;
    }

    rc = send_command(12u, 3u, 0u, MCI_CPSM_RESPONSE, 1);
    if (rc != SEND_OK || !(response0(12u) & 0xFFFF0000u)) {
        fail(12u, rc != SEND_OK ? (uint32_t)rc : 10u);
        return;
    }
    uint32_t rca = response0(12u) & 0xFFFF0000u;

    rc = send_command(13u, 9u, rca,
                      MCI_CPSM_RESPONSE | MCI_CPSM_LONGRSP, 1);
    if (rc != SEND_OK) {
        fail(13u, (uint32_t)rc);
        return;
    }

    rc = send_command(14u, 7u, rca, MCI_CPSM_RESPONSE, 1);
    if (rc != SEND_OK) {
        fail(14u, (uint32_t)rc);
        return;
    }
    delay(40000u);

    rc = send_command(15u, 13u, rca, MCI_CPSM_RESPONSE, 1);
    if (rc != SEND_OK || ((response0(15u) >> 9) & 0xFu) != 4u) {
        fail(15u, rc != SEND_OK ? (uint32_t)rc : 11u);
        return;
    }
}
