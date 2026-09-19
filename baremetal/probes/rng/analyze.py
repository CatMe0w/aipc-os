#!/usr/bin/env python3

from __future__ import annotations

import argparse
import collections
import math
import pathlib
import struct
import statistics


MAGIC = 0x524E4730
HEADER_ADDR = 0x32008000
OUTPUT_ADDR = 0x32010000
HEADER_BYTES = OUTPUT_ADDR - HEADER_ADDR


def hmin(probability: float) -> float:
    return max(0.0, -math.log2(probability)) if probability else math.inf


def summarize(name: str, words: tuple[int, ...]) -> None:
    counts = collections.Counter(words)
    common_word, common_count = counts.most_common(1)[0]
    equal = sum(a == b for a, b in zip(words, words[1:]))
    bit_ones = [0] * 32
    bit_flips = [0] * 32

    previous = words[0]
    for index, word in enumerate(words):
        for bit in range(32):
            bit_ones[bit] += (word >> bit) & 1
        if index:
            changed = previous ^ word
            for bit in range(32):
                bit_flips[bit] += (changed >> bit) & 1
        previous = word

    bit_hmin = [
        hmin(max(ones, len(words) - ones) / len(words)) for ones in bit_ones
    ]
    byte_counts = collections.Counter()
    for word in words:
        byte_counts.update(word.to_bytes(4, "little"))
    common_byte, common_byte_count = byte_counts.most_common(1)[0]

    print(f"[{name}]")
    print(f"samples={len(words)} unique_words={len(counts)}")
    print(
        f"word_mcv=0x{common_word:08x} count={common_count} "
        f"p={common_count / len(words):.9f} hmin={hmin(common_count / len(words)):.6f} bits/word"
    )
    print(
        f"byte_mcv=0x{common_byte:02x} count={common_byte_count} "
        f"p={common_byte_count / (4 * len(words)):.9f} "
        f"hmin={hmin(common_byte_count / (4 * len(words))):.6f} bits/byte"
    )
    print(
        f"adjacent_equal={equal}/{len(words) - 1} "
        f"({equal / (len(words) - 1):.9f})"
    )
    print(
        f"bit_mcv_hmin_min={min(bit_hmin):.6f} "
        f"bit_mcv_hmin_max={max(bit_hmin):.6f} bits/bit"
    )
    print(
        "bit_one_rates="
        + " ".join(f"{ones / len(words):.6f}" for ones in bit_ones)
    )
    print(
        "bit_flip_rates="
        + " ".join(f"{flips / (len(words) - 1):.6f}" for flips in bit_flips)
    )


def summarize_sweep_positions(
    words: tuple[int, ...], nc_base: int, nc_words: int
) -> None:
    estimates: list[tuple[float, int]] = []
    unique_counts: list[int] = []
    for position in range(nc_words):
        values = words[position::nc_words]
        counts = collections.Counter(values)
        estimates.append(
            (hmin(counts.most_common(1)[0][1] / len(values)), position)
        )
        unique_counts.append(len(counts))

    repeats = sum(
        words[index] == words[index - nc_words]
        for index in range(nc_words, len(words))
    )
    comparisons = len(words) - nc_words
    entropy_values = [estimate for estimate, _ in estimates]
    worst_entropy, worst_position = min(estimates)
    best_entropy, best_position = max(estimates)
    print("[sweep positions]")
    print(
        f"same_address_repeat={repeats}/{comparisons} "
        f"({repeats / comparisons:.9f})"
    )
    print(
        f"per_address_mcv_hmin_min={worst_entropy:.6f}@0x{nc_base + 4 * worst_position:08x} "
        f"median={statistics.median(entropy_values):.6f} "
        f"max={best_entropy:.6f}@0x{nc_base + 4 * best_position:08x} bits/word"
    )
    print(
        f"per_address_unique_min={min(unique_counts)} "
        f"median={statistics.median(unique_counts):.1f} "
        f"max={max(unique_counts)}"
    )


