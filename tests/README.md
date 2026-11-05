# Host-side cassette tests

These tests exercise the tape encoder, ring buffer, WRITE command decoder, fastloader, browser player and IDX handling without an ESP-01. Pi1551 test cases were used as an independent reference for C16 pulse lengths, double headers, extended TAP records and Ultimate-style IDX entries. The VICE/Pi1551 interoperability test needs VICE, ROMs and Pi1551 binaries and is not included here.

Run from the repository root with Node.js and a C++17 compiler:

```sh
node tests/player_index.mjs
node tests/playback_awake.mjs
c++ -std=c++17 -Isrc tests/write_decoder_test.cpp src/write_decoder.cpp -o /tmp/ESPsette-write-test
/tmp/ESPsette-write-test
python3 -m unittest discover -s tests -p 'test_write_vice.py' -v
c++ -std=c++17 -Isrc tests/computer_profile_test.cpp -o /tmp/ESPsette-profile-test
/tmp/ESPsette-profile-test
python3 -m unittest discover -s tests -p 'test_firmware_package.py' -v
python3 -m unittest discover -s tests -p 'test_demo_site.py' -v
c++ -std=c++17 -Isrc tests/fastloader_settings_test.cpp -o /tmp/ESPsette-settings-test
/tmp/ESPsette-settings-test
c++ -std=c++17 -Isrc tests/fastloader_wire_test.cpp -o /tmp/ESPsette-wire-test
/tmp/ESPsette-wire-test
c++ -std=c++17 -Itests/support -Isrc tests/prg_vectors_test.cpp src/prg_encoder.cpp src/tape_source.cpp -o /tmp/ESPsette-prg-test
/tmp/ESPsette-prg-test
c++ -std=c++17 -Itests/support -Isrc tests/tape_source_ring_test.cpp src/tape_source.cpp -o /tmp/ESPsette-ring-test
/tmp/ESPsette-ring-test
c++ -std=c++17 -Itests/support -Isrc tests/embedded_demo_test.cpp src/prg_encoder.cpp src/tape_source.cpp -o /tmp/ESPsette-demo-test
/tmp/ESPsette-demo-test
python3 -m unittest discover -s tests -p 'test_ota_image.py' -v
python3 -m unittest discover -s tests -p 'test_pio_ota.py' -v
python3 -m unittest discover -s tests -p 'test_embedded_assets.py' -v
```

`playback_awake.mjs` checks native and HTTP-compatible screen protection, including visibility changes, delayed grants, denial, retry throttling, media priming, and cleanup. `computer_profile_test.cpp` checks all four VICE tape clocks, profile parsing and legacy PAL aliases. `prg_vectors_test.cpp` compares every generated C64 and C16 pulse for both PAL and NTSC with an independent block sequence. The C16 cases also check pulse durations against the Pi1551 vectors. Autostart cases check absolute $0302 loading, header code placement and checksums for both machines/video standards, including the 171-byte limit. `tape_source_ring_test.cpp` checks that consumed bytes are released from the 8 KB streaming ring. `player_index.mjs` covers TAP v1/v2 headers and record boundaries, IDX offsets, counter-based seeking, autoplay, machine defaults, controls, recovery after a lost HTTP response, immediate TAP/PRG insertion, file ordering and extension case, saved PAL/NTSC PRG defaults and TAP header overrides, preview setting persistence, Demo with soft MOTOR, the segment buffer meter, mechanical counter transitions and unified VFD computer/time/PAL indicators, attaching IDX without restarting playback, PREV/NEXT boundaries, named Jump selection, automatic IDX title/selection tracking during playback, and absolute elapsed/total time after a jump.

These are host-side tests. They do not measure I2S timing, electrical polarity, DMA delays or decoding by a physical computer. The real C64 has been tested with the embedded TAP and browser TAP. C64 PAL fastloading was also confirmed on hardware on 2026-11-04; the other paths still need hardware checks.

The previous embedded TAP has been replaced by the original Circles PRG.
`embedded_demo_test.cpp` verifies both logical PRG layouts and compares every
standard pulse with a physically duplicated reference; it can also export a
TAP by accepting an output filename. The [Circles VICE tests](../experiments/demo/README.md)
load the real program with traps off in standard and fast modes, compare all
bytes and reach its SYS entry. The new demo awaits hardware verification.
`test_embedded_assets.py` reconstructs the original PRG and gzip web page,
including bundled assets, credits and licenses. `test_ota_image.py` checks
compression integrity and sector-rounded flash staging limits. Firmware
builds reject oversized OTA images and packaging verifies the gzip payload.

The [PRG fastloader tests](../experiments/fastloader/README.md) retain a portable
C sender model (`make fastloader-test`), actual-code cycle analyzer and optional
VICE matrices for the complete standard-ROM bootstrap and isolated transport.
`make native` needs ACME and builds the C64/TED assembly embedded in firmware.
`fastloader_settings_test.cpp` checks every timing field's bounds and defaults.
`fastloader_wire_test.cpp` checks all byte values, pair orders, READ variants and
configured delays through the same byte transmitter used by ESP GPIO. Browser
tests cover saved enabled/timing settings, rejected values, defaults, active
transfer guards, polling without losing edits, and standard/fast PRG selection.
The full bootstrap matrix has 32 checks; transport regression has 39 including
expected unsafe-timing failures. These do not replace ESP hardware timing tests.

For manual browser checks, [fixtures/](fixtures/README.md) contains a synthetic two-minute TAP and matching IDX with six markers. The browser test also validates their duration, marker boundaries and seeking.

`write_decoder_test.cpp` verifies C64/TED command headers, parity and checksums,
copy matching, ignored filenames/program data, maximum request length, overflow
and fastloader suspension/resumption. Browser tests cover received-request
metadata and suspension status. No test or decoder stores a program file.
WRITE preview tests require ejection even after a completed PRG. Active TAP
READ, paused files, partly buffered streams and embedded demos cannot publish
headers or increment WRITE errors; an empty deck can receive again.

`test_write_vice.py` replays twelve actual ROM SAVE captures from C64, C16 and
Plus/4 PAL/NTSC through the firmware decoder. It checks direct WRITE polarity,
MOTOR pauses between header and data, repeated SAVEs, incorrect polarity,
damaged/incomplete headers, timestamp jitter/wraparound and aborted-SAVE
recovery. Compressed delta events preserve every physical edge and the
original trace hash; replay needs neither VICE nor ROMs. See the
[capture procedure](../experiments/write/README.md) to regenerate them.

Fastloader UI tests check byte-based progress/ETA, accelerated counters/reels,
data/MOTOR stalls without releasing wake protection, completion, ordinary WRITE
filenames and configured-rate preview behavior. Encoder tests compare virtual
payload duration with the independently generated standard pulse stream.

Preview control tests run the built-in fastload sample through its shortened
bootstrap, bounded buffer refill, receiver pause/resume and completion. They
check WRITE isolation during fastload and header/request examples afterward,
without changing saved settings. STOP tests check that the button springs back
while the released PLAY state persists.

Fastloader switch tests check immediate persistence, automatic reinsertion of
the same stopped PRG, rollback on rejection, and independence from unsaved or
invalid timing fields. Timing-only saves preserve the enabled flag.
