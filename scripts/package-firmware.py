#!/usr/bin/env python3
"""Package the default ESP-01 build for Actions artifacts and dated releases."""

import argparse
from datetime import date
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = 'esp01_1m_serial'


def validate_tag(tag):
    if not re.fullmatch(r'v\d{4}-\d{2}-\d{2}', tag):
        raise ValueError('Release tag must be vYYYY-MM-DD')
    date.fromisoformat(tag[1:])
    return tag


def package(build_dir, output_dir, tag, revision):
    if tag:
        validate_tag(tag)
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise ValueError('Expected a full Git commit SHA')
    sources = [build_dir / ('firmware.' + ext) for ext in ('bin', 'elf')]
    for source in sources:
        if not source.is_file() or source.stat().st_size == 0:
            raise ValueError(f'Missing or empty build output: {source}')
    if output_dir.exists() and any(output_dir.iterdir()):
        raise ValueError('Output directory must be empty')
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = f'ESPsette-esp01-1m-{tag or "dev"}-{revision[:7]}'
    for source in sources:
        shutil.copyfile(source, output_dir / (stem + source.suffix))
    metadata = {
        'release_tag': tag or None, 'git_commit': revision,
        'environment': ENVIRONMENT, 'flash_size_bytes': 1048576,
        'flash_address': '0x0', 'flash_mode': 'dout',
        'flash_frequency': '40m', 'ota': False,
    }
    (output_dir / (stem + '.json')).write_text(json.dumps(metadata, indent=2) + '\n')
    (output_dir / 'FLASHING.txt').write_text(
        'ESPsette / ESP-01 / 1 MB flash\n\n'
        'Disconnect the Commodore interface. Use 3.3 V power and UART logic.\n'
        'While flashing: GPIO0-GND jumper, EN at 3.3 V, common GND,\n'
        'adapter TXD -> ESP RXD, adapter RXD -> ESP TXD, DTR -> RST.\n'
        'Keep GPIO2 HIGH at reset. Our esptool.cfg pulses DTR only.\n\n'
        'From a checkout after make setup, using its DTR reset configuration:\n'
        'ESPTOOL_CFGFILE=esptool.cfg tools/venv/bin/python -m esptool '
        '--chip esp8266 --port PORT write-flash --flash-mode dout '
        '--flash-freq 40m --flash-size 1MB 0x0 ' + stem + '.bin\n\n'
        'Replace PORT and the binary path as needed. Without DTR, manually\n'
        'pulse RST LOW with GPIO0 grounded before running esptool.\n'
        'After flashing: remove GPIO0-GND, disconnect DTR-RST, reset to run.\n'
        'No separate filesystem image is needed. OTA is disabled.\n'
    )
    files = sorted(output_dir.iterdir())
    (output_dir / 'SHA256SUMS.txt').write_text(''.join(
        f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n' for path in files))
    return stem


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / '.pio/build' / ENVIRONMENT)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'dist')
    parser.add_argument('--release-tag', default='')
    args = parser.parse_args()
    try:
        revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip()
        stem = package(args.build_dir, args.output_dir, args.release_tag, revision)
        if os.environ.get('GITHUB_OUTPUT'):
            with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                output.write(f'artifact_name={stem}\n')
        print(f'Firmware package: {stem}')
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Firmware packaging failed: {error}\n')


if __name__ == '__main__':
    main()
