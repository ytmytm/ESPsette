# PRG fastloader protocol and regression tests

The firmware supports a standard ROM bootstrap followed by a two-bit GPIO
transfer. Enable **Use fastloader for PRG** in Playback settings; the default is
off. Timing settings are stored in LittleFS. C64/C16/Plus/4 PAL and NTSC have
passed host/VICE tests; ESP timing and electrical behavior still need hardware
verification.

Native code is in [`native/fastloader/`](../../native/fastloader/).
[BOOTSTRAP.md](BOOTSTRAP.md) describes autostart and memory layout;
[TIMING.md](TIMING.md) records the final timing budget.

## Wires

| Line | Fast-transfer direction | C64 input | C16/Plus/4 input |
| --- | --- | --- | --- |
| READ | ESP GPIO3 → computer, strobe | CIA1 `$DC0D`, FLAG bit 4 | CPU `$01`, bit 4 |
| WRITE | ESP GPIO2 → computer, first bit of pair | CPU `$01`, bit 3 | CPU `$01`, bit 1 |
| SENSE | ESP GPIO1 → computer, second bit of pair | CPU `$01`, bit 4 | `$FD10`, bit 2 |
| MOTOR | Computer → ESP GPIO0, block request | Output latch `$01`, inverted bit 5 | Output latch `$01`, inverted bit 3 |

READ is direct GPIO during both fast stages, with no I2S/DMA output. WRITE is
released by the computer before ESP drives it. The mode signature temporarily
uses WRITE in the opposite direction; see [BOOTSTRAP.md](BOOTSTRAP.md).
The [standard WRITE command receiver](../../docs/WRITE-COMMANDS.md) is suspended
for the entire selected fastloader session, so ESP output cannot be decoded as
a computer command.

## Block transfer

1. The receiver disables interrupts and display DMA, releases WRITE and starts
   with READ high and MOTOR on. C64 also disables sprites and masks both CIAs.
2. Each change of MOTOR requests a block. The first block is a six-byte
   descriptor: destination, nonzero length, entry address; all little-endian
   16-bit words. Entry zero selects BASIC RUN. Subsequent requests carry up to
   256 payload bytes; the descriptor is not part of that count.
3. The sender waits for the configured MOTOR settling delay and available
   bytes, then sends each byte as four WRITE:SENSE pairs:
   **b4:b5, b6:b7, b0:b1, b2:b3**.
4. Set both data lines, wait setup, strobe READ, preserve data through hold.
   C64 needs a falling edge per pair and a high interval between pairs.
   TED uses alternating LOW/HIGH/LOW/HIGH levels. Wait the pair gap before
   the next pair, or the byte pause after the fourth pair.
5. Stop after the agreed block length. The receiver requests another block
   only after storing the previous one. A request arriving during the last
   hold/pause is retained. A short final block has its exact remaining length.
6. Release WRITE and SENSE after the final hold, before normal ROM operation
   resumes. The receiver restores the machine and launches the program.

MOTOR changes alternate levels; they are not short off/on pulses. The receiver
uses a private copy of the output latch: never derive a MOTOR write from live
`$01` data, or use read-modify-write instructions on that port. CIA FLAG is an
event latch, not an edge queue; two strobes before acknowledgement can merge.
Polling uses bit 4, not the delayed IRQ summary in bit 7. On TED, WRITE and
SENSE require separate reads and must both remain stable until sampled.

There is no per-pair ACK. Delays may grow while lines remain stable, but late
events must never be caught up by shortening setup/hold or byte pauses.
The ESP busy-waits using CPU cycle counts, leaves interrupts enabled, feeds
the watchdog and services HTTP between blocks. The bounded ring retains the
next block until transmitted. STOP cancels a fast transfer; reset the receiver
computer before starting another interrupted loader.

## Persistent settings

`POST /fastloader-config` takes either `enabled=0|1` alone for the immediate
switch or all ten timing fields below without `enabled` for the Save button;
`GET /state` returns the saved configuration in `fastloader`. The firmware
validates the complete candidate, writes `/fastloader.tmp`, then atomically
renames it to `/fastloader.json`. Missing, corrupt, incompatible or out-of-range
configuration files use the defaults. Each update preserves the other group:
toggling uses saved timings, even if the panel contains invalid unsaved edits;
saving timings preserves the saved enabled flag. A stopped browser-owned PRG
is immediately reinserted from the start after a successful switch, recreating
its one-copy/two-copy logical stream. Without the source file in this tab, the
preference is saved and the PRG must be reinserted. Timing changes apply to the
next PRG session. The panel preserves unsaved timing edits across polling and
switch saves, and restores the switch position if saving fails. Default timings
do not change the switch. Active transfers cannot be reconfigured.

