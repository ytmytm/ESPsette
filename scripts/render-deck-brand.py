#!/usr/bin/env python3
"""Render the fixed ESPSette cover logo; requires Pillow and a local font."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    'font', nargs='?', type=Path,
    default=Path('/mnt/batibat/Maciejdev/c128-front/MicrogrammaDBolExt Regular.ttf'),
    help='Path to MicrogrammaDBolExt Regular.ttf (default: %(default)s)',
)
args = parser.parse_args()
scale = 4
font = ImageFont.truetype(str(args.font), 26 * scale)
text = 'ESPSette'
left, top, right, bottom = font.getbbox(text)
padding = 2 * scale
image = Image.new('RGBA', (right - left + padding * 2, bottom - top + padding * 2))
ImageDraw.Draw(image).text((padding - left, padding - top), text, font=font, fill='#d9e4e7')
target = Path(__file__).resolve().parent.parent / 'web/assets/espsette-brand.png'
image.save(target, optimize=True)
print(f'{target.name}: {image.width}x{image.height}, {target.stat().st_size} bytes')
