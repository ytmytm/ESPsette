#!/usr/bin/env python3
"""Embed web/player.html into the firmware header without external assets."""

from pathlib import Path

root = Path(__file__).resolve().parent.parent
source = (root / "web/player.html").read_text(encoding="utf-8")
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
