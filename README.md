# espsette

Firmware ESP-01 do odtwarzania sygnału kasetowego przez port datasette C64 lub C16/Plus4 PAL. Projekt jest w trakcie migracji ze starego szkicu Arduino; [analiza i plan](ANALIZA_PROJEKTU.md) opisują kolejne etapy.

## Stan kodu

- `src/main.cpp` zawiera historyczny odtwarzacz TAP z nałożoną poprawką tempa I2S, obsługi MOTOR i kończenia transmisji.
- `include/draxcaught.h` pozostaje małym demonstracyjnym TAP w PROGMEM. Duże TAP/PRG **tylko ze strony**: przeglądarka trzyma plik i dokłada porcje do bufora 8 KB na ESP (bez cache w LittleFS).
- Dane Wi-Fi wprowadza **WiFiManager** (captive portal jak w ESPHome: skan SSID + hasło). Hostname / autoplay to drobne pliki w LittleFS; poświadczenia sieci trzyma WiFiManager.
- Strumieniowanie TAP, tekstowy IDX w formacie Ultimate 64 i podstawowa strona decku są zaimplementowane w tej gałęzi. Generator standardowej kasety z PRG dla C64 i C16/Plus4 PAL jest zaimplementowany. Dekoder nagłówka SAVE z WRITE jest gotowy w osobnym profilu sprzętowym; turbo pozostaje kolejnym etapem. Strona ma skompresowany obraz kasety, animowane szpulki i pięć klawiszy decku.
- Domyślny build ma `ESPSETTE_GPIO_DISABLED=1` (Wi-Fi/AP/strona bez sterowania pinami kasety); do C64/C16 wyłączyć tę flagę.

## Układ katalogów

| Ścieżka | Znaczenie |
| --- | --- |
| `src/main.cpp` | firmware |
| `include/` | nagłówki i tymczasowy obraz TAP |
| `src/network_config.*` | WiFiManager captive portal + hostname w LittleFS |
| `src/tape_source.*` | ograniczony bufor kasety zasilany porcjami z przeglądarki |
| `src/prg_encoder.*` | standardowe impulsy kasety C64/C16 generowane z PRG |
| `src/write_decoder.*` | weryfikacja podwójnego nagłówka `SAVE` C64 i C16/Plus4 bez wykonywania komend |
| `web/player.html` | źródło strony i samodzielny podgląd lokalny |
| `scripts/embed-player.py` | generuje `src/player_page.h` z HTML przed budowaniem |
| `include/local_ota.h.example` | opcjonalne hasło OTA, bez danych Wi-Fi |
| `scripts/` | lokalny bootstrap, PlatformIO, `esp_boot_dump.py` |
| `esptool.cfg` | reset DTR-only dla CH340 (wemos.cc) |
| `platformio.ini` | przypięta platforma ESP8266, profile flashowania |
| `tools/` | ignorowane przez Git: `uv`, Python, PlatformIO, pakiety i toolchainy |
| `~/.cache/espsette/` | lokalny cache `uv` / PlatformIO (poza NFS) |
| `.pio/` | ignorowane wyniki budowania |

## Flash ESP-01 przez USB-UART 3,3 V (krok po kroku)

Programować **tylko zasilaniem 3,3 V**. 5 V na VCC albo na liniach TX/RX zabija ESP8266. Moduł odłączyć od obwodu C64/C16 na czas flashowania.

