#!/usr/bin/env python3
"""Count paths in the assembled receiver, including addressing and branch pages.

Counts CPU cycles, not just source mnemonics. TED refresh is a separate bound:
5 stolen ticks per 114 ticks in VICE's blank-screen fast mode.
"""
import argparse
import itertools
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
# Opcode: (length, base cycles). Conditional branches are resolved below.
OPS = {
    0x05: (2, 3), 0x20: (3, 6), 0x26: (2, 5), 0x29: (2, 2), 0x2e: (3, 6),
    0x45: (2, 3), 0x48: (1, 3), 0x49: (2, 2), 0x4a: (1, 2), 0x4d: (3, 4),
    0x60: (1, 6), 0x68: (1, 4), 0x85: (2, 3), 0x8d: (3, 4),
    0x91: (2, 6), 0x99: (3, 5), 0xa2: (2, 2), 0xa5: (2, 3),
    0xa9: (2, 2), 0xad: (3, 4), 0xc0: (2, 2), 0xc6: (2, 5),
    0xc8: (1, 2), 0xca: (1, 2), 0xce: (3, 6), 0xd0: (2, 2),
    0xe6: (2, 5), 0xf0: (2, 2),
}
CLOCKS = {"c64": (985248, 1022730), "c16": (1773447, 1789772)}
PROFILES = {"c64": (48, 80), "c16": (26, 48)}


def analyze(c16, directory):
    family = "c16" if c16 else "c64"
    prg, symbols = directory / f"{family}.prg", directory / f"{family}.labels"
    subprocess.run(["acme", f"-DC16={c16}", "-f", "cbm", "-o", str(prg),
                    "-l", str(symbols), str(ROOT / "receiver.asm")], check=True)
    raw = prg.read_bytes()
    origin = int.from_bytes(raw[:2], "little")
    memory = dict(enumerate(raw[2:], origin))
    labels = {m[0]: int(m[1], 16) for m in re.findall(
        r"\b(\w+)\s*=\s*\$([0-9a-f]+)", symbols.read_text())}
    wait = labels["wait_read" if c16 else "wait_flag"]

    def instruction(pc):
        opcode = memory[pc]
        length, cycles = OPS[opcode]
        argument = sum(memory[pc + i] << (8 * (i - 1)) for i in range(1, length))
        return opcode, length, cycles, argument

    def walk(kind, address_carry=False, length_borrow=False):
        pc, cycles = wait, 0
        returns = [labels["header" if kind == "header" else "load"] + 3]
        samples = []
        branch_pages = []
        for step in range(200):
            if step and pc == wait:
                return cycles, samples, branch_pages
            opcode, size, cost, argument = instruction(pc)
            next_pc = pc + size
            if opcode in (0xd0, 0xf0):
                target = next_pc + (argument if argument < 128 else argument - 256)
                if target == labels["pair"] and memory.get(pc - 1) == 0xca:
                    take = kind == "pair"
                elif target == wait:
                    take = False  # Current read detected the strobe.
                elif target == labels["no_carry"]:
                    take = not address_carry
                elif target == labels["no_borrow"]:
                    take = not length_borrow
                elif target in (labels["load"], labels["header"], labels["have_block"]):
                    take = True
                else:
                    raise AssertionError(f"Unclassified branch at ${pc:04x}")
                if take:
                    cost += 1
                    if next_pc >> 8 != target >> 8:
                        cost += 1
                        branch_pages.append(pc)
                    next_pc = target
            elif opcode == 0x20:
                returns.append(next_pc)
                next_pc = argument
            elif opcode == 0x60:
                next_pc = returns.pop()
            cycles += cost
            if opcode in (0xa5, 0xad) and argument in (1, 0xfd10):
                samples.append({"address": hex(argument), "cycle": cycles})
            pc = next_pc
        raise AssertionError("Path failed to return to polling loop")

    pair, samples, pages = walk("pair")
    header, _, _ = walk("header")
    paths = [dict(address_carry=a, length_borrow=b, cycles=walk("byte", a, b)[0])
             for a, b in itertools.product((False, True), repeat=2)]
    poll_pc, poll = wait, 0
    while True:
        op, size, cost, _ = instruction(poll_pc)
        poll += cost
        poll_pc += size
        if op in (0xd0, 0xf0):
            poll += 1  # Taken polling branch, including a page crossing if any.
            poll += int(poll_pc >> 8 != wait >> 8)
            break
    poll_read_cycle = 3 if c16 else 4
    # C16's first $01 is READ polling; later $01 is WRITE, $FD10 is SENSE.
    data_samples = samples[1:] if c16 else samples
    latest_data = poll + max(s["cycle"] for s in data_samples) - poll_read_cycle

    def refresh_bound(cpu_cycles):
        return 5 * ((cpu_cycles + 108) // 109) if c16 else 0

    longest_byte = max(p["cycles"] for p in paths)
    result = dict(machine=family, clocks_hz=dict(zip(("pal", "ntsc"), CLOCKS[family])),
                  poll_cycles=poll, pair_service_cycles=pair,
                  header_byte_service_cycles=header, payload_byte_paths=paths,
                  latest_data_sample_cycles=latest_data,
                  branch_page_penalties=pages,
                  pair_wall_bound_ticks=pair + refresh_bound(pair),
                  byte_wall_bound_ticks=longest_byte + refresh_bound(longest_byte),
                  latest_sample_wall_bound_ticks=latest_data + refresh_bound(latest_data),
                  receiver_sha256=__import__("hashlib").sha256(raw).hexdigest())
    hold, pause = PROFILES[family]
    result["profile_us"] = dict(setup=3, hold=hold, gap=3, byte_pause=pause)
    margins = {}
    for video, hz in result["clocks_hz"].items():
        ticks = lambda us: (us * hz + 999999) // 1000000
        pair_ticks = ticks(hold) + 2 * ticks(3)
        byte_ticks = ticks(hold) + ticks(pause) + ticks(3)
        hold_ticks = ticks(hold) + ticks(3)
        assert pair_ticks >= result["pair_wall_bound_ticks"] + 2
        assert byte_ticks >= result["byte_wall_bound_ticks"] + 3
        assert hold_ticks >= result["latest_sample_wall_bound_ticks"] + 8
        # Do not rely on VICE rounding each microsecond interval up to a CPU
        # tick: the future ESP sender will use real wall-clock durations.
        pair_real = (hold + 6) * hz / 1000000
        byte_real = (hold + pause + 3) * hz / 1000000
        assert pair_real >= result["pair_wall_bound_ticks"] + 1.5
        assert byte_real >= result["byte_wall_bound_ticks"] + 2.5
        margins[video] = dict(pair_ticks=pair_ticks, byte_ticks=byte_ticks,
                              pair_margin=pair_ticks - result["pair_wall_bound_ticks"],
                              byte_margin=byte_ticks - result["byte_wall_bound_ticks"],
                              data_hold_margin=hold_ticks - result["latest_sample_wall_bound_ticks"],
                              pair_margin_without_rounding=round(pair_real - result["pair_wall_bound_ticks"], 3),
                              byte_margin_without_rounding=round(byte_real - result["byte_wall_bound_ticks"], 3))
    result["profile_margins_ticks"] = margins
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="ESPsette-timing-") as directory:
        results = [analyze(c16, Path(directory)) for c16 in (0, 1)]
    output = json.dumps(results, indent=2) + "\n"
    print(output, end="")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output)
