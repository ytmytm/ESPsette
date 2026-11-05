# Circles demo validation

The original [Circles PRG](../../assets/demo/README.md) is C64 PAL only. Both
paths were checked with the isolated VICE build used by the fastloader tests:

```sh
python3 experiments/demo/run-vice.py
python3 experiments/fastloader/run-bootstrap-vice.py --machine c64 \
  --prg assets/demo/circles.prg --expect-pc 0x080d \
  --out tools/fastloader-vice/circles-fast
```

The first command exports the firmware's standard pulses from the embedded
two-copy logical PRG view, checks them against a physically duplicated
reference, then uses VICE's datasette and real ROM LOAD/RUN. Tape traps are
disabled. The second uses the standard-ROM header bootstrap and fast receiver
with the original 7742 payload bytes. Both compare all loaded bytes with the
original file and stop at its BASIC SYS entry `$080D`, before its first machine
instruction. This checks loading and reaching the entry, not SID audio output.

Both passed on 2026-11-04; the payload SHA-256 is
`e7ecbd32e967f91f7616dc8d5887a190b4b59971f4ac90ce177e469f86c94607`.
VICE logs, generated TAP and dumps remain in ignored
`tools/fastloader-vice/circles-standard/` and `circles-fast/`.

Host tests and CI also check the single/repeated embedded views, pulse
equivalence, original PRG bytes and credits,
standard/fast demo selection independent of uploaded-file defaults, autoplay,
and repeated insertion after changing the saved fastloader switch.


Both standard and fast Circles loading were confirmed on real C64 PAL hardware
on 2026-11-05. OTA remains disabled in the validated firmware.