def write_nist_symbols(
    output_dir: pathlib.Path, name: str, words: tuple[int, ...]
) -> None:
    symbols: dict[int, int] = {}
    encoded = bytearray()
    for word in words:
        if word not in symbols:
            if len(symbols) == 256:
                raise SystemExit(
                    f"{name} has more than 256 observed symbols; reduction is required"
                )
            symbols[word] = len(symbols)
        encoded.append(symbols[word])

    bits = max(1, (len(symbols) - 1).bit_length())
    path = output_dir / f"{name}-symbols-{bits}bit.bin"
    path.write_bytes(encoded)
    print(f"wrote_nist={path} symbols={len(symbols)} bits_per_symbol={bits}")


def write_nist_bitstream(
    output_dir: pathlib.Path, name: str, words: tuple[int, ...]
) -> None:
    encoded = bytearray(32 * len(words))
    for word_index, word in enumerate(words):
        offset = 32 * word_index
        for bit_index in range(32):
            encoded[offset + bit_index] = (word >> (31 - bit_index)) & 1

    path = output_dir / f"{name}-raw-bits.bin"
    path.write_bytes(encoded)
    print(f"wrote_nist_bitstream={path} bits={len(encoded)} order=msb-first")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=pathlib.Path)
    parser.add_argument("--nist-dir", type=pathlib.Path)
    parser.add_argument("--nist-bitstream", action="append", default=[])
    args = parser.parse_args()

    data = args.capture.read_bytes()
    if len(data) < HEADER_BYTES:
        raise SystemExit("capture is shorter than the result header")

    header = struct.unpack_from("<16I", data)
    if header[0] != MAGIC:
        raise SystemExit(f"bad magic 0x{header[0]:08x}")
    if header[2] != 0:
        raise SystemExit(f"probe did not finish; stage={header[2]}")

    sample_words = header[3]
    version = header[1]
    if version == 1:
        stream_bytes = header[11]
        stream_names = ("sweep", "fixed1580", "driven")
    elif version == 2:
        stream_bytes = header[12]
        stream_names = ("sweep", "fixed1580", "fixed1584", "driven")
    else:
        raise SystemExit(f"unsupported capture version {version}")

    expected = HEADER_BYTES + len(stream_names) * stream_bytes
    if len(data) != expected:
        raise SystemExit(f"capture size {len(data)} != expected {expected}")

    print(
        f"version={version} samples_per_stream={sample_words} "
        f"nc=0x{header[4]:08x}..0x{header[5] - 1:08x} words={header[6]}"
    )

    streams: dict[str, tuple[int, ...]] = {}
    for index, name in enumerate(stream_names):
        offset = HEADER_BYTES + index * stream_bytes
        words = struct.unpack_from(f"<{sample_words}I", data, offset)
        streams[name] = words
        summarize(name, words)

    summarize_sweep_positions(streams["sweep"], header[4], header[6])

    driven = streams["driven"]
    exact = 0
    matching_bits = 0
    for index, word in enumerate(driven):
        pattern = 0xFFFFFFFF if index & 1 else 0
        exact += word == pattern
        matching_bits += 32 - (word ^ pattern).bit_count()
    print("[driven dependence]")
    print(f"exact_pattern={exact}/{len(driven)} ({exact / len(driven):.9f})")
    print(
        f"pattern_bit_agreement={matching_bits}/{32 * len(driven)} "
        f"({matching_bits / (32 * len(driven)):.9f})"
    )

    if args.nist_dir:
        args.nist_dir.mkdir(parents=True, exist_ok=True)
        for name, words in streams.items():
            write_nist_symbols(args.nist_dir, name, words)
        for name in args.nist_bitstream:
            if name not in streams:
                raise SystemExit(f"unknown stream for bitstring: {name}")
            write_nist_bitstream(args.nist_dir, name, streams[name])


if __name__ == "__main__":
    main()
