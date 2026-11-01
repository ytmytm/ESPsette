# Datasette interface wiring notes

This is the **signal-level sketch for the current prototype**, before a KiCad schematic or PCB. The firmware pin mapping is fixed by `src/main.cpp`. The C64 playback path has been tested with an embedded TAP and a TAP streamed from the browser; the exact component-level circuit has not yet been captured as a measured, reproducible schematic. Recheck connector orientation, voltages and polarity on the assembled prototype before copying it.

## Connections

| Datasette signal | ESP-01 pin | Direction | Required level at the ESP / behavior |
| --- | --- | --- | --- |
| GND | GND | common reference | Shared ground for the C64, 3.3 V supply and ESP-01 |
| MOTOR (C64 edge contact C / 3) | GPIO0 | C64 → ESP | **HIGH = running**, LOW = stopped in the current firmware; GPIO0 also selects boot mode |
| SENSE / PLAY (F / 6) | TXD / GPIO1 | ESP → C64 | LOW = PLAY pressed, HIGH = released |
| READ (D / 4) | RXD / GPIO3 | ESP → C64 | I2S pulse output; the firmware drives GPIO3 LOW for idle and expects an **inverting output stage** so READ is idle HIGH |
| WRITE (E / 5) | GPIO2 | C64 → ESP | Optional diagnostic SAVE header input; disabled in the default build, requires protected 3.3 V levels and HIGH at reset |
| +5 V supply | 3.3 V regulator input, if used | C64 → supply | **Never** connect directly to ESP VCC; verify that the datasette port can supply the regulator plus Wi-Fi current, or use a separate supply |

```text
  C64 datasette port                                  ESP-01
  ─────────────────                                  ──────
  MOTOR ──[voltage/polarity conditioning, active HIGH]──► GPIO0
  SENSE ◄──[PLAY output stage, LOW = pressed]────────── GPIO1 / TXD
  READ  ◄──[inverting, 5 V-safe output stage]────────── GPIO3 / RXD / I2S
  WRITE ──[optional protected, boot-safe input]───────► GPIO2
  GND   ─────────────────────────────────────────────── GND
  +5 V  ──[regulator, if port current allows]─────────► 3.3 V VCC + EN

  USB-UART TX/RX/DTR ──[flash-only connection]────────► RXD/TXD/RST
  GPIO0 ──[flash-only jumper to GND]                   (remove for RUN)
```

The old prototype notes describe a 4N35 MOTOR input with a 470 Ω LED-side resistor, collector at 3.3 V, emitter at GPIO0 and a 10 kΩ pull-down: MOTOR active makes GPIO0 HIGH, matching the firmware. That circuit also holds GPIO0 LOW while MOTOR is idle, which selects the ESP8266 serial bootloader **on reset**. It can work during a running session yet fail to start normally when the C64 motor is off. A later open-collector proposal (4N35 collector to GPIO0 with a 10 kΩ pull-up and emitter to GND) starts safely but makes MOTOR active LOW, so it **does not match this firmware**. Neither variant should be copied into a PCB without resolving the boot condition and checking the actual prototype. The published connection diagram above deliberately specifies the required behavior at GPIO0 instead of treating either variant as finished hardware.

GPIO3's LOW idle state also makes a direct GPIO3-to-READ wire incorrect for the present firmware. Use the inverting output stage already assumed by the code and check the C64-side idle HIGH and pulse polarity. Do not connect a 5 V datasette output directly to an ESP8266 GPIO on the assumption that it is 5 V tolerant. ESP8266 is a 3.3 V device; the [Espressif supply guide](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-hardware/esp8266.html) recommends a supply capable of Wi-Fi current peaks. The [ESP8266 boot-mode guide](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html) specifies GPIO0 HIGH for normal boot, GPIO0 LOW for serial flashing and GPIO2 HIGH for both modes.

For a C16/Plus/4, the electrical signal roles are similar but the physical 1531 connector differs from the C64 edge connector. Confirm its pinout before making an adapter. Only the C64 playback path has been checked on real hardware so far.

## Connector space required on the PCB

Reserve board and enclosure space for **both** tape interfaces:

- **C64:** mating datasette edge connector, **2 × 6 contacts, 3.96 mm (0.156 inch) pitch**. The computer exposes the PCB edge; the device needs the mating connector. The pitch is 3.96 mm, not 3.69 mm; Commodore lists the 1530 connector as “12POL.RM3.96” in its [parts list](https://retroport.de/wp-content/uploads/2018/10/ersatzteilliste_91.pdf).
- **C16 / Plus/4:** **mini-DIN 7** tape connection. Reuse the connector symbol/footprint and mechanical reference from [Pi1551-III](https://github.com/ytmytm/Pi1551-III) or [Pi1551-HAT](https://github.com/ytmytm/Pi1551-HAT), adapting the signal mapping to ESPsette. Pi1551-HAT's [Conn_DIN_round.pretty](https://github.com/ytmytm/Pi1551-HAT/tree/pi1551-hat-smd/Conn_DIN_round.pretty) contains `Mini_DIN_round_7.kicad_mod`, the `_canned` variant and matching STEP models. Pi1551-III carries the same library under `Pi1551-III Module-rotated/Conn_DIN_round.pretty`.

Check the selected connector's pin numbering, mating face versus solder-side view, cable arrangement and mechanical clearances before transferring the footprint. Provide both connection options, with only one computer connected at a time; do not tie two computers' power outputs together.

## Before drawing the PCB

- Measure actual GPIO0 and MOTOR levels with MOTOR stopped, running and during reset. Decide on a boot-safe active-HIGH input stage or change firmware polarity together with the circuit.
- Record the real READ inverter and SENSE output components, their values and the C64-side idle voltages. Check that ESP outputs are not exposed to 5 V through pull-ups.
- Keep GPIO2 HIGH at reset and protect it against the Commodore WRITE voltage before enabling `PROFILE=1m-write` or `4m-write`. A future bidirectional turbo protocol will also need safe direction control; the present firmware only listens.
- Add local 3.3 V decoupling, measure supply voltage during Wi-Fi peaks, and allow the USB-UART programmer to be disconnected from the C64 lines while flashing.
- Optional SENSE, READ and WRITE activity LEDs should use buffered indicator circuits so they do not load or reshape cassette pulses. Fast READ/WRITE pulses may need pulse stretching to become visible.
