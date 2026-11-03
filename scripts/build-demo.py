#!/usr/bin/env python3
"""Build the static GitHub Pages demo from the firmware's browser UI."""
import argparse
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parent.parent


def build_demo(output: Path) -> None:
    source = (ROOT / 'web/player.html').read_text(encoding='utf-8')
    source, count = re.subn(r'^const preview = .*?;$', 'const preview = true;',
                            source, count=1, flags=re.MULTILINE)
    if count != 1:
        raise ValueError('Player preview switch not found; refusing to publish a hardware UI')
    source = source.replace('<h2>Local preview</h2>', '<h2>Demo mode</h2>')
    samples = ('<p>Try the indexed test tape: '
               '<a href="samples/ui-test.tap" download>Sample TAP</a> + '
               '<a href="samples/ui-test.idx" download>Sample IDX</a>.</p>')
    source = source.replace('<h2>Demo mode</h2>', '<h2>Demo mode</h2>' + samples, 1)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'index.html').write_text(source, encoding='utf-8')
    shutil.copytree(ROOT / 'web/assets', output / 'assets', dirs_exist_ok=True)
    (output / 'samples').mkdir(exist_ok=True)
    for name in ('ui-test.tap', 'ui-test.idx'):
        shutil.copy2(ROOT / 'tests/fixtures' / name, output / 'samples' / name)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/demo')
    args = parser.parse_args()
    build_demo(args.output)
    print(f'Static ESPsette demo built in {args.output}')
