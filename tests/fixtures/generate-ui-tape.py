#!/usr/bin/env python3
"""Generate synthetic TAP/IDX files for manual browser player checks."""
from pathlib import Path
import struct

CLOCK_HZ = 985248
SECTION_SECONDS = 20
PATTERNS = ((48, 67, 85), (48, 48, 85), (85, 67, 85),
            (67, 48, 67), (85, 85, 48), (48, 67, 48))
NAMES = ("Start", "Second section", "Third section", "Halfway", "Fifth section", "Last section")
root = Path(__file__).resolve().parent
payload = bytearray()
entries = []

for section, pattern in enumerate(PATTERNS):
    entries.append((20 + len(payload), section * SECTION_SECONDS, NAMES[section]))
    # Reserve a quarter-second extended pulse at the end of each section.
    remaining = SECTION_SECONDS * CLOCK_HZ - CLOCK_HZ // 4
    pulse = 0
    while remaining:
        cycles = min(pattern[pulse % len(pattern)] * 8, remaining)
        if cycles % 8 == 0 and 0 < cycles // 8 < 256:
            payload.append(cycles // 8)
        else:
            payload.extend(b"\0" + cycles.to_bytes(3, "little"))
        remaining -= cycles
        pulse += 1
    payload.extend(b"\0" + (CLOCK_HZ // 4).to_bytes(3, "little"))

header = bytearray(20)
header[:12] = b"C64-TAPE-RAW"
header[12] = 1
struct.pack_into("<I", header, 16, len(payload))
(root / "ui-test.tap").write_bytes(header + payload)
lines = ["# Synthetic C64 PAL TAP v1, 02:00, six indexed sections.",
         "# UI test signal only; contains no Commodore program."]
for offset, seconds, name in entries:
    lines.append(f"0x{offset:08X} {seconds // 60:02}:{seconds % 60:02} {name}")
(root / "ui-test.idx").write_text("\n".join(lines) + "\n", encoding="ascii")
print(f"ui-test.tap: {len(header) + len(payload)} bytes, 02:00, {len(entries)} IDX positions")
