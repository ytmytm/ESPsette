# Timer1/NMI WRITE experiment

This work stays on **`experiment/write-nmi-sampling`**, branched from the
OTA-version baseline **`33942e6840d6`** on `feature/circles-prg-demo-ota`.
The baseline keeps GPIO interrupts and the empty-deck requirement. No baseline
commits are rewritten or replaced.

## Flash order and rollback

1. Flash the baseline first. Reload the page once to obtain its new version
   field. **Network / OTA → Firmware** must show `33942e6840d6`.
2. Build/OTA this experimental branch. The displayed ID must change to this
   branch's clean `git rev-parse --short=12 HEAD` value. It is polled from the
   running ESP, so an already-open baseline page can observe the new ID.
3. First test `SAVE "TEST",1` and `SAVE "ESP:TEST",1` on an empty deck with
   PLAY pressed. Then insert a standard TAP, press PLAY and repeat SAVE while
   READ is active. A selected fast PRG still owns WRITE exclusively: finish or
   eject that session before testing SAVE.
4. Expand **Playback details** / **Debug / ESP state**. Record header/error
   counts, `writeSampleMaxGapUs`, `writeSampleGaps`, `writeSampleMaxWorkUs`, heap,
   stack watermark and I2S/source underruns. Also check that browser polling
   and Wi-Fi remain usable.

To return to the original receiver:

```sh
git switch feature/circles-prg-demo-ota
make ota PROFILE=1m OTA_HOST=DEVICE_IP
```

The displayed version returns to `33942e6840d6`. The separately packaged
baseline `.bin` / `.bin.gz` also remains available under
`dist/ota-version-33942e6/`. UART flashing remains available if OTA cannot be
reached. Both branches retain the same 64 KB LittleFS layout and settings.

## Implementation

FRC1/Timer1 runs at 80 MHz divided by 16, with hardware auto-reload every
200 ticks (40 µs). Its NMI callback reads physical MOTOR/WRITE/SENSE, then feeds
the existing decoder when a relevant edge is observed. C64 still uses rising
and TED falling periods. No pulse queue, dynamic memory or logging is used.
Sampling runs only with PLAY pressed, and stops for STOP, fastloader ownership
or OTA. This branch permits simultaneous ordinary READ and WRITE reception.

Timer1 is reserved by this experiment. The project has no PWM/Servo/Tone use;
those APIs must not be added while this receiver owns Timer1. I2S remains on
SLC/DMA and Wi-Fi remains enabled. Neither its configuration nor the CPU's
configured clock is changed. The pinned SDK's `system_get_time()` is a simple
IRAM RAM/register read; it supplies timestamps independently of temporary SDK
CPU turbo changes. CCOUNT measures callback body work, normalized to 80 MHz.
This measurement excludes SDK NMI entry/exit and a little bookkeeping.

NMI can preempt `noInterrupts()`. Only the sampler mutates the decoder while
running. Main-loop control first disables its callback before changing state,
and `/state` copies a sequence-checked snapshot. Eight failed snapshot attempts
return HTTP 503 rather than spinning indefinitely.

`writeSampleMaxGapUs` is the largest interval observed while the sampler runs,
including idle MOTOR periods. `writeSampleGaps` counts intervals over 80 µs
with MOTOR active. A gap during a partial header rejects it with `capture_gap`;
pilot searching resynchronizes without publishing a request, and already
verified headers are not reopened by damaged/discarded program data.

## Verification and limits

```sh
python3 -m unittest discover -s tests -p 'test_write_sampler.py' -v
make build PROFILE=1m
python3 experiments/write/run-vice.py --sampler --read-playback --name ESP:TEST \
  --out tools/fastloader-vice/nmi-read-pal
python3 experiments/write/run-vice.py --sampler --read-playback --name ESP:TEST \
  --ntsc --out tools/fastloader-vice/nmi-read-ntsc
```

The production sampler replays all twelve original VICE SAVE traces at all
40 integer-microsecond phase offsets. Tests also cover 0–5 µs callback jitter,
32-bit clock wraparound, suspended reception and rejection after a 500 µs gap.
Firmware build checks require the sampler/decoder and their direct NMI calls
to reside in IRAM/ROM. OTA staging checks remain enabled.

These are sampling/protocol tests, not a hardware latency measurement. Actual
NMI cadence, work time, power behavior, Wi-Fi throughput and I2S underruns
must be checked on the ESP. The original parity, XOR, countdown and matching
double-header requirements remain in force; no checksum or parity repair is
attempted to make a test pass.

The optional VICE READ test drives a repeating short/medium/long pulse train
while the real KERNAL emits SAVE. All six C64/C16/Plus4 PAL/NTSC cases passed
with `ESP:TEST`. A C64 PAL control replay through the GPIO decoder also passed.
This tests the computer's response to concurrent READ activity, including CIA
FLAG events; it does not model ESP DMA or Wi-Fi interrupt contention. The
integration adapter remains separate from the disposable VICE source tree.
