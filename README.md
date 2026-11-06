# ESPsette

**[Open the browser demo](https://ytmytm.github.io/ESPsette/)** — simulated ESP, no hardware needed.

**A Wi-Fi cassette deck for your Commodore, built around an ESP-01.** Open its page, drop a tape file and type `LOAD` on the computer. Your browser streams the file over Wi-Fi; the ESP8266 turns it into timed I2S pulses on the datasette READ line. The computer controls MOTOR, and the page controls the virtual PLAY key through SENSE.

Small as a *pesette* — a little scale for your LOADs.

Files stay in the browser and pass through a 12 KB buffer on the ESP. No SD card or tape storage in flash is needed.

![Cassette in the web player](web/assets/cassette.webp)

## Tested and pending

Hardware results confirmed on **C64 PAL, 1–6 November 2026**:

| Feature | Status |
| --- | --- |
| C64 PAL: TAP streamed from the browser | Passed on real hardware, including Rambo III Side 1 with repeated MOTOR pauses; latest test uses a 12 KB source buffer and Wi-Fi power saving disabled (6 November) |
| C64 PAL: PRG → standard tape pulses | Passed on real hardware |
| C64 PAL: PRG + fastloader | Passed on real hardware |
| Embedded Circles PRG, C64 PAL | Standard and fast modes passed on real hardware |
| C64 PAL: WRITE header and command requests | Passed on real hardware with an empty deck; eject TAP/PRG first |
| C16/Plus/4 and NTSC: PRG loading and WRITE reception | Host/VICE tests pass; hardware tests pending |
| C16/Plus/4 TAP playback; all supported TAP v1/v2 variants | Full hardware coverage pending |
| IDX seeking and counter-based REW/FF | Host tests pass; hardware tests pending |
| OTA updates, ESP-01 1 MB | Passed on real hardware over IP with mDNS disabled; Rambo III Side 1 also passed after updating (6 November) |

WRITE uses GPIO edge interrupts and requires an empty deck. The Timer1/NMI
experiment remains archived. Compressed OTA is enabled with mDNS disabled.
Updating over IP and subsequent C64 PAL TAP playback passed on real hardware.

## Hardware

The [KiCad project](kicad/espsette.kicad_pro) includes the schematic, routed PCB and project-local libraries. See the [wiring notes](kicad/WIRING.md) and [connection plan](kicad/PLAN_POLACZEN.md) for the selected circuit.

- [Schematic PDF](kicad/plots/espsette.pdf)
- [Gerber and drill files](kicad/gerbers/)
- [Local symbols, footprints and 3D models](kicad/libraries/README.md)

![ESPsette PCB — top view](media/01.pcbtop.png)

![ESPsette PCB — side view](media/02.pcbside.png)

## Build

Requires `make`, `curl`, Python 3 and **ACME** (6502 assembler), plus internet access for the initial tool download.
Install ACME with `sudo apt-get install acme` on Debian/Ubuntu or `brew install acme` on macOS. Host tests also use a C++17 compiler and Node.js. On macOS, install the Command Line Tools if needed (`xcode-select --install`). Run from the repository root:

```sh
make setup                    # Install project tools once
make native                   # Assemble C64 and C16/Plus4 loaders with ACME
make build PROFILE=1m         # Assemble loaders and build for a 1 MB ESP-01
```

`make setup` installs project tools under `tools/bin/` and
`~/.cache/ESPsette/`, with separate Python environments for each OS. No global
PlatformIO install is required. The default build includes cassette GPIO
support, the Circles PRG demo and compressed OTA. See [platformio.ini](platformio.ini).

| Profile | Use |
| --- | --- |
| `1m` (default) | 1 MB flash, 64 KB LittleFS; compressed OTA enabled |
| `4m` | 4 MB flash; OTA enabled |
| `512k` | Current firmware exceeds 512 KB; use a larger module |

Release packages target 1 MB modules.

## Embedded demo

**Demo** inserts [Circles](https://csdb.dk/release/?id=262238), music by
**Shogoon**, code by **YTM**. It is a **C64 PAL** PRG, always loaded with that
profile regardless of the saved default for uploaded files. With **Use
fastloader for PRG** off, use normal `LOAD`, then `RUN`. With it on, `LOAD`
autostarts the bootstrap, fastloads the PRG and runs it. Changing the switch
while the embedded demo is stopped reinserts it in the selected mode.

The [7744-byte PRG](assets/demo/circles.prg) is stored once in flash; standard
loading generates both tape copies without storing a TAP.
[Credits and checksum](assets/demo/README.md) identify the source.
`scripts/embed-demo.py` generates its flash header during the build.

## PRG fastloader

Enable **Use fastloader for PRG** in **Playback settings** (off by default).
The switch saves immediately and reinserts a stopped PRG in the selected mode.
Stop active transfers before changing it. TAP files keep their recorded timing.

Normal `LOAD` starts a small bootstrap through the ROM tape protocol. The
bootstrap then receives the fast receiver and PRG over READ, WRITE and SENSE,
driven directly by GPIO with I2S stopped. Supported PRG ranges are
`$0801..$7FFF` on C64 and `$1001..$3FFF` on C16/Plus/4. Programs start with BASIC
RUN unless the session API supplies a decimal machine-code `entry` address.

**Advanced fastloader timings** contains the numeric delays in microseconds,
including separate C64/TED hold and byte-gap values. **Save fastloader settings**
applies them to the next PRG session; **Default timings** fills in the defaults
for saving. The switch and timings persist in LittleFS (`/fastloader.json`).
Missing or invalid settings use the defaults. If loading fails on hardware,
increase the delays or disable fastloading.

Native sources live in [`native/fastloader/`](native/fastloader/).
`make native` generates the ignored `include/fastloader_native.h`; PlatformIO's
pre-build hook regenerates it automatically for all profiles. A post-link check
verifies that the GPIO transmitter and busy wait reside in IRAM. The firmware
GitHub Actions workflow installs ACME, assembles both targets, runs sender,
settings, wire-order and cycle-budget tests, then builds the ESP image.
[Protocol, timing and emulator tests](experiments/fastloader/README.md) describe
the three stages and the limits of simulation. C64 PAL fastloading has been
confirmed on real hardware; other models/video standards await verification.

During fastloading, the deck shows payload bytes sent/remaining, percentage,
measured kB/s and estimated time remaining. Progress follows completed ESP
payload blocks. Buffer and receiver waits pause the reels and progress while
keeping screen sleep protection active.

The counter advances by the equivalent standard tape duration, so it runs much
faster than real time. Reels also spin faster during the fast stages, unless
reduced motion is enabled. Completed PRGs stay at 100% until rewound or ejected.

## WRITE command requests

The ESP can receive request names through standard SAVE headers.
`SAVE "ESP:TEST",1` publishes `TEST` in **Playback details** and `/state`.
Only the transport is implemented: there are no command handlers or planned
uses yet, and receiving a request triggers no action.

**Eject the TAP/PRG before sending a request.** The deck must remain empty,
including after a paused or completed load; STOP alone is not enough. Then
press PLAY in the browser to supply SENSE and enter SAVE on the computer.
The receiver checks MOTOR on each edge and detaches its interrupt while a
source is inserted or the fastloader owns GPIO2.

Ordinary names such as `SAVE "NAZWA",1` are displayed without publishing a
command. Program bytes are discarded; the ESP records no file.
See the [WRITE protocol](docs/WRITE-COMMANDS.md) for framing, validation and GPIO
ownership.

## Flash over USB-UART

Download the `.bin` from [Releases](https://github.com/ytmytm/ESPsette/releases), or build it locally. Each release includes `FLASHING.txt` with the command for flashing the binary at `0x0`, plus SHA-256 checksums. The released image targets a **1 MB ESP-01** and includes Circles; no separate filesystem image is needed.

Disconnect the ESP-01 from the Commodore interface. Use **3.3 V power and 3.3 V UART logic**, with a supply able to handle Wi-Fi current peaks. On a USB-C Mac, use a suitable adapter for your USB-UART programmer.

CH340G adapters such as [4tronix USB340G](https://shop.4tronix.co.uk/products/usb340g) have a voltage switch: **select 3.3 V before powering** and verify VCC and signal levels on the actual adapter. Our planned PCB header matches `GND / CTS / VCC / TXD / RXD / DTR`; CTS is unused. **Adapter TXD connects to ESP RXD, and adapter RXD to ESP TXD** (the PCB routes this crossover).

If the CH340 adapter does not appear as a serial port, check its driver. [HobbyComponents CH340 drivers](https://github.com/hobbycomponents/ch340-drivers) provides Windows drivers and notes that Linux usually includes a suitable driver.

**Keep GPIO0 shorted to GND throughout flashing. Connect the adapter's DTR to ESP RST.** These are the connections used by our CH340/wemos.cc setup and [esptool.cfg](esptool.cfg):

| USB-UART / supply | ESP-01 |
| --- | --- |
| 3.3 V | VCC and CH_PD / EN |
| GND | GND **and GPIO0** (flash jumper) |
| TXD | RXD / GPIO3 |
| RXD | TXD / GPIO1 |
| **DTR** | **RST** |
| No connection | GPIO2 (must remain HIGH at reset), CTS, RTS |

The configured reset sequence pulses **DTR only**; it does not use RTS. For a programmer without DTR, enter flash mode by briefly pulling RST to GND while GPIO0 is grounded.

```sh
ls /dev/cu.*                                  # Find the port on macOS
make flash-id PORT=/dev/cu.usbserial-XXXX       # Check the actual flash size
make flash PROFILE=1m PORT=/dev/cu.usbserial-XXXX
```

`make flash` also builds the firmware. On Linux, use your port such as `/dev/ttyUSB0`. `make chip-info PORT=...` prints additional chip information.

**After flashing:** remove the GPIO0–GND jumper, disconnect DTR–RST and reset the module. GPIO0 must be HIGH at reset to run the firmware. See [wiring notes and ESP-01 connections](kicad/WIRING.md) before reconnecting the Commodore interface.

## OTA and firmware version

OTA is enabled for 1 MB and 4 MB modules. If the installed firmware has OTA
disabled, first install this version over USB-UART with `make flash`.
Refresh the player page after flashing.

OTA uses the device IP address; mDNS discovery is disabled.
The VFD Wi-Fi icon shows the ESP-to-router signal; hover for RSSI in dBm.
Wi-Fi power saving is disabled to reduce delivery latency.
During PLAY, compact status and upload replies update the VFD and feed uploads. Full
diagnostics and the waveform refresh after STOP, completion or error.
Stop MOTOR and transfers before updating:

```sh
make ota PROFILE=1m OTA_HOST=DEVICE_IP
```

The upload uses `firmware.bin.gz`; the ESP8266 bootloader decompresses it on
reboot. Use the uncompressed `.bin` for UART flashing. Each build checks that
the sector-aligned running image and gzip upload fit before LittleFS. OTA
preserves settings and Wi-Fi configuration. It releases PLAY and suspends
WRITE capture while writing flash. Updates use the local network without a
password by default; optional authentication is described in
[`include/local_ota.h.example`](include/local_ota.h.example).

**Network / OTA → Firmware** shows the running ESP's 12-character Git commit ID.
The page gets it from the ESP, including after an OTA reboot. `-dirty` means
tracked files differed from that commit at build time. `make build`, `make
flash` and `make ota` print the ID, including when the build is cached.
`make version` prints the checkout's ID without building.

## Load a tape

1. Connect MOTOR → GPIO0, SENSE ← GPIO1 and READ ← GPIO3 through the interface described in [kicad/WIRING.md](kicad/WIRING.md). C16/Plus/4 needs a 1531 connector or adapter.
2. On first boot, join `ESPsette-XXXXXX` and configure Wi-Fi in the captive portal. The device falls back to its AP if the saved network fails. No Wi-Fi credentials are built in.
3. Open `http://DEVICE_IP/` (find the IP in your router). The default hostname is `ESPsette`; `.local` resolution is not guaranteed.
4. Choose **Demo** or insert a TAP/PRG using the file picker or drag and drop. PRGs use the saved computer profile and filename as the tape name. For TAP navigation, add an Ultimate-style `.idx` file, select a position and press **JUMP**, or use **PREV/NEXT**. The counter still shows time within the whole tape.
5. Press PLAY in the browser and type `LOAD` on the Commodore. **Autoplay** presses PLAY automatically when a file is inserted. MOTOR starts and pauses playback, including the pause after the header.

The four profiles use the tape clocks from [VICE `vice/src/tape/tap.c`](https://github.com/VICE-Team/svn-mirror/blob/main/vice/src/tape/tap.c):

| Profile | Tape clock |
| --- | ---: |
| C64 PAL | 985248 Hz |
| C64 NTSC | 1022730 Hz |
| C16 PAL | 886724 Hz |
| C16 NTSC | 894886 Hz |

C16 cassette timers use half the CPU clock. TAP v1/v2 selects PAL/NTSC from
header byte 14; the PRG profile does not override TAP metadata. Saved `c64` and
`c16` settings from older firmware mean PAL.

Keep the browser tab visible while streaming. The page requests screen sleep protection
during playback, including fastloader MOTOR pauses and buffer waits. STOP,
completion, errors and disconnection release that protection. Its status is
shown in **Playback details**; use **Keep screen awake** if the browser requires
a tap. HTTPS/localhost uses the
[Wake Lock API](https://developer.mozilla.org/en-US/docs/Web/API/Screen_Wake_Lock_API);
ESP HTTP uses a [silent-video fallback](web/assets/wake/README.md). Neither
prevents manual locking, lid closure or OS suspension.

PLAY and STOP control SENSE; the Commodore controls MOTOR. STOP springs back
when released, leaving the deck stopped. REW/FF work with MOTOR stopped.
Settings live in LittleFS; tape files stay in the browser. The page has no login
and is intended for a trusted LAN.

## Debug and local UI preview

Expand **Playback details → Debug / ESP state** on the page to inspect reset
reason, stack/heap headroom, transfer state and error counters.

Normal firmware has no UART log: GPIO1 and GPIO3 are used for SENSE and READ.

Run `make preview` or open [web/player.html](web/player.html) to use simulated
ESP state. Fonts and graphics are bundled for offline use.

Preview controls include **Fastload PRG demo**, **Pause receiver**,
**Simulate SAVE "NAZWA"** and **Simulate ESP:TEST**. They exercise progress,
reels, sleep protection and received WRITE metadata without contacting hardware.
WRITE simulation requires an empty deck, matching the firmware. Preview settings use browser local storage.

For seeking tests, use the two-minute [ui-test.tap](tests/fixtures/ui-test.tap)
and its [IDX](tests/fixtures/ui-test.idx) with six navigation markers.
See [tests/README.md](tests/README.md) for host tests and `make help` for targets.

The [hosted demo](https://ytmytm.github.io/ESPsette/) uses the same page with
simulated state and downloadable samples. **Demo** loads Circles as C64 PAL
in the selected standard/fast mode. GitHub Pages updates after UI changes on
`main` through `.github/workflows/demo.yml`.

## CI and releases

GitHub Actions runs host tests and compiles the default 1 MB firmware on code pushes, pull requests and manual runs. Download the package from the run's **Artifacts** section; it contains the `.bin`, debug ELF, build metadata, checksums and flashing instructions.

To release, publish a GitHub Release with a tag such as `v2026-11-01` pointing to the desired commit. The workflow builds that tag and automatically attaches the same files to the release. Tags must use `vYYYY-MM-DD` with a valid calendar date. Pushing a tag alone does not publish a release. Release filenames contain the same 12-character ID shown on the device. The
package includes `.bin` for UART and `.bin.gz` for OTA, plus the ELF, metadata,
checksums and `FLASHING.txt`. The Actions summary and metadata record the full
commit as well. A differing ID identifies a different commit, not necessarily
a newer one.

## References

- [Ultimate Tape Archive](https://uta.pokefinder.org/) — preserved C64 and VIC-20 tapes.
- [VICE TAP format](https://vice-emu.sourceforge.io/vice_17.html) and [datasette counter model](https://github.com/VICE-Team/svn-mirror/blob/main/vice/src/datasette/datasette.c).
- [Tapuino](https://github.com/sweetlilmre/tapuino) and [TapuinoNext](https://github.com/sweetlilmre/TapuinoNext).
- [TAP/IDX notes](https://www.luigidifraia.com/technical-info/#TAP_Index_format_IDX) and [Ultimate index implementation](https://github.com/GideonZ/1541ultimate/blob/master/software/filetypes/filetype_tap.cc).
- [ESP8266 boot modes](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html), [PlatformIO](https://docs.platformio.org/en/latest/platforms/espressif8266.html) and [Arduino OTA](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html).

No license has been selected yet.

## Release checklist

- [ ] Choose the release commit and confirm its GitHub Actions tests and firmware build pass.
- [ ] Download its artifact, verify `SHA256SUMS.txt` and flash the packaged 1 MB `.bin` using `FLASHING.txt`.
- [ ] Check cold boot/reset with MOTOR stopped, Wi-Fi setup/fallback, and the player page on desktop and phone.
- [ ] On C64 PAL, load a browser TAP and PRGs in standard and fast modes, including Circles; check MOTOR pauses and completion.
- [ ] Eject the source, press PLAY and verify SAVE header/ESP: request reception. Check that inserting a TAP/PRG blocks WRITE capture.
- [ ] In **Network / OTA**, compare **Firmware** with the commit ID in the package filename; verify it again after OTA.
- [ ] Test any newly advertised feature on hardware; update the tested/pending table and release notes with the actual results and limitations.
- [ ] Confirm the wiring and flashing instructions match the tested setup.
- [ ] Publish a GitHub Release for that commit with a valid `vYYYY-MM-DD` tag and concise release notes.
- [ ] Wait for the release workflow to pass; confirm the versioned `.bin` and `.bin.gz`, ELF, metadata, `FLASHING.txt` and checksums are attached to the release.
