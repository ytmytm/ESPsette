#!/usr/bin/env python3
"""Embed the player UI and assets; wake media is decoded to Blob URLs in JS."""

from pathlib import Path
import argparse
import gzip
import base64
import re

root = Path(__file__).resolve().parent.parent
source = (root / "web/player.html").read_text(encoding="utf-8")
for script_path in re.findall(r'<script src="(assets/[A-Za-z0-9_./-]+\.js)"></script>', source):
    script = (root / "web" / script_path).read_text(encoding="utf-8")
    if "</script" in script.lower():
        raise SystemExit("External script contains an HTML script terminator")
    source = source.replace('<script src="' + script_path + '"></script>', "<script>\n" + script + "\n</script>")
for asset_path in sorted(set(re.findall(r"assets/[A-Za-z0-9_./-]+\.(?:webp|png|woff2|mp4|webm)", source))):
    mime = {".woff2":"font/woff2", ".png":"image/png", ".webp":"image/webp", ".mp4":"video/mp4", ".webm":"video/webm"}[Path(asset_path).suffix]
    asset = base64.b64encode((root / "web" / asset_path).read_bytes()).decode("ascii")
    source = source.replace(asset_path, "data:" + mime + ";base64," + asset)
# Keep font/media attribution and licenses in the served page as well as the assets.
licenses = [root / "web/assets/fonts/caveat-OFL.txt", root / "web/assets/wake/LICENSE.txt"]
license_comment = "/* Bundled font and wake media licenses:\n" + "\n".join("\n".join(line.rstrip() for line in path.read_text(encoding="utf-8").splitlines()) for path in licenses) + "\n*/\n"
source = source.replace("<style>", "<style>\n" + license_comment, 1)
# Compact whitespace so the PROGMEM page fits the 1MB OTA slot with ArduinoOTA.
source = re.sub(r"<!--.*?-->", "", source, flags=re.S)
source = re.sub(r">\s+<", "><", source)
source = re.sub(r"\n\s+", "\n", source)
source = source.strip()
data = gzip.compress(source.encode('utf-8'), compresslevel=9, mtime=0)
data = data[:9] + b'\xff' + data[10:]
rows = [', '.join(f'0x{x:02x}' for x in data[i:i+16]) for i in range(0, len(data), 16)]
header = ('#pragma once\n#include <stdint.h>\n#include <pgmspace.h>\n\n'
          '// Generated gzip HTML by scripts/embed-player.py. Do not edit.\n'
          'static const uint8_t PLAYER_PAGE[] PROGMEM = {\n  ' + ',\n  '.join(rows) + '\n};\n')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, default=root / 'src/player_page.h')
target = parser.parse_args().out
if not target.exists() or target.read_text(encoding='utf-8') != header:
    target.write_text(header, encoding='utf-8')
