#!/usr/bin/env python3
"""Cold KERNAL LOAD -> header autostart -> low-RAM receiver -> PRG execution.

The monitor only types LOAD and observes execution/memory. It never loads the
bootstrap or receiver into RAM, and never switches the device's runtime mode.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--machine", choices=["c64", "c16", "plus4", "all"], default="all")
parser.add_argument("--ntsc", action="store_true")
parser.add_argument("--mode", choices=["basic", "machine"], default="basic")
parser.add_argument("--size", type=int, default=4096)
parser.add_argument("--out", type=Path, default=REPO / "tools/fastloader-vice/bootstrap-results")
parser.add_argument("--build", type=Path, default=REPO / "tools/fastloader-vice/build")
parser.add_argument("--roms", type=Path, default=Path("/usr/local/share/vice"))
parser.add_argument("--address", type=lambda x: int(x, 0), help="Machine-code fixture load/entry address")
parser.add_argument("--cia-model", type=int, choices=[0, 1], default=0)
parser.add_argument("--prg", type=Path, help="Test an original C64 PAL BASIC PRG instead of a synthetic fixture")
parser.add_argument("--expect-pc", type=lambda x: int(x, 0), help="Expected SYS entry for --prg")
args = parser.parse_args()
if args.prg and (args.machine != 'c64' or args.ntsc or args.mode != 'basic' or args.expect_pc is None):
    parser.error('--prg requires --machine c64, PAL, --mode basic and --expect-pc')
if args.expect_pc is not None and not args.prg:
    parser.error('--expect-pc requires --prg')
if not 13 <= args.size <= 30719:
    parser.error("machine-code fixture size must be 13..30719")
if args.address is not None and args.mode != "machine":
    parser.error("--address is only available for the machine-code fixture")
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
exporter = out / "make-bootstrap"
subprocess.run(["c++", "-std=c++17", "-I" + str(REPO / "tests/support"), "-I" + str(REPO / "src"),
                str(ROOT / "make-bootstrap.cpp"), str(REPO / "src/prg_encoder.cpp"),
                str(REPO / "src/tape_source.cpp"), "-o", str(exporter)], check=True)


def basic_program(address):
    data = bytearray()
    for line, body in [(10, b"\x972032,90:\x972033,165"), (20, b"\x8920")]:
        following = address + len(data) + 5 + len(body)
        data += following.to_bytes(2, "little") + line.to_bytes(2, "little") + body + b"\0"
    return bytes(data + b"\0\0")


results = []
for machine in (["c64", "c16", "plus4"] if args.machine == "all" else [args.machine]):
    c16 = machine != "c64"
    case = out / (machine + ("-ntsc" if args.ntsc else "-pal"))
    case.mkdir(exist_ok=True)
    stage_address = 0x0800 if c16 else 0x0400
    stage_prg, stage_labels = case / "receiver.prg", case / "receiver.labels"
    subprocess.run(["acme", f"-DC16={int(c16)}", "-DBOOT=1", f"-DORIGIN={stage_address}",
                    "-f", "cbm", "-o", str(stage_prg), "-l", str(stage_labels),
                    str(ROOT.parents[1] / "native/fastloader/receiver.asm")], check=True)
    stage = stage_prg.read_bytes()[2:]
    stage_pages = (len(stage) + 255) // 256
    padded = stage.ljust(stage_pages * 256, b"\0")
    stage_raw = case / "receiver-padded.bin"
    stage_raw.write_bytes(padded)
    stub = case / "bootstrap.prg"
    subprocess.run(["acme", f"-DC16={int(c16)}", f"-DSTAGE_PAGES={stage_pages}",
                    f"-DSTAGE_ADDR={stage_address}", "-f", "cbm", "-o", str(stub),
                    str(ROOT.parents[1] / "native/fastloader/bootstrap.asm")], check=True)
    early_input = bytes([0x78, 0xa9, 0x09 if c16 else 0x27, 0x85, 0x00])
    if stub.read_bytes()[2:7] != early_input:
        raise RuntimeError("bootstrap must release WRITE within its first seven CPU cycles")
    periods, tap = case / "standard-periods.bin", case / "bootstrap.tap"
    subprocess.run([str(exporter), "c16" if c16 else "c64", "ntsc" if args.ntsc else "pal",
                    str(stub), str(periods), str(tap), "ESPSETTE BOOT"], check=True)
    address = (0x1001 if c16 else 0x0801) if args.mode == "basic" else 0x2000
    if args.address is not None:
        address = args.address
    if args.prg:
        raw = args.prg.read_bytes()
        address, payload, entry = int.from_bytes(raw[:2], 'little'), raw[2:], 0
    elif args.mode == "basic":
        payload = basic_program(address)
        entry = 0
    else:
        code = bytes.fromhex("a9 5a 8d f0 07 a9 a5 8d f1 07")
        payload = code + b"\x4c" + (address + len(code)).to_bytes(2, "little")
        payload += bytes((i * 73) & 255 for i in range(max(0, args.size - len(payload))))
        entry = address
    low, high = (0x1001, 0x4000 if machine == "c16" else 0x8000) if c16 else (0x0801, 0x8000)
    if not low <= address < address + len(payload) <= high:
        parser.error(f"{machine}: payload must fit ${low:04x}..${high-1:04x}")
    stream = case / "stream.bin"
    stream.write_bytes(address.to_bytes(2, "little") + len(payload).to_bytes(2, "little") +
                       entry.to_bytes(2, "little") + payload)
    stage_dump, payload_dump, marker_dump = (case / name for name in ("stage-loaded.bin", "payload.bin", "marker.bin"))
    for path in (stage_dump, payload_dump, marker_dump):
        path.unlink(missing_ok=True)
    commands = case / "monitor.txt"
    commands.write_text('resourceset "TapePort1Device" "8"\n'
                        '> $07f0 00 00\n'
                        f'break ${stage_address:04x}\n' +
                        (f'break ${args.expect_pc:04x}\n' if args.prg else 'watch store $07f1 if A == $a5\n') +
                        'keybuf load\\x0d\n'
                        'g\n'
                        f'bsave "{stage_dump}" 0 ${stage_address:04x} ${stage_address+len(padded)-1:04x}\n'
                        'delete 1\n'
                        'g\n'
                        f'bsave "{payload_dump}" 0 ${address:04x} ${address+len(payload)-1:04x}\n'
                        f'bsave "{marker_dump}" 0 $07f0 $07f1\n'
                        'r\nquit\n')
    roms = args.roms.resolve() / ("PLUS4" if c16 else "C64")
    model = machine + ("ntsc" if args.ntsc else "")
    command = [str(args.build.resolve() / "src" / ("xplus4" if c16 else "x64sc")),
               "-default", "-model", model, "+sound", "-warp", "-console", "-seed", "1",
               "+trapdevice1", "-limitcycles", "150000000", "-initbreak", "ready",
               "-moncommands", str(commands),
               "-kernal", str(roms / ("kernal-318005-05.bin" if c16 and args.ntsc else
                                     "kernal-318004-05.bin" if c16 else "kernal-901227-03.bin")),
               "-basic", str(roms / ("basic-318006-01.bin" if c16 else "basic-901226-01.bin"))]
    if c16:
        command += ["-functionlo", str(roms / "3plus1-317053-01.bin"),
                    "-functionhi", str(roms / "3plus1-317054-01.bin")]
    else:
        command += ["-chargen", str(roms / "chargen-901225-01.bin"), "-cia1model", str(args.cia_model)]
    env = os.environ.copy()
    env.update(ESPSETTE_FL_STREAM=str(stream), ESPSETTE_FL_BOOT_PERIODS=str(periods),
               ESPSETTE_FL_BOOT_RECEIVER=str(stage_raw))
    log = case / "vice.log"
    with log.open("w") as output:
        try:
            status = subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT, timeout=45).returncode
        except subprocess.TimeoutExpired:
            status = None
    text = log.read_text(errors="replace")
    actual = payload_dump.read_bytes() if payload_dump.exists() else b""
    marker = marker_dump.read_bytes() if marker_dump.exists() else b""
    stage_ok = stage_dump.exists() and stage_dump.read_bytes() == padded
    result = dict(machine=machine, ntsc=args.ntsc, mode=args.mode, address=address,
                  payload_bytes=len(payload), bootstrap_bytes=len(stub.read_bytes())-2,
                  receiver_bytes=len(stage), stage_pages=stage_pages,
                  standard_rom_load=True, trap_device1=False,
                  signature_seen="signature=ca65" in text, stage_ok=stage_ok,
                  payload_ok=actual == payload,
                  executed=bool(re.search(rf'\.C:{args.expect_pc:04x}\b', text, re.I)) if args.prg else marker == bytes([90, 165]),
                  returncode=status, payload_sha256=hashlib.sha256(payload).hexdigest(), log=str(log))
    result["passed"] = status == 0 and all(result[key] for key in ("signature_seen", "stage_ok", "payload_ok", "executed"))
    results.append(result)
    print(json.dumps(result), flush=True)
(out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
raise SystemExit(0 if all(r["passed"] for r in results) else 1)
