#!/usr/bin/env python3
# Still, let loose mine LLMs.

import argparse
import struct
from pathlib import Path


MAGIC = 0x30504454
HEADER_WORDS = 44
RX_CAPACITY = 256
TRACE_CAPACITY = 2048
RX_VALID = 1 << 8
RX_START_ERROR = 1 << 9
RX_PARITY_ERROR = 1 << 10
RX_STOP_ERROR = 1 << 11
RX_PHASE_SHIFT = 16
RESULT_SIZE = HEADER_WORDS * 4 + (RX_CAPACITY + TRACE_CAPACITY) * 8

HEADER_NAMES = (
    "magic",
    "version",
    "mode",
    "complete",
    "timer_hz",
    "duration_ticks",
    "clock_gpio",
    "data_gpio",
    "initial_levels",
    "final_levels",
    "timer2_before",
    "sharepin_con2_before",
    "sharepin_con1_before",
    "gpio_dir1_before",
    "gpio_out1_before",
    "gpio_pull1_before",
    "gpio_in1_before",
    "io_con1_before",
    "uart1_ctrl_before",
    "uart1_status_before",
    "uart1_cfg_before",
    "sharepin_con2_after",
    "sharepin_con1_after",
    "gpio_dir1_after",
    "gpio_out1_after",
    "gpio_pull1_after",
    "gpio_in1_after",
    "io_con1_after",
    "uart1_ctrl_after",
    "uart1_status_after",
    "uart1_cfg_after",
    "enable_send_status",
    "enable_reply",
    "disable_send_status",
    "disable_reply",
    "rx_count",
    "rx_frame_errors",
    "trace_count",
    "trace_dropped",
    "timer_running",
    "id_send_status",
    "status_send_status",
    "poll_send_failures",
    "poll_reply_errors",
)

SEND_STATUS = {
    0xFFFFFFFF: "not run",
    0: "ok",
    1: "idle-high timeout",
    2: "request clock-high timeout",
    3: "request clock-low timeout",
    4: "data clock-high timeout",
    5: "data clock-low timeout",
    6: "parity clock-high timeout",
    7: "parity clock-low timeout",
    8: "stop clock-high timeout",
    9: "stop clock-low timeout",
    10: "link-ack data-low timeout",
    11: "link-ack clock-high timeout",
    12: "link-ack data-high timeout",
}

PHASE_NAMES = {
    1: "enable reply",
    2: "data",
    3: "disable reply",
    4: "F2 reply",
    5: "device ID",
    6: "E9 reply",
    7: "status",
    8: "initial F5",
    9: "remote F0",
    10: "poll EB",
    11: "poll data",
    12: "stream EA",
    13: "final F5",
    14: "init reply",
    15: "init extra",
    16: "init data",
}


def level_text(levels: int) -> str:
    return f"CLK={(levels >> 0) & 1} DATA={(levels >> 1) & 1}"


def frame_text(value: int) -> str:
    if value == 0xFFFFFFFF:
        return "none"
    flags = []
    if value & RX_VALID:
        flags.append("valid")
    if value & RX_START_ERROR:
        flags.append("bad-start")
    if value & RX_PARITY_ERROR:
        flags.append("bad-parity")
    if value & RX_STOP_ERROR:
        flags.append("bad-stop")
    return f"0x{value & 0xFF:02x} ({', '.join(flags) or 'unclassified'})"


def timed_values(data: bytes, offset: int, count: int) -> list[tuple[int, int]]:
    return [struct.unpack_from("<II", data, offset + i * 8) for i in range(count)]


def decode_trace_frames(initial: int, trace: list[tuple[int, int]]) -> list[int]:
    falling_bits = []
    previous = initial
    for _, levels in trace:
        if (previous & 1) and not (levels & 1):
            falling_bits.append((levels >> 1) & 1)
        previous = levels

    frames = []
    index = 0
    while index + 11 <= len(falling_bits):
        bits = falling_bits[index : index + 11]
        parity = sum(bits[1:10]) & 1
        if bits[0] == 0 and parity == 1 and bits[10] == 1:
            frames.append(sum(bits[i + 1] << i for i in range(8)))
            index += 11
        else:
            index += 1
    return frames


def signed_delta(value: int, negative: bool) -> int:
    return value - 256 if negative else value


