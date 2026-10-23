# espsette

Firmware ESP-01 do odtwarzania sygnału kasetowego przez port datasette C64 lub C16/Plus4 PAL. Projekt jest w trakcie migracji ze starego szkicu Arduino; [analiza i plan](ANALIZA_PROJEKTU.md) opisują kolejne etapy.

## Stan kodu

- `src/main.ino` zawiera historyczny odtwarzacz TAP z nałożoną poprawką tempa I2S, obsługi MOTOR i kończenia transmisji.
- `include/draxcaught.h` pozostaje demonstracyjnym TAP. Można też wskazać w przeglądarce duży plik TAP C64 lub C16/Plus4 PAL v1/v2 albo zwykły PRG; strona podaje dane porcjami do bufora 8 KB ESP.
- Dane Wi-Fi są wprowadzane przez portal AP i zapisywane do LittleFS. Nie są kompilowane do firmware. Ta gałąź implementuje konfigurację; wcześniejszy wariant do pierwszej próby pozostaje w sąsiednim worktree `../espsette/`.
- Strumieniowanie TAP, tekstowy IDX w formacie Ultimate 64 i podstawowa strona decku są zaimplementowane w tej gałęzi. Generator standardowej kasety z PRG dla C64 i C16/Plus4 PAL jest zaimplementowany. Dekoder nagłówka SAVE z WRITE jest gotowy w osobnym profilu sprzętowym; turbo pozostaje kolejnym etapem. Strona ma skompresowany obraz kasety, animowane szpulki i pięć klawiszy decku.
- Zmiany w tym worktree nie były kompilowane ani sprawdzane na ESP-01. Pierwszą próbę sprzętową z osadzonym TAP można wykonać z sąsiedniego worktree `../espsette/`; tę gałąź skompilować osobno później.

## Układ katalogów

| Ścieżka | Znaczenie |
| --- | --- |
| `src/main.ino` | firmware |
| `include/` | nagłówki i tymczasowy obraz TAP |
| `src/network_config.*` | portal AP, połączenie STA i konfiguracja LittleFS |
| `src/tape_source.*` | ograniczony bufor kasety zasilany porcjami z przeglądarki |
| `src/prg_encoder.*` | standardowe impulsy kasety C64/C16 generowane z PRG |
| `src/write_decoder.*` | weryfikacja podwójnego nagłówka `SAVE` C64 i C16/Plus4 bez wykonywania komend |
| `web/player.html` | źródło strony i samodzielny podgląd lokalny |
| `scripts/embed-player.py` | generuje `src/player_page.h` z HTML przed budowaniem |
| `include/local_ota.h.example` | opcjonalne hasło OTA, bez danych Wi-Fi |
| `scripts/` | lokalny bootstrap i uruchamianie PlatformIO |
| `platformio.ini` | przypięta platforma ESP8266, profile flashowania |
| `tools/` | ignorowane przez Git: `uv`, Python, PlatformIO, pakiety i toolchainy |
| `.pio/` | ignorowane wyniki budowania |

## Mac z USB-C: pierwszy flash

Potrzebny jest programator USB-UART 3,3 V z gniazdem ESP-01 oraz przewód lub przejściówka USB-C. Powinien zapewniać stabilne zasilanie 3,3 V i przełączenie GPIO0 w tryb programowania. Programowanie szeregowe wykonujemy z modułem odłączonym od obwodu C64. Dla adaptera bez automatycznego resetu trzeba ręcznie ustawić tryb FLASH, zresetować ESP i po wgraniu wrócić do RUN.

