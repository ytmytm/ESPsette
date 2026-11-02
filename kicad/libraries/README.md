# Lokalne biblioteki ESPsette

Biblioteki są zarejestrowane w projektowych `../sym-lib-table` i `../fp-lib-table`
pod nazwą **ESPsette**. Otwórz ponownie `espsette.kicad_pro`; w selektorach
symboli i footprintów wyszukaj `ESPsette`. Nie trzeba dodawać bibliotek globalnie.
Ścieżki używają `${KIPRJMOD}` i nie zależą od katalogu z klonami repozytoriów.

| Symbol | Domyślny footprint | Źródło |
| --- | --- | --- |
| `ESPsette:ESP-01v090` | `ESPsette:ESP-01` | jdunmire/kicad-ESP8266 |
| `ESPsette:C64_TAPE_PORT` | `ESPsette:C64-Cassette-Port-Female` | SukkoPera/DatassetteAudioAdapter, cache `Connector_Conn_01x06_Male` |
| `ESPsette:Mini_DIN_7` | `ESPsette:Mini_DIN_round_7` | ytmytm/Pi1551-HAT |
| `ESPsette:Mini_DIN_7_canned` | `ESPsette:Mini_DIN_round_7_canned` | ytmytm/Pi1551-HAT |

Dodatkowy footprint `ESPsette:C64-Cassette-Port-Male` pochodzi z
SukkoPera/OpenC64TapePortBreakOut. Zachowano jego oryginalne pady `1–6` oraz
`A–F`: przy wykorzystaniu trzeba przypisać te same sieci parom `1/A`, `2/B`,
`3/C`, `4/D`, `5/E`, `6/F`. Nie jest bezpośrednim zamiennikiem żeńskiego
footprintu z symbolem sześciopinowym. Symbol `C64_TAPE_PORT` obsługuje żeński
footprint, gdzie obie strony mają już wspólną numerację `1–6`.

## Modele 3D

W `ESPsette.3dshapes` znajdują się:

- `c64_datassette_connector.step` — przypisany do żeńskiego złącza C64;
- `Conn_Mini_DIN_round_7.stp` i `Conn_Mini_DIN_round_7_canned.stp` — przypisane do odpowiednich footprintów;
- `ESP-01-fixed v3.step` i `ESP-01-detachable v2.step` — RoboticsBrno/semafor, modele referencyjne, bez automatycznego przypisania do footprintu ESP-01.

Zachowano przesunięcia, skalę i obroty modeli złącz ze źródeł. Zmieniono ich
ścieżki na lokalne. Modele ESP wymagają dopasowania początku układu,
orientacji i wysokości rzeczywistej podstawki 2×4; patrz `../WIRING.md`.
Nie zmieniano geometrii ani numeracji padów zaimportowanych footprintów.
Dwa stare symbole przekonwertowano z formatu `.lib` na `.kicad_sym`, zachowując
geometrię, numery, nazwy, typy i położenie pinów; dodano lokalne przypisania footprintów.
Symbole Mini-DIN wyjęto z cache schematu Pi1551-HAT i przypisano lokalne footprinty.
Metalowe mocowania `SH` pozostają osobnymi padami footprintów, jak w źródle.

## Zasilanie i pozostałe repozytoria

Układ AMS1117 z plus4-tcbm2sd wykorzystuje standardowe zasoby KiCad:
`Regulator_Linear:AMS1117-3.3`,
`Package_TO_SOT_SMD:SOT-223-3_TabPin2` oraz
`Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` dla modułu.
Nie wymaga dodatkowej biblioteki producenta. Wartości i dobór kondensatorów:
`../WIRING.md`, sekcja 2. Nie importowano całego schematu zasilania.

Pobrano brakujące klony `RoboticsBrno/semafor` i `ytmytm/Pi1551-III` do
`kicad-other-projects`. Pi1551-III jest alternatywnym źródłem złącz Mini-DIN;
użyto kompletu z Pi1551-HAT wskazanego w tabeli w `WIRING.md`.

## Autorstwo i licencje

`SOURCES.json` zawiera adresy repozytoriów, dokładne commity, pliki źródłowe,
ich SHA-256 i opis zmian. `licenses/` zachowuje pliki licencji lub README ze
źródłowymi deklaracjami. Zasoby pochodzą z różnych projektów i nie mają jednej
wspólnej licencji:

- **jdunmire/kicad-ESP8266**: CC BY-SA 4.0, z zachowaną informacją MIT (xesscorp, 2015) z `LICENSE.md`.
- **SukkoPera/DatassetteAudioAdapter**: projekt deklaruje CC BY-NC-SA 4.0, copyright SukkoPera 2025; kopia deklaracji w README w `licenses/`.
- **RoboticsBrno/semafor**: MIT, copyright RoboticsBrno (RobotikaBrno), 2020.
- **ytmytm/Pi1551-HAT**: zachowano `RPi_Hat.LICENSE` (MIT, xesscorp, 2015). Plik nie opisuje oddzielnie autorstwa modeli Mini-DIN; zachowano je wraz z pochodzeniem, bez przypisywania im nowej licencji.
- **SukkoPera/OpenC64TapePortBreakOut**: README deklaruje Open Hardware i prosi o udostępnianie zmian; brak osobnego, precyzyjnego tekstu licencji w klonie.
- Ogólne symbole `Conn_01x06_Male` i `Mini-DIN-7` pochodzą z bibliotek społeczności KiCad; zachowano również opis CC BY-SA 4.0 z wyjątkiem dla projektów elektronicznych (`licenses/KiCad-symbols-copyright.txt`, kopia z lokalnej instalacji).

## Sprawdzenie importu

Sprawdzono strukturę plików, istnienie wszystkich lokalnych odwołań do modeli,
zgodność numerów pinów symboli z padami przypisanych footprintów oraz
zgodność skopiowanych modeli ze źródłami. Trzy starsze footprinty wczytano
przez `pcbnew` 6. Modele i footprinty wymagają jeszcze oględzin w KiCad 9 i
porównania z zakupionymi częściami. Dostępny linuksowy KiCad jest w wersji 6;
uruchomienie Windowsowego CLI KiCad 9 z tej sesji WSL kończy się błędem interop.
