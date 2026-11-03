# Offline cassette lettering fonts

Caveat (regular, 400) is bundled under the SIL Open Font License 1.1. The original notice and full license are in `caveat-OFL.txt`. Upstream project:

- https://github.com/google/fonts/tree/main/ofl/caveat

`SOURCES.json` records download URLs, upstream and bundled SHA-256 hashes, sizes and FontTools processing. These are static, reduced WOFF2 fonts with Basic Latin, Latin-1, Latin Extended-A (including Polish), combining accents, common punctuation and euro. Optional OpenType layout features are omitted. Separate Latin and Latin Extended subsets retain their upstream CSS Unicode ranges.

The preview loads these files locally. `scripts/embed-player.py` embeds every referenced image/font as a data URL and includes the license notice in the served HTML. Browsers do not contact an external font service. Normal builds do not need FontTools or network access to obtain fonts.
