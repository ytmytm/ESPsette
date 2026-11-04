#!/usr/bin/env python3
"""Build an isolated, pinned VICE with the ESPsette tape-port experiment.

Requires a local checkout of https://github.com/VICE-Team/svn-mirror.
The checkout, including any Parobek patches, is only read by git archive.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
PIN = "eaf9b61f5560f921ebaa25349b09e797f351eb1e"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--repository", required=True, type=Path)
parser.add_argument("--revision", default=PIN)
parser.add_argument("--root", type=Path, default=ROOT.parents[1] / "tools/fastloader-vice")
parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 2))
args = parser.parse_args()
base = args.root.resolve()
source, build = base / "source", base / "build"
source.mkdir(parents=True, exist_ok=True)
build.mkdir(exist_ok=True)
revision = subprocess.check_output(["git", "-C", str(args.repository.resolve()),
                                    "rev-parse", args.revision], text=True).strip()
stamp = base / "revision.txt"
if stamp.exists() and stamp.read_text().strip() != revision:
    raise SystemExit("Build root already contains a different revision; use a fresh --root")
if not (source / "src/tapeport/tapeport.c").exists():
    archive = subprocess.Popen(["git", "-C", str(args.repository.resolve()),
                                "archive", revision + ":vice"], stdout=subprocess.PIPE)
    try:
        subprocess.run(["tar", "-x", "-C", str(source)], stdin=archive.stdout, check=True)
    finally:
        archive.stdout.close()
    if archive.wait():
        raise SystemExit("git archive failed")
stamp.write_text(revision + "\n")
subprocess.run([sys.executable, str(ROOT / "patch-vice.py"), str(source)], check=True)
if not (source / "src/tapeport/Makefile.in").exists():
    subprocess.run(["./autogen.sh"], cwd=source, check=True)
if not (build / "Makefile").exists():
    subprocess.run([str(source / "configure"), "--enable-headlessui", "--disable-html-docs",
                    "--without-alsa", "--without-pulse", "--without-resid", "--without-residfp",
                    "--without-libcurl", "--without-libusb"], cwd=build, check=True)
subprocess.run(["make", f"-j{args.jobs}"], cwd=build, check=True)
print(f"VICE revision {revision}\n{build / 'src/x64sc'}\n{build / 'src/xplus4'}")
