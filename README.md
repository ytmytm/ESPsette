# ESPsette

**A Wi-Fi cassette deck for your Commodore, built around an ESP-01.** Open its page, drop a tape file and type `LOAD` on the computer. Your browser streams the file over Wi-Fi; the ESP8266 turns it into timed I2S pulses on the datasette READ line. The computer controls MOTOR, and the page controls the virtual PLAY key through SENSE.

Files stay in the browser and pass through an 8 KB buffer on the ESP. No SD card or tape storage in flash is needed.

![Cassette in the web player](web/assets/cassette.webp)

## Tested and pending

Hardware tests on **1 November 2026**:

| Feature | Status |
| --- | --- |
| C64 loading the embedded demo TAP | Passed on real hardware |
| C64 loading a TAP streamed from the browser | Passed on real hardware |
| PRG → standard tape pulses, C64 and C16/Plus/4 PAL | Host tests pass; hardware test pending |
| C16/Plus/4 playback; all supported TAP v1/v2 variants | Full hardware coverage pending |
| IDX seeking and counter-based REW/FF | Host tests pass; hardware test pending |
| WRITE / SAVE header decoding | Host tests pass; hardware test pending; disabled by default |
| Turbo loader | Not implemented |
| OTA updates | Disabled in the default firmware; use serial flashing |

## Build

Requires `make`, `curl` and internet access for the initial tool download. On macOS, install the Command Line Tools if needed (`xcode-select --install`). Run from the repository root:

```sh
make setup                    # Install project tools once
make build PROFILE=1m         # Build for a 1 MB ESP-01
```

Tools are installed in `tools/`, with Python and PlatformIO packages/caches under `~/.cache/ESPsette/`. No global PlatformIO installation is required. The default build enables real cassette GPIOs and includes the demo TAP. Build settings are in [platformio.ini](platformio.ini).

| Profile | Use |
| --- | --- |
| `1m` (default) | 1 MB flash, 64 KB LittleFS |
| `4m` | ESP-01 with 4 MB flash |
| `512k` | 512 KB flash; current firmware may not fit |
| `1m-write`, `4m-write` | Optional WRITE input; requires protected, boot-safe wiring |

## Flash over USB-UART

Download the `.bin` from [Releases](https://github.com/ytmytm/ESPsette/releases), or build it locally. Each release includes `FLASHING.txt` with the command for flashing the binary at `0x0`, plus SHA-256 checksums. The released image targets a **1 MB ESP-01** and includes the demo; no separate filesystem image is needed.

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

OTA is disabled because the 1 MB image with the embedded demo leaves insufficient space for a second image. The existing `make ota` target requires firmware built with OTA enabled and enough free flash; it is not ready for the default setup.

## Load a tape

1. Connect MOTOR → GPIO0, SENSE ← GPIO1 and READ ← GPIO3 through the interface described in [kicad/WIRING.md](kicad/WIRING.md). C16/Plus/4 needs a 1531 connector or adapter.
2. On first boot, join `ESPsette-XXXXXX` and configure Wi-Fi in the captive portal. The device falls back to its AP if the saved network fails. No Wi-Fi credentials are built in.
3. Open `http://DEVICE_IP/` (find the IP in your router). The default hostname is `ESPsette`; `.local` resolution is not guaranteed.
4. Use the embedded demo, or select/drop a TAP or PRG. For PRG, select C64 or C16/Plus/4 PAL; the ESP generates the tape header and data pulses. For indexed TAP files, select/drop the accompanying Ultimate-style `.idx` too.
5. Autoplay presses virtual PLAY when a tape is inserted; otherwise press PLAY on the page. Type `LOAD` on the Commodore. Playback starts when MOTOR turns on and pauses whenever MOTOR stops, including after the header.

Keep the browser tab open and the phone/computer awake while streaming. PLAY/STOP control SENSE; the Commodore controls MOTOR. REW/FF seek while MOTOR is stopped. The animated cassette and counter show playback progress. Settings live in LittleFS; tape files stay in the browser. The page is intended for a trusted LAN and has no login.

## Debug and local UI preview

Expand **Debug / ESP state** on the page, or connect to colored RemoteDebug logs:

```sh
nc DEVICE_IP 23
```

Normal firmware has no UART log: GPIO1 and GPIO3 are used for SENSE and READ.

Open [web/player.html](web/player.html) directly in a browser or run `make preview` to try the UI with simulated ESP state. See [tests/README.md](tests/README.md) for host tests and `make help` for build targets.

## CI and releases

GitHub Actions runs host tests and compiles the default 1 MB firmware on code pushes, pull requests and manual runs. Download the package from the run's **Artifacts** section; it contains the `.bin`, debug ELF, build metadata, checksums and flashing instructions.

To release, publish a GitHub Release with a tag such as `v2026-11-01` pointing to the desired commit. The workflow builds that tag and automatically attaches the same files to the release. Tags must use `vYYYY-MM-DD` with a valid calendar date. Pushing a tag alone does not publish a release. Experimental WRITE/OTA profiles are not included.

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
- [ ] On a real C64, load both the embedded demo and a browser-streamed TAP; check the MOTOR pause after the header, resume and completion.
- [ ] Test any newly advertised feature on hardware; update the tested/pending table and release notes with the actual results and limitations.
- [ ] Confirm the wiring and flashing instructions match the tested setup.
- [ ] Publish a GitHub Release for that commit with a valid `vYYYY-MM-DD` tag and concise release notes.
- [ ] Wait for the release workflow to pass; confirm the `.bin`, ELF, metadata, `FLASHING.txt` and checksums are attached to the release.
