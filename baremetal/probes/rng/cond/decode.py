#!/usr/bin/env python3
"""Decode captures from the interleaved delay probe.

Answers two questions the probe was built for.

    delay  does the read value depend on how long the bus floated between
           reads, which separates a decay or leakage mechanism from one that
           is resampled fresh on every read
    drift  how far the estimate moves across rounds inside a run, and across
           runs spanning minutes

    uv run --with numpy baremetal/probes/rng/cond/decode.py run*.bin
"""

from __future__ import annotations

import argparse
import pathlib

import numpy as np

HEADER_BASE = 0x32008000
# RNG2 is the delay probe, RNG3 the L2 load probe. Both share this layout; the
# per-condition column holds delay iterations for one and a condition id for
# the other.
MAGICS = {0x524E4732: "delay", 0x524E4733: "load", 0x524E4734: "load2"}
LOAD_LABELS = {0: "quiet", 1: "wr_far", 2: "wr_near", 3: "rd_near", 4: "burst"}
LOAD2_LABELS = {0: "bare", 1: "quiet", 2: "wr_far", 3: "wr_near",
                4: "rd_near", 5: "burst"}


def hmin(p: float) -> float:
    return float(-np.log2(p))


def mcv(words: np.ndarray) -> tuple[float, int, int]:
    vals, counts = np.unique(words, return_counts=True)
    return hmin(counts.max() / len(words)), len(counts), int(vals[counts.argmax()])


def load(path: pathlib.Path):
    raw = np.fromfile(path, dtype="<u4")
    h = raw[:32]
    if int(h[0]) not in MAGICS:
        raise SystemExit(f"{path}: bad magic 0x{h[0]:08x}")
    kind = MAGICS[int(h[0])]
    if h[2] != 0:
        raise SystemExit(f"{path}: probe did not finish; stage={h[2]}")
    ndelay, chunk, rounds = int(h[3]), int(h[4]), int(h[5])
    timer_hz = int(h[7])
    delays = [int(h[16 + i]) for i in range(ndelay)]
    tim_off = (int(h[8]) - HEADER_BASE) // 4
    out_off = (int(h[9]) - HEADER_BASE) // 4
    timing = raw[tim_off:tim_off + ndelay * rounds * 2].reshape(ndelay, rounds, 2)
    samples = raw[out_off:out_off + ndelay * rounds * chunk]
    samples = samples.reshape(ndelay, rounds, chunk)
    return dict(delays=delays, chunk=chunk, rounds=rounds, timer_hz=timer_hz,
                timing=timing, samples=samples, spin_ticks=int(h[13]),
                addr=int(h[6]), kind=kind)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("captures", nargs="+", type=pathlib.Path)
    ap.add_argument("--bit", type=int, default=12,
                    help="active bit to track separately")
    args = ap.parse_args()

    runs = [load(p) for p in args.captures]
    first = runs[0]
    delays = first["delays"]
    kind = first["kind"]

    def label(d: int) -> str:
        if kind == "load2":
            return LOAD2_LABELS.get(d, str(d))
        if kind == "load":
            return LOAD_LABELS.get(d, str(d))
        return str(d)
    chunk, timer_hz = first["chunk"], first["timer_hz"]
    print(f"addr=0x{first['addr']:08x} delays={delays} chunk={chunk} "
          f"rounds={first['rounds']} runs={len(runs)}")
    ns_per_spin = first["spin_ticks"] / timer_hz * 1e9 / 10000
    print(f"spin_calibration={ns_per_spin:.2f} ns per iteration")

    print(f"\n=== per delay, pooled over all {len(runs)} runs ===")
    print(f"{'cond':>8} {'ns/read':>10} {'hmin':>9} {'unique':>7} "
          f"{'mode':>10} {'bit%d_one' % args.bit:>10} {'bit_flip':>9} {'n':>8}")
    pooled = {}
    for di, d in enumerate(delays):
        flat = np.concatenate([r["samples"][di].reshape(-1) for r in runs])
        el = np.concatenate([r["timing"][di, :, 1] for r in runs]).astype(np.float64)
        ns = float(np.median(el)) / timer_hz * 1e9 / chunk
        hm, uniq, mode = mcv(flat)
        b = ((flat >> args.bit) & 1).astype(np.uint8)
        one = float(b.mean())
        flip = float((b[1:] != b[:-1]).mean())
        pooled[d] = (ns, hm, uniq, mode, one, flip)
        print(f"{label(d):>8} {ns:>10.1f} {hm:>9.6f} {uniq:>7} 0x{mode:08x} "
              f"{one:>10.6f} {flip:>9.6f} {len(flat):>8}")

    print(f"\n=== {kind} effect ===")
    ns = [pooled[d][0] for d in delays]
    hs = [pooled[d][1] for d in delays]
    ones = [pooled[d][4] for d in delays]
    modes = {pooled[d][3] for d in delays}
    print(f"  inter-read interval spans {min(ns):.1f} ns to {max(ns):.1f} ns "
          f"({max(ns) / min(ns):.0f}x)")
    print(f"  hmin spans {min(hs):.6f} to {max(hs):.6f} "
          f"(spread {max(hs) - min(hs):.6f})")
    print(f"  bit{args.bit} one-rate spans {min(ones):.6f} to {max(ones):.6f} "
          f"(spread {max(ones) - min(ones):.6f})")
    print(f"  distinct modes across delays: {len(modes)} "
          + " ".join(f"0x{m:08x}" for m in sorted(modes)))

    print("\n=== drift within each run (pooled across delays per round) ===")
    for i, r in enumerate(runs, 1):
        per_round = np.array([mcv(r["samples"][:, k, :].reshape(-1))[0]
                              for k in range(r["rounds"])])
        span = float(r["timing"][:, :, 1].sum()) / timer_hz
        print(f"  run{i}: sampling={span:.2f}s "
              f"round_hmin min={per_round.min():.6f} "
              f"median={np.median(per_round):.6f} max={per_round.max():.6f} "
              f"range={np.ptp(per_round):.6f} sd={per_round.std(ddof=1):.6f}")

    print("\n=== drift across runs (each run pooled) ===")
    whole = []
    for i, r in enumerate(runs, 1):
        flat = r["samples"].reshape(-1)
        hm, uniq, mode = mcv(flat)
        b = ((flat >> args.bit) & 1)
        whole.append(hm)
        print(f"  run{i}: hmin={hm:.6f} unique={uniq} mode=0x{mode:08x} "
              f"bit{args.bit}_one={float(b.mean()):.6f}")
    whole = np.array(whole)
    print(f"  across runs: min={whole.min():.6f} max={whole.max():.6f} "
          f"range={np.ptp(whole):.6f} sd={whole.std(ddof=1):.6f}")


if __name__ == "__main__":
    main()
