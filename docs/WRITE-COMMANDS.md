# WRITE command request channel

GPIO2/WRITE has two roles: the computer sends command requests using standard
ROM tape headers, and the ESP drives WRITE as one of the two data bits during
fastloading. Command reception is enabled in the normal hardware firmware;
no separate WRITE build profile is required. Soft-GPIO builds cannot receive
physical commands.

This restores the previous header receiver as a command transport. It does
not record or store programs. **Requests are published, but command handlers
are not implemented yet.** No deck, network or filesystem action follows a
received request. `ESP:TEST` is a useful example for checking reception, not
an executable operation.

## Quick reminder

- `SAVE "NAZWA",1`: display the last verified filename; no command is published.
- `SAVE "ESP:TEST",1`: publish request `TEST`; no handler executes it yet.
- The ESP stores neither program. Press PLAY to supply SENSE for a real SAVE.
- Reception requires MOTOR high on every captured edge and no selected fastloader session.

The browser demo has **Simulate SAVE "NAZWA"** and **Simulate ESP:TEST** buttons.
They reproduce the UI metadata and fastloader ownership rules without hardware.

## Sending a request

1. Eject the current tape/PRG in the browser, then press PLAY. No source file
   needs to be inserted. PLAY supplies SENSE; the computer controls MOTOR.
2. On C64 or C16/Plus/4, enter:

   ```basic
   SAVE "ESP:TEST",1
   ```

3. Wait for the computer's normal SAVE operation to finish. In **Playback
   details**, WRITE commands should show **Request received (not executed)**,
   and the last request should be `ESP:TEST` with an increased request ID.

SAVE is used only to make the computer emit its standard WRITE waveform.
The ESP shows every verified header filename in **Playback details**, including
ordinary names such as `SAVE "NAZWA",1`. Only `ESP:` names publish command
requests. It decodes the header and ignores the following program bytes; it does
not create a file or retain a copy of the program in RAM or LittleFS. Ordinary
filenames without the exact uppercase `ESP:` prefix are ignored as commands.
The prefix must be followed by a nonempty request, up to 12 characters, within
the standard 16-character filename. Padding spaces at the end are removed.
Non-printable filename bytes are displayed as `?`.

## Framing and validation

A GPIO CHANGE ISR timestamps full, same-polarity WRITE periods while MOTOR is
on and GPIO2 belongs to the command receiver. **WRITE is wired directly, with
no inverter.** C64 ROM symbols are measured between rising edges; C16/Plus/4
ROM symbols are measured between falling edges. The decoder requires 32
consecutive short pilot periods to select the family and its edge polarity,
then ignores the other polarity. It does not infer the machine from one long
pulse: TED rising-edge periods can resemble C64 byte markers.

A bounded 512-entry ring transfers periods and edge polarity to the main loop;
protocol decoding runs outside the ISR. Each entry holds a 15-bit microsecond
period and one polarity bit; long gaps saturate at 32767 us. The ISR
checks the physical MOTOR level on every edge, not only when a session starts:

```cpp
if (!writeActive || !writeCommandsAllowed() || !(GPI & (1u << PIN_MOTOR))) return;
```

`writeCommandsAllowed()` accepts only fastloader phases OFF or DONE. The
interrupt is detached in all other phases, including the standard bootstrap.

A request is published only after checking:

- first-copy countdown 137..129 and repeat countdown 9..1;
- all 192 header bytes, byte parity and both XOR checksums;
- identical header copies;
- header type 1 or 3 and an end address greater than the start address;
- the first C64 block terminator, or the supported native TED block ending.

The second copy is accepted immediately after its verified checksum. C64 ROM
SAVE turns MOTOR off before the final terminator's last rising edge, so
requiring that edge while also gating capture on MOTOR loses a valid header.