def decode_packets(values: list[int]) -> list[tuple[int, int, int]]:
    packets = []
    index = 0
    while index + 3 <= len(values):
        status, x_raw, y_raw = values[index : index + 3]
        if (status & 0x08) == 0:
            index += 1
            continue
        x = signed_delta(x_raw, bool(status & 0x10))
        y = signed_delta(y_raw, bool(status & 0x20))
        packets.append((status, x, y))
        index += 3
    return packets


def main() -> None:
    parser = argparse.ArgumentParser(description="Decode an AK7802 touchpad probe result")
    parser.add_argument("result", type=Path)
    args = parser.parse_args()

    data = args.result.read_bytes()
    if len(data) < RESULT_SIZE:
        raise SystemExit(f"short result: got {len(data)} bytes, need {RESULT_SIZE}")

    words = struct.unpack_from(f"<{HEADER_WORDS}I", data)
    header = dict(zip(HEADER_NAMES, words, strict=True))
    if header["magic"] != MAGIC:
        raise SystemExit(f"bad magic: 0x{header['magic']:08x}")

    rx_count = min(header["rx_count"], RX_CAPACITY)
    trace_count = min(header["trace_count"], TRACE_CAPACITY)
    rx_offset = HEADER_WORDS * 4
    trace_offset = rx_offset + RX_CAPACITY * 8
    rx = timed_values(data, rx_offset, rx_count)
    trace = timed_values(data, trace_offset, trace_count)
    timer_hz = header["timer_hz"]
    mode = {0: "passive", 1: "active", 2: "identify", 3: "poll", 4: "init"}.get(
        header["mode"], f"unknown-{header['mode']}"
    )

    print(
        f"mode: {mode}, complete: {header['complete']}, "
        f"timer running: {header['timer_running']}"
    )
    print(f"pins: CLK=GPIO{header['clock_gpio']}, DATA=GPIO{header['data_gpio']}")
    print(f"levels: initial {level_text(header['initial_levels'])}, final {level_text(header['final_levels'])}")
    print(f"trace: {trace_count} transitions, {header['trace_dropped']} dropped")
    print(
        "registers before: "
        f"mux2=0x{header['sharepin_con2_before']:08x} "
        f"mux1=0x{header['sharepin_con1_before']:08x} "
        f"dir1=0x{header['gpio_dir1_before']:08x} "
        f"out1=0x{header['gpio_out1_before']:08x} "
        f"in1=0x{header['gpio_in1_before']:08x}"
    )
    print(
        "registers after:  "
        f"mux2=0x{header['sharepin_con2_after']:08x} "
        f"mux1=0x{header['sharepin_con1_after']:08x} "
        f"dir1=0x{header['gpio_dir1_after']:08x} "
        f"out1=0x{header['gpio_out1_after']:08x} "
        f"in1=0x{header['gpio_in1_after']:08x}"
    )

    falling_edges = 0
    previous = header["initial_levels"]
    for _, levels in trace:
        falling_edges += int(bool(previous & 1) and not bool(levels & 1))
        previous = levels
    print(f"clock falling edges: {falling_edges}")

    if header["mode"] == 0:
        frames = decode_trace_frames(header["initial_levels"], trace)
        if frames:
            print("passive valid frames: " + " ".join(f"{value:02x}" for value in frames[:64]))
        else:
            print("passive valid frames: none")
        return

    if header["mode"] == 3:
        print(
            "remote F0: "
            f"{SEND_STATUS.get(header['enable_send_status'], 'unknown')} | "
            f"reply {frame_text(header['enable_reply'])}"
        )
        print(
            "stream EA: "
            f"{SEND_STATUS.get(header['disable_send_status'], 'unknown')} | "
            f"reply {frame_text(header['disable_reply'])}"
        )
        print(
            f"initial F5: {SEND_STATUS.get(header['id_send_status'], 'unknown')}, "
            f"final F5: {SEND_STATUS.get(header['status_send_status'], 'unknown')}"
        )
        print(
            f"poll failures: send={header['poll_send_failures']} "
            f"reply={header['poll_reply_errors']}"
        )
        poll_data = []
        poll_count = 0
        for _, value in rx:
            phase = (value >> RX_PHASE_SHIFT) & 0xFF
            if phase == 10 and value & RX_VALID:
                poll_count += 1
            elif phase == 11 and value & RX_VALID:
                poll_data.append(value & 0xFF)
        packets = decode_packets(poll_data)
        print(f"polls: {poll_count}, motion packets: {len(packets)}")
        for index, (status, x, y) in enumerate(packets):
            buttons = status & 0x07
            overflow = (status >> 6) & 0x03
            print(
                f"  {index:02d}: status=0x{status:02x} buttons=0x{buttons:x} "
                f"dx={x:+d} dy={y:+d} overflow=0x{overflow:x}"
            )
        return

    if header["mode"] == 4:
        failed_step = header["poll_send_failures"]
        if failed_step == 0xFFFFFFFF:
            print("initialization: complete")
        else:
            print(
                f"initialization: failed at step {failed_step}, "
                f"detail=0x{header['poll_reply_errors']:08x}"
            )
        print(
            "final F5: "
            f"{SEND_STATUS.get(header['disable_send_status'], 'unknown')} | "
            f"reply {frame_text(header['disable_reply'])}"
        )
        extra = [value & 0xFF for _, value in rx if ((value >> RX_PHASE_SHIFT) & 0xFF) == 15]
        if extra:
            print("initialization extra bytes: " + " ".join(f"{value:02x}" for value in extra))
        data_bytes = [
            value & 0xFF
            for _, value in rx
            if ((value >> RX_PHASE_SHIFT) & 0xFF) == 16 and value & RX_VALID
        ]
        packets = decode_packets(data_bytes)
        print(f"motion packets: {len(packets)}")
        for status, x, y in packets[:64]:
            print(f"  status=0x{status:02x} dx={x:+d} dy={y:+d}")
        return

    if header["mode"] == 2:
        print(
            "disable F5: "
            f"{SEND_STATUS.get(header['disable_send_status'], 'unknown')} | "
            f"reply {frame_text(header['disable_reply'])}"
        )
        print(f"identify F2: {SEND_STATUS.get(header['id_send_status'], 'unknown')}")
        print(f"status E9:   {SEND_STATUS.get(header['status_send_status'], 'unknown')}")
        values_by_phase = {}
        for ticks, value in rx:
            phase = (value >> RX_PHASE_SHIFT) & 0xFF
            values_by_phase.setdefault(phase, []).append(value)
            time_ms = ticks * 1000.0 / timer_hz
            print(f"  {time_ms:9.3f} ms  {PHASE_NAMES.get(phase, 'unknown'):13} {frame_text(value)}")

        ids = values_by_phase.get(5, [])
        if ids and ids[0] & RX_VALID:
            print(f"device ID: 0x{ids[0] & 0xff:02x}")
        status = [value & 0xFF for value in values_by_phase.get(7, []) if value & RX_VALID]
        if len(status) == 3:
            flags, resolution, sample_rate = status
            print(
                f"status: flags=0x{flags:02x} "
                f"remote={(flags >> 6) & 1} reporting={(flags >> 5) & 1} "
                f"scaling_2_1={(flags >> 4) & 1} "
                f"resolution={resolution} sample_rate={sample_rate}"
            )
        return

    print(
        "enable F4: "
        f"{SEND_STATUS.get(header['enable_send_status'], 'unknown')} | "
        f"reply {frame_text(header['enable_reply'])}"
    )
    print(
        "disable F5: "
        f"{SEND_STATUS.get(header['disable_send_status'], 'unknown')} | "
        f"reply {frame_text(header['disable_reply'])}"
    )
    print(f"received frames: {rx_count}, target frame errors: {header['rx_frame_errors']}")

    data_bytes = []
    for ticks, value in rx:
        phase = (value >> RX_PHASE_SHIFT) & 0xFF
        valid = bool(value & RX_VALID)
        if phase == 2 and valid:
            data_bytes.append(value & 0xFF)
        if len(rx) <= 64:
            time_ms = ticks * 1000.0 / timer_hz
            print(f"  {time_ms:9.3f} ms  {PHASE_NAMES.get(phase, 'unknown'):13} {frame_text(value)}")

    packets = decode_packets(data_bytes)
    print(f"motion packets: {len(packets)}")
    for status, x, y in packets[:32]:
        buttons = status & 0x07
        overflow = (status >> 6) & 0x03
        print(f"  status=0x{status:02x} buttons=0x{buttons:x} dx={x:+d} dy={y:+d} overflow=0x{overflow:x}")


if __name__ == "__main__":
    main()