Gdy brak zapisanej sieci albo połączenie się nie uda, ESP podnosi AP `espsette-XXXXXX` z **captive portal** (WiFiManager): lista sieci, hasło, hostname. Po zapisie łączy się ze STA. **OTA** (`make ota OTA_HOST=<IP>`) nie zmienia Wi-Fi. Strona `/wifi` tylko pokazuje potwierdzenie; kasowanie sieci wymaga osobnego POST. Zwykły `make flash` nie kasuje poświadczeń WM; `erase_flash` tak.

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
3. `make flash-id PORT=/dev/cu.…` albo pełniejszy odczyt `make chip-info PORT=/dev/cu.…` (flash-id, chip-id, MAC, status flash); esptool sam impulsuje RST przez DTR.
4. `make setup` raz na maszynie, potem `make build PROFILE=1m` (domyślnie z `ESPSETTE_GPIO_DISABLED=1` do prób na samym USB-serial).
5. `make flash PROFILE=1m PORT=/dev/cu.…`
6. Po udanym wgraniu: **zdjąć GPIO0–GND**, **odłączyć DTR–RST** na czas normalnej pracy (inaczej adapter może resetować moduł), zresetować — firmware startuje.
7. Szukać AP `espsette-XXXXXX` albo IP w LAN i otworzyć portal / player.
8. Dopiero po sprawdzeniu sieci wkładać ESP-01 w układ z C64/C16 (MOTOR/SENSE/READ); do realnych GPIO ustawić `-DESPSETTE_GPIO_DISABLED=0`.

Diagnostyka boot ROM na USB: `PORT=/dev/cu.… uv run --with pyserial ./scripts/esp_boot_dump.py -n 3` (74880 baud, impuls DTR jak w `esptool.cfg`).

```sh
make setup
ls /dev/cu.*
make flash-id PORT=/dev/cu.usbserial-XXXX   # albo: make chip-info PORT=...
make build PROFILE=1m
make flash PROFILE=1m PORT=/dev/cu.usbserial-XXXX
# zdjąć GPIO0–GND, odłączyć DTR–RST, reset → RUN / AP
```

`make setup` instaluje lokalnie `uv`, Python 3.12 i PlatformIO w `tools/venv`; cache PlatformIO/uv ląduje w `~/.cache/espsette/` (lokalny dysk — NFS bez `flock` psuje build). Nie trzeba globalnego PlatformIO. Potrzebne: `curl`, `make`, internet przy pierwszym setupie, sterownik chipu UART. Lista celów: `make help`.

Typowe błędy: zasilanie 5 V; brak wspólnego GND; TX/RX nie skrzyżowane; flash bez GPIO0 w LOW; GPIO2 w LOW przy starcie; programowanie przy podłączonym C64.

Profil `PROFILE=512k` tylko kabel i może nie pomieścić binarki. `PROFILE=4m` to profil pamięci „dużego” flash przy tym samym pinoucie ESP-01. Rzeczywisty rozmiar flash: `make flash-id`.

## Piny kasety (C64 / C16)

Mapowanie firmware ↔ port datasette. Poziomy 5 V / izolacja / bootstrapping: [kicad/PLAN_POLACZEN.md](kicad/PLAN_POLACZEN.md). Domyślny build ma `ESPSETTE_GPIO_DISABLED=1` i **nie** steruje tymi pinami (tylko Wi‑Fi/AP/strona); do realnego toru kasety ustaw `-DESPSETTE_GPIO_DISABLED=0`.

| ESP-01 | GPIO | Kierunek | Datasette | Rola |
| --- | --- | --- | --- | --- |
| GPIO0 | 0 | ← wejście | **MOTOR** | HIGH = silnik włączony (C64/C16); nie wolno trzymać LOW przy boocie ESP |
| TXD | 1 | → wyjście | **SENSE** (PLAY) | LOW = wirtualne PLAY wciśnięte; to też UART TX — bez soft-GPIO nie ma logu na USB |
| GPIO2 | 2 | ← wejście (opcjonalnie) | **WRITE** | tylko profil `1m-write` / `4m-write`; musi być HIGH przy starcie ESP |
| RXD | 3 | → wyjście I2S | **READ** | impulsy taśmy; w spoczynku kod trzyma GPIO LOW (zakłada odwrócenie tranzystorem) |

Wspólne **GND** z komputerem / zasilaniem. UART programatora na GPIO1/GPIO3 tylko po odłączeniu torów SENSE/READ od portu kasety.

## Pierwsza próba C64 i zdalny debug

