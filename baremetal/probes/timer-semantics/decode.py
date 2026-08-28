#!/usr/bin/env python3
# Thanks to LLM. I don't have to write all these boilerplate unpacking code by hand.

import argparse
import struct
from pathlib import Path

MAGIC = 0x314B4D54
WRAP_MAGIC = 0x32524B54
LOAD_MAGIC = 0x334C4B54
LOAD_SAMPLES = 8
WRAPS = 4
WINDOW = 8
FULL_CYCLE = 1 << 26
NTIMERS = 5
ZC_SAMPLES = 1024
CLR_SAMPLES = 512
RELOAD_SAMPLES = 1024
COUNT_MASK = 0x03FFFFFF
CTRL_EN = 1 << 26
CTRL_LOAD = 1 << 27
CTRL_CLEAR = 1 << 28
CTRL_STA = 1 << 29
TIMER_HZ = 12_000_000

# Level 2 interrupt register at SYSCTRL+0x4c. Timer n uses enable bit 6-n and
# status bit 22-n, counting n from one.
L2_EN_BIT = (5, 4, 3, 2, 1)
L2_STA_BIT = (21, 20, 19, 18, 17)

LAYOUT = (
    ("magic", 1),
    ("version", 1),
    ("complete", 1),
    ("timer_hz", 1),
    ("l2_entry", 1),
    ("ctrl_entry", NTIMERS),
    ("live_entry", NTIMERS),
    ("past_end_ctrl", 1),
    ("past_end_live", 1),
    ("bitscan_ctrl", 32),
    ("bitscan_live", 32),
    ("width_pattern", 4),
    ("width_ctrl", 4),
    ("width_live", 4),
    ("start_count", 1),
    ("noload_live", 8),
    ("noload_ctrl", 1),
    ("load_live", 8),
    ("load_ctrl", 1),
    ("cal_first", 1),
    ("cal_last", 1),
    ("cal_samples", 1),
    ("zc_count", 1),
    ("zc_samples", 1),
    ("zc_live", ZC_SAMPLES),
    ("zc_ctrl", ZC_SAMPLES),
    ("clr_ctrl_before", 1),
    ("clr_ctrl_after", 1),
    ("clr_samples", 1),
    ("clr_live", CLR_SAMPLES),
    ("clr_ctrl", CLR_SAMPLES),
    ("spin_ticks", 1),
    ("sweep_live_armed", NTIMERS),
    ("sweep_live_later", NTIMERS),
    ("sweep_delta", NTIMERS),
    ("sweep_ctrl_expired", NTIMERS),
    ("sweep_live_expired", NTIMERS),
    ("sweep_l2_expired", NTIMERS),
    ("sweep_l2_cleared", NTIMERS),
    ("lat_iters", 1),
    ("lat_empty", 1),
    ("lat_live", 1),
    ("lat_ctrl", 1),
    ("lat_pair", 1),
    ("lat_dram", 1),
    ("ack_count", 1),
    ("ack_sta_seen", 1),
    ("ack_ctrl_expired", 1),
    ("ack_ctrl_after", 1),
    ("ack_live", 8),
    ("off_sta_seen", 1),
    ("off_ctrl_after", 1),
    ("off_live", 4),
    ("reload_count", 1),
    ("bare_ctrl_after", 1),
    ("bare_live", RELOAD_SAMPLES),
    ("keep_ctrl_after", 1),
    ("keep_live", RELOAD_SAMPLES),
)

WRAP_LAYOUT = (
    ("magic", 1),
    ("version", 1),
    ("complete", 1),
    ("loaded", 1),
    ("wraps_seen", 1),
    ("iters", 1),
    ("elapsed_lo", 1),
    ("elapsed_hi", 1),
    ("first_sample", 1),
    ("last_sample", 1),
    ("min_sample", 1),
    ("zero_seen", 1),
    ("wrap_iter", WRAPS),
    ("wrap_prev", WRAPS),
    ("wrap_cur", WRAPS),
    ("wrap_ctrl", WRAPS),
    ("wrap_cycle_lo", WRAPS),
    ("wrap_cycle_hi", WRAPS),
    ("wrap_before", WRAPS * WINDOW),
    ("wrap_after", WRAPS * WINDOW),
)

