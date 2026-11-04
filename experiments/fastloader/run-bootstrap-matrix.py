#!/usr/bin/env python3
"""Exercise cold ROM loading, header autostart and BASIC/machine-code handoff."""
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
parser.add_argument("--out", type=Path, default=ROOT.parents[1] / "tools/fastloader-vice/bootstrap-matrix")
parser.add_argument("--jobs", type=int, default=4)
args = parser.parse_args()
if args.jobs < 1:
    parser.error("--jobs must be positive")
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
cases = [(f"{mode}-{video}", ["--mode", mode] + (["--ntsc"] if video == "ntsc" else []))
         for mode in ("basic", "machine") for video in ("pal", "ntsc")]
cases += [(f"boundary-{size}", ["--mode", "machine", "--size", str(size)])
          for size in (255, 256, 257, 513)]
cases += [("unaligned", ["--mode", "machine", "--size", "513", "--address", "0x20f9"])]
cases += [(f"maximum-{machine}", ["--machine", machine, "--mode", "machine",
                                "--address", hex(address), "--size", str(size)])
          for machine, address, size in (("c64", 0x0801, 30719),
                                         ("c16", 0x1001, 12287),
                                         ("plus4", 0x1001, 28671))]
cases += [(f"new-cia-{video}", ["--machine", "c64", "--cia-model", "1"] +
           (["--ntsc"] if video == "ntsc" else [])) for video in ("pal", "ntsc")]


def run(case):
    name, options = case
    directory = out / name
    directory.mkdir(exist_ok=True)
    with (directory / "runner.log").open("w") as log:
        status = subprocess.run([sys.executable, str(ROOT / "run-bootstrap-vice.py"),
                                 "--build", str(args.build.resolve()), "--roms", str(args.roms.resolve()),
                                 "--out", str(directory), *options], stdout=log, stderr=subprocess.STDOUT).returncode
    results_path = directory / "results.json"
    if not results_path.exists():
        raise RuntimeError(f"{name}: runner did not produce results; see {directory / 'runner.log'}")
    results = json.loads(results_path.read_text())
    for result in results:
        result.pop("log", None)
        result.update(case=name, check_passed=status == 0 and result["passed"])
    return results


with ThreadPoolExecutor(max_workers=args.jobs) as pool:
    summary = [result for results in pool.map(run, cases) for result in results]
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
passed = sum(result["check_passed"] for result in summary)
print(f"{passed}/{len(summary)} bootstrap checks passed; {out / 'summary.json'}")
raise SystemExit(0 if passed == len(summary) else 1)
