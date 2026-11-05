# Circles

The embedded demo is the original **Circles** C64 PAL PRG, released in 2026.

- Music: **Shogoon** (Elysium / MultiStyle Labs).
- Code: **YTM** (Elysium; ESPsette's author).
- Release and original download: <https://csdb.dk/release/?id=262238>.
- File: `circles.prg`, 7744 bytes including the two-byte `$0801` load address.
- SHA-256: `ca0b6572b36473588e51d14017781ccee89c45656ad0a04875e0728abe4dcc3c`.

Use it on **C64 PAL only**. The Demo button keeps this profile even when the
default for uploaded PRGs is C16/Plus/4 or NTSC. With fastloader disabled,
normal LOAD receives both ROM copies, then use RUN. With fastloader enabled,
LOAD starts the header bootstrap and transfers the PRG at fast speed before
running it. Only one original PRG is stored in flash; the standard encoder
reads the payload twice without materializing a TAP or a duplicate PRG.

`scripts/embed-demo.py` generates the ignored `include/circles_demo.h` during
firmware builds. The static browser demo also offers the original PRG as a
download, with the same credits.
