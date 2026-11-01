# espsette

**A Wi-Fi cassette deck for the Commodore 64, built around an ESP-01.** Drop a TAP or PRG into the page served by the device, type `LOAD` on the computer, and watch the virtual tape run. The browser keeps the file; the ESP8266 accepts small chunks into an 8 KB buffer and turns them into timed pulses with I2S on the datasette **READ** line. The computer controls **MOTOR**, while the page's PLAY/STOP buttons control **SENSE**. No SD card or multi-megabyte flash cache is needed.

![Photographic cassette in the web player](web/assets/cassette.webp)

**Hardware status (1 November 2026):** `LOAD` was confirmed on a real C64 with the embedded demo TAP and with a TAP streamed from a browser. C16/Plus/4, PRG conversion, IDX seeking and WRITE capture have host-side tests but have **not** yet been verified on real hardware.

## What it does

- Plays C64 and C16/Plus/4 PAL TAP v1/v2. The browser retains large files and feeds the ESP on demand; it pauses uploads when the ring buffer is full, so a stopped C64 motor does not leave one long HTTP request open.
- Converts a PRG into standard cassette headers and data pulses as it streams. Choose C64 or C16/Plus/4 PAL in the page. The first two PRG bytes give the load address. This path is implemented but awaits hardware validation.
- Accepts a companion text `.idx` file for TAP. REW/FF jump between indexed positions, or through ten-count counter steps without an index. The displayed three-digit counter estimates *played* tape from emitted pulse time using the VICE/Pi1551 reel model; it is not the number of bytes uploaded.
- Offers a responsive deck interface with an animated cassette, PLAY, STOP, REW, FF and EJECT, plus a collapsible debug view. Autoplay presses PLAY on insertion by default. A local browser preview is available without firmware or hardware.
- Reports MOTOR, cassette and buffer state over HTTP and colored RemoteDebug logs on TCP port 23. The normal firmware uses the UART pins for SENSE and READ, so it does not log over serial.

The turbo-loader option is a future feature and is disabled. The optional WRITE header decoder is a separate build profile; it shows decoded SAVE header information on the page and performs no command or save action.

## Start using it