LOAD_LAYOUT = (
    ("magic", 1),
    ("version", 1),
    ("complete", 1),
    ("count", 1),
    ("single_ctrl_after", 1),
    ("single_live", LOAD_SAMPLES),
    ("two_ctrl_after", 1),
    ("two_live", LOAD_SAMPLES),
    ("attempts", 1),
    ("single_immediate_sta", 1),
    ("two_immediate_sta", 1),
)

TOTAL_WORDS = sum(n for _, n in LAYOUT)
WRAP_WORDS = sum(n for _, n in WRAP_LAYOUT)


def parse(blob: bytes, layout=LAYOUT) -> dict:
    need = sum(n for _, n in layout)
    if len(blob) < need * 4:
        raise SystemExit(f"result is {len(blob)} bytes, need at least {need * 4}")
    words = struct.unpack_from(f"<{need}I", blob, 0)
    out = {}
    pos = 0
    for name, count in layout:
        out[name] = words[pos] if count == 1 else list(words[pos : pos + count])
        pos += count
    return out


def show_wrap(r: dict) -> None:
    """
    The full scale wrap probe. Every read adds (previous - current) & mask to a
    64 bit total, thus the total between two wraps is the cycle length. The
    detection sample never lands exactly on the reload, so subtract how far past
    the reload each detection landed before comparing cycles.
    """
    loaded = r["loaded"]
    elapsed = r["elapsed_lo"] | (r["elapsed_hi"] << 32)
    seen = r["wraps_seen"]

    print("== full scale wrap ==")
    if not r["complete"]:
        print("WARNING: the stub did not reach the end\n")
    print(f"loaded {loaded:#x}, wraps seen {seen}, reads {r['iters']}")
    print(
        f"total {elapsed} ticks = {elapsed / TIMER_HZ:.3f} s, "
        f"{elapsed / r['iters']:.1f} ticks per read"
    )
    print(
        f"lowest sample {r['min_sample']:#x}, a sample landed exactly on zero: "
        f"{bool(r['zero_seen'])}"
    )
    print()

    exact = True
    prev_offset = None
    for k in range(seen):
        cycle = r["wrap_cycle_lo"][k] | (r["wrap_cycle_hi"][k] << 32)
        cur = r["wrap_cur"][k] & COUNT_MASK
        offset = (loaded - cur) & COUNT_MASK
        ctrl = r["wrap_ctrl"][k]
        line = (
            f"wrap {k}: read {r['wrap_iter'][k]:>9}  "
            f"{r['wrap_prev'][k] & COUNT_MASK:#010x} -> {cur:#010x}  "
            f"ctrl {ctrl:#010x} [{ctrl_flags(ctrl)}]  raw cycle {cycle}"
        )
        if k == 0 or prev_offset is None:
            print(line + "   (first wrap, the cycle is partial)")
        else:
            corrected = cycle - (offset - prev_offset)
            diff = corrected - FULL_CYCLE
            if diff:
                exact = False
            print(line)
            print(
                f"         detection landed {offset} ticks past the reload, "
                f"corrected cycle {corrected}, off by {diff:+d} from 2^26"
            )
        prev_offset = offset

    print()
    if seen >= 2 and exact:
        print("VERDICT: every cycle is exactly 2^26 ticks")
        print("         a 26 bit clocksource mask is correct and loses no time")
    elif seen < 2:
        print("VERDICT: not enough wraps to measure a cycle")
    else:
        print("VERDICT: the cycle is NOT exactly 2^26 ticks, see the offsets above")

    for k in range(seen):
        before = r["wrap_before"][k * WINDOW : (k + 1) * WINDOW]
        after = r["wrap_after"][k * WINDOW : (k + 1) * WINDOW]
        print(f"  wrap {k} before " + " ".join(f"{v & COUNT_MASK:#x}" for v in before))
        print(f"  wrap {k} after  " + " ".join(f"{v & COUNT_MASK:#x}" for v in after))
    print()