Po pierwszym uruchomieniu, gdy nie ma poprawnego `/config.json` albo sieć jest niedostępna, moduł tworzy otwarty AP `espsette-XXXXXX`. Połącz Maca lub telefon z tą siecią i otwórz `http://192.168.4.1/`. Portal pozwala podać SSID, hasło, nazwę hosta i opcjonalnie statyczny adres IPv4. Po zapisie ESP uruchomi się ponownie i spróbuje połączyć z siecią. Jeśli to się nie uda, AP wróci; konfiguracja pozostanie zachowana. Ustawienia można później zmienić pod `/wifi`, a przycisk „Ponów połączenie” ponawia próbę przez restart, gdy MOTOR jest wyłączony.

```sh
make setup
ls /dev/cu.*
make flash-id PORT=/dev/cu.usbserial-XXXX
make build PROFILE=1m
make flash PROFILE=1m PORT=/dev/cu.usbserial-XXXX
```

`make setup` pobiera `uv` i lokalnego Pythona 3.12 do `tools/`, a następnie instaluje w `tools/venv` przypięte PlatformIO i esptool. Pakiety platformy, biblioteki i toolchainy PlatformIO trafią do `tools/pio-core` przy pierwszym `make build`. Nie dodaje poleceń do profilu powłoki ani nie wymaga globalnego PlatformIO. Potrzebny jest `curl`, `make`, dostęp do internetu przy pierwszej instalacji i ewentualnie sterownik użytego adaptera USB-UART. Katalog `tools/` należy tworzyć osobno na Macu; nie przenosić do niego plików binarnych z WSL.

Profil `PROFILE=512k` jest przewidziany tylko do flashowania kablem i może nie pomieścić obecnej binarki. `PROFILE=4m` używa ogólnego profilu pamięci ESP8266; fizyczny moduł i połączenia pinów pozostają ESP-01. Faktyczną pojemność ustalać przez `make flash-id`. Parametry flash i zgodność API rdzenia/I2S trzeba potwierdzić pierwszą kompilacją oraz próbą na sprzęcie.

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

## OTA po uruchomieniu sieci

Po skopiowaniu `include/local_ota.h.example` do ignorowanego `include/local_ota.h`, ustawieniu tam niepustego `ESPSETTE_OTA_PASSWORD` i potwierdzeniu, że moduł ma miejsce na drugi obraz, można użyć:

```sh
make ota PROFILE=1m OTA_HOST=192.168.1.123
```

`make ota` prosi o hasło w terminalu i przekazuje je uploaderowi PlatformIO; musi być takie samo jak w obrazie aktualnie działającym na urządzeniu. Cel OTA buduje ten sam kod co cel szeregowy. Wymaga miejsca na drugi obraz firmware, dlatego moduły 512 KB pozostają przy kablu. Firmware obsługuje OTA tylko poza aktywnym odtwarzaniem. Adapter USB-UART zachować do odzyskania urządzenia po błędzie sieci lub nieudanej aktualizacji. Pierwszą próbę OTA wykonać dopiero po udanym wczytaniu kasety i pomiarze rozmiaru obrazu na Macu.

## Historia i archiwum

Oryginalny projekt znajduje się obok, w `../esptapuino/`, z nienaruszoną historią. **To archiwum zawiera dawne poświadczenia Wi-Fi i nie powinno być publikowane.** Historia `espsette` została przepisana tak, by usunąć definicje poświadczeń ze wszystkich czterech dawnych commitów przy zachowaniu dat autorów i committerów. Ujawnione hasło sieci należy zmienić. Ta gałąź nie używa lokalnego nagłówka z danymi sieci; portal zapisuje je do LittleFS. Otwarty AP i formularz HTTP przesyłają hasło bez szyfrowania radiowego, dlatego pierwszą konfigurację wykonywać tylko w zaufanym otoczeniu. Konfiguracja pod `/wifi` jest przeznaczona do zaufanej sieci LAN.

Źródła dla procesu: [PlatformIO ESP8266](https://docs.platformio.org/en/latest/platforms/espressif8266.html), [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp8266/index.html), [uv: lokalna instalacja](https://docs.astral.sh/uv/reference/installer/), [Arduino ESP8266 OTA](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html).
