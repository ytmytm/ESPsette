# ESPsette hardware plan

Status: connection plan for a future KiCad design. C64 embedded/browser TAP playback tested; component values and PCB layout still need verification.

## 1. ESP-01 and CAD assets

- Fit ESP-01 / ESP-01S in a **2 × 4 socket, 2.54 mm pitch**; mark module orientation and pin 1.
- Keep copper, components and enclosure metal away from the PCB antenna area.
- Symbol and matching footprint: [jdunmire/kicad-ESP8266](https://github.com/jdunmire/kicad-ESP8266):
  - [`ESP8266.lib`](https://github.com/jdunmire/kicad-ESP8266/blob/master/ESP8266.lib), symbol `ESP-01v090`.
  - [`ESP8266.pretty/ESP-01.kicad_mod`](https://github.com/jdunmire/kicad-ESP8266/blob/master/ESP8266.pretty/ESP-01.kicad_mod).
- 3D models: [RoboticsBrno/semafor/hw](https://github.com/RoboticsBrno/semafor/tree/master/hw): `ESP-01-fixed v3.step` and `ESP-01-detachable v2.step` (repository declares MIT).
- Check model origin, rotation, socket height and actual module dimensions. Do not mix pin numbering from different libraries. The RoboticsBrno board uses a custom footprint; use its STEP as a mechanical reference, not as a drop-in socket footprint.
- Imported assets must retain their applicable licenses/attribution. No CAD assets have been imported yet.

## 2. Power and decoupling

Copy the **dual assembly option** from [plus4-tcbm2sd's schematic](https://github.com/ytmytm/plus4-tcbm2sd/blob/main/tcbm2sd/tcbm2sd.kicad_sch): reserve both sets of footprints on the same PCB, connected to the same `+5V`, `REG_3V3` and GND nets. **Populate either A or B, never both regulators.**

| Assembly option | Parts / footprints |
| --- | --- |
| A — ready-made module | AMS1117-3.3 / HW764-style module on a 3-pin header; verify VIN/GND/VOUT order and module outline |
| B — direct SMD assembly | AMS1117-3.3 in SOT-223 + input and output capacitors; TCBM2SD uses U3 and C6/C7, **2 × 10 µF tantalum, 0805** |

- SMD connections: `+5V → VIN`, `VOUT → REG_3V3`, GND common; input capacitor `+5V → GND`, output capacitor `REG_3V3 → GND`, positive terminals on their respective rails.
- Preserve the reference circuit/layout approach, but verify capacitor values/ESR, voltage ratings and thermal copper for the actual regulator and ESP Wi-Fi load. The TCBM2SD BOM is the starting point, not an ESP load test.
- Local ESP bypass and bulk capacitors below are required provisions for **either** assembly option; they do not replace the regulator's input/output capacitors.

| Item | Connection / requirement |
| --- | --- |
| Reference BOM | [TCBM2SD power options](https://github.com/ytmytm/plus4-tcbm2sd/blob/main/HardwareFirmware.md#power-33v); retain both module and SMD mounting options |
| Supply capability | Design for at least 500 mA available to the ESP rail; verify regulator temperature and computer-port current budget during Wi-Fi activity |
| `C_BULK` | Reserve space for **100–470 µF**, initial choice **220 µF**, rated ≥6.3 V; positive to `3V3`, negative to GND, close to the ESP socket |
| Local bypass | 100 nF ceramic + 10 µF between `3V3` and GND at the socket, short connections |
| Supply selection | `JP_PWR`: select **REG_3V3 OR programmer 3.3 V** to feed `3V3`; never connect the two outputs together |

- A bulk capacitor supports transients; it does not compensate for an undersized supply. Measure the rail at the ESP during AP/STA traffic and playback.
- When powered directly by the programmer, isolate the regulator output to avoid reverse feeding it.
- Never feed 5 V into ESP VCC or GPIO. Some adapters marked “3.3 V” only change UART logic and still supply 5 V on VCC; measure first.
- References: [Espressif power guidance](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-hardware/esp8266.html), [power troubleshooting](https://docs.espressif.com/projects/esptool/en/latest/esp8266/troubleshooting.html).

## 3. Reset, boot straps and FLASH jumper

| Net / component | Connection |
| --- | --- |
| EN / CH_PD | Pull up to `3V3`, e.g. 10 kΩ; remain HIGH in RUN and FLASH |
| RST | Pull up to `3V3`, e.g. 10 kΩ |
| `SW_RESET` | Normally open momentary switch: **RST → GND** |
| GPIO0 | Pull up to `3V3`, e.g. 10 kΩ; MOTOR circuit must not defeat the selected boot mode |
| `JP_FLASH` | Removable jumper: **GPIO0 → GND**; open = RUN, closed at reset = serial bootloader |
| GPIO2 | Pull up to `3V3`, e.g. 10 kΩ; optional WRITE circuitry must allow HIGH during reset |

- GPIO0 is sampled during reset: LOW selects the bootloader, HIGH selects normal execution. It need not stay LOW after entering the bootloader.
- **Use a jumper for our DTR-reset workflow:** leave `JP_FLASH` closed during flashing so every reset re-enters the bootloader. Remove it and reset to RUN.
- A FLASH pushbutton is an alternative: hold it while pressing/releasing RESET, then release FLASH. Further automatic resets may require holding it again.
- [ESP8266 boot-mode reference](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html).

## 4. Programming header and in-circuit flashing

**Yes: flash with ESP-01 installed in its socket, with the entire ESPsette board disconnected from C64/C16.** Provide isolation links for cassette circuits that load GPIO0, GPIO1, GPIO2 or GPIO3 during programming. Use `JP_PWR` to select programmer power.

`J_UART`: **1 × 6, 2.54 mm**, pin 1 at the GND end. Match the CH340G adapter order **GND / CTS / VCC / TXD / RXD / DTR**, as on the [4tronix USB340G](https://shop.4tronix.co.uk/products/usb340g) type identified for this project. Labels below are from the **adapter's** perspective:

| Pin | Header label | Board connection |
| --- | --- | --- |
| 1 | GND | Common GND |
| 2 | CTS / NC | Unconnected pad; no trace to ESP |
| 3 | VCC / 3V3 ONLY | Programmer power → `JP_PWR` → `3V3` |
| 4 | TXD | Adapter TXD → ESP RXD / GPIO3 |
| 5 | RXD | ESP TXD / GPIO1 → adapter RXD |
| 6 | DTR | ESP RST via a removable link; reserve a series resistor (initial 1 kΩ, verify reset LOW) to limit contention with `SW_RESET` |

- **TX/RX are crossed electrically:** adapter TXD → ESP RXD, adapter RXD ← ESP TXD. Route this crossover on the PCB so a pin-for-pin cable can be used without swapping wires again. Mark the header `TXD→ESP_RX` and `RXD←ESP_TX` to avoid ambiguity.
- Set the adapter's voltage switch to **3.3 V before applying power**. Verify VCC and TXD/DTR HIGH levels; clone behavior may differ. The adapter must also supply enough current for ESP Wi-Fi, or use a separate suitable supply with shared GND and programmer VCC disconnected.
- Verify pin order and connector orientation on the purchased adapter before PCB manufacture; this is not a universal USB-UART pinout.
- FTDI-style cables use a different order (`GND, CTS, VCC, TXD, RXD, RTS`); RTS is not DTR. See the [FTDI cable datasheet](https://ftdichip.com/Support/Documents/DataSheets/Cables/DS_TTL-232R_CABLES.pdf).
- Our [`esptool.cfg`](../esptool.cfg) uses **DTR → RST**, DTR-only reset; RTS and CTS are unused.
- GPIO1/GPIO3 are UART during flashing and SENSE/READ during operation. No UART application logging in normal use.

| Step | Action |
| --- | --- |
| FLASH | Disconnect Commodore; isolate conflicting cassette circuits; select programmer power; close `JP_FLASH`; connect UART/DTR |
| Upload | `make flash PROFILE=1m PORT=...` (DTR pulses RST); without DTR, pulse `SW_RESET` manually with FLASH closed |
| RUN | Open `JP_FLASH`; disconnect programmer/DTR; restore signal links and correct supply; reset |

## 5. Tape signal map

| Signal | C64 contact pair | C16 / Plus/4 mini-DIN 7 pin | ESP-01 | Current firmware / circuit requirement |
| --- | --- | --- | --- | --- |
| GND | A / 1 | 1, 7 | GND | Common ground |
| +5 V | B / 2 | 2 | Regulator VIN only | Never ESP VCC directly |
| MOTOR | C / 3 | 3 | GPIO0 input | **HIGH = running**, LOW = stopped; voltage conditioning required |
| READ | D / 4 | 4 | GPIO3 I2S output | **Inverting output stage**: ESP LOW → tape READ idle HIGH |
| WRITE | E / 5 | 5 | GPIO2 input | Optional protected 5 V → 3.3 V input; capture disabled by default |
| SENSE | F / 6 | 6 | GPIO1 output | LOW = PLAY pressed; use a compatible output stage |

- Mini-DIN numbering checked against **J4** in [Pi1551-HAT's PCB](https://github.com/ytmytm/Pi1551-HAT/blob/pi1551-hat-smd/RPi_Hat.kicad_pcb): pin 7 is tied to GND; shell pads `SH` are unconnected in that reference.
- Use the connector's numbered contacts; plug mating-face and solder-side views are mirrored. Do not substitute the numbering of Pi1551-HAT's **J5 ribbon header**, which uses a different order.

**Resolve before schematic sign-off:**

- Current prototype notes: 4N35 LED fed from MOTOR through 470 Ω; collector at 3V3, emitter at GPIO0, 10 kΩ pull-down. This matches active-HIGH firmware but holds GPIO0 LOW with MOTOR stopped, preventing normal boot at reset. Check LED current at measured MOTOR voltage; MOTOR is not a 5 V logic output.
- Proposed alternative: 4N35 collector at GPIO0, emitter at GND, 10 kΩ pull-up. Boot-safe with MOTOR off, but requires **active-LOW MOTOR firmware and a RISING pause interrupt**. Do not combine it with unchanged firmware.
- Capture the actual READ inverter and SENSE circuit, values and measured levels. No direct exposure of ESP pins to 5 V pull-ups.
- WRITE: protect GPIO2 and its reset HIGH. Future bidirectional turbo needs direction control compatible with C64 actively driving both HIGH and LOW; current firmware only listens.
- Optional buffered SENSE/READ/WRITE indicators; stretch READ/WRITE pulses for visibility without changing tape timing.

## 6. Tape connectors and CAD references

Reserve space for **both** connectors; connect only one computer at a time.

| Interface | Mechanical requirement | CAD source |
| --- | --- | --- |
| C64 | Female tape-port edge connector, **2 × 6, 3.96 mm / 0.156 inch pitch** | [DatassetteAudioAdapter](https://github.com/SukkoPera/DatassetteAudioAdapter): [female footprint](https://github.com/SukkoPera/DatassetteAudioAdapter/blob/master/DatassetteAudioAdapter.pretty/C64-Cassette-Port-Female.kicad_mod), [STEP](https://github.com/SukkoPera/DatassetteAudioAdapter/blob/master/3dModels/c64_datassette_connector.step) |
| C16 / Plus/4 | **Mini-DIN 7**; verify cable arrangement and pinout | [Pi1551-HAT library](https://github.com/ytmytm/Pi1551-HAT/tree/pi1551-hat-smd/Conn_DIN_round.pretty): `Mini_DIN_round_7.kicad_mod`, `_canned` variant and STEP models; also in [Pi1551-III](https://github.com/ytmytm/Pi1551-III), `Pi1551-III Module-rotated/Conn_DIN_round.pretty` |

- C64 symbol reference: `CN2 / TAPE_PORT` in [DatassetteAudioAdapter.sch](https://github.com/SukkoPera/DatassetteAudioAdapter/blob/master/DatassetteAudioAdapter.sch), generic `Connector:Conn_01x06_Male` in [its cache](https://github.com/SukkoPera/DatassetteAudioAdapter/blob/master/DatassetteAudioAdapter-cache.lib). Six electrical signals map to paired contacts; the symbol name does not determine physical gender. Upstream design declares **CC BY-NC-SA 4.0**; check asset licensing before reuse.
- C64 fallback: PCB edge fingers, 2 × 6 at 3.96 mm, correct order and paired opposite contacts. [OpenC64TapePortBreakOut](https://github.com/SukkoPera/OpenC64TapePortBreakOut) provides `C64-Cassette-Port-Male.kicad_mod`. Needs a female cable/adapter to mate with the C64's male edge.
- Verify connector orientation, keying, insertion depth, board thickness, contact finish and enclosure clearance against purchased parts. C64 pitch reference: Commodore's [1530 parts listing, 12POL.RM3.96](https://retroport.de/wp-content/uploads/2018/10/ersatzteilliste_91.pdf).
