# espsette

Firmware ESP-01 do odtwarzania sygnału kasetowego przez port datasette C64. Projekt jest w trakcie migracji ze starego szkicu Arduino; [analiza i plan](ANALIZA_PROJEKTU.md) opisują kolejne etapy.

## Stan kodu

- `src/main.cpp` zawiera historyczny odtwarzacz TAP z nałożoną poprawką tempa I2S, obsługi MOTOR i kończenia transmisji.
- `include/draxcaught.h` to tymczasowy, osadzony obraz TAP. Docelowo plik będzie dostarczany przez przeglądarkę.
- W kodzie nie ma domyślnych poświadczeń Wi-Fi. Na pierwszy test można podać je w lokalnym, ignorowanym przez Git `include/local_wifi.h`. Portal konfiguracji i zapis w LittleFS są kolejnym etapem.
- Połączenia WRITE, dekoder komend, strumieniowanie z przeglądarki i strona kasety są zaplanowane, ale niezaimplementowane.
- Firmware kompiluje się na Macu dla profilu 1M; flash i test na sprzęcie C64 nadal trzeba potwierdzić lokalnie.

## Układ katalogów

| Ścieżka | Znaczenie |
| --- | --- |
| `src/main.cpp` | firmware |
| `include/` | nagłówki i tymczasowy obraz TAP |
| `include/local_wifi.h.example` | wzór lokalnych danych Wi-Fi do pierwszego testu |
| `scripts/` | lokalny bootstrap i uruchamianie PlatformIO |
| `platformio.ini` | przypięta platforma ESP8266, profile flashowania |
| `tools/` | ignorowane przez Git: `uv`, Python, venv PlatformIO |
| `~/.cache/espsette/` | lokalny cache `uv` / PlatformIO (poza NFS) |

## Flash ESP-01 przez USB-UART 3,3 V (krok po kroku)

Programować **tylko zasilaniem 3,3 V**. 5 V na VCC albo na liniach TX/RX zabija ESP8266. Moduł odłączyć od obwodu C64 na czas flashowania.

### Co jest potrzebne

- ESP-01 / ESP-01S
- adapter USB–UART na **3,3 V** (FTDI / CP2102 / CH340 itd.) albo gotowa przejściówka z gniazdem ESP-01
- na Macu: przejściówka USB-C jeśli adapter ma USB-A
- zworka / kabelek do zwarcia **GPIO0–GND** (tryb flash), ewentualnie drugi do **RST–GND** (reset), jeśli adapter nie ma przycisków

### Rozkład pinów ESP-01

Widok **z góry** (antena u góry, 8 pinów u dołu, dwa rzędy po cztery). Lewa kolumna / prawa kolumna:

```
            ANTENA
       ┌─────────────────┐
       │                 │
       │     ESP-01      │
       │                 │
       │  GND      TXD   │  ← rząd bliżej anteny
       │  GPIO2    CH_PD │
       │  GPIO0    RST   │
       │  RXD      VCC   │  ← rząd na krawędzi płytki
       └─────────────────┘
            piny w dół
```

To samo w tabeli (kolejność: od anteny do krawędzi):

| Lewa | Prawa |
| --- | --- |
| **GND** | **TXD** (GPIO1) |
| **GPIO2** | **CH_PD** (EN) |
| **GPIO0** | **RST** |
| **RXD** (GPIO3) | **VCC** (3,3 V) |

ESP-01S ma ten sam układ pinów; różnice to głównie LED / antena PCB — nazwy sygnałów się nie zmieniają.

### Rola pinów przy flashu

| Pin ESP-01 | Rola przy flashu |
| --- | --- |
| **VCC** | +3,3 V z adaptera (nie 5 V) |
| **GND** | masa wspólna z adapterem |
| **RXD** (GPIO3) | ← TXD adaptera USB-UART |
| **TXD** (GPIO1) | → RXD adaptera USB-UART |
| **CH_PD** (EN) | na stałe do **3,3 V** (moduł musi być włączony) |
| **GPIO0** | **GND** = tryb programowania; po flashu odłączyć / puścić w górę = RUN |
| **GPIO2** | zostawić wolny / podciągnięty w górę; **nie** zwierać do GND przy boocie |
| **RST** | impuls do **GND** = reset (wejście w tryb wybrany przez GPIO0) |

