# ESPsette hardware plan

**This branch uses MOTOR on GPIO1 and SENSE on GPIO0.** Apply the
[experimental modification](#experimental-motorsense-swap--2026-11-09)
before using this firmware. The base-board mapping below is historical.

Status: KiCad schematic in progress. Hardware decisions confirmed by the author on 2026-11-02 are recorded in [PLAN_POLACZEN.md](PLAN_POLACZEN.md). The author reports successful physical tests of the selected MOTOR topology and direct READ connection with the current firmware on 2026-11-01. These decisions supersede the earlier alternative circuits.

## 1. ESP-01 and CAD assets

- Fit ESP-01 / ESP-01S in a **2 × 4 socket, 2.54 mm pitch**; mark module orientation and pin 1.
- Keep copper, components and enclosure metal away from the PCB antenna area.
- Symbol and matching footprint: [jdunmire/kicad-ESP8266](https://github.com/jdunmire/kicad-ESP8266):
  - [`ESP8266.lib`](https://github.com/jdunmire/kicad-ESP8266/blob/master/ESP8266.lib), symbol `ESP-01v090`.
  - [`ESP8266.pretty/ESP-01.kicad_mod`](https://github.com/jdunmire/kicad-ESP8266/blob/master/ESP8266.pretty/ESP-01.kicad_mod).
- 3D models: [RoboticsBrno/semafor/hw](https://github.com/RoboticsBrno/semafor/tree/master/hw): `ESP-01-fixed v3.step` and `ESP-01-detachable v2.step` (repository declares MIT).
- Check model origin, rotation, socket height and actual module dimensions. Do not mix pin numbering from different libraries. The RoboticsBrno board uses a custom footprint; use its STEP as a mechanical reference, not as a drop-in socket footprint.
- Imported assets must retain their applicable licenses/attribution. The referenced custom assets are now available in the project-local `ESPsette` libraries; see [libraries/README.md](libraries/README.md) for inventory, source revisions, licenses and remaining mechanical checks.

## 2. Power and decoupling

Copy the **dual assembly option** from [plus4-tcbm2sd's schematic](https://github.com/ytmytm/plus4-tcbm2sd/blob/main/tcbm2sd/tcbm2sd.kicad_sch): reserve both sets of footprints on the same PCB, connected to the same `+5V`, `3V3` and GND nets. **Populate either A or B, never both regulators.**

| Assembly option | Parts / footprints |
| --- | --- |
| A — ready-made module | AMS1117-3.3 / HW764-style module on a 3-pin header; verify VIN/GND/VOUT order and module outline |
| B — direct SMD assembly | AMS1117-3.3 in SOT-223 + input and output capacitors; TCBM2SD uses U3 and C6/C7, **2 × 10 µF tantalum; copied footprints are EIA-3216-12 Kemet-S HandSolder** |

- SMD connections: `+5V → VIN`, `VOUT → 3V3`, GND common; input capacitor `+5V → GND`, output capacitor `3V3 → GND`, positive terminals on their respective rails.
- Preserve the reference circuit/layout approach, but verify capacitor values/ESR, voltage ratings and thermal copper for the actual regulator and ESP Wi-Fi load. The TCBM2SD BOM is the starting point, not an ESP load test.
- Keep regulator input/output capacitors C6/C7 and bulk capacitor C1. The author explicitly decided to omit the separate local 100 nF + 10 µF ESP bypass pair.

| Item | Connection / requirement |
| --- | --- |
| Reference BOM | [TCBM2SD power options](https://github.com/ytmytm/plus4-tcbm2sd/blob/main/HardwareFirmware.md#power-33v); retain both module and SMD mounting options |
| Supply capability | Design for at least 500 mA available to the ESP rail; verify regulator temperature and computer-port current budget during Wi-Fi activity |
| `C_BULK` | Reserve space for **100–470 µF**, current schematic **100 µF**, rated ≥6.3 V; positive to `3V3`, negative to GND, close to the ESP socket |
| Local bypass | Separate 100 nF + 10 µF pair omitted by explicit project decision |
| Programmer supply | UART pin 3 connects directly to `3V3` and powers the ESP during flashing; no supply-selection jumper |

- A bulk capacitor supports transients; it does not compensate for an undersized supply. Measure the rail at the ESP during AP/STA traffic and playback.
- Programmer 3.3 V, ESP VCC and the regulator output share the same `3V3` net. No regulator-output isolation is fitted. During programming the board is disconnected from C64/C16; the programmer is removed before reconnecting the computer.
- Never feed 5 V into ESP VCC or GPIO. Some adapters marked “3.3 V” only change UART logic and still supply 5 V on VCC; measure first.
- References: [Espressif power guidance](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-hardware/esp8266.html), [power troubleshooting](https://docs.espressif.com/projects/esptool/en/latest/esp8266/troubleshooting.html).

## Experimental MOTOR/SENSE swap — 2026-11-09

Branch `test/motor-gpio1-sense-gpio0` targets a modified version of the base
board described below: **MOTOR = GPIO1 through a 1 kΩ series resistor;
SENSE = GPIO0 with its existing 10 kΩ pull-up to 3V3**. READ stays on GPIO3
and bidirectional WRITE stays on GPIO2. The MOTOR 10 kΩ pull-down stays on
the optocoupler side of the series resistor; MOTOR remains active HIGH.
This branch has not yet been tested on hardware.

Cut the old MOTOR/GPIO0 and SENSE/GPIO1 connections near the ESP and cross
the two signal branches as described in
[the modification instructions](PLAN_POLACZEN.md#wariant-testowy-tej-gałęzi--2026-11-09).
Keep JP_FLASH on the ESP's GPIO0 side of the cut. Keep programmer RXD directly
connected to GPIO1; only the MOTOR branch gets the series resistor. The
USB-UART flashing procedure below still applies with the board unplugged
from the Commodore. The old MOTOR/SENSE mapping in the remaining sections
documents the base board, not this experimental firmware.

GPIO1 transmits ROM boot messages before firmware releases UART TX and
configures MOTOR as an input. The series resistor limits contention current;
verify MOTOR levels/edges during fastloader transfers. Also verify cold starts
with MOTOR off/on, PLAY/STOP, standard playback, WRITE commands and serial
flashing. GPIO2 must still be HIGH at boot; the Commodore SENSE pull-up to
5 V remains an interface-voltage consideration.

## 3. Reset, boot straps and FLASH jumper

| Net / component | Connection |
| --- | --- |
| EN / CH_PD | Direct connection to `3V3` |
| RST | RESET button and direct programmer DTR connection; no external pull-up |
| `SW_RESET` | Normally open momentary switch: **RST → GND** |
| GPIO0 | 4N35 emitter (pin 4) and 10 kΩ pull-down to GND; retain the tested active-HIGH MOTOR topology |
| `JP_FLASH` | Removable jumper: **GPIO0 → GND**; open = RUN, closed at reset = serial bootloader |
| GPIO2 | Direct WRITE connection; no external pull-up |

- GPIO0 is sampled during reset: LOW selects the bootloader, HIGH selects normal execution. It need not stay LOW after entering the bootloader.
- **Use a jumper for our DTR-reset workflow:** leave `JP_FLASH` closed during flashing so every reset re-enters the bootloader. Remove it and reset to RUN.
- A FLASH pushbutton is an alternative: hold it while pressing/releasing RESET, then release FLASH. Further automatic resets may require holding it again.
- [ESP8266 boot-mode reference](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html).

### Tested C64 startup

On the tested C64 setup, cassette-port power starts ESPsette automatically.
No separate supply, manual reset, C64 reset or MOTOR workaround is needed.
The author confirmed this sequence on 2026-11-06. GPIO0's level at the instant
of reset has not been measured; the normal SENSE/MOTOR relationship alone does
not establish that level.

## 4. Programming header and in-circuit flashing

These disconnection rules apply to **USB-UART flashing**. During **OTA**,
ESPsette can remain connected to a powered-on C64/C16, which supplies its
power; MOTOR and transfers must be stopped.

**Flash with ESP-01 installed in its socket, with the entire ESPsette board physically unplugged from the Commodore cassette port, even when the computer is switched off.** The programmer is always removed afterward, and RESET is not pressed while it is connected. No cassette isolation jumpers, DTR disconnect jumper or DTR series resistor are planned. UART pin 3 supplies the ESP directly through the shared 3V3 rail; no supply-selection jumper is fitted.

`J_UART`: **1 × 6, 2.54 mm**, pin 1 at the GND end. Match the CH340G adapter order **GND / CTS / VCC / TXD / RXD / DTR**, as on the [4tronix USB340G](https://shop.4tronix.co.uk/products/usb340g) type identified for this project. Labels below are from the **adapter's** perspective:

| Pin | Header label | Board connection |
| --- | --- | --- |
| 1 | GND | Common GND |
| 2 | CTS / NC | Unconnected pad; no trace to ESP |
| 3 | VCC / 3V3 ONLY | Programmer 3.3 V → `3V3` directly → ESP VCC |
| 4 | TXD | Adapter TXD → ESP RXD / GPIO3 |
| 5 | RXD | ESP TXD / GPIO1 → adapter RXD |
| 6 | DTR | Direct connection to ESP RST; no removable link or series resistor under the agreed programming workflow |

- **TX/RX are crossed electrically:** adapter TXD → ESP RXD, adapter RXD ← ESP TXD. Route this crossover on the PCB so a pin-for-pin cable can be used without swapping wires again. Mark the header `TXD→ESP_RX` and `RXD←ESP_TX` to avoid ambiguity.
- Set the adapter's voltage switch to **3.3 V before applying power**. Verify VCC and TXD/DTR HIGH levels; clone behavior may differ. The adapter must also supply enough current for ESP Wi-Fi, or use a separate suitable supply with shared GND and programmer VCC disconnected.
- Verify pin order and connector orientation on the purchased adapter before PCB manufacture; this is not a universal USB-UART pinout.
- FTDI-style cables use a different order (`GND, CTS, VCC, TXD, RXD, RTS`); RTS is not DTR. See the [FTDI cable datasheet](https://ftdichip.com/Support/Documents/DataSheets/Cables/DS_TTL-232R_CABLES.pdf).
- Our [`esptool.cfg`](../esptool.cfg) uses **DTR → RST**, DTR-only reset; RTS and CTS are unused.
- GPIO1/GPIO3 are UART during flashing and SENSE/READ during operation. No UART application logging in normal use.

| Step | Action |
| --- | --- |
| FLASH | Disconnect Commodore; close `JP_FLASH`; connect the 3.3 V programmer and UART/DTR |
| Upload | `make flash PROFILE=1m PORT=...` (DTR pulses RST); without DTR, pulse `SW_RESET` manually with FLASH closed |
| RUN | Disconnect programmer; open `JP_FLASH`; reconnect to the C64 and power on; ESPsette starts from cassette-port power |

## 5. Tape signal map

| Signal | C64 contact pair | C16 / Plus/4 mini-DIN 7 pin | ESP-01 | Current firmware / circuit requirement |
| --- | --- | --- | --- | --- |
| GND | A / 1 | 1, 7 | GND | Common ground |
| +5 V | B / 2 | 2 | Regulator VIN only | Never ESP VCC directly |
| MOTOR | C / 3 | 3 | GPIO0 input | **HIGH = running**, LOW = stopped; voltage conditioning required |
| READ | D / 4 | 4 | GPIO3 output | Direct connection, no inverter; author reports successful tests with current firmware |
| WRITE | E / 5 | 5 | GPIO2 bidirectional | WRITE command input; fastloader mode selection and data; no external pull-up |
| SENSE | F / 6 | 6 | GPIO1 output | Direct connection with 10 kΩ pull-up to 3V3; LOW = PLAY pressed |

- Mini-DIN numbering checked against **J4** in [Pi1551-HAT's PCB](https://github.com/ytmytm/Pi1551-HAT/blob/pi1551-hat-smd/RPi_Hat.kicad_pcb): pin 7 is tied to GND; shell pads `SH` are unconnected in that reference.
- Use the connector's numbered contacts; plug mating-face and solder-side views are mirrored. Do not substitute the numbering of Pi1551-HAT's **J5 ribbon header**, which uses a different order.

**Selected hardware, confirmed by the author on 2026-11-02:**

- MOTOR: LED fed through 470 Ω; 4N35 pin 5 → 3V3, pin 4 → GPIO0, 10 kΩ pull-down to GND. Retain active-HIGH detection and the existing FALLING pause interrupt. The author reports this topology worked with the current firmware in physical tests on 2026-11-01. The former proposal to swap collector/emitter connections and use a pull-up is withdrawn.
- READ: GPIO3 directly to cassette READ. No inverting transistor and no firmware polarity change requested; the author confirmed this connection worked with the current firmware in the same hardware tests. The firmware comment assuming an external transistor is outdated for this design.
- No external RST/GPIO2 pull-ups and no separate local 100 nF + 10 µF ESP bypass pair, by explicit project decision.
- SENSE: direct GPIO1 connection with 10 kΩ pull-up; WRITE: direct GPIO2 connection.

## 6. Tape connectors and CAD references

Reserve space for **both** connectors; connect only one computer at a time.

| Interface | Mechanical requirement | CAD source |
| --- | --- | --- |
| C64 | Female tape-port edge connector, **2 × 6, 3.96 mm / 0.156 inch pitch** | [DatassetteAudioAdapter](https://github.com/SukkoPera/DatassetteAudioAdapter): [female footprint](https://github.com/SukkoPera/DatassetteAudioAdapter/blob/master/DatassetteAudioAdapter.pretty/C64-Cassette-Port-Female.kicad_mod), [STEP](https://github.com/SukkoPera/DatassetteAudioAdapter/blob/master/3dModels/c64_datassette_connector.step) |
| C16 / Plus/4 | **Mini-DIN 7**; verify cable arrangement and pinout | [Pi1551-HAT library](https://github.com/ytmytm/Pi1551-HAT/tree/pi1551-hat-smd/Conn_DIN_round.pretty): `Mini_DIN_round_7.kicad_mod`, `_canned` variant and STEP models; also in [Pi1551-III](https://github.com/ytmytm/Pi1551-III), `Pi1551-III Module-rotated/Conn_DIN_round.pretty` |

- C64 symbol reference: `CN2 / TAPE_PORT` in [DatassetteAudioAdapter.sch](https://github.com/SukkoPera/DatassetteAudioAdapter/blob/master/DatassetteAudioAdapter.sch), generic `Connector:Conn_01x06_Male` in [its cache](https://github.com/SukkoPera/DatassetteAudioAdapter/blob/master/DatassetteAudioAdapter-cache.lib). Six electrical signals map to paired contacts; the symbol name does not determine physical gender. Upstream design declares **CC BY-NC-SA 4.0**; check asset licensing before reuse.
- C64 fallback: PCB edge fingers, 2 × 6 at 3.96 mm, correct order and paired opposite contacts. [OpenC64TapePortBreakOut](https://github.com/SukkoPera/OpenC64TapePortBreakOut) provides `C64-Cassette-Port-Male.kicad_mod`. Needs a female cable/adapter to mate with the C64's male edge.
- Verify connector orientation, keying, insertion depth, board thickness, contact finish and enclosure clearance against purchased parts.