1. Build and flash an ESP-01 with the [serial flashing steps](#build-and-flash-an-esp-01) below. Use the real GPIO profile (the default).
2. Connect the module to the C64 datasette port through the signal-conditioning circuit described in [the wiring notes](kicad/WIRING.md). The C16/Plus/4 needs a suitable 1531 connector or adapter.
3. On first boot, join the `espsette-XXXXXX` access point and open its captive portal. Select your Wi-Fi network. If the saved network later fails, the device returns to its AP. The default hostname is `espsette`, but use the IP shown by your router until name resolution is verified.
4. Open `http://DEVICE_IP/` on a computer or phone. The embedded demo is inserted after boot; you can replace it by dropping a TAP or PRG onto the page. Keep that page open while streaming a browser file.
5. Leave autoplay enabled, or press PLAY on the page. On the C64, type `LOAD` and press RETURN. The C64 starts MOTOR; espsette then starts READ pulses. When the C64 stops MOTOR after the header to display the name, playback pauses. It resumes at the same position when MOTOR runs again.

The page is intended for a trusted local network and has no login. Wi-Fi credentials are entered through WiFiManager on the device; none are built into the firmware. The small hostname/autoplay/default-computer settings live in LittleFS. TAP and PRG files do not.

## Build and flash an ESP-01

Use a USB-to-UART adapter that supplies **3.3 V power and 3.3 V logic**, or a programmer with an ESP-01 socket. Check that its 3.3 V supply can handle Wi-Fi current peaks. A Mac with only USB-C may need a USB-C adapter. For the first setup, install `make` and `curl`; `make setup` puts the virtual environment and PlatformIO launcher under `tools/`, with Python and PlatformIO package caches under `~/.cache/espsette/`. It does not require a global PlatformIO installation. Disconnect the Commodore interface while serial flashing.

With the antenna at the top, looking at the module from above, the ESP-01 pins are:

```text
            ANTENNA
       GND       TXD / GPIO1
       GPIO2     CH_PD / EN
       GPIO0     RST
       RXD/GPIO3 VCC / 3.3 V
```

| ESP-01 pin | Programmer connection |
| --- | --- |
| VCC, GND | Regulated 3.3 V and common ground |
| RXD / GPIO3 | USB-UART TXD |
| TXD / GPIO1 | USB-UART RXD |
| CH_PD / EN | 3.3 V for both flash and run |
| GPIO0 | GND **at reset** for the serial bootloader; release/high at reset to run firmware |
| GPIO2 | Leave high at reset; do not ground it |
| RST | Pulse low to reset; on the tested CH340/wemos.cc adapter, connect DTR to RST while flashing |

Check the installed flash size before choosing a profile. For a 1 MB ESP-01 on macOS:

```sh
make setup
ls /dev/cu.*
make flash-id PORT=/dev/cu.usbserial-XXXX
make build PROFILE=1m
make flash PROFILE=1m PORT=/dev/cu.usbserial-XXXX
```

Hold GPIO0 low, reset, then run `flash-id` or `flash`. The repository's `esptool.cfg` uses DTR to pulse RST for the tested CH340 adapter. Other adapters may need a manual RST pulse. After flashing, remove the GPIO0-to-GND jumper, disconnect DTR from RST if the adapter holds reset, and reset again to boot the application. `make chip-info PORT=...` gives further read-only chip information. `make help` lists all targets. Espressif documents the [ESP8266 boot straps](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html) and [3.3 V supply requirements](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-hardware/esp8266.html).

The default `PROFILE=1m` uses 64 KB of LittleFS and includes the embedded demo. `PROFILE=4m` supports ESP-01 modules with 4 MB flash; `PROFILE=512k` may not fit the current image. Check the actual chip with `flash-id` first. The firmware's default build flags enable real cassette GPIOs and disable OTA. The 1 MB image with its embedded demo has insufficient free flash for a second OTA image. `make ota` is an experimental build path, **not** an update method for the current default firmware; use serial flashing. A future smaller image or a tested 4 MB profile could enable OTA with `ESPSETTE_OTA=1`.

## Playback, controls and diagnostics

The browser sends at most 1024 bytes per `POST /chunk`; the ESP keeps an 8 KB ring. `GET /state` reports loaded/ejected, MOTOR, played position, free buffer space and error counters. If the browser loses a response while its tab remains open, it can reconcile the session and resume at the ESP's offset. Reloading or closing the tab loses the browser's file handle; insert the file again. A sleeping phone may also stop feeding the buffer.

The TAP parser supports C64 and C16/Plus/4 PAL v1/v2. The optional IDX parser accepts Ultimate 64 style text entries with byte offsets; it does not support Manosoft Princess indexes. Seeking is available while MOTOR is stopped. On a PRG, REW starts a new session of the same file and FF is unavailable. The player cannot tell whether the Commodore decoded the pulses successfully; its counter tracks pulses sent to READ.

To inspect the live state, expand **Debug / ESP state** in the page or connect to RemoteDebug on the device's IP:

```sh
nc DEVICE_IP 23
```

The colored telnet log only reaches connected clients and does not replay messages from before connection. In the default build, UART output is unavailable because GPIO1 and GPIO3 are cassette pins. The optional `1m-write` and `4m-write` profiles enable WRITE header capture on GPIO2, which requires a protected, boot-safe input circuit and is still unverified on hardware.

## Develop without reflashing

Open [web/player.html](web/player.html) directly in a browser, or run `make preview`. The page detects `file://` and simulates ESP state, including MOTOR and cassette controls. This checks the interface, not electrical timing or Wi-Fi throughput. `make build` embeds the same page into `src/player_page.h` before compiling.

For offline host-side checks, see [tests/README.md](tests/README.md). The firmware is built with the pinned PlatformIO ESP8266 platform and Arduino core in [platformio.ini](platformio.ini). Sources are in `src/`; `include/draxcaught.h` holds the bundled demo TAP. The browser source and assets live in `web/`. KiCad files can be added under `kicad/` after the circuit is captured and checked.

## Tape resources and references

- [Ultimate Tape Archive](https://uta.pokefinder.org/) — a possible source of preserved C64 and VIC-20 tapes.
- [VICE TAP format](https://vice-emu.sourceforge.io/vice_17.html) and [VICE datasette counter model](https://github.com/VICE-Team/svn-mirror/blob/main/vice/src/datasette/datasette.c).
- [Tapuino](https://github.com/sweetlilmre/tapuino) and [TapuinoNext](https://github.com/sweetlilmre/TapuinoNext) — related cassette projects with different hardware architectures.
- [Luigi Di Fraia's TAP/IDX notes](https://www.luigidifraia.com/technical-info/#TAP_Index_format_IDX) and [Ultimate 64 index implementation](https://github.com/GideonZ/1541ultimate/blob/master/software/filetypes/filetype_tap.cc).
- [PlatformIO ESP8266](https://docs.platformio.org/en/latest/platforms/espressif8266.html) and [Arduino ESP8266 OTA guide](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html).

No license has been selected for this repository yet.