Krzyżowanie UART: **TX adaptera → RX ESP**, **RX adaptera → TX ESP**. Wspólne GND obowiązkowe.

Zworka flash: **GPIO0 ↔ GND** (lewa kolumna, trzeci pin od anteny). Reset: krótko **RST ↔ GND** (prawa kolumna, trzeci od anteny).

### Zworka FLASH vs RUN

ESP8266 czyta GPIO0 **w momencie resetu**:

1. **Flash:** zworka **GPIO0–GND**, potem impuls RST–GND (albo wyjęcie/włożenie USB).
2. **Uruchomienie firmware:** zdjąć zworkę z GPIO0 (stan wysoki), znowu RST.

Gotowe programatory ESP-01 często mają przełącznik `FLASH`/`UART` albo przyciski IO0 + RST — to to samo co zworka GPIO0 i reset.

### CH340 bez RTS (sprawdzone na adapterze wemos.cc)

Tani CH340 ma **DTR**, ale zwykle **bez użytecznego RTS**. Działający układ na czas flashu:

| Adapter CH340 | ESP-01 |
| --- | --- |
| **GND** | **GPIO0** (zworka trybu programowania; masa wspólna z resztą połączeń) |
| **TXD** | **RXD** |
| **RXD** | **TXD** |
| **VCC** (3,3 V) | **VCC** |
| **DTR** | **RST** |

**CH_PD** trzymaj na **3,3 V** (na module / do VCC). **GPIO2** nie zwierać do GND. **CTS** / **R/C** zostawić niepodłączone.

`esptool.cfg` pulsuje tylko DTR (polarność dopasowana do tego adaptera). Ładują ją `make flash-id` i `make flash` przez `ESPTOOL_CFGFILE`.

Po wgraniu: **odłączyć GPIO0 od GND**, zresetować (DTR/RST albo USB) — start firmware w trybie RUN.

### Kolejność na stole

1. Połączyć jak w tabeli powyżej (GPIO0–GND, TX↔RX, VCC, DTR–RST, CH_PD→3V3).
2. Podłączyć USB do Maca; `ls /dev/cu.*` (`cu.usbserial-…` / `cu.wchusbserial-…`).
3. `make flash-id PORT=/dev/cu.…` — ma wypisać flash (np. 1 MB); esptool sam impulsuje RST przez DTR.
4. (Opcjonalnie Wi-Fi) `cp include/local_wifi.h.example include/local_wifi.h` i wpisać SSID/hasło.
5. `make setup` raz na maszynie, potem `make build PROFILE=1m`.
6. `make flash PROFILE=1m PORT=/dev/cu.…`
7. Po udanym wgraniu: **zdjąć GPIO0–GND**, zresetować — firmware startuje.
8. Dopiero potem wkładać ESP-01 w układ z C64 (MOTOR/SENSE/READ).

Bez `local_wifi.h` odtwarzanie osadzonego TAP nadal działa; nie będzie STA ani RemoteDebug. Przy złych danych Wi-Fi start czeka do ~20 s, potem gra dalej bez sieci.

```sh
make setup
ls /dev/cu.*
make flash-id PORT=/dev/cu.usbserial-XXXX
cp include/local_wifi.h.example include/local_wifi.h   # opcjonalnie
make build PROFILE=1m
make flash PROFILE=1m PORT=/dev/cu.usbserial-XXXX
# zdjąć GPIO0–GND, reset RST → RUN
```

`make setup` instaluje lokalnie `uv`, Python 3.12 i PlatformIO w `tools/venv`; cache PlatformIO/uv ląduje w `~/.cache/espsette/` (lokalny dysk — NFS bez `flock` psuje build). Nie trzeba globalnego PlatformIO. Potrzebne: `curl`, `make`, internet przy pierwszym setupie, sterownik chipu UART.

Typowe błędy: zasilanie 5 V; brak wspólnego GND; TX/RX nie skrzyżowane; flash bez GPIO0 w LOW; GPIO2 w LOW przy starcie; programowanie przy podłączonym C64.

