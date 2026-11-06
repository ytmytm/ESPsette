# ESPsette

**A Wi-Fi cassette deck for your Commodore, built around an ESP-01.**
Open its page, select a TAP or PRG and type `LOAD` on the computer.
Files stream from your browser through a 12 KB buffer; you need no SD card.

Small as a *pesette* — a little scale for your LOADs.

**[Try the browser demo](https://ytmytm.github.io/ESPsette/)** without hardware.

![Cassette in the web player](web/assets/cassette.webp)

## What works

Hardware tests on **C64 PAL, 1–6 November 2026**:

| Feature | Status |
| --- | --- |
| TAP playback from the browser | Passed, including Rambo III Side 1 and its MOTOR pauses |
| PRG loading with standard tape pulses | Passed |
| PRG loading with the fastloader | Passed |
| Built-in Circles demo | Passed in standard and fast modes |
| WRITE filenames and command requests | Passed with an empty deck; EJECT first |
| OTA updates on a 1 MB ESP-01 | Passed over IP, including playback after updating |
| C16/Plus/4 and NTSC PRG loading and WRITE | Host/VICE tests pass; hardware tests pending |
| C16/Plus/4 TAP playback and other TAP variants | Full hardware coverage pending |
| IDX navigation and counter-based REW/FF | Host tests pass; hardware tests pending |

## Load your first program

You need an ESP-01 with at least **1 MB flash**, the Commodore interface and a
**2.4 GHz Wi-Fi network**. Your browser must be on the same local network as
the ESP. The player has no login; use it on a trusted LAN.

1. Install the firmware using [USB-UART](#install-over-usb-uart), then connect
   the ESP through the [Commodore interface](kicad/WIRING.md).
   Turn on the C64: its cassette port powers ESPsette, which starts automatically.
   C16/Plus/4 needs a 1531 connector or adapter.
2. On first boot, join **ESPsette-XXXXXX** and choose your Wi-Fi network in the
   setup page. If it does not open automatically, visit `http://192.168.4.1/`.
   The ESP opens this setup network again if it cannot connect.
3. Rejoin your normal network. Find the ESP's IP address in your router and
   open `http://DEVICE_IP/`.
   Use the IP address; mDNS discovery is disabled.
4. Choose **Demo**, select a `.tap` or `.prg`, or drop a file onto the deck.
   For PRGs, first choose your computer and PAL/NTSC in **Playback settings**.
   TAP files supply their own computer profile.
5. Press **PLAY** on the page and type `LOAD` on the Commodore.
   **Autoplay** presses PLAY for you when you insert a file.
6. For a BASIC PRG loaded at standard speed, type `RUN` after loading.
   The fastloader runs it automatically. For TAPs, follow the original tape's
   loading instructions.

**Keep the browser tab open and visible until loading finishes.** The file
stays in that tab. Sleep protection helps keep the screen awake; it cannot
prevent manual locking, lid closure or OS suspension. If offered, tap
**Keep screen awake**.

### Deck controls

- **PLAY / STOP** control the virtual cassette key. The Commodore starts and
  pauses the tape through MOTOR, including the pause after the header.
- **STOP** springs back when released; the deck stays stopped. It pauses
  standard playback but cancels an active fastload.
- **REW / FF** seek with MOTOR stopped.
- **EJECT** removes the inserted file.
- For named positions, add an Ultimate-style `.idx` with your TAP, or add it
  later. Select a position and press **JUMP**, or use **PREV/NEXT**.

If the page asks you to stop MOTOR before inserting, ejecting or seeking,
cancel the current LOAD/SAVE on the Commodore. The web STOP key releases PLAY;
the computer controls MOTOR.

The elapsed time and counter refer to the whole TAP, including after a jump.
Playback settings survive a reset.

## Built-in demo

**Demo** loads [Circles](https://csdb.dk/release/?id=262238), with music by
**Shogoon** and code by **YTM**. It is for **C64 PAL only**, regardless of your
saved profile for uploaded files.

- Fastloader off: use `LOAD`, then `RUN`.
- Fastloader on: use `LOAD`; the demo loads and starts automatically.

The original [7744-byte PRG](assets/demo/circles.prg) is included in the
firmware. See [credits and checksum](assets/demo/README.md).

## Use the PRG fastloader

Enable **Use fastloader for PRG** in **Playback settings**. It is off by default.
The switch saves immediately and reloads a stopped PRG in the selected mode.
Stop the transfer before changing it.

Use normal `LOAD`. A short program loads at standard speed, then receives the
rest at fast speed. The web player starts it with BASIC `RUN`; machine-code
programs need a BASIC starter for automatic launch.

TAP files keep their recorded timing. Supported fastloader load ranges are `$0801..$7FFF` on C64 and `$1001..$3FFF` on C16/Plus/4.
Use standard loading for PRGs outside those ranges.

During the fast stage:

- The progress bar shows bytes sent, percentage, speed and estimated time left.
- The counter shows the equivalent standard tape time, so it advances rapidly.
- The reels spin faster and pause while waiting for data or the computer.
- Progress stays at 100% until you rewind or eject.

If a PRG fails to load, try standard loading. You can also expand
**Advanced fastloader timings** and increase the delays:

- **Save fastloader settings** saves the numeric timings for the next load.
- **Default timings** restores the fields; press Save to apply them.

The fastloader switch and timings remain saved after a reset.
[Protocol and timing details](experiments/fastloader/README.md) are available
for troubleshooting.

## Read the display

- **Wi-Fi icon:** signal between the ESP and router. Hover for RSSI in dBm.
- **BUF:** how much of the ESP's streaming buffer is filled.
- **Playback details:** loading phase, errors and screen sleep protection.
- **Debug / ESP state:** current firmware ID, reset reason, memory and buffer
  diagnostics. If loading stops unexpectedly, copy this JSON before resetting
  or ejecting.

During PLAY, the display receives compact status updates. Full diagnostics and
the waveform refresh after STOP, completion or an error.

If a load fails, save the Debug JSON first. Then stop MOTOR on the Commodore
and press **REW** to restart from the file retained in the tab. If the tab has
been closed or reloaded, select the file again.

## Receive WRITE requests

The ESP can display names sent by the Commodore's SAVE command.
It does not record the program to a file.

1. **EJECT** any TAP or PRG, even if loading has already finished.
2. Press **PLAY** on the page.
3. On the Commodore, enter `SAVE "NAZWA",1`.
4. Look for the verified filename in **Playback details**.

Names beginning with `ESP:`, such as `SAVE "ESP:TEST",1`, publish a command
request. **There are no command handlers or planned uses yet; requests do not
execute anything.** The deck must stay empty while receiving WRITE.

See the [WRITE guide](docs/WRITE-COMMANDS.md) for the request format and details.

## Install over USB-UART

Download the 1 MB firmware package from
[Releases](https://github.com/ytmytm/ESPsette/releases), or
[build it from source](#build-from-source). The package includes `FLASHING.txt`
and SHA-256 checksums.

Use the **uncompressed `.bin`** for serial flashing. The image includes Circles;
no separate filesystem image is needed.

### Connect the programmer

Disconnect the ESP from the Commodore interface. Use **3.3 V power and UART
logic**, with a supply able to handle Wi-Fi current peaks. If your CH340G
adapter has a voltage switch, select **3.3 V** and verify its signal levels.

Keep **GPIO0 connected to GND throughout flashing**:

| USB-UART / supply | ESP-01 |
| --- | --- |
| 3.3 V | VCC and CH_PD / EN |
| GND | GND and GPIO0 (flash jumper) |
| TXD | RXD / GPIO3 |
| RXD | TXD / GPIO1 |
| DTR | RST |
| No connection | GPIO2 (must stay HIGH at reset), CTS, RTS |

The PCB routes the TX/RX crossover. Its programmer header is
`GND / CTS / VCC / TXD / RXD / DTR`; CTS is unused.
Our [esptool configuration](esptool.cfg) pulses DTR only. Without DTR, briefly
pull RST to GND while GPIO0 is grounded before flashing.

If the adapter is missing from your serial ports, check its
[CH340 driver](https://github.com/hobbycomponents/ch340-drivers).

### Flash and restart

For a downloaded binary, follow its `FLASHING.txt` to write it at `0x0`.
From a [prepared source checkout](#build-from-source), use:

```sh
make flash-id PORT=/dev/ttyUSB0          # Check the module's flash size
make flash PROFILE=1m PORT=/dev/ttyUSB0  # Build and upload
```

On macOS, use your `/dev/cu.usbserial-...` port; `ls /dev/cu.*` lists ports.
`make chip-info PORT=...` prints additional chip information.

After flashing:

1. Remove the GPIO0–GND jumper and disconnect DTR–RST.
2. Reset the ESP. GPIO0 must be HIGH at reset for normal operation.
3. Follow the [wiring notes](kicad/WIRING.md) before reconnecting the Commodore.

## Update over Wi-Fi

OTA is enabled for 1 MB and 4 MB modules. If your installed firmware has OTA
disabled, install this version over USB-UART first.

From a [prepared source checkout](#build-from-source):

1. Stop MOTOR and all transfers.
2. Build and upload the checkout's firmware over your local network:

   ```sh
   make ota PROFILE=1m OTA_HOST=DEVICE_IP
   ```

3. After the ESP restarts, **refresh its page**.
4. Check **Network / OTA → Firmware** against the expected commit ID.

Use the ESP's IP address. OTA preserves settings and Wi-Fi configuration.
It has no password by default; you can
[configure one](include/local_ota.h.example).

OTA uses `.bin.gz`, which the bootloader decompresses. Each build checks that
the update fits in flash alongside the running image and saved settings.

### Change the Wi-Fi network

To change the saved Wi-Fi network, stop MOTOR and use
**Network / OTA → Forget Wi-Fi…**. This opens a confirmation page; confirming
restarts the ESP into Wi-Fi setup. OTA updates keep the saved network.

### Check the firmware version

The page shows the running ESP's 12-character Git commit ID. Release filenames
contain the same ID, for example:

```text
ESPsette-esp01-1m-v2026-11-01-0123456789ab.bin.gz
```

`make build`, `make flash` and `make ota` print the ID. `make version` shows it
without building. A `-dirty` suffix means the checkout had local changes at
build time. The commit hash identifies the revision; use release notes and
dates to choose an update.

## Build from source

You need `make`, `curl`, Python 3, **ACME** (6502 assembler) and internet access
for the first tool download.

- Debian/Ubuntu: install ACME with `sudo apt-get install acme`.
- macOS: use `brew install acme`; install Command Line Tools with
  `xcode-select --install` if needed.

Run from the repository root:

```sh
make setup             # Download the build tools
make build PROFILE=1m  # Assemble the loaders and compile the firmware
```

`make setup` installs tools under `tools/bin/` and `~/.cache/ESPsette/`.
No global PlatformIO installation is needed. `make build` assembles the native
fastloader automatically; `make native` builds just the 6502 code.

| Profile | Module |
| --- | --- |
| `1m` (default) | 1 MB flash; OTA enabled |
| `4m` | 4 MB flash; OTA enabled |
| `512k` | Experimental; the default OTA image does not fit |

Release packages target 1 MB modules. The `512k` profile inherits OTA settings;
a build without OTA needs separate size and hardware checks.

## Hardware files

- [Wiring and ESP-01 connections](kicad/WIRING.md)
- [Schematic PDF](kicad/plots/espsette.pdf)
- [Gerber and drill files](kicad/gerbers/)
- [KiCad project](kicad/espsette.kicad_pro) and [connection plan](kicad/PLAN_POLACZEN.md)
- [Symbols, footprints and 3D models](kicad/libraries/README.md)

![ESPsette PCB — top view](media/01.pcbtop.png)

![ESPsette PCB — side view](media/02.pcbside.png)

<details>
<summary>Local preview, tests and releases</summary>

Use `make preview` or open [web/player.html](web/player.html) to try the UI
locally. The [hosted demo](https://ytmytm.github.io/ESPsette/) also runs without
hardware. Both simulate loading and WRITE reception; preview settings stay in
browser local storage. Fonts, graphics and screen-wake media work offline.

Host tests need Node.js and a C++17 compiler. See [test instructions](tests/README.md),
[native fastloader sources](native/fastloader/) and
[fastloader protocol](experiments/fastloader/README.md).
For navigation tests, use [ui-test.tap](tests/fixtures/ui-test.tap) with
[ui-test.idx](tests/fixtures/ui-test.idx).

GitHub Actions builds the 1 MB firmware and runs host tests on pushes, pull
requests and manual runs. Its artifact includes `.bin`, `.bin.gz`, the debug
ELF, build metadata, checksums and `FLASHING.txt`. The package JSON, instructions
and Actions summary record the full commit ID.

### Release checklist

- [ ] Choose the commit and confirm its Actions tests and build pass.
- [ ] Download the artifact, verify `SHA256SUMS.txt` and flash the packaged `.bin`.
- [ ] Check cold boot, Wi-Fi setup/fallback and the player on desktop and phone.
- [ ] On C64 PAL, load a TAP and standard/fast PRGs, including Circles; check MOTOR pauses.
- [ ] EJECT, press PLAY and check WRITE reception; inserting a file must block it.
- [ ] Test OTA and compare **Network / OTA → Firmware** with the package filename.
- [ ] Update the tested features and limitations above with hardware results.
- [ ] Check the wiring and flashing instructions against the tested setup.
- [ ] Publish a GitHub Release tagged `vYYYY-MM-DD` with a valid calendar date.
- [ ] Confirm the workflow attached the firmware, metadata, instructions and checksums.

Publishing the release builds and attaches its package. Pushing a tag alone
does not publish a release.

</details>

## References

- [Ultimate Tape Archive](https://uta.pokefinder.org/)
- [VICE TAP format](https://vice-emu.sourceforge.io/vice_17.html) and [counter model](https://github.com/VICE-Team/svn-mirror/blob/main/vice/src/datasette/datasette.c)
- [Tapuino](https://github.com/sweetlilmre/tapuino) and [TapuinoNext](https://github.com/sweetlilmre/TapuinoNext)
- [TAP/IDX notes](https://www.luigidifraia.com/technical-info/#TAP_Index_format_IDX) and [Ultimate index implementation](https://github.com/GideonZ/1541ultimate/blob/master/software/filetypes/filetype_tap.cc)
- [ESP8266 boot modes](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html), [PlatformIO](https://docs.platformio.org/en/latest/platforms/espressif8266.html) and [Arduino OTA](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html)

No license has been selected yet.
