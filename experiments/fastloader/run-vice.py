#!/usr/bin/env python3
"""Run actual 6502 receivers against the portable C sender in patched VICE."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=REPO / "tools/fastloader-vice/build")
parser.add_argument("--roms", type=Path, default=Path("/usr/local/share/vice"))
parser.add_argument("--out", type=Path, default=REPO / "tools/fastloader-vice/results")
parser.add_argument("--machine", choices=["c64", "plus4", "c16", "all"], default="all")
parser.add_argument("--hold-us", type=int)
parser.add_argument("--motor-off-us", type=int, default=1000)
parser.add_argument("--motor-on-us", type=int, default=5)
parser.add_argument("--pause-us", type=int, help="Interbyte pause (default: C64 19 us, TED 11 us)")
parser.add_argument("--cia-model", type=int, choices=[0, 1], default=0, help="C64: old 6526 or new 8521")
parser.add_argument("--block-bytes", type=int, choices=[1, 256], default=256)
parser.add_argument("--jitter-us", type=int, default=0)
parser.add_argument("--phase-ticks", type=int, default=0, help="Delay first request to vary raster/poll phase")
parser.add_argument("--seed", type=int, default=1)
parser.add_argument("--pattern", choices=["mixed", "random", "zero", "ones", "alternating"], default="mixed")
parser.add_argument("--size", type=int, default=4096)
parser.add_argument("--address", type=lambda value: int(value, 0), default=0x2000)
parser.add_argument("--prg", type=Path, help="Use a real PRG instead of generated test data")
parser.add_argument("--ntsc", action="store_true")
parser.add_argument("--trace", action="store_true")
args = parser.parse_args()
if not 1 <= args.size <= 0x7000:
    parser.error("size must be 1..28672; each machine's address range is checked separately")
for value in (args.hold_us, args.motor_off_us, args.motor_on_us, args.phase_ticks,
              args.pause_us, args.jitter_us):
    if value is not None and value < 0:
        parser.error("timing arguments must be nonnegative")
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
machines = ["c64", "plus4", "c16"] if args.machine == "all" else [args.machine]
results = []
for machine in machines:
    pause_us = args.pause_us if args.pause_us is not None else (19 if machine == "c64" else 11)
    hold_us = args.hold_us if args.hold_us is not None else 16
    name = machine + ("-ntsc" if args.ntsc else "-pal")
    case = out / name
    case.mkdir(exist_ok=True)
    receiver = case / "receiver.prg"
    labels = case / "labels.txt"
    subprocess.run(["acme", f"-DC16={int(machine != 'c64')}", f"-DBLOCK_SIZE={args.block_bytes}", "-f", "cbm",
                    "-o", str(receiver), "-l", str(labels), str(ROOT / "receiver.asm")],
                   check=True)
    done = int(re.search(r"\bdone\s*=\s*\$([0-9a-f]+)", labels.read_text())[1], 16)
    # Deliberately exercise every byte value and adjacent repeated data pairs.
    payload = bytes(((i * 73) ^ (i >> 3) ^ (i >> 8)) & 255 for i in range(args.size))
    if args.pattern == "random":
        import random
        payload = random.Random(args.seed).randbytes(args.size)
    elif args.pattern == "zero":
        payload = bytes(args.size)
    elif args.pattern == "ones":
        payload = bytes([255]) * args.size
    elif args.pattern == "alternating":
        payload = bytes(0x55 if i & 1 else 0xaa for i in range(args.size))
    destination = args.address
    if args.prg:
        prg = args.prg.read_bytes()
        if len(prg) < 3:
            parser.error("PRG must contain a load address and a nonempty payload")
        destination, payload = int.from_bytes(prg[:2], "little"), prg[2:]
    limit = 0x4000 if machine == "c16" else 0x8000
    if destination < 0x1000 or destination + len(payload) > limit:
        parser.error(f"{machine}: experiment supports payloads in $1000..${limit - 1:04x}")
    stream = case / "stream.bin"
    stream.write_bytes(destination.to_bytes(2, "little") +
                       len(payload).to_bytes(2, "little") +
                       destination.to_bytes(2, "little") + payload)
    dump = case / "memory.bin"
    dump.unlink(missing_ok=True)
    commands = case / "monitor.txt"
    commands.write_text(f'load "{receiver}" 0\n'
                        'resourceset "TapePort1Device" "8"\n'
                        f'break ${done:04x}\n'
                        'g $0800\n'
                        f'bsave "{dump}" 0 ${destination:04x} ${destination + len(payload) - 1:04x}\n'
                        'quit\n')
    family = "C64" if machine == "c64" else "PLUS4"
    roms = args.roms.resolve() / family
    executable = args.build.resolve() / "src" / ("x64sc" if machine == "c64" else "xplus4")
    model = ("c64ntsc" if args.ntsc else "c64") if machine == "c64" else (
        ("c16ntsc" if args.ntsc else "c16") if machine == "c16" else
        ("plus4ntsc" if args.ntsc else "plus4"))
    command = [str(executable), "-default", "-model", model,
               "-kernal", str(roms / ("kernal-901227-03.bin" if machine == "c64" else
                                     ("kernal-318005-05.bin" if args.ntsc else "kernal-318004-05.bin"))),
               "-basic", str(roms / ("basic-901226-01.bin" if machine == "c64" else "basic-318006-01.bin")),
               "+sound", "-warp", "-console", "-seed", str(args.seed), "-limitcycles",
               str(max(2000000, (len(payload) + 6) * 6 * (
                   max(args.motor_on_us, args.motor_off_us) + pause_us +
                   4 * (hold_us + 6 +
                        args.jitter_us) + args.jitter_us + 100))),
               "-moncommands", str(commands)]
    if machine == "c64":
        command += ["-chargen", str(roms / "chargen-901225-01.bin"), "-cia1model", str(args.cia_model)]
    else:
        command += ["-functionlo", str(roms / "3plus1-317053-01.bin"),
                    "-functionhi", str(roms / "3plus1-317054-01.bin")]
    env = os.environ.copy()
    env.update(ESPSETTE_FL_STREAM=str(stream),
               ESPSETTE_FL_PHASE_TICKS=str(args.phase_ticks),
               ESPSETTE_FL_BLOCK_BYTES=str(args.block_bytes),
               ESPSETTE_FL_MOTOR_OFF_US=str(args.motor_off_us),
               ESPSETTE_FL_MOTOR_ON_US=str(args.motor_on_us),
               ESPSETTE_FL_PAUSE_US=str(pause_us),
               ESPSETTE_FL_HOLD_US=str(hold_us),
               ESPSETTE_FL_JITTER_US=str(args.jitter_us))
    if args.trace:
        env["ESPSETTE_FL_TRACE"] = str(case / "signals.csv")
    log = case / "vice.log"
    timed_out = False
    with log.open("w") as output:
        try:
            process = subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT,
                                     timeout=45)
            returncode = process.returncode
        except subprocess.TimeoutExpired:
            returncode = None
            timed_out = True
    actual = dump.read_bytes() if dump.exists() else b""
    text = log.read_text(errors="replace")
    stats = re.search(r"ESPSETTE_FL sent=(\d+) total=(\d+) ticks=(\d+) hz=(\d+) requests=(\d+)", text)
    result = dict(machine=machine, ntsc=args.ntsc, size=len(payload), address=destination,
                  hold_us=hold_us,
                  motor_off_us=args.motor_off_us, motor_on_us=args.motor_on_us,
                  pause_us=pause_us, jitter_us=args.jitter_us,
                  block_bytes=args.block_bytes,
                  phase_ticks=args.phase_ticks, seed=args.seed, pattern=args.pattern,
                  cia_model=args.cia_model if machine == "c64" else None,
                  passed=returncode == 0 and actual == payload,
                  returncode=returncode, timed_out=timed_out,
                  payload_sha256=hashlib.sha256(payload).hexdigest(), log=str(log))
    if stats:
        sent, total, ticks, hz, requests = map(int, stats.groups())
        expected_requests = 1 + (len(payload) + args.block_bytes - 1) // args.block_bytes
        result["passed"] &= requests == expected_requests
        result.update(sent=sent, total=total, ticks=ticks, hz=hz,
                      requests=requests, expected_requests=expected_requests,
                      bytes_per_second=round(len(payload) * hz / ticks, 1) if ticks else None)
    results.append(result)
    print(json.dumps(result), flush=True)
(out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
raise SystemExit(0 if all(r["passed"] for r in results) else 1)