| Field | Default µs | Accepted µs |
| --- | ---: | ---: |
| `setupUs` | 3 | 1–1000 |
| `holdC64Us` / `holdTedUs` | 16 / 16 | 1–1000 |
| `gapUs` | 3 | 1–1000 |
| `bytePauseC64Us` / `bytePauseTedUs` | 19 / 11 | 0–5000 |
| `bootHoldUs` | 80 | 1–1000 |
| `bootPauseUs` | 80 | 0–5000 |
| `motorOnUs` / `motorOffUs` | 5 / 1000 | 0–10000 |

These limits allow experimental tuning; accepting a number does not guarantee
correct reception. Reduce delays only with receiver-cycle and hardware evidence.
The compact bootstrap uses serial MSB-first pairs, then switches to the layout
above for the descriptor and program.

## Progress telemetry and display

`/state` reports `fastloaderBytes` and `fastloaderTotalBytes` for the PRG payload,
excluding the PRG load-address prefix, descriptor and receiver download.
Counts advance after completed blocks. `fastloaderElapsedMs` measures elapsed
wall time from the first payload request, includes source/MOTOR waits and freezes
at completion. The UI derives average bytes/second and ETA from these values.
`fastloaderWaitingForData` and `fastloaderWaitingForMotor` identify source and
receiver stalls; the reels pause while wake protection remains active.

For a fast session, `totalCycles` and `playedCycles` are presentation values:
the standard bootstrap duration plus the ordinary ROM duration of both copies
of the total/sent payload. The encoder computes this arithmetically; no extra
TAP or file copy is generated. The VFD/counter therefore accelerate with the
actual byte count. During the fast receiver and payload stages reels switch from 1.6 s to 0.28 s per rotation,
independent of the first rate sample. This is a visual speed cue, not a measured
RPM display; reduced-motion settings still apply.
Progress is polled at 100 ms during fast stages even after the whole file has
been uploaded. The completed session remains available at 100% until explicit
rewind/ejection, so the final report cannot disappear between polls.

## Build and tests

Firmware builds require ACME; `make native` assembles both computers and generates
`include/fastloader_native.h`. PlatformIO also runs this automatically. Host tests
need Python 3, a C/C++17 compiler and Node.js. There is no need for VICE to build
or flash firmware.

```sh
make native
make fastloader-test
python3 experiments/fastloader/analyze-timing.py --boot
c++ -std=c++17 -Isrc tests/fastloader_wire_test.cpp -o /tmp/wire-test
/tmp/wire-test
```

The optional emulator harness uses the pinned VICE revision
`eaf9b61f5560f921ebaa25349b09e797f351eb1e`, with an isolated tape-port device.
It does not modify the user's Parobek checkout. On Debian/Ubuntu install:

```sh
sudo apt-get install acme build-essential autoconf automake libtool flex bison pkg-config libz-dev
python3 experiments/fastloader/build-vice.py --repository /path/to/svn-mirror
python3 experiments/fastloader/run-bootstrap-matrix.py
python3 experiments/fastloader/run-matrix.py
```

The mirror must contain the pinned revision and `vice/` directory. Supply your
own ROMs, default path `/usr/local/share/vice`; `--roms` overrides it. Emulator
source/build and results go under ignored `tools/fastloader-vice/`. The separate
[integration patch](vice-patches/README.md) remains reviewable in the repository.

`run-bootstrap-vice.py` types normal LOAD with ROM traps disabled, compares the
loaded receiver and full PRG, and verifies subsequent execution. It never loads
code through the monitor. `run-vice.py` directly installs the receiver to isolate
transport errors. Its regression matrix includes deliberately unsafe timing as
negative controls. The portable C alarm model only sees physical port events,
not the receiver PC or registers. The ESP wire-order test separately exercises
its actual byte transmitter with all 256 values, both pair layouts, both READ
variants and changed settings.

Validated matrices: **32/32** complete bootstrap checks and **39/39** transport
checks, including expected failures for unsafe timing. Bootstrap cases include
BASIC/machine code, PAL/NTSC, both C64 CIA models, 255/256/257/513-byte boundaries,
unaligned destinations and maximum supported RAM ranges. VICE timing results
are simulations, not measured ESP throughput or analog MOTOR delays.
