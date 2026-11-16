#include "cfw.h"
#include "timer.h"

#define BRANCH  0xFF000000u  /* compares the condition and the opcode */
#define B       0xEA000000u
#define BL      0xEB000000u
#define BX_LR   0xE12FFF1Eu

/* In the eboot resident. */
extern uint8_t eboot_menu_read_byte[], eboot_menu_get_string[];
extern uint8_t eboot_menu_get_char[], eboot_menu_get_integer[];
extern uint8_t eboot_menu_ask_yes_no[], eboot_menu_secs[];
extern uint32_t eboot_menu_putc;

/* The Boot Menu reads BootDelay from the boot configuration. A delay of 0
 * skips the countdown, so the patch loads BOOT_DELAY. */
static const uint32_t delay_match[] = {
    0xE5973004u,  /* ldr  r3, [r7, #4]  */
    0xE59F0000u,  /* ldr  r0, =*        */
    0xE5D3500Eu,  /* ldrb r5, [r3, #14] */
};
static const uint32_t delay_mask[] = { EXACT, 0xFFFFF000u, EXACT };
#define DELAY_LOAD  2
#define BOOT_DELAY  15u
#define MOV_R5_IMM  0xE3A05000u

/* The stock console draws its last line partly below the screen. The top
 * margin and the first line must stay equal. */
static const uint32_t height_match[] = {
    0xE3A06E1Eu,  /* mov  r6, #480      */
    0xE1A031A3u,  /* lsr  r3, r3, #3    */
    0xE3A07E32u,  /* mov  r7, #800      */
    0xE58E6004u,  /* str  r6, [lr, #4]  */
};
static const uint32_t height_mask[] = { EXACT, EXACT, EXACT, EXACT };
#define TOP_MARGIN     13  /* mov lr, #5 */
#define FIRST_LINE     16  /* mov r6, #5 */
#define MOV_LR_5       0xE3A0E005u
#define MOV_R6_5       0xE3A06005u
#define MOV_LR_11      0xE3A0E00Bu
#define MOV_R6_11      0xE3A0600Bu
#define MOV_R6_468     0xE3A06F75u

static const uint32_t putc_match[] = {
    0xE92D4010u,  /* push {r4, lr} */
    0xE3500008u,  /* cmp  r0, #8   */
    0x0A000000u,  /* beq  *        */
    0xE3500009u,  /* cmp  r0, #9   */
    0x0A000000u,  /* beq  *        */
    0xE350000Au,  /* cmp  r0, #10  */
    0x0A000000u,  /* beq  *        */
    0xE350000Du,  /* cmp  r0, #13  */
    0x0A000000u,  /* beq  *        */
};
static const uint32_t putc_mask[] = {
    EXACT, EXACT, BRANCH, EXACT, BRANCH, EXACT, BRANCH, EXACT, BRANCH,
};

/* The stock EBOOT does not implement OEMReadDebugByte, which returns no
 * key, and OEMWriteDebugByte, which prints nothing. The first call in the
 * Boot Menu countdown goes to OEMReadDebugByte. */
static const uint32_t countdown_match[] = {
    0xEB000000u,  /* bl   OEMReadDebugByte */
    0xE20040FFu,  /* and  r4, r0, #255     */
    0xE3540020u,  /* cmp  r4, #32          */
    0x0A000000u,  /* beq  *                */
    0xE354000Du,  /* cmp  r4, #13          */
};
static const uint32_t countdown_mask[] = {
    BRANCH, EXACT, EXACT, BRANCH, EXACT,
};

/* The two calls to OEMEthGetSecs in the countdown */
static const uint32_t secs_start_match[] = {
    0xEB000000u,  /* bl   OEMEthGetSecs */
    0xE1A08000u,  /* mov  r8, r0        */
    0xE3A04000u,  /* mov  r4, #0        */
    0xE3550000u,  /* cmp  r5, #0        */
    0x0A000000u,  /* beq  *             */
};
static const uint32_t secs_start_mask[] = {
    BRANCH, EXACT, EXACT, EXACT, BRANCH,
};

static const uint32_t secs_loop_match[] = {
    0xEB000000u,  /* bl   OEMEthGetSecs */
    0xE1500008u,  /* cmp  r0, r8        */
    0x9A000000u,  /* bls  *             */
};
static const uint32_t secs_loop_mask[] = { BRANCH, EXACT, BRANCH };

/* The stock EBOOT does not implement the line, character and integer input
 * of the menu. The pattern is the first two functions and the start of the
 * third. */
static const uint32_t input_match[] = {
    0xE3500000u,  /* line: cmp r0, #0 */
    0xE3A03000u,
    0x03A00000u,
    0x012FFF1Eu,
    0xE2412001u,
    0xE3A01000u,
    0xE1530002u,
    0xE7C31000u,
    0xB2833001u,
    0xBAFFFFFBu,
    0xE2830001u,
    0xE12FFF1Eu,
    0xE3A00000u,  /* character: mov r0, #0 */
    0xE12FFF1Eu,
    0xE92D4010u,  /* integer: push {r4, lr} */
    0xE24DD010u,
    0xE1A04000u,
};
static const uint32_t input_mask[] = {
    EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT,
    EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT,
};
#define GET_CHAR     12
#define GET_INTEGER  14

/* The stock EBOOT does not implement the y/n prompt of the menu: it prints
 * the question, then loops forever. */
