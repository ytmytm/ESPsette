# Standard-ROM bootstrap and autostart

Three stages take an ordinary ROM `LOAD` through to PRG execution. Native
sources are in `native/fastloader/`; `make native` builds both targets with ACME.

| Component | C64 | C16 / Plus/4 |
| --- | --- | --- |
| Autostart vector, standard data block | `$0302..$0303` | `$0302..$0303` |
| Code inside standard header tail | `$0351`, 163 B | `$0347`, 153 B |
| Full receiver | `$0400`, 482 B; send 512 B | `$0800`, 626 B; send 768 B |
| Final ROM handoff trampoline | `$033C` | `$0333` |
| Firmware PRG address range | `$0801..$CFFF` | `$1001..$3FFF` |

The emulator also tests Plus/4 payloads through `$7FFF`; the current firmware
uses the common 16 KB C16 limit for its combined C16/Plus4 profile.
C64 writes beneath BASIC ROM reach RAM without changing the memory mapping;
the receiver keeps ROM/I/O visible and stops before `$D000`. A program that
needs to read that RAM later must select its own memory mapping. Payloads
overlapping the loader or reaching I/O are not supported.

## Standard tape stage

`PrgEncoder::beginAutostart()` generates a type-3 absolute header for a two-byte
load at `$0302`. The vector targets code in the unused header tail, after the
16-character filename. It has 171 available bytes. TED stores the type byte
separately at `$F8`, so its code lands at `$0347`, not `$0348`.

The production encoder generates both copies, parity and checksums, using its
ordinary pilot lengths. Two hundred short tail pulses allow the ROM to finish
the second data copy. The vector bytes are independent of the streamed PRG;
the PRG ring remains retained at offset 2 until the GPIO payload stage.

The first instructions are `SEI`, `LDA #DDR`, `STA $00`: WRITE is input after
seven CPU cycles, using DDR `$27` on C64 and `$09` on TED. The bootstrap then
blanks the screen; C64 additionally disables sprites and CIA interrupt sources.

## Mode selection

The stub temporarily makes WRITE an output and sends the physical word `$CA65`,
MSB first, sampled by ESP on each MOTOR-on transition. Each MOTOR-off interval
exceeds 1 ms even at fast TED NTSC speed. An additional MOTOR-on interval
separates bits. The 7501 output inversion is accounted for in the stub.

The ESP keeps WRITE input throughout selection. Upon the `110` signature
prefix, its MOTOR ISR stops the I2S transmitter and switches READ high to GPIO,
well before the stub clears stale FLAG events. DMA allocation cleanup occurs
later in loop, not in the ISR. This prevents queued standard pulses or the idle
level from creating a false first fast strobe. The complete `$CA65` word arms
the receiver download. The final MOTOR-on is not itself a block request.

Immediately after the final signature bit, the computer releases WRITE again,
without the usual final MOTOR-on delay, and requests the first receiver page
by changing MOTOR. ESP starts driving data only after that request and its
settling delay. WRITE stays input on the computer throughout both downloads.
The full bootstrap receiver never repeats the cold monitor test's WRITE-output
initialization.

## Receiver download

The small stub uses simple MSB-first pairs: b7:b6, b5:b4, b3:b2, b1:b0.
It requests whole 256-byte pages with no descriptor; address and page count
are assembled into the stub. Default hold and byte pause are both 80 µs;
setup and pair gap are each 3 µs. All are configurable in the ESP settings.
After the last page, the sender changes to the optimized descriptor/payload
protocol in [README.md](README.md).

C64 uses screen RAM for the full receiver. TED uses color RAM: `$0400` contains
executable BASIC helpers and system state which must survive for RUN. The
receiver normalizes MOTOR to on before requesting its descriptor with off;
the sender ignores that normalization edge after the last bootstrap page.
A request arriving during the final pair hold remains pending.

## Payload and handoff

The descriptor contains destination, length and entry as little-endian words.
The optimized receiver reads up to 256 bytes per MOTOR transition. It owns
`$F7..$FF` and the stack. Pair assembly is inline, Y indexes the destination
and counts bytes, and only page rollover increments the pointer high byte.

Once the payload is stored, the receiver copies a small handoff trampoline
into the now-unused tape buffer. It restores ports/timers, KERNAL and BASIC
vectors, initializes the screen, restores program bounds, clears cassette
status and keyboard input, and launches the program. Screen initialization
may overwrite the receiver without overwriting the trampoline. Entry zero
selects BASIC CLR/RUN; any other entry selects JMP with IRQs enabled.
The ESP releases WRITE/SENSE after the final data hold before ROM resumes.

The harness exports ordinary periods and a TAP for stage one. That TAP alone
cannot load the PRG: the complete sender must recognize the signature and
supply the two fast stages. TED's ordinary pulses begin LOW, with LOW/HIGH
halves belonging to the same period. Starting at the other phase would pair
adjacent pulse lengths and prevent the ROM from recognizing the header.

## Reproducing the complete load

```sh
python3 experiments/fastloader/run-bootstrap-vice.py --machine all
python3 experiments/fastloader/run-bootstrap-vice.py --machine all --ntsc --mode machine
python3 experiments/fastloader/run-bootstrap-matrix.py
```

Monitor commands only type LOAD and observe execution/memory. ROM device-1
traps are disabled. Tests compare every downloaded receiver/payload byte and
confirm BASIC POKE or machine-code marker execution after ROM restoration.
The 32-case matrix covers three machines, both video standards, both launch
paths, block/page boundaries, unaligned destinations, maximum RAM payloads and
both C64 CIA variants. The ESP GPIO implementation and configurable delays also
have host wire-order tests. C64 PAL fastloading was confirmed on real hardware
on 2026-11-04; other hardware variants still need testing.

There is no payload CRC or automatic retransmission. STOP releases the ESP
outputs; reset the computer before retrying an interrupted receiver. The
current ROM entry points target stock C64 BASIC and TED BASIC 3.5.