Po starcie ESP trzyma SENSE w stanie zwolnionego PLAY podczas konfiguracji Wi-Fi i sprawdzania osadzonego TAP. Po poprawnej walidacji wciska wirtualny PLAY (SENSE niski), jeżeli autoplay jest włączony; w przeciwnym razie czeka na ręczne PLAY na stronie. Wpisanie `LOAD` na C64 uruchamia MOTOR; dopiero wtedy firmware rozpoczyna impulsy READ. Zatrzymanie MOTOR wstrzymuje odtwarzanie i po wznowieniu ma kontynuować z zapamiętanej pozycji.

Po konfiguracji Wi-Fi odczytać jego IP w panelu routera. Na Macu otworzyć `http://ADRES_IP/` — strona pokazuje deck, stan MOTOR, kasetę, odtworzony czas i bufor. W trybie AP portal działa pod `/`, a odtwarzacz pod `/player`. Kolorowe logi RemoteDebug są na porcie TCP 23; przed wpisaniem `LOAD` na C64 połączyć terminal:

Domyślny hostname firmware to `espsette`. Ta sama nazwa jest przekazywana do Wi-Fi, ArduinoOTA i RemoteDebug. Obecny firmware nie uruchamia mDNS, więc `espsette.local` nie jest jeszcze gwarantowanym adresem; do pierwszej próby użyć IP przydzielonego przez router.

```sh
nc ADRES_IP 23
```

Logowanie przez UART pozostaje wyłączone, bo te piny sterują SENSE i READ. RemoteDebug wysyła zdarzenia tylko do podłączonego klienta, więc wcześniejszych komunikatów startowych nie odtworzy; co 10 sekund pojawia się stan GPIO, a przy próbie ładowania zdarzenia MOTOR i odtwarzacza. Sprawdzić rozpoznanie nagłówka przez C64, przerwę MOTOR po nagłówku, wznowienie i zakończenie odczytu. Jeżeli nie ma strony ani logu, najpierw sprawdzić IP, konfigurację Wi-Fi i czy Mac oraz ESP są w tej samej sieci. Sam TAP można próbować odtwarzać bez Wi-Fi.

## Lokalny podgląd strony bez ESP-01

Otwórz [web/player.html](web/player.html) bezpośrednio w przeglądarce albo użyj `make preview`. Przy adresie `file://` strona używa symulowanego stanu ESP: można wybrać TAP lub PRG, włączyć/wyłączyć MOTOR, wysunąć kasetę i obserwować wskaźniki. To symulacja interfejsu i przepływu żądań, **nie test sygnału na GPIO ani wydajności Wi-Fi**. Ten sam HTML jest przed budowaniem wstawiany do firmware przez `make embed-web`, więc zmiany UI nie wymagają flashowania, dopóki nie są gotowe do próby na urządzeniu. `make preview` nie wymaga instalacji PlatformIO.

