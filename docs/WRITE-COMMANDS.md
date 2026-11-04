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

A GPIO rising-edge ISR timestamps full WRITE periods while MOTOR is on and
GPIO2 belongs to the command receiver. A bounded 512-entry ring transfers the
periods to the main loop; protocol decoding runs outside the ISR. Pulse timing
automatically distinguishes C64 and C16/Plus/4.

A request is published only after checking:

- first-copy countdown 137..129 and repeat countdown 9..1;
- all 192 header bytes, byte parity and both XOR checksums;
- identical header copies;
- header type 1 or 3 and an end address greater than the start address;
- C64 terminators, or the supported native TED block endings.

After a verified header, further data is ignored until a new MOTOR session.
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
node tests/player_index.mjs
make build PROFILE=1m
```

Host tests cover C64 and TED recordings, native TED endings, parity/checksum
errors, mismatched copies, suspension/resumption, ignored program data,
non-command names, maximum-length requests and overflow. Browser tests check
request status/history and the fastloader suspension display. The ESP build
uses the existing I2S mode which does not drive clock outputs on GPIO2.
Physical WRITE reception and timing still need validation on the actual board.