def ctrl_flags(value: int) -> str:
    names = []
    for bit, name in ((CTRL_EN, "EN"), (CTRL_LOAD, "LOAD"), (CTRL_CLEAR, "CLEAR"), (CTRL_STA, "STA")):
        if value & bit:
            names.append(name)
    extra = value & ~(COUNT_MASK | CTRL_EN | CTRL_LOAD | CTRL_CLEAR | CTRL_STA)
    if extra:
        names.append(f"undef:{extra:#x}")
    return "|".join(names) if names else "-"


def show_entry(r: dict) -> None:
    print("== entry state ==")
    print(f"complete            {r['complete']}")
    print(f"SYSCTRL+0x4c        {r['l2_entry']:#010x}")
    for t in range(NTIMERS):
        ctrl = r["ctrl_entry"][t]
        print(
            f"  timer{t + 1} ctrl={ctrl:#010x} count={ctrl & COUNT_MASK:#x} "
            f"[{ctrl_flags(ctrl)}] live={r['live_entry'][t] & COUNT_MASK:#x}"
        )
    print(
        f"past the end        ctrl(0x2c)={r['past_end_ctrl']:#010x} "
        f"live(0x114)={r['past_end_live']:#010x}"
    )
    print()


def show_bitscan(r: dict) -> None:
    print("== control register bit scan (timer2) ==")
    print("wrote      ctrl read     live read   note")
    stuck = []
    for n in range(32):
        wrote = 1 << n
        ctrl = r["bitscan_ctrl"][n]
        live = r["bitscan_live"][n]
        if n <= 25:
            if ctrl & wrote:
                note = "count bit, holds"
            else:
                stuck.append(n)
                note = "count bit, READS BACK ZERO"
        elif ctrl & wrote:
            note = "read write"
        else:
            note = "write only strobe or unimplemented"
        print(f"{wrote:#010x} {ctrl:#010x}   {live:#010x}  {note}")
    if stuck:
        print(f"count bits that do not hold: {stuck}")
        print("-> usable count width is not 26 bits")
    else:
        print("-> all 26 count bits hold, width is 26 bits")
    for n, name in ((26, "EN"), (27, "LOAD"), (28, "CLEAR"), (29, "STA"), (30, "bit30"), (31, "bit31")):
        holds = bool(r["bitscan_ctrl"][n] & (1 << n))
        print(f"-> bit {n} ({name}) reads back: {holds}")
    print()


def show_width(r: dict) -> None:
    print("== loaded count as seen in the live register ==")
    for i in range(4):
        pat = r["width_pattern"][i]
        ctrl = r["width_ctrl"][i]
        live = r["width_live"][i]
        drift = (pat - (live & COUNT_MASK)) & COUNT_MASK
        print(
            f"wrote {pat:#010x}  ctrl {ctrl:#010x}  live {live & COUNT_MASK:#010x}  "
            f"drift {drift}"
        )
    print()


def show_start(r: dict) -> None:
    count = r["start_count"]
    print(f"== start up, loaded count {count:#x} ==")
    for label, live, ctrl in (
        ("EN only, no LOAD", r["noload_live"], r["noload_ctrl"]),
        ("EN and LOAD", r["load_live"], r["load_ctrl"]),
    ):
        seq = [v & COUNT_MASK for v in live]
        moved = len(set(seq)) > 1
        started = any(v != 0 for v in seq)
        print(f"{label}:")
        print("  live " + " ".join(f"{v:#x}" for v in seq))
        print(f"  ctrl {ctrl:#010x} [{ctrl_flags(ctrl)}]")
        first_lag = (count - seq[0]) & COUNT_MASK
        print(f"  first read is {first_lag} ticks below the loaded count")
        print(f"  counter moving: {moved}, non zero: {started}")
    ctrl_live = (r["load_ctrl"] & COUNT_MASK) != (r["load_live"][7] & COUNT_MASK)
    print(
        "-> the control register reads back "
        + ("the loaded value, not a live count" if ctrl_live else "something that tracks the live count")
    )
    print()


