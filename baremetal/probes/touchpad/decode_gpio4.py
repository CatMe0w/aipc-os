#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path

BUCKETS = 10
EVENTS = 1024
SIZE = (22 + 2 * BUCKETS) * 4 + EVENTS * 8
KEY_BIT = 1 << 4
POWER_BIT = 1 << 3


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('result', type=Path)
    parser.add_argument('--events', type=int, default=20,
                        help='transitions to print, 0 for all')
    args = parser.parse_args()
    data = args.result.read_bytes()
    if len(data) != SIZE:
        raise ValueError(f'Expected {SIZE} result bytes')
    h = struct.unpack_from(f'<{22 + 2 * BUCKETS}I', data)
    if h[0] != 0x334b464c or h[16] > EVENTS:
        raise ValueError('Invalid GPIO4 result header')
    hz = h[2]
    samples = h[22:22 + BUCKETS]
    high = h[22 + BUCKETS:22 + 2 * BUCKETS]

    print(f'complete={h[1]} elapsed={h[18] / hz:.3f} s')
    print(f'mux={h[4]:08x} direction={h[5]:08x}/{h[11]:08x}/{h[12]:08x}')
    print(f'pull={h[7]:08x}/{h[10]:08x}/{h[13]:08x}')
    print(f'input: before={h[8]:08x} start={h[14]:08x} end={h[15]:08x}')
    print(f'transitions={h[16]} dropped={h[17]}')

    print('  sec    samples       high    high%')
    for i, (s, n) in enumerate(zip(samples, high)):
        print(f'  {i:3d} {s:10d} {n:10d} {100 * n / s if s else 0:8.4f}')

    events = [struct.unpack_from('<2I', data, (22 + 2 * BUCKETS) * 4 + i * 8)
              for i in range(h[16])]
    limit = len(events) if args.events == 0 else min(args.events, len(events))
    for ticks, value in events[:limit]:
        print(f'  {ticks / hz * 1000:10.3f} ms  input={value:08x} '
              f'gpio3={bool(value & POWER_BIT):d} gpio4={bool(value & KEY_BIT):d}')
    if limit < len(events):
        print(f'  ... {len(events) - limit} more, last at '
              f'{events[-1][0] / hz * 1000:.3f} ms')

    # The pad settles for tens of milliseconds after the pull-down goes off.
    # Only the seconds after that carry information about the button.
    settled_samples = sum(samples[1:])
    settled_high = sum(high[1:])
    late = sum(1 for ticks, _ in events if ticks >= h[2])
    print(f'after the first second: {settled_samples} samples, '
          f'{settled_high} high, {late} transitions')

    passed = (h[1] == 1 and h[17] == 0 and h[4] & 2 == 0
              and h[5] == h[12] and h[7] == h[13] and h[10] == h[7] | KEY_BIT
              and settled_samples > 0)
    print(f'validation: {"PASS" if passed else "FAIL"}')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