static const uint32_t yesno_match[] = {
    0xE92D4010u,  /* push  {r4, lr}    */
    0xE3510000u,  /* cmp   r1, #0      */
    0xE1A01000u,  /* mov   r1, r0      */
    0xE59F001Cu,  /* ldr   r0, =*      */
    0x13A04079u,  /* movne r4, #'y'    */
    0x03A0406Eu,  /* moveq r4, #'n'    */
    0xEB000000u,  /* bl    *           */
    0xE59F0008u,  /* ldr   r0, =*      */
    0xE20410FFu,  /* and   r1, r4, #255 */
    0xEB000000u,  /* bl    *           */
    0xEAFFFFFEu,  /* b     .           */
};
static const uint32_t yesno_mask[] = {
    EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, BRANCH, EXACT, EXACT, BRANCH,
    EXACT,
};

/* A matched instruction, and the VA at which it executes */
struct site {
    uint32_t *insn;
    uint32_t va;
};

static struct site console_putchar, read_byte_call, secs_start, secs_loop;
static struct site input_fns, yes_no;

static bool delay_callback(struct pf_patch *patch, uint32_t *stream)
{
    cfw_patch_once(patch);
    stream[DELAY_LOAD] = MOV_R5_IMM | BOOT_DELAY;
    return true;
}

static bool height_callback(struct pf_patch *patch, uint32_t *stream)
{
    if (stream[TOP_MARGIN] != MOV_LR_5 || stream[FIRST_LINE] != MOV_R6_5)
        return false;
    cfw_patch_once(patch);
    stream[0] = MOV_R6_468;
    stream[TOP_MARGIN] = MOV_LR_11;
    stream[FIRST_LINE] = MOV_R6_11;
    return true;
}

#define RECORD(name, site)                                              \
    static bool name(struct pf_patch *patch, uint32_t *stream)          \
    {                                                                   \
        cfw_patch_once(patch);                                          \
        site.insn = stream;                                             \
        site.va = pf_ptr_to_va(cfw_range, stream);                      \
        return true;                                                    \
    }

RECORD(putc_callback, console_putchar)
RECORD(countdown_callback, read_byte_call)
RECORD(secs_start_callback, secs_start)
RECORD(secs_loop_callback, secs_loop)
RECORD(input_callback, input_fns)
RECORD(yesno_callback, yes_no)

/* The callbacks only record the matches. eboot_menu_finish() writes the
 * branches, because some of them need the address of another match. */
static void eboot_menu_patch(pf_patchset_t *set)
{
    pf_maskmatch(set, "boot delay", delay_match, delay_mask,
                 ARRAY_SIZE(delay_match), true, delay_callback);
    pf_maskmatch(set, "console height", height_match, height_mask,
                 ARRAY_SIZE(height_match), false, height_callback);
    pf_maskmatch(set, "console putchar", putc_match, putc_mask,
                 ARRAY_SIZE(putc_match), true, putc_callback);
    pf_maskmatch(set, "boot countdown", countdown_match, countdown_mask,
                 ARRAY_SIZE(countdown_match), true, countdown_callback);
    pf_maskmatch(set, "countdown clock start", secs_start_match,
                 secs_start_mask, ARRAY_SIZE(secs_start_match), true,
                 secs_start_callback);
    pf_maskmatch(set, "countdown clock loop", secs_loop_match,
                 secs_loop_mask, ARRAY_SIZE(secs_loop_match), true,
                 secs_loop_callback);
    pf_maskmatch(set, "ipl input", input_match, input_mask,
                 ARRAY_SIZE(input_match), true, input_callback);
    pf_maskmatch(set, "ipl yes or no", yesno_match, yesno_mask,
                 ARRAY_SIZE(yesno_match), true, yesno_callback);
}

static void branch(uint32_t op, struct site at, int32_t word, uint32_t to)
{
    uint32_t from = at.va + (uint32_t)(word * 4);

    at.insn[word] = op | (((to - from - 8u) >> 2) & 0x00FFFFFFu);
}

static void eboot_menu_init(void)
{
    cfw_resident_install(CFW_EBOOT);
}

static void eboot_menu_finish(void)
{
    /* Words from the countdown call to OEMReadDebugByte */
    int32_t read_byte = ((int32_t)(*read_byte_call.insn << 8) >> 8) + 2;

    /* OEMWriteDebugByte is the one instruction before OEMReadDebugByte. */
    if (read_byte_call.insn[read_byte - 1] != BX_LR)
        panic("Missing patch: OEMWriteDebugByte");

    *(uint32_t *)cfw_resident_ptr(&eboot_menu_putc) = console_putchar.va;
    branch(B, read_byte_call, read_byte - 1, console_putchar.va);
    branch(B, read_byte_call, read_byte, (uint32_t)eboot_menu_read_byte);
    branch(BL, secs_start, 0, (uint32_t)eboot_menu_secs);
    branch(BL, secs_loop, 0, (uint32_t)eboot_menu_secs);
    branch(B, input_fns, 0, (uint32_t)eboot_menu_get_string);
    branch(B, input_fns, GET_CHAR, (uint32_t)eboot_menu_get_char);
    branch(B, input_fns, GET_INTEGER, (uint32_t)eboot_menu_get_integer);
    branch(B, yes_no, 0, (uint32_t)eboot_menu_ask_yes_no);
}

/* The eboot resident leaves TIMER2 running. aipc-boot stops TIMER2 before
 * EBOOT, so WinCE starts from the same state with and without the menu. */
static void eboot_menu_nk_enter(void)
{
    timer_stop();
}

cfw_component_t cfw_eboot_menu = {
    .info = {
        .id = "eboot_menu",
        .name = "EBOOT menu",
        .description = "Unhide the stock boot menu.",
        .flags = CFW_EXTRA,
    },
    .init = eboot_menu_init,
    .finish = eboot_menu_finish,
    .nk_enter = eboot_menu_nk_enter,
    .patches = {
        { CFW_EBOOT, NULL, eboot_menu_patch },
        { 0, NULL, NULL },
    },
};