Profil `PROFILE=512k` tylko kabel i może nie pomieścić binarki. `PROFILE=4m` to profil pamięci „dużego” flash przy tym samym pinoucie ESP-01. Rzeczywisty rozmiar flash: `make flash-id`.

## Pierwsza próba C64 i zdalny debug

Po starcie ESP trzyma SENSE w stanie zwolnionego PLAY podczas konfiguracji Wi-Fi i sprawdzania osadzonego TAP. Po poprawnej walidacji naciska wirtualny PLAY (SENSE niski) i czeka na MOTOR. Wpisanie `LOAD` na C64 uruchamia MOTOR; dopiero wtedy firmware rozpoczyna impulsy READ. Zatrzymanie MOTOR wstrzymuje odtwarzanie i po wznowieniu ma kontynuować z zapamiętanej pozycji.

Po wgraniu i uruchomieniu modułu odczytać jego IP w panelu routera. Na Macu otworzyć `http://ADRES_IP/` — obecna prosta strona podaje stan GPIO, MOTOR, odtwarzacza i długość TAP. Kolorowe logi RemoteDebug są na porcie TCP 23; przed wpisaniem `LOAD` na C64 połączyć terminal:

Domyślny hostname firmware to `espsette`. Ta sama nazwa jest przekazywana do Wi-Fi, ArduinoOTA i RemoteDebug. Obecny firmware nie uruchamia mDNS, więc `espsette.local` nie jest jeszcze gwarantowanym adresem; do pierwszej próby użyć IP przydzielonego przez router.

```sh
nc ADRES_IP 23
```

Logowanie przez UART pozostaje wyłączone, bo te piny sterują SENSE i READ. RemoteDebug wysyła zdarzenia tylko do podłączonego klienta, więc wcześniejszych komunikatów startowych nie odtworzy; co 10 sekund pojawia się stan GPIO, a przy próbie ładowania zdarzenia MOTOR i odtwarzacza. Sprawdzić rozpoznanie nagłówka przez C64, przerwę MOTOR po nagłówku, wznowienie i zakończenie odczytu. Jeżeli nie ma strony ani logu, najpierw sprawdzić IP, konfigurację Wi-Fi i czy Mac oraz ESP są w tej samej sieci. Sam TAP można próbować odtwarzać bez Wi-Fi.

## OTA po uruchomieniu sieci

Po ustawieniu w lokalnym `include/local_wifi.h` także niepustego `ESPSETTE_OTA_PASSWORD` i potwierdzeniu, że moduł ma miejsce na drugi obraz, można użyć:

```sh
make ota PROFILE=1m OTA_HOST=192.168.1.123
```

`make ota` prosi o hasło w terminalu i przekazuje je uploaderowi PlatformIO; musi być takie samo jak w obrazie aktualnie działającym na urządzeniu. Cel OTA buduje ten sam kod co cel szeregowy. Wymaga miejsca na drugi obraz firmware, dlatego moduły 512 KB pozostają przy kablu. Firmware obsługuje OTA tylko poza aktywnym odtwarzaniem. Adapter USB-UART zachować do odzyskania urządzenia po błędzie sieci lub nieudanej aktualizacji. Pierwszą próbę OTA wykonać dopiero po udanym wczytaniu kasety i pomiarze rozmiaru obrazu na Macu.

## Historia i archiwum

Oryginalny projekt znajduje się obok, w `../esptapuino/`, z nienaruszoną historią. **To archiwum zawiera dawne poświadczenia Wi-Fi i nie powinno być publikowane.** Historia `espsette` została przepisana tak, by usunąć definicje poświadczeń ze wszystkich czterech dawnych commitów przy zachowaniu dat autorów i committerów. Ujawnione hasło sieci należy zmienić. Lokalny plik `include/local_wifi.h` służy tylko próbom uruchomieniowym; docelowy portal zapisze poświadczenia w LittleFS.

Źródła dla procesu: [PlatformIO ESP8266](https://docs.platformio.org/en/latest/platforms/espressif8266.html), [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp8266/index.html), [uv: lokalna instalacja](https://docs.astral.sh/uv/reference/installer/), [Arduino ESP8266 OTA](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html).