After a verified header, the decoder counts but never stores the bytes in both
following program copies (including countdowns and checksums). Their contents,
parity failures and capture overflows cannot publish requests or overwrite the
verified result. ROM SAVE briefly toggles MOTOR between the header and program
records: about 32 ms on C64 and under 1 ms on TED in the captured traces. This
pause preserves the discard state. A subsequent SAVE can begin as soon as the
two program copies have been consumed. If SAVE is aborted or program capture
is damaged, leave MOTOR off for at least one second before retrying; a restart
after that quiet interval resets an unfinished discard. Brief TED pauses can
be missed by main-loop polling without losing the discard state.

A damaged, partial or overflowing capture cannot publish a request. There is
no per-byte command acknowledgement on READ or SENSE; reception is observable
in the web UI and `/state`. The decoder keeps the last verified request and
its ID across subsequent bad/non-command headers. IDs reset when ESP restarts.

## GPIO ownership during fastloading

Command reception is suspended for the **entire selected fastloader session**:
from inserting a fast PRG, through standard LOAD, mode signature and both fast
stages, until completion, cancellation or ejection releases that session.
Before using WRITE commands again, finish the transfer or eject the fast PRG.

The WRITE interrupt is detached while fastloading to avoid interrupts from
ESP-generated data. The ISR also checks ownership before queuing any edge, so
output data cannot enter the command ring during a mode transition. Suspending also disables capture, discards
queued durations and resets the partial decoder frame. The decoder itself
rejects input while suspended. After completion or cancellation, GPIO2 returns
to input and reception can restart on MOTOR. The fastloader's `$CA65` mode
signature remains a separate protocol, not a standard WRITE command header.

## State API

`GET /state` exposes:

| Field | Meaning |
| --- | --- |
| `writeEnabled` | Physical WRITE capture is available in this build |
| `writeListening` | Capture is currently armed with MOTOR on and GPIO2 available |
| `writeStatus` | Decoder status, including `suspended_fastloader`, `command_received_no_action`, `header_ignored`, or an error |
| `writeCommand` | Last verified request without the `ESP:` prefix |
| `writeCommandId` | Count of verified command requests since boot |
| `writeName` / `writeMachine` | Last verified header filename and detected computer family |
| `writeType` / `writeStart` / `writeEnd` | Header metadata; never a request to create a file |
| `writeHeaders` | Count of all verified headers, including ignored ordinary filenames |
| `writeErrors` / `writeOverflows` | Invalid-frame and capture-overflow counters |

`writeCommandId` changes only for a complete verified `ESP:` request. Pollers
should use that ID to distinguish a new request from repeated state responses.
`writeType`, `writeStart` and `writeEnd` are decoder diagnostics and may describe
a partial frame; only the command/ID pair is published after full validation.

## Tests and hardware status

```sh
c++ -std=c++17 -Isrc tests/write_decoder_test.cpp src/write_decoder.cpp -o /tmp/write-command-test
/tmp/write-command-test
python3 -m unittest discover -s tests -p 'test_write_vice.py' -v
node tests/player_index.mjs
make build PROFILE=1m
```

Host tests cover C64 and TED recordings, native TED endings, parity/checksum
errors, mismatched copies, suspension/resumption, ignored program data,
non-command names, maximum-length requests and overflow. Browser tests check
request status/history and the fastloader suspension display. The ESP build
uses the existing I2S mode which does not drive clock outputs on GPIO2.

[VICE WRITE tests](../experiments/write/README.md) execute the actual KERNAL
`SAVE "TEST",1` and `SAVE "ESP:TEST",1`, with device-1 traps disabled, on C64,
C16 and Plus/4 in PAL and NTSC. Twelve raw physical-edge captures are retained
as small compressed delta-event fixtures for ROM-free CI replay. Tests also
exercise consecutive SAVEs, motor gating, incorrect polarity, missing edges,
truncation, timestamp jitter, `micros()` wraparound and aborted-SAVE recovery.

The previous receiver failed a real C64 PAL `SAVE "TEST",1` with `pulse_error`
and `unexpected_terminator`. The missing-final-edge failure was reproduced in
VICE before this fix. The revised path still needs a hardware retest; VICE
does not validate ESP interrupt latency, Wi-Fi load or electrical timing.
