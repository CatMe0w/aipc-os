#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('result', type=Path)
    args = parser.parse_args()
    data = args.result.read_bytes()
    if len(data) != 20736:
        raise ValueError('Expected 20736 result bytes')
    h = struct.unpack_from('<64I', data)
    if h[0] != 0x31555054 or h[1] != 1 or h[27] > 1024:
        raise ValueError('Invalid UART result header')
    print(f'mode={h[2]} complete={h[3]} baud={h[5]} PLL={h[6]:08x} ASIC={h[7]}')
    print(f'control: before={h[8]:08x} set={h[9]:08x} readback={h[40]:08x} after={h[10]:08x}')
    print(f'gate={h[39]:08x} L2={h[17]:08x}/{h[18]:08x} buffers={h[41]:08x}/{h[42]:08x}')
    print(f'init failure={h[35]:08x} send={h[25]} final F5={h[36]} reply={h[37]:08x}')
    print(f'bytes={h[26]} events={h[27]} dropped={h[28]} errors={h[29]} full={h[30]} timeouts={h[31]} batches={h[32]}')
    print(f'last status={h[33]:08x} config={h[34]:08x}')
    print(f'mux={h[19]:08x}/{h[20]:08x} direction={h[21]:08x}/{h[22]:08x} output={h[23]:08x}/{h[24]:08x}')
    passed = (h[3] == 1 and h[35] == 0xffffffff and h[25] == 0
              and h[36] == 0 and h[37] & 0xffff == 0x1fa
              and not any(h[28:31])
              and all(h[a] == h[b] for a, b in
                      ((8, 10), (15, 16), (17, 18), (19, 20), (21, 22), (23, 24))))
    stream = bytearray()
    for i in range(h[27]):
        ticks, status, config, word, count = struct.unpack_from('<5I', data, 256 + i * 20)
        if count not in (1, 2, 3, 4):
            raise ValueError('Invalid event byte count')
        raw = word.to_bytes(4, 'little')[:count]
        stream.extend(raw)
        if i < 16:
            print(f'{ticks / h[4] * 1000:9.3f} ms status={status:08x} config={config:08x} data={raw.hex(" ")}')
    print(f'stream prefix: {stream[:64].hex(" ")}')
    passed &= len(stream) == h[26]
    if h[2] == 0:
        expected = bytes(x & 255 for x in h[43:47])
        print(f'GPIO baseline: {expected.hex(" ")}, UART queries: {h[47]}')
        match = (h[47] == 8 and all(x & 0xff00 == 0x100 for x in h[43:47])
                 and expected[0] == 0xfa and stream == expected * h[47])
        print(f'All responses match baseline: {match}')
        passed &= match
    elif stream and stream[0] == 0xfa:
        motion = stream[1:]
        print(f'motion: {len(motion) // 3} packets, {len(motion) % 3} trailing bytes')
        bad = 0
        for i in range(0, len(motion) - 2, 3):
            status, x, y = motion[i:i + 3]
            bad += not bool(status & 8)
            if i < 48:
                print(f'  status={status:02x} dx={x - (256 if status & 16 else 0):+d} dy={y - (256 if status & 32 else 0):+d}')
        print(f'packets without sync bit: {bad}')
        passed &= not bad and bool(motion) and len(motion) % 3 == 0
    else:
        passed = False
    print(f'validation: {"PASS" if passed else "FAIL"}')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
