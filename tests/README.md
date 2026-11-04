# Host-side cassette tests

These tests exercise the tape encoder, ring buffer, WRITE header decoder, browser player and IDX handling without an ESP-01. Pi1551 test cases were used as an independent reference for C16 pulse lengths, double headers, extended TAP records and Ultimate-style IDX entries. The VICE/Pi1551 interoperability test needs VICE, ROMs and Pi1551 binaries and is not included here.

Run from the repository root with Node.js and a C++17 compiler:

```sh
node tests/player_index.mjs
node tests/playback_awake.mjs
c++ -std=c++17 -Isrc tests/computer_profile_test.cpp -o /tmp/ESPsette-profile-test
/tmp/ESPsette-profile-test
python3 -m unittest discover -s tests -p 'test_firmware_package.py' -v
python3 -m unittest discover -s tests -p 'test_demo_site.py' -v
c++ -std=c++17 -Isrc tests/write_decoder_test.cpp src/write_decoder.cpp -o /tmp/ESPsette-write-test
/tmp/ESPsette-write-test
c++ -std=c++17 -Itests/support -Isrc tests/prg_vectors_test.cpp src/prg_encoder.cpp src/tape_source.cpp -o /tmp/ESPsette-prg-test
/tmp/ESPsette-prg-test
c++ -std=c++17 -Itests/support -Isrc tests/tape_source_ring_test.cpp src/tape_source.cpp -o /tmp/ESPsette-ring-test
/tmp/ESPsette-ring-test
```

`playback_awake.mjs` checks native and HTTP-compatible screen protection, including visibility changes, delayed grants, denial, retry throttling, media priming, and cleanup. `computer_profile_test.cpp` checks all four VICE tape clocks, profile parsing and legacy PAL aliases. `prg_vectors_test.cpp` compares every generated C64 and C16 pulse for both PAL and NTSC with an independent block sequence. The C16 cases also check pulse durations against the Pi1551 vectors. `write_decoder_test.cpp` covers valid and damaged headers for both computers. `tape_source_ring_test.cpp` checks that consumed bytes are released from the 8 KB streaming ring. `player_index.mjs` covers TAP v1/v2 headers and record boundaries, IDX offsets, counter-based seeking, autoplay, machine defaults, controls, recovery after a lost HTTP response, immediate TAP/PRG insertion, file ordering and extension case, saved PAL/NTSC PRG defaults and TAP header overrides, preview setting persistence, Demo with soft MOTOR, the segment buffer meter, mechanical counter transitions and unified VFD computer/time/PAL indicators, attaching IDX without restarting playback, PREV/NEXT boundaries, named Jump selection, automatic IDX title/selection tracking during playback, and absolute elapsed/total time after a jump.

These are host-side tests. They do not measure I2S timing, electrical polarity, DMA delays or decoding by a physical computer. The real C64 has been tested with the embedded TAP and browser TAP; the other paths still need hardware checks.

For manual browser checks, [fixtures/](fixtures/README.md) contains a synthetic two-minute TAP and matching IDX with six markers. The browser test also validates their duration, marker boundaries and seeking.