def show_calibration(r: dict) -> None:
    span = (r["cal_first"] - r["cal_last"]) & COUNT_MASK
    per = span / r["cal_samples"] if r["cal_samples"] else 0
    print("== capture loop calibration ==")
    print(f"first {r['cal_first']:#x}  last {r['cal_last']:#x}  samples {r['cal_samples']}")
    print(f"{span} ticks over the window, {per:.1f} ticks per pass, {per * 1000 / 12:.0f} ns per pass")
    print()


def classify_crossing(live: list[int], ctrl: list[int], count: int) -> None:
    vals = [v & COUNT_MASK for v in live]
    sta = [(c & CTRL_STA) != 0 for c in ctrl]

    sta_at = next((i for i, s in enumerate(sta) if s), None)
    rise_at = next((i for i in range(1, len(vals)) if vals[i] > vals[i - 1]), None)

    print(f"loaded count {count:#x} ({count})")
    print(f"first sample {vals[0]:#x}  last sample {vals[-1]:#x}")
    print(f"STA first set at sample {sta_at}")
    print(f"first rise in the count at sample {rise_at}")

    if rise_at is None:
        tail = vals[-32:]
        if all(v == 0 for v in tail):
            print("VERDICT: the counter STOPS AT ZERO and stays there")
            print("         a free running clocksource on this timer is not possible")
            print("         without clearing the interrupt on every wrap")
        else:
            print("VERDICT: no wrap seen inside the window, the count was too large")
        return

    before = vals[rise_at - 1]
    after = vals[rise_at]
    print(f"count went {before:#x} -> {after:#x}")

    def near(a: int, b: int) -> bool:
        return abs(a - b) <= max(64, b // 32)

    if near(after, count):
        print("VERDICT: the counter AUTO RELOADS from the loaded value")
        print("         periodic mode needs no reload write in the handler")
    elif near(after, COUNT_MASK):
        print("VERDICT: the counter WRAPS TO FULL SCALE and keeps running")
        print("         a 26 bit free running clocksource works, no overflow IRQ needed")
    else:
        print(f"VERDICT: unexpected value after the crossing, {after:#x}")

    zeros = sum(1 for v in vals[max(0, rise_at - 8) : rise_at] if v == 0)
    print(f"samples reading exactly zero just before the rise: {zeros}")
    if zeros > 1:
        print("         the counter lingers at zero, expect lost time on every wrap")

    if sta_at is not None:
        held = sum(1 for s in sta[sta_at:] if s)
        print(f"STA stays set for {held} of the {len(sta) - sta_at} samples after it sets")


def show_zero_crossing(r: dict) -> None:
    print("== phase 5: what happens at zero ==")
    classify_crossing(r["zc_live"], r["zc_ctrl"], r["zc_count"])
    print()


def show_clear_reload(r: dict, loaded: int) -> None:
    print("== phase 6: writing CLEAR and EN only, the AK98 reload idiom ==")
    print(
        f"ctrl before {r['clr_ctrl_before']:#010x} [{ctrl_flags(r['clr_ctrl_before'])}]"
    )
    print(
        f"ctrl after  {r['clr_ctrl_after']:#010x} [{ctrl_flags(r['clr_ctrl_after'])}]"
    )
    vals = [v & COUNT_MASK for v in r["clr_live"]]
    print(f"live after  first {vals[0]:#x}  last {vals[-1]:#x}")
    moving = vals[0] != vals[-1]
    print(f"counter restarted: {moving}")
    if moving:
        near_load = abs(vals[0] - loaded) <= max(64, loaded // 32) if loaded else False
        print(
            "  first sample is "
            + ("back at the loaded count" if near_load else "not at the loaded count")
        )
        if loaded:
            classify_crossing(r["clr_live"], r["clr_ctrl"], loaded)
    print()


def show_sweep(r: dict) -> None:
    print("== phase 7: every timer, same short count ==")
    print("timer  armed      moved  ctrl expired          live  L2 status bits set")
    for t in range(NTIMERS):
        ctrl = r["sweep_ctrl_expired"][t]
        l2 = r["sweep_l2_expired"][t]
        cleared = r["sweep_l2_cleared"][t]
        set_bits = [b for b in range(16, 27) if l2 & (1 << b)]
        gone = [b for b in set_bits if not cleared & (1 << b)]
        expect = L2_STA_BIT[t]
        mark = "" if expect in set_bits else "  <- expected bit %d" % expect
        print(
            f"{t + 1:5d}  {r['sweep_live_armed'][t] & COUNT_MASK:#010x} "
            f"{r['sweep_delta'][t]:6d}  {ctrl:#010x} [{ctrl_flags(ctrl)}] "
            f"{r['sweep_live_expired'][t] & COUNT_MASK:#010x}  {set_bits} cleared {gone}{mark}"
        )
    print(f"spin loop measured at {r['spin_ticks']} ticks ({r['spin_ticks'] / 12000:.2f} ms)")
    print()


def show_latency(r: dict) -> None:
    iters = r["lat_iters"]
    print(f"== phase 8: read cost over {iters} passes ==")

    def ns(ticks: int) -> float:
        return ticks * 1000.0 / (TIMER_HZ / 1_000_000) / iters

    base = r["lat_empty"]
    rows = (
        ("empty loop", r["lat_empty"]),
        ("SYSCTRL live read", r["lat_live"]),
        ("SYSCTRL ctrl read", r["lat_ctrl"]),
        ("live + ctrl pair", r["lat_pair"]),
        ("DRAM read", r["lat_dram"]),
    )
    for label, ticks in rows:
        net = max(0, ticks - base)
        print(f"{label:20s} {ticks:8d} ticks  {ns(ticks):7.1f} ns/pass  net {ns(net):7.1f} ns")
    if r["lat_pair"] > r["lat_live"] > base:
        saved = (r["lat_pair"] - r["lat_live"]) * 1000.0 / 12 / iters
        print(f"-> dropping the second read saves about {saved:.0f} ns per clocksource read")
    print()


def show_ack(r: dict) -> None:
    count = r["ack_count"]
    print(f"== phase 9: one plain write as the tick acknowledge, count {count:#x} ==")
    print(f"interrupt seen: {bool(r['ack_sta_seen'])}")
    print(
        f"ctrl expired {r['ack_ctrl_expired']:#010x} [{ctrl_flags(r['ack_ctrl_expired'])}]"
    )
    print(
        f"after writing CLEAR|EN with a zero count field: "
        f"{r['ack_ctrl_after']:#010x} [{ctrl_flags(r['ack_ctrl_after'])}]"
    )
    live = [v & COUNT_MASK for v in r["ack_live"]]
    print("  live " + " ".join(f"{v:#x}" for v in live))
    kept = (r["ack_ctrl_after"] & COUNT_MASK) == count
    running = len(set(live)) > 1
    acked = not (r["ack_ctrl_after"] & CTRL_STA)
    print(f"  reload value kept: {kept}, still running: {running}, interrupt cleared: {acked}")
    if kept and running and acked:
        print("-> a periodic tick needs ONE write, no read modify write")
    else:
        print("-> one plain write is not enough, keep the read modify write")

    print(f"interrupt seen before the stop test: {bool(r['off_sta_seen'])}")
    print(
        f"after writing CLEAR alone: {r['off_ctrl_after']:#010x} "
        f"[{ctrl_flags(r['off_ctrl_after'])}]"
    )
    off = [v & COUNT_MASK for v in r["off_live"]]
    print("  live " + " ".join(f"{v:#x}" for v in off))
    print(f"  counter frozen: {len(set(off)) == 1}")
    print()


def show_reload_source(r: dict) -> None:
    count = r["reload_count"]
    print(f"== phase 10 and 11: where the automatic reload takes its value, period {count:#x} ==")

    for label, ctrl_after, live in (
        ("acknowledged with a zero count field", r["bare_ctrl_after"], r["bare_live"]),
        ("acknowledged with the period written back", r["keep_ctrl_after"], r["keep_live"]),
    ):
        vals = [v & COUNT_MASK for v in live]
        rise = next((i for i in range(1, len(vals)) if vals[i] > vals[i - 1]), None)
        print(f"{label}:")
        print(f"  ctrl after {ctrl_after:#010x} count field {ctrl_after & COUNT_MASK:#x}")
        if rise is None:
            moving = len(set(vals)) > 1
            print(f"  no further reload inside the window, counter moving: {moving}")
            print(f"  first {vals[0]:#x} last {vals[-1]:#x}")
        else:
            print(f"  next reload at sample {rise}, {vals[rise - 1]:#x} -> {vals[rise]:#x}")
            if abs(vals[rise] - count) <= max(64, count // 32):
                print("  -> reloaded the original period")
            else:
                print("  -> reloaded something else")
    print()


def show_load(r: dict) -> None:
    count = r["count"]
    print("== LOAD strobe timing ==")
    if not r["complete"]:
        print("WARNING: the stub did not reach the end\n")
    print(f"count written {count:#x}\n")

    for label, ctrl, live in (
        ("one write of count|EN|LOAD", r["single_ctrl_after"], r["single_live"]),
        ("count first, then count|EN|LOAD", r["two_ctrl_after"], r["two_live"]),
    ):
        vals = [v & COUNT_MASK for v in live]
        print(f"{label}:")
        print(f"  ctrl after {ctrl:#010x} [{ctrl_flags(ctrl)}]")
        print("  live " + " ".join(f"{v:#x}" for v in vals))
        loaded = abs(vals[0] - count) <= 256
        print(f"  count loaded correctly: {loaded}")
        print(f"  interrupt raised at once: {bool(ctrl & CTRL_STA)}")

    n = r["attempts"]
    a, b = r["single_immediate_sta"], r["two_immediate_sta"]
    print()
    print(f"over {n} attempts each:")
    print(f"  one write     raised the interrupt at once {a} times")
    print(f"  two writes    raised the interrupt at once {b} times")
    print()
    if a == n and b == 0:
        print("VERDICT: a write that changes the count field and strobes LOAD in the")
        print("         same access raises the interrupt status, although it loads the")
        print("         count correctly. Let the count settle in its own write first.")
    elif a == 0 and b == 0:
        print("VERDICT: both forms are clean")
    else:
        print("VERDICT: mixed result, look at the counts above")
    print()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Decode an AK7802 timer semantics probe result"
    )
    parser.add_argument("path", type=Path)
    parser.add_argument("--dump-crossing", type=int, default=0,
                        help="print N raw samples around the zero crossing")
    args = parser.parse_args()

    blob = args.path.read_bytes()
    magic = struct.unpack_from("<I", blob, 0)[0]

    if magic == WRAP_MAGIC:
        show_wrap(parse(blob, WRAP_LAYOUT))
        return

    if magic == LOAD_MAGIC:
        show_load(parse(blob, LOAD_LAYOUT))
        return

    r = parse(blob)
    if r["magic"] != MAGIC:
        raise SystemExit(
            f"bad magic {magic:#010x}, expected {MAGIC:#010x} or {WRAP_MAGIC:#010x}"
        )
    if not r["complete"]:
        print("WARNING: the stub did not reach the end, results are partial\n")

    show_entry(r)
    show_bitscan(r)
    show_width(r)
    show_start(r)
    show_calibration(r)
    show_zero_crossing(r)
    show_clear_reload(r, r["zc_count"])
    show_sweep(r)
    show_latency(r)
    show_ack(r)
    show_reload_source(r)

    if args.dump_crossing:
        vals = [v & COUNT_MASK for v in r["zc_live"]]
        rise = next((i for i in range(1, len(vals)) if vals[i] > vals[i - 1]), None)
        if rise is None:
            print("no crossing to dump")
            return
        lo = max(0, rise - args.dump_crossing // 2)
        hi = min(len(vals), rise + args.dump_crossing // 2)
        print(f"== raw samples {lo}..{hi - 1} ==")
        for i in range(lo, hi):
            ctrl = r["zc_ctrl"][i]
            here = " <-" if i == rise else ""
            print(f"{i:5d}  live {vals[i]:#010x}  ctrl {ctrl:#010x} [{ctrl_flags(ctrl)}]{here}")


if __name__ == "__main__":
    main()
