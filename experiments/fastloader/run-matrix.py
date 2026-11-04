#!/usr/bin/env python3
"""Regression matrix, including negative controls that must fail to receive."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=ROOT.parents[1] / "tools/fastloader-vice/build")
parser.add_argument("--roms", type=Path, default=Path("/usr/local/share/vice"))
parser.add_argument("--out", type=Path, default=ROOT.parents[1] / "tools/fastloader-vice/matrix")
args = parser.parse_args()
cases = [
    ("baseline-pal", [], True),
    ("baseline-ntsc", ["--ntsc"], True),
    ("no-motor-delay", ["--motor-on-us", "0", "--motor-off-us", "0"], True),
    ("byte-sync-control", ["--block-bytes", "1"], True),
    ("stalls-and-jitter", ["--size", "513", "--address", "0x20f9", "--pause-us", "50000",
                          "--jitter-us", "50"], True),
    ("slow-motor", ["--size", "513", "--motor-off-us", "5000", "--motor-on-us", "200"], True),
    ("one-byte", ["--size", "1", "--address", "0x3fff"], True),
    *[(f"boundary-{size}", ["--size", str(size), "--address", "0x20f9"], True)
      for size in (255, 256, 257, 512, 513)],
    ("unsafe-c64", ["--size", "256", "--machine", "c64", "--hold-us", "1",
                    "--motor-off-us", "0"], False),
    ("unsafe-c16", ["--size", "256", "--machine", "c16", "--hold-us", "1",
                    "--motor-off-us", "0"], False),
    ("unsafe-byte-gap", ["--size", "257", "--machine", "c64", "--pause-us", "0"], False),
]
summary = []
ok = True
for name, options, expected in cases:
    case_out = args.out.resolve() / name
    command = [sys.executable, str(ROOT / "run-vice.py"), "--build", str(args.build),
               "--roms", str(args.roms), "--out", str(case_out), *options]
    process = subprocess.run(command)
    results = json.loads((case_out / "results.json").read_text())
    # A failed negative control must come from a running sender, not a missing
    # executable, ROM, parser error or wall-clock timeout.
    case_ok = process.returncode == (0 if expected else 1) and all(
        r["passed"] == expected and not r["timed_out"] and
        r.get("total") == r["size"] + 6 and (expected or r.get("sent", 0) > 0) and
        (not expected or r.get("requests") == r.get("expected_requests"))
        for r in results)
    for result in results:
        result.pop("log", None)
        result.update(case=name, expected_pass=expected, check_passed=case_ok)
        summary.append(result)
    ok &= case_ok
args.out.mkdir(parents=True, exist_ok=True)
(args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(f"{len(summary)} integration checks: {'PASS' if ok else 'FAIL'}")
raise SystemExit(0 if ok else 1)
