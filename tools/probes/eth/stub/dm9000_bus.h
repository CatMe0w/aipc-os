#ifndef DM9000_BUS_H
#define DM9000_BUS_H

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off) REG32(SYSCTRL_BASE + (off))

#define SHAREPIN1 0x78u
#define SHAREPIN1_CLEAR ((1u << 0) | (1u << 24))

#define G2_DIR 0x84u
#define G2_OUT 0x88u
#define G2_IN 0xC0u

#define G4_DIR 0x94u
#define G4_OUT 0x98u

#define CMD_BIT (1u << 15)
#define DATA_SHIFT 16
#define DATA_MASK (0xFFu << DATA_SHIFT)
#define INT_BIT (1u << 24)
#define IOR_BIT (1u << 25)
#define IOW_BIT (1u << 26)
#define CS_BIT (1u << 6)

#define DM9K_NCR 0x00u
#define DM9K_NSR 0x01u
#define DM9K_VIDL 0x28u
#define DM9K_VIDH 0x29u
#define DM9K_PIDL 0x2Au
#define DM9K_PIDH 0x2Bu
#define DM9K_CHIPR 0x2Cu

static inline void dm9k_settle(void)
{
    for (volatile uint32_t i = 0; i < 40u; i++)
        __asm__ volatile("" : : : "memory");
}

static inline void dm9k_delay(uint32_t units)
{
    for (volatile uint32_t i = 0; i < units * 20000u; i++)
        __asm__ volatile("" : : : "memory");
}

static inline void dm9k_cs(uint32_t level)
{
    if (level)
        SYSCTRL(G4_OUT) |= CS_BIT;
    else
        SYSCTRL(G4_OUT) &= ~CS_BIT;
    dm9k_settle();
}

static inline void dm9k_pin(uint32_t bit, uint32_t level)
{
    if (level)
        SYSCTRL(G2_OUT) |= bit;
    else
        SYSCTRL(G2_OUT) &= ~bit;
    dm9k_settle();
}

static inline void dm9k_data_out(void)
{
    SYSCTRL(G2_DIR) &= ~DATA_MASK;
    dm9k_settle();
}

static inline void dm9k_data_in(void)
{
    SYSCTRL(G2_DIR) |= DATA_MASK;
    dm9k_settle();
}

static inline void dm9k_data_drive(uint8_t v)
{
    uint32_t r = SYSCTRL(G2_OUT);

    r = (r & ~DATA_MASK) | ((uint32_t)v << DATA_SHIFT);
    SYSCTRL(G2_OUT) = r;
    dm9k_settle();
}

static inline uint8_t dm9k_data_sample(void)
{
    return (uint8_t)((SYSCTRL(G2_IN) & DATA_MASK) >> DATA_SHIFT);
}

/* IOW# latches on its rising edge, so driving data after it falls is fine. */
static inline void dm9k_index_write(uint8_t reg)
{
    dm9k_data_out();
    dm9k_cs(0);
    dm9k_pin(CMD_BIT, 0);
    dm9k_pin(IOW_BIT, 0);
    dm9k_data_drive(reg);
    dm9k_pin(IOW_BIT, 1);
    dm9k_cs(1);
}

static inline uint8_t dm9k_index_read(void)
{
    uint8_t v;

    dm9k_data_in();
    dm9k_cs(0);
    dm9k_pin(CMD_BIT, 0);
    dm9k_pin(IOR_BIT, 0);
    v = dm9k_data_sample();
    dm9k_pin(IOR_BIT, 1);
    dm9k_cs(1);

    return v;
}

static inline uint8_t dm9k_data_read(void)
{
    uint8_t v;

    dm9k_data_in();
    dm9k_cs(0);
    dm9k_pin(CMD_BIT, 1);
    dm9k_pin(IOR_BIT, 0);
    v = dm9k_data_sample();
    dm9k_pin(IOR_BIT, 1);
    dm9k_cs(1);

    return v;
}

static inline void dm9k_data_write(uint8_t val)
{
    dm9k_data_out();
    dm9k_cs(0);
    dm9k_pin(CMD_BIT, 1);
    dm9k_pin(IOW_BIT, 0);
    dm9k_data_drive(val);
    dm9k_pin(IOW_BIT, 1);
    dm9k_cs(1);
}

static inline uint8_t dm9k_read(uint8_t reg)
{
    dm9k_index_write(reg);
    return dm9k_data_read();
}

static inline void dm9k_write(uint8_t reg, uint8_t val)
{
    dm9k_index_write(reg);
    dm9k_data_write(val);
}

static inline void dm9k_bus_init(void)
{
    SYSCTRL(SHAREPIN1) &= ~SHAREPIN1_CLEAR;

        SYSCTRL(G2_DIR) &= ~(CMD_BIT | IOR_BIT | IOW_BIT | DATA_MASK);
    SYSCTRL(G2_DIR) |= INT_BIT;
    SYSCTRL(G4_DIR) &= ~CS_BIT;

    SYSCTRL(G2_OUT) |= CMD_BIT | IOR_BIT | IOW_BIT;
    SYSCTRL(G4_OUT) |= CS_BIT;
    dm9k_settle();
}

static inline void dm9k_snapshot(volatile uint32_t *p)
{
    p[0] = SYSCTRL(0x74);
    p[1] = SYSCTRL(0x78);
    p[2] = SYSCTRL(G2_DIR);
    p[3] = SYSCTRL(G2_OUT);
    p[4] = SYSCTRL(G2_IN);
    p[5] = SYSCTRL(G4_DIR);
    p[6] = SYSCTRL(G4_OUT);
}

#endif /* DM9000_BUS_H */
