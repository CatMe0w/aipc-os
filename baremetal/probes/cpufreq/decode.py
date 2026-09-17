import struct
import sys
from pathlib import Path


MAGIC_MEASURE = 0x43505531
MAGIC_SWITCH = 0x43505532
MAGIC_ASIC3X = 0x4153335A
MAGIC_DQS = 0x44515330

COPY_TOO_BIG = 0xBADC0DE1
COPY_MISMATCH = 0xBADC0DE2

POINT_NAMES = (
    "0 reference",
    "1 control, asic /4",
    "2 control released",
    "3 0x64 bit28 + strobe14",
    "4 0x64 bit28 + strobe12",
    "5 0x04 bit28 + strobe14",
    "6 0x04 bit28 + strobe12",
    "7 both bits + both strobes",
)
CONTROL_POINT = 1
VARIANT_POINTS = (3, 4, 5, 6, 7)

TIMER_HZ = 12_000_000
REF_TICKS_124 = 1166   # measured bus loop ticks at the 124 MHz boot clock
BASELINE_HZ = 248_000_000


def decode_measure(w: list[int]) -> None:
    window, inner, ctl = w[3], w[4], w[5]

    print(f"CLKDIV1 at entry   0x{w[1]:08x}  "
          f"cpu_src={'PLL1' if w[1] & (1 << 15) else 'ASIC'}")
    print(f"ANALOG_CTRL2       0x{w[2]:08x}")
    print(f"CP15 c1            0x{ctl:08x}  "
          f"mmu={ctl & 1}  dcache={(ctl >> 2) & 1}  icache={(ctl >> 12) & 1}")
    print(f"window             {window} ticks ({window / TIMER_HZ * 1000:.1f} ms), "
          f"{inner} iterations per batch")
    print()

    points = (
        ("baseline (PLL1)", w[6], w[7], w[8], None, None),
        ("switched (ASIC)", w[11], w[12], w[13], w[9], w[10]),
        ("restored (PLL1)", w[16], w[17], w[18], w[14], w[15]),
    )

    base_rate = w[6] / (w[7] / TIMER_HZ)
    cycles_per_batch = BASELINE_HZ / base_rate

    print(f"{'point':16s} {'batches':>8s} {'ticks':>9s} {'rate/s':>10s} "
          f"{'implied MHz':>12s} {'ddr bad':>8s} {'strobe':>8s}  CLKDIV1")
    for name, batches, ticks, bad, spins, readback in points:
        rate = batches / (ticks / TIMER_HZ)
        mhz = rate * cycles_per_batch / 1e6
        spins_s = "-" if spins is None else str(spins)
        rb = "-" if readback is None else f"0x{readback:08x}"
        print(f"{name:16s} {batches:8d} {ticks:9d} {rate:10.1f} "
              f"{mhz:12.1f} {bad:8d} {spins_s:>8s}  {rb}")

    print()
    print(f"cycles per batch   {cycles_per_batch:.1f} "
          f"({cycles_per_batch / inner:.2f} per inner iteration, "
          f"assuming the baseline is {BASELINE_HZ / 1e6:.0f} MHz)")

    ratio = (w[6] / w[7]) / (w[11] / w[12])
    print(f"baseline / switched  {ratio:.4f}")
    if abs(ratio - 2.0) < 0.05:
        print("  -> bit 15 halves the core clock. PLL1 248 MHz, ASIC 124 MHz.")
    elif abs(ratio - 1.0) < 0.02:
        print("  -> no change. Either bit 15 does nothing, or the loop is not "
              "core paced (check that icache=1 above).")
    else:
        print("  -> unexpected ratio, do not draw a conclusion from it")

    if w[1] != w[19]:
        print(f"WARNING: CLKDIV1 not restored, entry 0x{w[1]:08x} "
              f"exit 0x{w[19]:08x}")
    if w[8] or w[13] or w[18]:
        print("WARNING: DDR check failed, see the ddr bad column")


def decode_switch(w: list[int]) -> None:
    target = "PLL1 (248 MHz)" if w[4] else "ASIC (124 MHz)"
    print(f"requested          {target}")
    print(f"CLKDIV1 before     0x{w[1]:08x}  "
          f"cpu_src={'PLL1' if w[1] & (1 << 15) else 'ASIC'}")
    print(f"CLKDIV1 after      0x{w[3]:08x}  "
          f"cpu_src={'PLL1' if w[3] & (1 << 15) else 'ASIC'}")
    print(f"strobe iterations  {w[2]}")
    if w[2] >= 1_000_000:
        print("WARNING: the PLL1_EN strobe never cleared")


