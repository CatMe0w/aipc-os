/*
 * Compute where execution goes after the instruction at g_regs.pc.
 * ARM state only. Thumb is reported as undecidable.
 */

#include "rsp.h"

#define CPSR_T  (1u << 5)

/* r15 reads as pc+8. sp and lr live outside the array. */
static uint32_t reg(uint32_t n)
{
    if (n == 15)
        return g_regs.pc + 8;
    if (n == 14)
        return g_regs.lr;
    if (n == 13)
        return g_regs.sp;
    return g_regs.r[n];
}

static uint32_t cond_true(uint32_t cond, uint32_t cpsr)
{
    uint32_t n = (cpsr >> 31) & 1u;
    uint32_t z = (cpsr >> 30) & 1u;
    uint32_t c = (cpsr >> 29) & 1u;
    uint32_t v = (cpsr >> 28) & 1u;

    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return c;
    case 0x3: return !c;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return c && !z;
    case 0x9: return !c || z;
    case 0xA: return n == v;
    case 0xB: return n != v;
    case 0xC: return !z && n == v;
    case 0xD: return z || n != v;
    default:  return 1;
    }
}

/* Shift amount zero: LSR/ASR -> 32, ROR -> RRX */
static uint32_t shifted(uint32_t rm, uint32_t type, uint32_t amount,
                        uint32_t carry)
{
    switch (type) {
    case 0:
        return amount >= 32 ? 0 : rm << amount;
    case 1:
        if (!amount || amount >= 32)
            return 0;
        return rm >> amount;
    case 2:
        if (!amount || amount >= 32)
            return (rm & 0x80000000u) ? 0xFFFFFFFFu : 0;
        return (uint32_t)((int32_t)rm >> amount);
    default:
        if (!amount)
            return (carry << 31) | (rm >> 1);       /* RRX */
        amount &= 31;
        if (!amount)
            return rm;
        return (rm >> amount) | (rm << (32 - amount));
    }
}

static uint32_t operand2(uint32_t insn, uint32_t cpsr)
{
    uint32_t carry = (cpsr >> 29) & 1u;

    if (insn & (1u << 25)) {                        /* rotated immediate */
        uint32_t imm = insn & 0xFFu;
        uint32_t rot = ((insn >> 8) & 0xFu) * 2;

        return rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    }

    {
        uint32_t rm = reg(insn & 0xFu);
        uint32_t type = (insn >> 5) & 3u;
        uint32_t amount;

        if (insn & (1u << 4))
            amount = reg((insn >> 8) & 0xFu) & 0xFFu;
        else
            amount = (insn >> 7) & 0x1Fu;

        return shifted(rm, type, amount, carry);
    }
}

static uint32_t alu(uint32_t op, uint32_t a, uint32_t b, uint32_t carry)
{
    switch (op) {
    case 0x0: return a & b;         /* AND */
    case 0x1: return a ^ b;         /* EOR */
    case 0x2: return a - b;         /* SUB */
    case 0x3: return b - a;         /* RSB */
    case 0x4: return a + b;         /* ADD */
    case 0x5: return a + b + carry; /* ADC */
    case 0x6: return a - b - !carry;/* SBC */
    case 0x7: return b - a - !carry;/* RSC */
    case 0xC: return a | b;         /* ORR */
    case 0xD: return b;             /* MOV */
    case 0xE: return a & ~b;        /* BIC */
    default:  return ~b;            /* MVN */
    }
}

static uint32_t load32(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static uint32_t popcount(uint32_t v)
{
    uint32_t n = 0;

    while (v) {
        v &= v - 1;
        n++;
    }
    return n;
}

uint32_t arm_next_pc(uint32_t *next)
{
    uint32_t pc = g_regs.pc;
    uint32_t cpsr = g_regs.cpsr;
    uint32_t insn;
    uint32_t cond;

    if (cpsr & CPSR_T)
        return 0;

    insn = load32(pc);
    cond = insn >> 28;

    /* 0xF: unconditional space. Only BLX(imm) branches, and it enters Thumb. */
    if (cond == 0xF)
        return 0;

    if (!cond_true(cond, cpsr)) {
        *next = pc + 4;
        return 1;
    }

    /* B and BL */
    if ((insn & 0x0E000000u) == 0x0A000000u) {
        int32_t off = (int32_t)(insn << 8) >> 6;     /* sign extend, times 4 */

        *next = pc + 8 + (uint32_t)off;
        return 1;
    }

    /* BX and BLX register */
    if ((insn & 0x0FFFFFD0u) == 0x012FFF10u) {
        uint32_t target = reg(insn & 0xFu);

        if (target & 1u)
            return 0;                               /* into Thumb */
        *next = target & ~3u;
        return 1;
    }

    /* Load with pc as the destination */
    if ((insn & 0x0C100000u) == 0x04100000u &&
        ((insn >> 12) & 0xFu) == 15u) {
        uint32_t base = reg((insn >> 16) & 0xFu);
        uint32_t off;
        uint32_t addr;

        if (insn & (1u << 25)) {
            uint32_t rm = reg(insn & 0xFu);

            off = shifted(rm, (insn >> 5) & 3u, (insn >> 7) & 0x1Fu,
                          (cpsr >> 29) & 1u);
        } else {
            off = insn & 0xFFFu;
        }

        addr = (insn & (1u << 24))                  /* pre indexed */
             ? ((insn & (1u << 23)) ? base + off : base - off)
             : base;
        *next = load32(addr) & ~3u;
        return 1;
    }

    /* Block load with pc in the list */
    if ((insn & 0x0E100000u) == 0x08100000u && (insn & (1u << 15))) {
        uint32_t base = reg((insn >> 16) & 0xFu);
        uint32_t count = popcount(insn & 0xFFFFu);
        uint32_t up = insn & (1u << 23);
        uint32_t pre = insn & (1u << 24);
        uint32_t addr;

        /* pc is the highest numbered register, always at the top */
        if (up)
            addr = base + (count - 1) * 4 + (pre ? 4 : 0);
        else
            addr = base - (pre ? 4 : 0);
        *next = load32(addr) & ~3u;
        return 1;
    }

    /* Data processing writing pc, excluding the compares that write nothing */
    if ((insn & 0x0C000000u) == 0 && ((insn >> 12) & 0xFu) == 15u) {
        uint32_t op = (insn >> 21) & 0xFu;

        if (op >= 0x8 && op <= 0xB) {               /* TST TEQ CMP CMN */
            *next = pc + 4;
            return 1;
        }
        *next = alu(op, reg((insn >> 16) & 0xFu), operand2(insn, cpsr),
                    (cpsr >> 29) & 1u) & ~3u;
        return 1;
    }

    *next = pc + 4;
    return 1;
}
