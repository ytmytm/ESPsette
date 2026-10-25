#!/usr/bin/env python3
"""Embed the player HTML and its small cassette image into firmware."""

from pathlib import Path
import base64
import re

root = Path(__file__).resolve().parent.parent
source = (root / "web/player.html").read_text(encoding="utf-8")
asset = base64.b64encode((root / "web/assets/cassette.webp").read_bytes()).decode("ascii")
source = source.replace("assets/cassette.webp", "data:image/webp;base64," + asset)
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
