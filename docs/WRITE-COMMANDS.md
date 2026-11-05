# WRITE command request channel

GPIO2/WRITE has two roles: the computer sends command requests using standard
ROM tape headers, and the ESP drives WRITE as one of the two data bits during
fastloading. Command reception is enabled in the normal hardware firmware;
no separate WRITE build profile is required. Soft-GPIO builds cannot receive
physical commands.

This restores the previous header receiver as a command transport. It does
not record or store programs. **Requests are published, but command handlers
are not implemented and no uses have been selected yet.** No deck, network or filesystem action follows a
received request. `ESP:TEST` is a useful example for checking reception, not
an executable operation.

## Quick reminder

- `SAVE "NAZWA",1`: display the last verified filename; no command is published.
- `SAVE "ESP:TEST",1`: publish request `TEST`; no handler executes it yet.
- The ESP stores neither program. Press PLAY to supply SENSE for a real SAVE.
- Reception requires an empty deck, MOTOR high on every captured edge, and no active fastloader ownership.

The browser demo has **Simulate SAVE "NAZWA"** and **Simulate ESP:TEST** buttons.
They reproduce the UI metadata and empty-deck requirement without hardware.

## Sending a request

1. Eject the current tape/PRG in the browser, then press PLAY. The deck must
   stay empty, including after a completed fastload. PLAY supplies SENSE; the computer controls MOTOR.
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
then switches the hardware interrupt to that single polarity, halving the
interrupt rate. It does not infer the machine from one long
pulse: TED rising-edge periods can resemble C64 byte markers.

The fixed-size decoder runs directly in the GPIO ISR, entirely in IRAM. It
uses no heap allocation, logging, flash calls or pulse queue. The former
512-entry queue consumed 1 KB of RAM and depended on synchronous HTTP handling
returning within roughly 100 ms; it has been removed. Both WRITE and MOTOR
events are decoded without waiting for the main loop. Long periods saturate
at 32767 us. The ISR
checks the physical MOTOR level on every edge, not only when a session starts:

```cpp
if (!writeActive || !writeCommandsAllowed() || !(GPI & (1u << PIN_MOTOR))) return;
```

`writeCommandsAllowed()` requires `TapeSource::Kind::EMPTY`, fastloader phase
OFF or DONE. It reads the source kind directly through a
volatile inline load in IRAM. The interrupt is detached whenever any TAP, PRG
or embedded demo is present, even while stopped, buffering, paused, failed or
finished. Checking only `tapeLoaded` would miss an inserted, partly buffered
stream. Insertion blocks ISR decoding immediately; the loop then detaches the
interrupt and clears the partial frame. Ejection re-arms capture and samples
the current MOTOR level.
OTA is disabled in the validated firmware; no network update can run during
command reception.

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
after that quiet interval resets an unfinished discard. MOTOR's ISR handles
even brief TED pauses independently of HTTP polling.

A damaged, partial or overflowing capture cannot publish a request. There is
no per-byte command acknowledgement on READ or SENSE; reception is observable
in the web UI and `/state`. The decoder keeps the last verified request and
its ID across subsequent bad/non-command headers. IDs reset when ESP restarts.

## GPIO ownership during fastloading

Command reception is suspended for the **entire selected fastloader session**:
from inserting a fast PRG, through standard LOAD, mode signature and both fast
stages, until completion, cancellation or ejection releases that session.
Before using WRITE commands again, eject the fast PRG; completion alone keeps
the file inserted and WRITE disabled.

The WRITE interrupt is detached while fastloading to avoid interrupts from
ESP-generated data. Both ISRs also check ownership before decoding, so output
data cannot enter the command decoder during a mode transition. Suspending
disables capture and resets the partial decoder frame. The decoder itself
rejects input while suspended. After completion or cancellation, GPIO2 returns
to input; reception can restart on MOTOR once the deck is empty. The fastloader's `$CA65` mode
signature remains a separate protocol, not a standard WRITE command header.

The page displays **Eject tape to receive WRITE** for every inserted source.
Blocked READ playback cannot increment WRITE errors or publish new headers;
previously verified filename/request history is retained.

## State API

`GET /state` exposes:

| Field | Meaning |
| --- | --- |
| `writeEnabled` | Physical WRITE capture is available in this build |
| `writeListening` | Capture is armed with an empty deck, MOTOR on and GPIO2 available |
| `writeStatus` | Decoder status, including `requires_empty_deck`, `suspended_fastloader`, `command_received_no_action`, `header_ignored`, or an error |
| `writeCommand` | Last verified request without the `ESP:` prefix |
| `writeCommandId` | Count of verified command requests since boot |
| `writeName` / `writeMachine` | Last verified header filename and detected computer family |
| `writeType` / `writeStart` / `writeEnd` | Header metadata; never a request to create a file |
| `writeHeaders` | Count of all verified headers, including ignored ordinary filenames |
| `writeErrors` / `writeOverflows` | Invalid-frame counter; legacy overflow field stays zero because there is no pulse queue |
| `resetReason` | ESP8266 reset code: 0 power-on, 1 hardware watchdog, 2 exception, 3 software watchdog, 4 software restart, 5 deep sleep, 6 external reset |
| `resetException` / `resetPc` | Exception cause and program counter, present after reset reason 2 |
| `freeStack` | Continuation-stack unused-space watermark, in bytes |

`writeCommandId` changes only for a complete verified `ESP:` request. Pollers
should use that ID to distinguish a new request from repeated state responses.
`writeType`, `writeStart` and `writeEnd` are decoder diagnostics and may describe
a partial frame; only the command/ID pair is published after full validation.

The web diagnostics show the last reset reason, exception location when
available, heap and stack watermark, so UART access is not required. `/state`
copies a small decoder snapshot under a brief interrupt lock, then formats
and sends it with interrupts enabled. Its JSON arena is a bounded temporary
heap allocation; placing a 2 KB arena on the ESP8266's 4 KB continuation stack
left too little space for HTTP/TCP and GPIO interrupt frames. The response
String reserves its final size once, avoiding repeated buffer reallocations.
Allocation failure returns HTTP 503 rather than proceeding with an empty
document. Build checks verify the decoder's IRAM placement and reject direct
flash calls from the WRITE/MOTOR ISR paths.

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
VICE before the framing fix. On 2026-11-05, header and command reception were
confirmed on real C64 PAL hardware with an empty deck. READ streaming and WRITE
reception are not supported together. C16/Plus/4 and NTSC still need hardware
validation; VICE does not validate ESP interrupt latency or electrical timing.
