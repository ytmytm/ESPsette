# Player artwork

`cassette.webp` is the cassette image. `espsette-brand.png` is a transparent, antialiased rendering of the fixed text **ESPSette**, placed on the deck cover separately from the cassette.

The logo was rendered from the local `MicrogrammaDBolExt Regular.ttf` in `/mnt/batibat/Maciejdev/c128-front/`. Only the rendered PNG is included in the project; the font is not distributed or required by browsers or firmware builds.

To regenerate it with Pillow using the default font path above:

```sh
python3 scripts/render-deck-brand.py
python3 scripts/embed-player.py
```

Pass a different font path as the optional argument: `python3 scripts/render-deck-brand.py '/path/to/MicrogrammaDBolExt Regular.ttf'`.

The player embeds the logo, cassette image and [offline Caveat font](fonts/README.md) in the firmware page.
