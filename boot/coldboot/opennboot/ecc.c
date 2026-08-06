#include "ecc.h"

/* bit offset (MSB first) = 8*base + 2*pos + parity - 4 */
static const int seg_base[5] = { -112, 16, 144, 272, 400 };

int ecc_locate(uint32_t reg, uint32_t *byte_out, uint8_t *mask_out)
{
    uint32_t sel = (reg >> 9) & 0x3FFu;
    if (sel == 0u || (sel & (sel - 1u)) != 0u)
        return -1;

    int i = 0;
    while ((sel & (1u << i)) == 0u)
        i++;

    int base   = seg_base[i >> 1];
    int parity = (0x2AA >> i) & 1;      /* odd member of each pair adds one */

    int r = 2 * (int)(reg & 0x1FFu) + parity - 4;
    if (r < 0) {
        r += 8;
        base -= 1;
    }

    int idx = base + (r >> 3);
    if (idx < 0)
        return -1;

    *byte_out = (uint32_t)idx;
    *mask_out = (uint8_t)(1u << (~r & 7));
    return 0;
}

/* Stricter than the OEM loader: out-of-range positions and empty register
 * sets are failures here, not silent pass-throughs. */
int ecc_apply(const uint32_t *regs, uint32_t n,
              uint8_t *data, uint8_t *tag, uint32_t tag_len)
{
    uint32_t applied = 0;

    for (uint32_t k = 0; k < n; k++) {
        if (regs[k] == 0u)
            continue;

        uint32_t byte;
        uint8_t  mask;
        if (ecc_locate(regs[k], &byte, &mask) != 0)
            return -1;

        if (byte < 512u)
            data[byte] ^= mask;
        else if (byte - 512u < tag_len)
            tag[byte - 512u] ^= mask;
        else
            return -1;

        applied++;
    }

    return applied ? 0 : -1;
}
