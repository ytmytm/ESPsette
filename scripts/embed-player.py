#!/usr/bin/env python3
"""Embed the player HTML, cassette image and offline fonts into firmware."""

from pathlib import Path
import base64
import re

root = Path(__file__).resolve().parent.parent
source = (root / "web/player.html").read_text(encoding="utf-8")
for asset_path in sorted(set(re.findall(r"assets/[A-Za-z0-9_./-]+\.(?:webp|png|woff2)", source))):
    mime = {".woff2":"font/woff2", ".png":"image/png", ".webp":"image/webp"}[Path(asset_path).suffix]
    asset = base64.b64encode((root / "web" / asset_path).read_bytes()).decode("ascii")
    source = source.replace(asset_path, "data:" + mime + ";base64," + asset)
# Keep font attribution and the OFL in the served page as well as the assets.
licenses = [root / "web/assets/fonts" / name for name in ("caveat-OFL.txt",)]
license_comment = "/* Bundled font licenses:\n" + "\n".join("\n".join(line.rstrip() for line in path.read_text(encoding="utf-8").splitlines()) for path in licenses) + "\n*/\n"
source = source.replace("<style>", "<style>\n" + license_comment, 1)
# Compact whitespace so the PROGMEM page fits the 1MB OTA slot with ArduinoOTA.
source = re.sub(r"<!--.*?-->", "", source, flags=re.S)
source = re.sub(r">\s+<", "><", source)
source = re.sub(r"\n\s+", "\n", source)
source = source.strip()
if ')PAGE"' in source:
    raise SystemExit('HTML contains the C++ raw-string delimiter')
header = '''#pragma once

#include <pgmspace.h>

// Generated from web/player.html by scripts/embed-player.py. Do not edit here.
static const char PLAYER_PAGE[] PROGMEM = R"PAGE(''' + source + ''')PAGE";
'''
target = root / "src/player_page.h"
if not target.exists() or target.read_text(encoding="utf-8") != header:
    target.write_text(header, encoding="utf-8")