Opcjonalnie można pobrać [film magnetofonu z Pixabay](https://pixabay.com/videos/tape-recorder-music-stale-nostalgic-118790/) i wskazać go w polu „film w okienku”. Wideo pozostaje lokalnym plikiem przeglądarki; odtwarza się przy `motorOn` i zatrzymuje przy postoju. Nie jest dołączone do repozytorium ani obrazu ESP. Bez filmu strona pokazuje skompresowaną kasetę WebP z animowanymi szpulkami CSS. Obraz pozostaje w `web/assets/` i jest osadzany w HTML przy `make embed-web`; rozmiar binarki wymaga pomiaru na Macu. Strona źródłowa opisuje materiał jako MP4 1920×1080, 50 FPS, dostępny na licencji Pixabay Content License.

## Klawisze decku i autoplay

PLAY/STOP ustawiają linię SENSE; nie sterują silnikiem, którym nadal zarządza C64/C16. Autoplay jest domyślnie włączony, zapisuje się w LittleFS i wciska PLAY przy włożeniu TAP/PRG lub demo. Po wyłączeniu autoplay nowa kaseta zaczyna ze zwolnionym SENSE. Domyślny komputer dla PRG (C64 albo C16/Plus4 PAL) także zapisuje się w LittleFS; początkowo jest nim C64. Można go zmienić na stronie bez zmiany już włożonej kasety, a dla pojedynczego PRG wybrać inny komputer w formularzu pliku. Dla TAP komputer wynika z nagłówka, a osadzone demo jest C64. EJECT zwalnia PLAY i wysuwa źródło po zatrzymaniu MOTOR. REW/FF działają przy wyłączonym MOTOR: dla TAP z IDX skaczą do poprzedniej/następnej pozycji, a bez IDX do kolejnych dziesiątek trzycyfrowego licznika; skok trafia na punkt lokalnego indeksu czasu położony na granicy rekordu TAP. Licznik stosuje ten sam model szpuli i zaokrąglenie co Pi1551 (grubość taśmy 12,7 µm, promień piasty 10,7 mm, prędkość taśmy 47,6 mm/s, przełożenie 0,525). Model i stałe pochodzą z VICE. Wskazanie zależy od oszacowanego czasu impulsów faktycznie wysłanych na READ, nie od liczby bajtów przesłanych po Wi-Fi; może różnić się od mechanicznego licznika konkretnego magnetofonu. Dla PRG REW tworzy nową sesję tego samego programu, FF jest niedostępny; dla osadzonego demo REW zaczyna od początku. Rozwijana sekcja „Debug / ESP state” pokazuje surowy `/state` i liczniki diagnostyczne. Komunikaty strony, portalu Wi-Fi, odpowiedzi HTTP i logi RemoteDebug są po angielsku. Panel jest przeznaczony do zaufanej sieci LAN; nie ma logowania.

[Model licznika VICE](https://github.com/VICE-Team/svn-mirror/blob/main/vice/src/datasette/datasette.h), [implementacja VICE](https://github.com/VICE-Team/svn-mirror/blob/main/vice/src/datasette/datasette.c).

## Strumieniowanie TAP w tej gałęzi

Strona skanuje TAP lokalnie, tworzy sesję przez `POST /session`, a następnie wysyła maksymalnie 1024 bajty na żądanie `POST /chunk` z ID sesji i bezwzględnym offsetem. Można też wskazać towarzyszący `.idx` (lub upuścić TAP i IDX razem): parser przyjmuje do 512 wpisów dziesiętnych, `0x` szesnastkowych i ósemkowych z zerem na początku; odrzuca offsety nagłówka (<20), wartości poza 32 bitami, pozycje spoza TAP oraz wewnątrz rekordu impulsu. `GET /state` zwraca wolne miejsce 8 KB bufora, MOTOR, `loaded/ejected`, postęp nadawania READ, błąd oraz diagnostykę. Przy pełnym buforze przeglądarka nie utrzymuje otwartego uploadu; po pauzie MOTOR dosyła następne porcje. `POST /eject`, `POST /demo` i `POST /seek` działają tylko przy wyłączonym MOTOR. Przeskok IDX opróżnia bufor i zmienia ID sesji; przeglądarka dosyła TAP od wybranego absolutnego offsetu. Po utracie połączenia otwarta karta zachowuje plik i odczytuje `/state`: jeśli ID sesji, typ i rozmiar logiczny są te same, przyjmuje offset ESP jako wiążący i wznawia dosyłanie bez duplikatów. Przy zmianie sesji lub błędzie odtwarzania przestaje dosyłać i wymaga ponownego włożenia pliku. Odświeżenie karty traci uchwyt do pliku; nie ma wtedy automatycznego wznowienia. Sumę cykli z pliku wylicza przeglądarka; ESP używa jej tylko do prezentacji czasu, a nie do sterowania sygnałem.

`/state` podaje także `freeHeap`, `maxFreeBlock`, największy czas pętli i obsługi HTTP w mikrosekundach oraz liczniki niedoborów I2S/źródła i błędów uploadu. Liczniki rosną od startu ESP i służą do diagnozy pierwszych prób; nie potwierdzają poprawności impulsów na READ. Tekstowy plan torów sprzętowych jest w [kicad/PLAN_POLACZEN.md](kicad/PLAN_POLACZEN.md); rysunek KiCad pozostaje odłożony.

Ten transport obsługuje **C64 PAL i C16/Plus4 PAL TAP v1 i v2**. W v1 jeden rekord oznacza pełny okres, a w v2 kolejne półokresy; po skoku IDX przeglądarka przekazuje parzystość liczby poprzednich rekordów, aby zachować poziom sygnału. [Specyfikacja VICE](https://vice-emu.sourceforge.io/vice_17.html) formalnie opisuje C64 TAP v1 i C16 TAP v2; C64 v2 oraz C16 v1 przyjmujemy jako spotykane warianty zgodności. Interpretację v2 niezależnie od sygnatury potwierdza [kod Tapuino](https://github.com/sweetlilmre/tapuino/blob/master/tapuino.c). Plik pozostaje w otwartej karcie przeglądarki; jej uśpienie może opróżnić bufor i zakończyć LOAD błędem. 8 KB bufora i czas obsługi HTTP wymagają pomiaru na prawdziwym ESP-01/C64, szczególnie dla szybkich loaderów. Endpointy są przeznaczone do zaufanej sieci LAN. ESP sprawdza zakres offsetu przeskoku, a granicę rekordu sprawdza przeglądarka podczas skanowania TAP; bez pełnej kopii kasety ESP nie może niezależnie potwierdzić granicy dla dowolnego offsetu. Format Manosoft Princess nie jest obsługiwany. Na modułach 512 KB lub 1 MB może zabraknąć miejsca na połączenie demonstracyjnego TAP, strony, LittleFS i OTA; rozmiar obrazu trzeba zmierzyć podczas kompilacji na Macu.

Po zakończeniu odtwarzania `/state` zachowuje fazę `done` i odtworzony czas także po zatrzymaniu MOTOR; nowa sesja, wysunięcie lub ponowne uruchomienie osadzonej kasety zerują licznik. Odtwarzacz nie wie, czy komputer poprawnie zdekodował dane — to czas wysłanego sygnału READ.

## Standardowy PRG C64 i C16/Plus4 PAL

Wybór `.prg` uruchamia osobną sesję. Dwa pierwsze bajty pliku określają adres ładowania; w polu „Nazwa na kasecie” można ustawić nazwę widoczną na C64. Firmware tworzy nagłówek typu 1, pilot, synchronizację, parzystość, sumy XOR, obie kopie nagłówka i obie kopie danych. Impulsy powstają na bieżąco, bez zapisywania pliku TAP. Przeglądarka czyta dwa razy te same bajty PRG przez `Blob.slice()`; ESP widzi je jako kolejne offsety logiczne, więc nie musi pamiętać całego programu. Po nagłówku C64 może zatrzymać MOTOR, a odtwarzacz zachowuje pozycję do wznowienia.

Obsługiwany jest standardowy `LOAD` na C64 PAL oraz C16/Plus4 PAL bez turbo. PRG musi zawierać co najmniej jeden bajt danych, a adres końcowy mieścić się do `$FFFF`. Wybór C16/Plus4 jest aktywny; opcja loadera turbo pozostaje wyłączona. PRG nie ma pozycji IDX. Implementację przebiegu porównano z [kodem WAV-PRG](https://github.com/fabzzap/wav-prg/blob/master/prg2wav_core/prg2wav_core.c); nadal wymaga kompilacji na Macu i sprawdzenia impulsów oraz ładowania na C64 i C16/Plus4. C16 wymaga odpowiedniego adaptera do portu 1531; to nie jest ten sam fizyczny wtyk co C64.

## Diagnostyczny odbiór WRITE

`src/write_decoder.*` automatycznie rozpoznaje standardowy nagłówek `SAVE` C64 albo C16/Plus4 po długościach impulsów: sekwencję synchronizacji, parzystość bajtów, sumę XOR, znacznik końca i zgodność drugiej kopii. Po poprawnym odbiorze `/state` oraz strona pokazują rozpoznaną maszynę, nazwę, typ, adresy i informację „brak przypisanej akcji”. Dane programu po nagłówku są ignorowane. Firmware nie interpretuje nazwy jako komendy i niczego przez WRITE nie nadaje.

Domyślny profil **nie podłącza przerwania GPIO2**. `PROFILE=1m-write` lub `4m-write` włącza nasłuch dopiero dla płytki, która chroni GPIO2 przed 5 V, gwarantuje wysoki stan przy starcie ESP i odłącza C64 podczas flashowania. Profil wymaga API I2S bez zegara na GPIO2; przy jego braku kompilacja kończy się błędem. Do czasu sprawdzenia schematu i pomiaru nie należy łączyć WRITE C64 bezpośrednio z ESP-01. Strona w zwykłym profilu wyraźnie pokazuje, że nasłuch jest nieaktywny; w lokalnym podglądzie można kliknąć „Symuluj SAVE”, aby obejrzeć prezentację wyniku. Test dekodera obu maszyn dla komputera hosta jest w `tests/write_decoder_test.cpp` i czeka na uruchomienie na Macu. C16 przyjmuje także dwie kopie nagłówka bez osobnego znacznika końca, jak w wektorze Pi1551; C64 używa znacznika długi+krótki. Odstępy między zboczami WRITE trzeba potwierdzić na obu komputerach.

Wektory generatora PRG i przypadki IDX na podstawie testów Pi1551 opisuje [tests/README.md](tests/README.md).

## Flash layout 1 MB, OTA i brak cache TAP

Profil `PROFILE=1m` **musi** używać `eagle.flash.1m64.ld` (ustawione w `platformio.ini`): ~958 KB na szkic + **64 KB LittleFS**. Domyślny layout PlatformIO dla `esp01_1m` (256 KB FS) zostawia tylko ~762 KB na firmware — po WiFiManagerze, stronie i ArduinoOTA obraz się nie mieści.

To świadomy kompromis:

- **OTA działa** (`make ota`, domyślnie bez hasła na zaufanej LAN).
- LittleFS starczy na hostname, autoplay i flagi migracji — **nie** na przechowywanie kaset.
- Dlatego TAP/PRG **streamujemy ze strony** (plik w karcie przeglądarki → `POST /chunk` do okna 8 KB). Lokalny cache TAP w LittleFS na tym układzie **nie jest planowany** i nie zmieści się obok OTA bez rezygnacji z aktualizacji Wi‑Fi albo bez większego flash (np. 4 MB).

Osadzone demo `draxcaught` siedzi w PROGMEM (w obrazie firmware), nie w LittleFS.

```sh
make ota PROFILE=1m OTA_HOST=192.168.1.123
```

Opcjonalne hasło OTA: `include/local_ota.h` + ta sama wartość w `ESPSETTE_OTA_PASSWORD` przy `make ota`. Firmware przyjmuje OTA tylko poza aktywnym odtwarzaniem. Adapter USB-UART zachować do odzyskania. Moduły 512 KB zwykle nie mieszczą tego firmware ani OTA.

## Historia i archiwum

Oryginalny projekt znajduje się obok, w `../esptapuino/`, z nienaruszoną historią. **To archiwum zawiera dawne poświadczenia Wi-Fi i nie powinno być publikowane.** Historia `espsette` została przepisana tak, by usunąć definicje poświadczeń ze wszystkich czterech dawnych commitów przy zachowaniu dat autorów i committerów. Ujawnione hasło sieci należy zmienić. Ta gałąź nie używa lokalnego nagłówka z danymi sieci; portal zapisuje je do LittleFS. Otwarty AP i formularz HTTP przesyłają hasło bez szyfrowania radiowego, dlatego pierwszą konfigurację wykonywać tylko w zaufanym otoczeniu. Konfiguracja pod `/wifi` jest przeznaczona do zaufanej sieci LAN.

Źródła dla procesu: [PlatformIO ESP8266](https://docs.platformio.org/en/latest/platforms/espressif8266.html), [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp8266/index.html), [uv: lokalna instalacja](https://docs.astral.sh/uv/reference/installer/), [Arduino ESP8266 OTA](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html).
