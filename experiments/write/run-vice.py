#!/usr/bin/env python3
"""Run real ROM SAVE with tape traps disabled, capture physical GPIO, replay it.

Uses the isolated VICE build shared with the fastloader experiment. No encoder
or decoder runs in VICE; only WRITE pin levels and MOTOR events are exported.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--machine", choices=["c64", "c16", "plus4", "all"], default="all")
parser.add_argument("--ntsc", action="store_true")
parser.add_argument("--name", default="TEST")
parser.add_argument("--out", type=Path, default=REPO / "tools/fastloader-vice/write-results")
parser.add_argument("--build", type=Path, default=REPO / "tools/fastloader-vice/build")
parser.add_argument("--roms", type=Path, default=Path("/usr/local/share/vice"))
parser.add_argument("--fixtures", type=Path, help="Export passing raw traces as compact delta-event fixtures")
args = parser.parse_args()
if not 1 <= len(args.name) <= 16 or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:" for c in args.name):
    parser.error("name must be 1..16 uppercase letters, digits or colons")
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
replay = out / "replay"
subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + str(REPO / "src"),
                str(ROOT / "replay.cpp"), str(REPO / "src/write_decoder.cpp"), "-o", str(replay)], check=True)
results = []
for machine in (["c64", "c16", "plus4"] if args.machine == "all" else [args.machine]):
    c16 = machine != "c64"
    case = out / (machine + ("-ntsc" if args.ntsc else "-pal") + "-" + args.name.replace(":", "-"))
    case.mkdir(exist_ok=True)
    trace, marker, monitor = (case / name for name in ("write.csv", "completed.bin", "monitor.txt"))
    marker.unlink(missing_ok=True)
    monitor.write_text('resourceset "TapePort1Device" "9"\n'
                       '> $07f1 00\n'
                       'watch store $07f1 if A == $a5\n'
                       f'keybuf 10 save"{args.name.lower()}",1:poke2033,165:end\\x0drun\\x0d\n'
                       'g\n'
                       f'bsave "{marker}" 0 $07f1 $07f1\n'
                       'quit\n')
    roms = args.roms.resolve() / ("PLUS4" if c16 else "C64")
    command = [str(args.build.resolve() / "src" / ("xplus4" if c16 else "x64sc")),
               "-default", "-model", machine + ("ntsc" if args.ntsc else ""),
               "+sound", "-warp", "-console", "-seed", "1", "+trapdevice1",
               "-limitcycles", "180000000", "-initbreak", "ready", "-moncommands", str(monitor),
               "-kernal", str(roms / ("kernal-318005-05.bin" if c16 and args.ntsc else
                                     "kernal-318004-05.bin" if c16 else "kernal-901227-03.bin")),
               "-basic", str(roms / ("basic-318006-01.bin" if c16 else "basic-901226-01.bin"))]
    if c16:
        command += ["-functionlo", str(roms / "3plus1-317053-01.bin"),
                    "-functionhi", str(roms / "3plus1-317054-01.bin")]
    else:
        command += ["-chargen", str(roms / "chargen-901225-01.bin")]
    env = os.environ.copy()
    env["ESPSETTE_WRITE_TRACE"] = str(trace)
    with (case / "vice.log").open("w") as log:
        status = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=45).returncode
    decoded = {}
    if trace.exists():
        with trace.open() as source:
            decoded = json.loads(subprocess.check_output([str(replay)], stdin=source, text=True))
    completed = marker.exists() and marker.read_bytes() == b"\xa5"
    expected_status = "command_received_no_action" if args.name.startswith("ESP:") else "header_ignored"
    passed = (status == 0 and completed and decoded.get("name") == args.name and
              decoded.get("headers") == 1 and decoded.get("errors") == 0 and
              decoded.get("status") == expected_status)
    result = dict(machine=machine, ntsc=args.ntsc, name=args.name, completed=completed,
                  trap_device1=False, decoded=decoded, passed=passed, trace=str(trace))
    results.append(result)
    print(json.dumps(result), flush=True)
    if passed and args.fixtures:
        args.fixtures.mkdir(parents=True, exist_ok=True)
        lines = trace.read_text().splitlines()
        previous, events = 0, []
        for line in lines[1:]:
            tick, signal, high = line.split(",")
            tick = int(tick)
            events.append([tick - previous, signal, int(high)])
            previous = tick
        fixture = dict(machine=machine, ntsc=args.ntsc, name=args.name,
                       vice_revision="eaf9b61f5560f921ebaa25349b09e797f351eb1e",
                       trace_sha256=hashlib.sha256(trace.read_bytes()).hexdigest(),
                       hz=int(lines[0].split(",")[1]), events=events)
        encoded = json.dumps(fixture, separators=(",", ":")).encode() + b"\n"
        (args.fixtures / (case.name + ".json.gz")).write_bytes(gzip.compress(encoded, mtime=0))
(out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
raise SystemExit(0 if all(r["passed"] for r in results) else 1)
