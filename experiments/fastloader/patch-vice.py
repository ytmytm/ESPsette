#!/usr/bin/env python3
"""Apply the isolated tape-port experiment to a disposable VICE source tree."""
import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent
source = args.source.resolve()

patch = root / "vice-patches/espsette-fastloader.patch"
command = ["patch", "--batch", "-p2", "-d", str(source), "-i", str(patch)]
# Check the complete patch before mutation. A reverse dry run recognizes an
# already patched development tree; the model sources are refreshed below.
forward = subprocess.run([*command, "--forward", "--dry-run"], capture_output=True, text=True)
if forward.returncode == 0:
    subprocess.run([*command, "--forward"], check=True)
else:
    reverse = subprocess.run([*command, "--reverse", "--dry-run"], capture_output=True, text=True)
    if reverse.returncode != 0:
        raise SystemExit("VICE source does not match the integration patch:\n" + forward.stdout + forward.stderr)

tape = source / "src/tapeport"
for name in ("sender.c", "sender.h", "vice-device.c"):
    shutil.copyfile(root / name, tape / name)
print(f"Patched disposable VICE source: {source}")