def decode_asic3x(w: list[int]) -> None:
    r = w[4:]

    print(f"l2text copied      {w[1]} bytes, worker at offset {w[2]}")
    print(f"CLKDIV1 at entry   0x{r[0]:08x}  asic_div_field={(r[0] >> 6) & 7}  "
          f"cpu_src={'PLL1' if r[0] & (1 << 15) else 'ASIC'}")
    print(f"ANALOG_CTRL2 entry 0x{r[1]:08x}  bit28={(r[1] >> 28) & 1}")
    print(f"CLKDIV1 cpu->ASIC  0x{r[3]:08x}  strobe={r[2]}")
    print(f"CLKDIV1 at /4      0x{r[4]:08x}  asic_div_field={(r[4] >> 6) & 7}  "
          f"strobes {r[5]}, {r[6]}")
    print(f"CLKDIV1 final      0x{r[9]:08x}   ANALOG_CTRL2 final 0x{r[20]:08x}")
    print()

    ticks = {}
    print(f"{'point':28s} {'alu ticks':>10s} {'reg ticks':>10s}")
    for i, name in enumerate(POINT_NAMES):
        a_t, g_t = r[25 + i * 4], r[27 + i * 4]
        ticks[i] = (a_t, g_t)
        print(f"{name:28s} {a_t:10d} {g_t:10d}")

    print()
    print("readback after arming each variant:")
    for n, i in enumerate(VARIANT_POINTS):
        cd, an = r[10 + n * 2], r[11 + n * 2]
        print(f"  {POINT_NAMES[i]:28s} CLKDIV1=0x{cd:08x} bit28={(cd >> 28) & 1}"
              f"   ANALOG_CTRL2=0x{an:08x} bit28={(an >> 28) & 1}")

    def ratio(i):
        return ticks[i][0] / ticks[0][0], ticks[i][1] / ticks[0][1]

    print()
    if not ticks[CONTROL_POINT][0]:
        print("CONTROL NOT RUN, build with ASIC3X_CONTROL=1. Nothing below is "
              "evidence.")
        return
    ctl_a, ctl_g = ratio(CONTROL_POINT)
    print(f"CONTROL, asic /4 against /2   alu {ctl_a:.4f}  reg {ctl_g:.4f}")
    if abs(ctl_a - 2.0) > 0.05:
        print("  -> the ALU loop did not show the factor of two. Nothing below "
              "is evidence.")
        return
    print("  -> the ALU loop tracks the ASIC clock. It is the instrument.")

    scaled = ticks[CONTROL_POINT][1] - ticks[0][1]
    fixed = ticks[0][1] - scaled
    print(f"  -> the register loop is attenuated. Of {ticks[0][1]} reference "
          f"ticks, {scaled} scale with the ASIC clock and {fixed} do not.")
    reg_3x = (scaled * 1.5 + fixed) / ticks[0][1]
    print(f"\nA /3 would read {ticks[0][0] * 1.5:.0f} alu ticks against "
          f"{ticks[0][0]}, and a register ratio near {reg_3x:.3f}.")
    print()

    hit = False
    for i in VARIANT_POINTS:
        a, g = ratio(i)
        verdict = ("/3 ACTIVE" if abs(a - 1.5) < 0.05 else
                   "no change" if abs(a - 1.0) < 0.01 else
                   f"unexpected {a:.3f}")
        if abs(a - 1.5) < 0.05:
            hit = True
        print(f"{POINT_NAMES[i]:28s} alu {a:.4f}  reg {g:.4f}   {verdict}")

    print()
    if hit:
        print("At least one arming sequence divides the ASIC clock by three.")
    else:
        print("No arming sequence changed the ASIC clock. Neither register bit "
              "and neither strobe reaches a /3 divider on this part.")


