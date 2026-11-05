# Real ROM SAVE / direct WRITE regression

This experiment uses the same isolated, pinned VICE build as the
[fastloader tests](../fastloader/README.md). Its capture-only tape-port device
supplies PLAY/SENSE and logs physical WRITE levels and MOTOR changes with CPU
timestamps. It never encodes pulses, reads KERNAL internals, intercepts SAVE,
or runs the ESP decoder inside the emulator. The host replay feeds the actual
firmware decoder with same-polarity periods, using the ESP ISR's motor gating,
microsecond conversion and 15-bit period saturation.
After pilot recognition, replay follows the firmware's single-edge hardware
selection. Both MOTOR and WRITE callbacks feed the decoder directly; replay
requires no main-loop drains or callbacks during the entire SAVE operation.

The shared integration patch remains in
`experiments/fastloader/vice-patches/espsette-fastloader.patch`; the adapter is
kept here as `vice-device.c`. The Parobek VICE source tree is never modified.
Build the disposable emulator using the existing helper:

```sh
python3 experiments/fastloader/build-vice.py --repository /path/to/VICE/svn-mirror
```

This needs the build dependencies listed in the fastloader documentation,
plus local Commodore ROMs (default `/usr/local/share/vice`). ROMs and emulator
binaries are not committed. Use a fresh ignored build root when replacing an
older integration patch, or reverse the old patch before applying the new one.

From the repository root:

```sh
python3 experiments/write/run-vice.py
python3 experiments/write/run-vice.py --ntsc
python3 experiments/write/run-vice.py --name ESP:TEST
python3 experiments/write/run-vice.py --name ESP:TEST --ntsc
python3 -m unittest discover -s tests -p 'test_write_vice.py' -v
```

Each VICE run executes BASIC `10 SAVE "TEST",1:POKE2033,165:END` (or the requested
name), followed by RUN. Device-1 tape traps are disabled. The monitor only
types the program, selects capture device 9, and observes the completion POKE;
it never injects tape pulses or a decoded header. `--machine c64`, `c16` or
`plus4` narrows the default three-machine matrix. `--build`, `--roms` and
`--out` override local paths. Raw CSV events, monitor scripts, VICE logs and
JSON results stay under ignored `tools/fastloader-vice/write-results/`.

All twelve SAVE/name/model/video-standard combinations passed on 2026-11-04.
Each decoded exactly one header, the expected name and addresses, and zero
errors. `ESP:TEST` published exactly one request, without executing a handler.

## Failures reproduced and fixed

- C64 uses rising-to-rising periods on the direct wire. VICE reproduced the
  old receiver's `unexpected_terminator`: both header copies were valid, but
  MOTOR fell before the final terminator's last rising edge. Accepting the
  second checksum completes the already fully verified header.
- TED needs falling-to-falling periods on the direct wire. VICE's TED output
  callback uses a complemented convention, which the adapter converts back
  to physical CPU-pin levels. Rising periods mix adjacent halfwaves and the
  old receiver misidentified TED as C64, producing `pulse_error`.
- C64 briefly stops MOTOR after the header; TED also makes a very short
  transition. Restarting the decoder there made program bytes look like a
  new header. The revised decoder preserves discard state through that gap,
  counts both program copies without retaining them, and preserves verified
  metadata. An aborted or damaged payload can be cleared by a second of
  MOTOR off before the next SAVE.

Physical WRITE passed on C64 PAL on 2026-11-05 with an empty deck. Eject any
TAP/PRG before SAVE; ordinary READ and command reception must not run together.
C16/Plus/4 and NTSC hardware tests remain pending. The emulator exercises ROM
waveforms and protocol state; it cannot validate ESP ISR latency, interrupt
loss, Wi-Fi contention or electrical behavior. ±10 us timestamp jitter tests
are an explicit simulated bound, not a hardware measurement.

The first hardware follow-up reported intermittent success, parity/pulse
errors and resets with the browser open (about 23 KB free heap). Protocol
replay alone did not cover HTTP stalls, ISR placement or continuation-stack
pressure. The next revision removes the 1 KB pulse queue, decodes directly in
IRAM, selects one interrupt edge after the pilot, and processes MOTOR in its
ISR. The state response uses a temporary bounded heap arena instead of a
2 KB stack arena, and the browser displays reset/exception and stack
diagnostics. This path is now validated on C64 PAL with an empty deck. The
original reset cause was not captured, so stack overflow remains a hypothesis.
The later Timer1/NMI receiver and OTA experiments were withdrawn following
standard playback failures; their code is retained on `archive/post-first-ota`.

## Retained fixtures

Add `--fixtures tests/fixtures/write` to each of the four capture commands to
regenerate the committed fixtures. Export happens only after the ROM has
completed SAVE and replay passes. Each gzip JSON stores machine, video
standard, filename, pinned VICE revision, clock, original CSV SHA-256 and every
event as `[delta_cycles, signal, high]`. Delta encoding compresses repeated
pilot timing efficiently while reconstructing the original CSV exactly.
Gzip timestamps are fixed for reproducibility. No ROM or program image is
included. CI builds the C++ replay and runs these captures without VICE.
