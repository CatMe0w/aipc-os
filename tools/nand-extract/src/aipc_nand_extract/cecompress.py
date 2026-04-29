from __future__ import annotations

from dataclasses import dataclass

from ._vendor import wincedecompr

CECOMPRESS_FLAG = 0x00002000
CECOMPRESS_BLOCK_SIZE = 0x1000


class CECompressError(ValueError):
    pass


def _read_u24le(data: bytes | bytearray | memoryview, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16)


@dataclass(frozen=True)
class _CECompressLayout:
    uncompressed_size: int


def parse_cecompress_layout(data: bytes, expected_size: int | None = None) -> _CECompressLayout | None:
    if len(data) < 6:
        return None

    uncompressed_size = _read_u24le(data, 0)
    if uncompressed_size <= 0:
        return None
    if expected_size is not None and uncompressed_size > expected_size:
        return None

    block_count = ((uncompressed_size - 1) >> 12) + 1
    table_entries = block_count + 1
    table_size = table_entries * 3
    if table_size > len(data):
        return None

    block_ends = tuple(_read_u24le(data, index * 3) for index in range(1, table_entries))
    previous = table_size
    remaining = uncompressed_size
    for end in block_ends:
        if end < previous or end > len(data):
            return None
        if end - previous < 16:
            return None
        window_bits = int.from_bytes(data[previous : previous + 4], "little")
        block_uncompressed_size = int.from_bytes(data[previous + 4 : previous + 8], "little")
        block_compressed_size = int.from_bytes(data[previous + 8 : previous + 12], "little")
        if not 15 <= window_bits <= 21:
            return None
        if block_uncompressed_size != min(CECOMPRESS_BLOCK_SIZE, remaining):
            return None
        if block_compressed_size > end - previous - 16:
            return None
        remaining -= block_uncompressed_size
        previous = end
    if block_ends[-1] != len(data):
        return None
    if remaining != 0:
        return None

    return _CECompressLayout(uncompressed_size=uncompressed_size)


def maybe_decompress_cecompress(data: bytes, expected_size: int | None = None) -> bytes | None:
    layout = parse_cecompress_layout(data, expected_size)
    if layout is None:
        return None
    try:
        return decompress_cecompress(data, expected_size, layout)
    except Exception:
        return None


def decompress_cecompress(data: bytes, expected_size: int | None = None, layout: _CECompressLayout | None = None) -> bytes:
    layout = layout if layout is not None else parse_cecompress_layout(data, expected_size)
    if layout is None:
        raise CECompressError("invalid CECOMPRESS block table")

    output = bytearray(layout.uncompressed_size + CECOMPRESS_BLOCK_SIZE)
    size = wincedecompr.CEDecompressROM(data, len(data), output, layout.uncompressed_size, 0, 1, CECOMPRESS_BLOCK_SIZE)
    if size < 0:
        raise CECompressError("CEDecompressROM failed")
    if size != layout.uncompressed_size:
        raise CECompressError(f"CECOMPRESS size mismatch: got {size}, expected {layout.uncompressed_size}")
    if expected_size is not None and size > expected_size:
        raise CECompressError(f"CECOMPRESS output exceeds section virtual size: {size} > {expected_size}")
    return bytes(output[:size])
