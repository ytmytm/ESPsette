#!/usr/bin/env python3
"""Stress shorter timing over initial phases, address carries, data and jitter."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=ROOT.parents[1] / "tools/fastloader-vice/build")
parser.add_argument("--roms", type=Path, default=Path("/usr/local/share/vice"))
parser.add_argument("--out", type=Path, default=ROOT.parents[1] / "tools/fastloader-vice/timing-matrix")
parser.add_argument("--jobs", type=int, default=4)
args = parser.parse_args()
cases = []
for machine in ("c64", "plus4", "c16"):
    for ntsc in (False, True):
        video = "ntsc" if ntsc else "pal"
        for phase in range(18 if machine == "c64" else 114):
            name = f"phase-{machine}-{video}-{phase}"
            cases.append((name, machine, ntsc, ["--size", "513", "--address", "0x20fe",
                                               "--phase-ticks", str(phase)]))
        for pattern in ("mixed", "zero", "ones", "alternating", "random"):
            for jitter in (0, 1, 50):
                name = f"data-{machine}-{video}-{pattern}-{jitter}"
                cases.append((name, machine, ntsc, ["--size", "8192", "--address", "0x10ff",
                                                   "--pattern", pattern, "--jitter-us", str(jitter),
                                                   "--seed", "1729", "--motor-on-us", "0",
                                                   "--motor-off-us", "0"]))


def run(case):
    name, machine, ntsc, options = case
    out = args.out.resolve() / name
    command = [sys.executable, str(ROOT / "run-vice.py"), "--build", str(args.build.resolve()),
               "--roms", str(args.roms.resolve()), "--out", str(out), "--machine", machine,
               *( ["--ntsc"] if ntsc else []), *options]
    process = subprocess.run(command, stdout=subprocess.DEVNULL)
    result = json.loads((out / "results.json").read_text())[0]
    result["case"] = name
    result["check_passed"] = (process.returncode == 0 and result["passed"] and
                              result.get("sent") == result.get("total") == result["size"] + 6 and
                              result.get("requests") == result.get("expected_requests"))
    result.pop("log", None)
    if not result["check_passed"]:
        print(f"FAIL: {name}", flush=True)
    return result


with ThreadPoolExecutor(max_workers=args.jobs) as pool:
    results = []
    for count, result in enumerate(pool.map(run, cases), 1):
        results.append(result)
        if count % 50 == 0:
            print(f"{count}/{len(cases)} timing checks completed", flush=True)
args.out.mkdir(parents=True, exist_ok=True)
(args.out / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
ok = all(r["check_passed"] for r in results)
print(f"{len(results)} timing checks: {'PASS' if ok else 'FAIL'}")
raise SystemExit(0 if ok else 1)