def decode_apply(w: list[int]) -> None:
    r = w[4:]
    ok = "complete" if w[3] == 0xC0FFEE else f"INCOMPLETE(0x{w[3]:x})"
    # The loop is fetch bound with the caches off, thus it reads the bus clock.
    # 1166 ticks is the calibrated count at the 124 MHz boot clock. Deriving both
    # ends from that makes a re-run on an already applied board read correctly,
    # instead of assuming the entry clock is always the boot clock.
    before_mhz = 124.0 * REF_TICKS_124 / r[6] if r[6] else 0.0
    after_mhz = 124.0 * REF_TICKS_124 / r[8] if r[8] else 0.0
    what = "APPLY the tuned point" if w[2] == 8 else "REVERT to boot values"

    print(f"stage {w[2]} {ok}   {what}")
    print(f"  CLKDIV1 {'0x%08x' % r[0]} -> 0x{r[10]:08x}   "
          f"cpu_src={'PLL1' if r[10] & (1 << 15) else 'ASIC'}")
    print(f"  SDRAM_CFG2 0x{r[1]:08x} -> 0x{r[9]:08x}")
    print(f"  SDRAM_CFG3 0x{r[2]:08x} -> 0x{r[11]:08x}   "
          f"DQS {(r[2] >> 16) & 0xF} -> {(r[11] >> 16) & 0xF}")
    print(f"  PLL strobe spins {r[7]}")
    print()
    print(f"  bus clock   {before_mhz:.1f} MHz -> {after_mhz:.1f} MHz")
    core = after_mhz * 2 if r[10] & (1 << 15) else after_mhz
    print(f"  core clock  {core:.1f} MHz "
          f"({'on PLL1, the bus is PLL1/2' if r[10] & (1 << 15) else 'on the bus'})")
    print(f"  memory test after the change: {r[12]} and {r[13]} errors")
    print()
    print(f"  CPU workload before 0x{r[4]:08x}")
    print(f"  CPU workload after  0x{r[5]:08x}")
    if r[14] == 0:
        print("  -> the core computes the same result at both clocks")
    else:
        print("  -> MISMATCH, the core computed a different result. It does not "
              "run correctly at this clock.")
    if r[12] or r[13]:
        print("  -> WARNING: memory errors at the new point")
    if w[2] == 8:
        print()
        print("  The board is left at this operating point. Everything from here "
              "on, this debug session included, runs on it.")


def decode_dqs(w: list[int]) -> None:
    if w[2] in (8, 9):
        decode_apply(w)
        return
    r = w[4:]
    ok = "complete" if w[3] == 0xC0FFEE else f"INCOMPLETE(0x{w[3]:x})"
    boot_dqs = (r[2] >> 16) & 0xF
    mem = 2 * (r[4] + 62)
    meas = 124.0 * r[6] / r[8] if r[8] else 0.0

    print(f"stage {w[2]} {ok}")
    print(f"  target memory {mem} MHz, measured {meas:.1f} MHz, "
          f"clock delay {r[5]}")
    print(f"  CFG2 in use 0x{r[9]:08x}   CFG3 at boot 0x{r[2]:08x} "
          f"(DQS {boot_dqs}, clock delay {(r[2] >> 20) & 0xF})")
    print(f"  restored CLKDIV1 0x{r[10]:08x} CFG2 0x{r[11]:08x} "
          f"CFG3 0x{r[12]:08x}, {r[14]} errors after, done={r[15]}")
    print()

    print(f"  {'DQS':>4s} {'pass A':>7s} {'pass B':>7s}  {'':12s}")
    clean = []
    for i in range(16):
        a, b = r[16 + i * 2], r[16 + i * 2 + 1]
        tag = ""
        if a == 0 and b == 0:
            tag = "clean"
            clean.append(i)
        mark = "  <- boot value" if i == boot_dqs else ""
        print(f"  {i:4d} {a:7d} {b:7d}  {tag:12s}{mark}")

    print()
    if clean:
        runs, cur = [], [clean[0]]
        for v in clean[1:]:
            if v == cur[-1] + 1:
                cur.append(v)
            else:
                runs.append(cur); cur = [v]
        runs.append(cur)
        best = max(runs, key=len)
        print(f"  clean values: {clean}")
        print(f"  widest clean run: {best[0]}..{best[-1]} "
              f"({len(best)} wide), centre {best[len(best) // 2]}")
        print("  A wide run means real margin. A single clean value next to "
              "failing ones is luck, not margin.")
    else:
        print("  no DQS value is clean at this clock")
    if r[14]:
        print("  WARNING: errors after the restore, the board did not come "
              "back clean")


def main() -> None:
    data = Path(sys.argv[1]).read_bytes()
    words = list(struct.unpack(f"<{len(data) // 4}I", data[: len(data) // 4 * 4]))

    if words[0] == MAGIC_MEASURE:
        decode_measure(words)
    elif words[0] == MAGIC_SWITCH:
        decode_switch(words)
    elif words[0] == MAGIC_ASIC3X:
        decode_asic3x(words)
    elif words[0] == MAGIC_DQS:
        decode_dqs(words)
    elif words[0] == COPY_TOO_BIG:
        print("the L2 worker does not fit in the window, "
              f"{words[1]} bytes")
        sys.exit(1)
    elif words[0] == COPY_MISMATCH:
        print(f"the L2 copy did not read back, first bad word {words[3]}")
        sys.exit(1)
    else:
        print(f"unknown magic 0x{words[0]:08x}, the probe did not finish")
        sys.exit(1)


if __name__ == "__main__":
    main()
