#!/usr/bin/env python3
"""Dump ESP8266 ROM boot UART at 74880 baud; DTR reset like esptool.cfg."""

from __future__ import annotations

import argparse
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.stderr.write("Need pyserial:  python3 -m pip install pyserial\n")
    sys.exit(1)

# Match esptool.cfg custom_hard_reset_sequence = D0|W0.05|D1|W0.05|D0
# (DTR-only CH340 → RST; D0=deassert, D1=assert)
HARD_RESET = (("dtr", False), ("wait", 0.05), ("dtr", True), ("wait", 0.05), ("dtr", False))
BAUD = 74880


def apply_sequence(ser: serial.Serial, steps) -> None:
    for kind, value in steps:
        if kind == "dtr":
            ser.dtr = value
        elif kind == "wait":
            time.sleep(value)


def dump_until(ser: serial.Serial, seconds: float) -> bytes:
    deadline = time.monotonic() + seconds
    chunks: list[bytes] = []
    while time.monotonic() < deadline:
        waiting = ser.in_waiting
        if waiting:
            chunks.append(ser.read(waiting))
        else:
            time.sleep(0.01)
    return b"".join(chunks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        default=os.environ.get("PORT", "/dev/cu.usbserial-120"),
        help="Serial device (default: $PORT or /dev/cu.usbserial-120)",
    )
    parser.add_argument("-n", "--resets", type=int, default=3, help="How many hard resets")
    parser.add_argument("-t", "--seconds", type=float, default=1.5, help="Capture window after each reset")
    parser.add_argument("-i", "--interactive", action="store_true", help="Prompt Enter between resets")
    args = parser.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = BAUD
    ser.bytesize = serial.EIGHTBITS
    ser.parity = serial.PARITY_NONE
    ser.stopbits = serial.STOPBITS_ONE
    ser.timeout = 0.05
    # Idle DTR like end of esptool sequence (released)
    ser.dtr = False
    ser.rts = False
    ser.open()

    print(f"port={ser.port} baud={BAUD} resets={args.resets}", file=sys.stderr)
    try:
        for i in range(1, args.resets + 1):
            if args.interactive:
                input(f"[{i}/{args.resets}] Enter to hard-reset… ")
            else:
                print(f"\n=== reset {i}/{args.resets} ===", file=sys.stderr)
                time.sleep(0.2)

            ser.reset_input_buffer()
            apply_sequence(ser, HARD_RESET)
            data = dump_until(ser, args.seconds)
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
            if data and not data.endswith(b"\n"):
                sys.stdout.buffer.write(b"\n")
                sys.stdout.buffer.flush()
            if not data:
                print("(no bytes)", file=sys.stderr)
    finally:
        ser.dtr = False
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
