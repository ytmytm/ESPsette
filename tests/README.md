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
```

`playback_awake.mjs` checks native and HTTP-compatible screen protection, including visibility changes, delayed grants, denial, retry throttling, media priming, and cleanup. `computer_profile_test.cpp` checks all four VICE tape clocks, profile parsing and legacy PAL aliases. `prg_vectors_test.cpp` compares every generated C64 and C16 pulse for both PAL and NTSC with an independent block sequence. The C16 cases also check pulse durations against the Pi1551 vectors. Autostart cases check absolute $0302 loading, header code placement and checksums for both machines/video standards, including the 171-byte limit. `tape_source_ring_test.cpp` checks that consumed bytes are released from the 12 KB streaming ring. `player_index.mjs` covers TAP v1/v2 headers and record boundaries, IDX offsets, counter-based seeking, autoplay, machine defaults, controls, recovery after a lost HTTP response, immediate TAP/PRG insertion, file ordering and extension case, saved PAL/NTSC PRG defaults and TAP header overrides, preview setting persistence, Demo with soft MOTOR, the segment buffer meter, mechanical counter transitions and unified VFD computer/time/PAL indicators, attaching IDX without restarting playback, PREV/NEXT boundaries, named Jump selection, automatic IDX title/selection tracking during playback, and absolute elapsed/total time after a jump.

These are host-side tests. They do not measure I2S timing, electrical polarity, DMA delays or decoding by a physical computer. Real C64 PAL hardware tests pass for browser TAP, standard PRG, fast PRG, Circles in both modes, and WRITE requests with an empty deck. Other models and NTSC still need hardware checks.

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
check WRITE isolation while any source is inserted and header/request examples after ejection,
without changing saved settings. STOP tests check that the button springs back
while the released PLAY state persists.

Fastloader switch tests check immediate persistence, automatic reinsertion of
the same stopped PRG, rollback on rejection, and independence from unsaved or
invalid timing fields. Timing-only saves preserve the enabled flag.

The embedded demo is now the Circles C64 PAL PRG. `embedded_demo_test.cpp`
checks both logical layouts and compares every standard pulse with a duplicated
reference. UI tests cover standard/fast selection and reinsertion after changing
the fastloader switch. See [VICE demo tests](../experiments/demo/README.md).
OTA updates over IP and Rambo III Side 1 playback after updating passed
on real hardware with mDNS disabled (2026-11-06). WRITE capture requires an
empty deck, including after paused or completed playback; the UI and firmware
block reception until EJECT. No Timer1/NMI receiver is enabled.

`test_tap_dma.py` runs the firmware's actual pulse reader, DMA producer and
completion callback with a clocked model of the existing eight 64-word DMA
descriptors and the configured 12 KB source ring. Bursts of 81,197 µs HTTP calls reproduce the
old 32-word refill limit's I2S underrun near 49.6 seconds. A 1,017,556 µs wait
also defeats full refills unless cooperative yield servicing remains active.
The corrected path checks every generated word against an independent TAP
waveform, including the long pause, and restores the exact decoder position
when MOTOR stops during HTTP. The default fixture is synthetic. To replay a
local tape without committing it:

```sh
ESPSETTE_DMA_TAP=/path/to/file.tap python3 -m unittest discover -s tests -p test_tap_dma.py -v
```

The complete local Rambo III Side 1 TAP (715.573 seconds) also passes the model.
This models network wait/yield timing; it does not measure ESP hardware or
Wi-Fi interrupt latency. `maxDmaPumpGapUs` and `dmaServiceReady` in `/state`
expose service timing/readiness for the next hardware test. The UI shows the
maximum DMA refill gap alongside the source buffer diagnostics. The DMA allocation is unchanged; the source ring is now 12 KB.

Source-wait tests split a four-byte TAP record at each incoming byte in both
v1 and v2. Partial DMA words survive the wait without padding or duplicate
bits. A true missed DMA deadline counts as a source underrun, and an already
released source offset fails immediately with `source_window_lost`. The failure
position stays frozen instead of reverting to the last MOTOR pause. The full
Rambo replay also checks 227 MOTOR stop/resume cycles against every played bit.

A slow-upload model reproduces source starvation with 1 KB POSTs around 345 s
of the local Rambo tape. With 4 KB POSTs streamed through the existing 2 KB
HTTPUpload buffer, the whole tape completes without changing either the source
ring or DMA allocation. Browser tests check 4 KB framing, batched TAP refill,
short EOF tails and immediate delivery during source waits or prefill. Initial
loading now fills the configured ring (or reaches EOF) before starting READ.

`sourceWaiting` / `sourceWaits` report recoverable waits. `sourceReadOffset`,
`sourceWindowStart` and `sourceReceivedUntil` retain the missing read's source
window at the time of failure. These are diagnostics for hardware testing; host
models do not establish a bound on real Wi-Fi stalls.

Browser recovery tests reject malformed JSON and incomplete `/state` objects
without dropping the File or its session. Repeated playback-error polls keep
the same File, including after the ESP releases the failed stream on MOTOR
off; REW starts a fresh session from that File after MOTOR stops. A valid
foreign session still invalidates the old transfer.

Debug JSON contains the current ESP state. Browser request history and
poll/render timing snapshots have been removed, along with ESP loop/HTTP
maximum-time measurements. Buffer offsets, underrun counters, the DMA refill
gap and `sourceDataAgeAtErrorMs` remain available for playback diagnostics.

The VFD Wi-Fi icon uses `wifiConnected` and `wifiRssi` from both `/state`
and compact `/buffer` responses. It shows the ESP-to-router signal, with four pixel arc levels and
a dBm tooltip. Unknown/disconnected states and unavailable RSSI are distinct;
preview mode supplies a sample signal. UI tests cover level thresholds,
invalid samples and reconnection. No Wi-Fi scan or extra request is needed.

The UI fetches a full `/state` snapshot for a new session, then uses only
compact `/buffer` responses every 250 ms during standard PLAY (100 ms for
fastloader bootstrap/payload). This includes loader MOTOR pauses and playback of the buffered tail after file
upload completes. Full `/state` polling resumes every 500 ms after STOP,
completion or error. Opening Debug does not trigger full polling during PLAY;
configuration, scope and heap diagnostics remain at their last full snapshot.
Compact replies update played cycles, MOTOR, Wi-Fi, progress and upload credit.
They are merged with cached metadata and validated before reconciling the file.
Tests check the endpoint sequence, live RSSI/counter updates, uploads, MOTOR
pauses, the buffered tail, malformed replies and resuming full polls after STOP.
The compact handler retains its 768-byte heap JSON arena; DMA buffers are
unchanged. Wi-Fi power saving is explicitly disabled; `/state` reports
`wifiSleepMode: 0` for this test configuration.

Successful `/chunk` replies include the same live fields as `/buffer`, plus
`nextOffset` and `loaded`. The browser renders them and reuses acknowledged
credit in the next bounded upload batch. It sends another GET when credit is
insufficient or a POST result is uncertain. Tests cover consecutive batches
without credit GETs, exact offsets after lost/partial replies, invalid
acknowledgements and stopping on a playback error reported by an upload.
The source ring is now 12288 bytes; each POST still carries at most 4096 bytes.
Transport replies advertise `bufferCapacity` so the UI and preview show the
correct fill level. This adds 4096 bytes of static RAM compared with the earlier
8 KB build, extending tolerance for network stalls without changing DMA.
Neither change guarantees delivery through a prolonged Wi-Fi/TCP interruption.

Restored OTA checks cover deterministic gzip, sector-aligned staging limits,
serial/OTA upload selection, Git identity and versioned release filenames.
`test_embedded_assets.py` decompresses the generated web page and verifies its
assets and licenses. The 1 MB build verifies the actual linked flash budget.
`make version`, build/upload logs, the running `/state` / `/buffer` version and
release filenames all use the same 12-character commit ID (`-dirty` if needed).
On 2026-11-06, the user confirmed OTA over IP and Rambo III Side 1 playback
on real C64 PAL hardware. The latest successful playback test used a 12 KB
source ring, Wi-Fi power saving disabled and live-state upload acknowledgements.
mDNS remains disabled, and WRITE reception requires an empty deck (EJECT).

Earlier failures included a 3041 ms `/state` response with invalid JSON and
roughly three-second gaps in source delivery. Source underruns also occurred
with mDNS disabled, compact-only polling and Wi-Fi power saving disabled. The
successful 12 KB configuration does not establish the cause of those gaps.
