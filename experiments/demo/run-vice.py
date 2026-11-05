#!/usr/bin/env python3
"""Load the actual Circles PRG from standard ROM tape pulses, then reach SYS."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

REPO = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, default=REPO / 'tools/fastloader-vice/build')
parser.add_argument('--roms', type=Path, default=Path('/usr/local/share/vice/C64'))
parser.add_argument('--out', type=Path, default=REPO / 'tools/fastloader-vice/circles-standard')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
exporter, tap, dump = (out / name for name in ('export-demo', 'circles.tap', 'loaded.bin'))
subprocess.run(['c++', '-std=c++17', '-Itests/support', '-Isrc', 'tests/embedded_demo_test.cpp',
                'src/prg_encoder.cpp', 'src/tape_source.cpp', '-o', str(exporter)], cwd=REPO, check=True)
subprocess.run([str(exporter), str(tap)], cwd=REPO, check=True)
dump.unlink(missing_ok=True)
monitor = out / 'monitor.txt'
monitor.write_text('resourceget "TrapDevice1"\n'
                   'break $080d\ng\n'
                   f'bsave "{dump}" 0 $0801 $263e\nr\nresourceget "TrapDevice1"\nquit\n')
command = [str(args.build.resolve() / 'src/x64sc'), '-default', '-model', 'c64', '+sound',
           '-warp', '-console', '-seed', '1', '+trapdevice1', '-limitcycles', '300000000',
           '-initbreak', 'ready', '-moncommands', str(monitor), '-autostart', str(tap)]
for flag, name in [('kernal', 'kernal-901227-03.bin'), ('basic', 'basic-901226-01.bin'),
                   ('chargen', 'chargen-901225-01.bin')]:
    command += ['-' + flag, str(args.roms.resolve() / name)]
log = out / 'vice.log'
with log.open('w') as output:
    status = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=60).returncode
expected = (REPO / 'assets/demo/circles.prg').read_bytes()[2:]
actual = dump.read_bytes() if dump.exists() else b''
text = log.read_text(errors='replace')
result = dict(machine='c64-pal', mode='standard', trap_device1=False,
              payload_ok=actual == expected, reached_sys=bool(re.search(r'\.C:080d\b', text, re.I)),
              sha256=hashlib.sha256(actual).hexdigest(), returncode=status)
result['passed'] = (status == 0 and result['payload_ok'] and result['reached_sys']
                    and 'TrapDevice1=0' in text and 'ERROR --' not in text)
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
raise SystemExit(0 if result['passed'] else 1)
