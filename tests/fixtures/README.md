# Browser UI test tape

Use `ui-test.tap` and `ui-test.idx` in the local player preview. This is a synthetic C64 PAL TAP v1 signal, not a Commodore program. It lasts exactly two minutes and has six IDX markers, every 20 seconds from 00:00 through 01:40. Short and extended pulse records are included. The file exceeds the 8 KB ESP streaming buffer so the buffer indicator and chunk transfer can be exercised.

Manual checks:

1. Select both files or drop them onto the deck or file picker together, in either order, or load TAP first and add IDX afterwards.
2. With preview MOTOR on and Autoplay enabled, check reel motion, the rolling counter, VFD time and buffer ring.
3. Use PREV/NEXT or the position selector and JUMP to navigate between sections. Stop MOTOR for seeking on real hardware.
4. Load TAP alone again; the IDX row should disappear and PREV/NEXT should return to REW/FF with counter-based navigation.
5. Turn Autoplay off, reinsert TAP, and check that playback waits for PLAY.

Regenerate the files from the repository root with `python3 tests/fixtures/generate-ui-tape.py`. The generator uses only the Python standard library; generated files are committed so manual tests require no setup.
