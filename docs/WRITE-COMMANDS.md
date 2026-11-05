# WRITE command request channel

GPIO2/WRITE has two roles: the computer sends command requests using standard
ROM tape headers, and the ESP drives WRITE as one of the two data bits during
fastloading. Command reception is enabled in the normal hardware firmware;
no separate WRITE build profile is required. Soft-GPIO builds cannot receive
physical commands.

Reception uses the [Timer1/NMI sampler](../experiments/write/NMI.md), validated
on real C64 PAL hardware with READ idle and during streaming.

This restores the previous header receiver as a command transport. It does
not record or store programs. **Requests are published, but command handlers
are not implemented yet.** No deck, network or filesystem action follows a
received request. `ESP:TEST` is a useful example for checking reception, not
an executable operation.

## Quick reminder

- `SAVE "NAZWA",1`: display the last verified filename; no command is published.
- `SAVE "ESP:TEST",1`: publish request `TEST`; no handler executes it yet.
- The ESP stores neither program. Press PLAY to supply SENSE for a real SAVE.
- Reception requires PLAY, MOTOR on, and no active fastloader or OTA ownership.
- Ordinary READ playback can run at the same time; no ejection is required.

The browser demo has **Simulate SAVE "NAZWA"** and **Simulate ESP:TEST** buttons.
They reproduce UI metadata and fastloader ownership without hardware.

## Sending a request

1. Press PLAY in the browser. A tape or PRG may remain inserted, and ordinary
   READ playback may continue. PLAY supplies SENSE; the computer controls MOTOR.
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

Timer1 samples physical MOTOR, WRITE and SENSE every 40 µs through an NMI
callback. It measures same-polarity full periods, C64 rising and TED falling,
and uses the unchanged decoder's 32-pulse machine selection. The sampler and
decoder run in IRAM, with fixed small state and no pulse queue, heap allocation
or logging. The hardware timer is divided by 16 from 80 MHz and auto-reloads;
CPU turbo changes do not alter the requested sample cadence. The pinned SDK's
`system_get_time()` is an IRAM-only RAM/register read and supplies microsecond
timestamps. Callback work is additionally measured with CCOUNT, normalized
for the CPU frequency at NMI entry.

The sampler owns the decoder while active. The ordinary MOTOR ISR remains
responsible for READ/DMA and the fastloader, and does not mutate WRITE state.
`noInterrupts()` cannot protect against NMI: `/state` instead takes a
sequence-checked snapshot, with at most eight attempts before HTTP 503.
Main-loop lifecycle changes first disable the callback before changing its
state or detaching Timer1. PLAY release, fastloader ownership and OTA stop
sampling; none of these changes reconfigure I2S/DMA or disable Wi-Fi.

If an active sampling interval exceeds 80 µs, its gap counter increments.
An in-progress command frame is rejected with `capture_gap`; gaps while
searching for a pilot reset framing without publishing anything. See the
[NMI guide](../experiments/write/NMI.md) for limitations and diagnostics.

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
after that quiet interval resets an unfinished discard. MOTOR sampling handles
even brief TED pauses independently of HTTP polling.

A damaged, partial or overflowing capture cannot publish a request. There is
no per-byte command acknowledgement on READ or SENSE; reception is observable
in the web UI and `/state`. The decoder keeps the last verified request and
its ID across subsequent bad/non-command headers. IDs reset when ESP restarts.

## GPIO ownership during fastloading

Command reception is suspended for the **entire selected fastloader session**:
from inserting a fast PRG, through standard LOAD, mode signature and both fast
stages, until completion, cancellation or ejection releases that session.
After completion, reception is permitted with a PRG
still inserted. Normal READ playback is also permitted.

Timer1/NMI sampling is stopped while fastloading. The sampler also checks
ownership and physical SENSE before decoding, so ESP-generated output cannot
enter the decoder during a mode transition. Suspending resets the partial
frame. The decoder itself
rejects input while suspended. After completion or cancellation, GPIO2 returns
to input; reception can restart once PLAY is pressed and MOTOR is on. The fastloader's `$CA65` mode
signature remains a separate protocol, not a standard WRITE command header.

Previously verified filename/request history is retained while capture is
suspended. The sampler continues to require the same complete validated header
even during ordinary READ playback.

## State API

`GET /state` exposes:

| Field | Meaning |
| --- | --- |
| `writeEnabled` | Physical WRITE capture is available in this build |
| `writeListening` | Sampler is enabled with PLAY pressed, MOTOR on and GPIO2 available |
| `writeCaptureMode` / `writeAllowsRead` | `nmi-40us` capture and concurrent ordinary READ capability |
| `writeSampleCount` / `writeSampleMaxGapUs` | Sample count and largest observed interval, including idle MOTOR |
| `writeSampleGaps` / `writeSampleMaxWorkUs` | Active gaps over 80 µs and maximum normalized callback body time |
| `writeStatus` | Decoder status, including `capture_gap`, `suspended_ota`, `suspended_fastloader`, `command_received_no_action`, `header_ignored`, or an error |
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
copies a small decoder snapshot using a sequence counter, then formats
and sends it without masking NMI. Its JSON arena is a bounded temporary
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
VICE before the framing fix. On 5 November 2026, the Timer1/NMI receiver
was confirmed working on real C64 PAL hardware both with READ idle and while
streaming. C16/Plus/4 and NTSC hardware validation remains pending; VICE alone
does not validate ESP interrupt latency, Wi-Fi load or electrical timing.
