# espsette

Firmware ESP-01 do odtwarzania sygnału kasetowego przez port datasette C64. Projekt jest w trakcie migracji ze starego szkicu Arduino; [analiza i plan](ANALIZA_PROJEKTU.md) opisują kolejne etapy.

## Stan kodu

- `src/main.ino` zawiera historyczny odtwarzacz TAP z nałożoną poprawką tempa I2S, obsługi MOTOR i kończenia transmisji.
- `include/draxcaught.h` to tymczasowy, osadzony obraz TAP. Docelowo plik będzie dostarczany przez przeglądarkę.
- Dane Wi-Fi są wprowadzane przez portal AP i zapisywane do LittleFS. Nie są kompilowane do firmware. Ta gałąź implementuje konfigurację; wcześniejszy wariant do pierwszej próby pozostaje w sąsiednim worktree `../espsette/`.
- Połączenia WRITE, dekoder komend, strumieniowanie z przeglądarki i strona kasety są zaplanowane, ale niezaimplementowane.
- Zmiany w tym worktree nie były kompilowane ani sprawdzane na ESP-01. Pierwszą próbę sprzętową z osadzonym TAP można wykonać z sąsiedniego worktree `../espsette/`; tę gałąź skompilować osobno później.

## Układ katalogów

| Ścieżka | Znaczenie |
| --- | --- |
| `src/main.ino` | firmware |
| `include/` | nagłówki i tymczasowy obraz TAP |
| `src/network_config.*` | portal AP, połączenie STA i konfiguracja LittleFS |
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

Po starcie ESP trzyma SENSE w stanie zwolnionego PLAY podczas konfiguracji Wi-Fi i sprawdzania osadzonego TAP. Po poprawnej walidacji naciska wirtualny PLAY (SENSE niski) i czeka na MOTOR. Wpisanie `LOAD` na C64 uruchamia MOTOR; dopiero wtedy firmware rozpoczyna impulsy READ. Zatrzymanie MOTOR wstrzymuje odtwarzanie i po wznowieniu ma kontynuować z zapamiętanej pozycji.

Po konfiguracji Wi-Fi odczytać jego IP w panelu routera. Na Macu otworzyć `http://ADRES_IP/` — obecna prosta strona podaje stan GPIO, MOTOR, odtwarzacza i długość TAP. Kolorowe logi RemoteDebug są na porcie TCP 23; przed wpisaniem `LOAD` na C64 połączyć terminal:

Domyślny hostname firmware to `espsette`. Ta sama nazwa jest przekazywana do Wi-Fi, ArduinoOTA i RemoteDebug. Obecny firmware nie uruchamia mDNS, więc `espsette.local` nie jest jeszcze gwarantowanym adresem; do pierwszej próby użyć IP przydzielonego przez router.

```sh
nc ADRES_IP 23
```

Logowanie przez UART pozostaje wyłączone, bo te piny sterują SENSE i READ. RemoteDebug wysyła zdarzenia tylko do podłączonego klienta, więc wcześniejszych komunikatów startowych nie odtworzy; co 10 sekund pojawia się stan GPIO, a przy próbie ładowania zdarzenia MOTOR i odtwarzacza. Sprawdzić rozpoznanie nagłówka przez C64, przerwę MOTOR po nagłówku, wznowienie i zakończenie odczytu. Jeżeli nie ma strony ani logu, najpierw sprawdzić IP, konfigurację Wi-Fi i czy Mac oraz ESP są w tej samej sieci. Sam TAP można próbować odtwarzać bez Wi-Fi.

## OTA po uruchomieniu sieci

Po skopiowaniu `include/local_ota.h.example` do ignorowanego `include/local_ota.h`, ustawieniu tam niepustego `ESPSETTE_OTA_PASSWORD` i potwierdzeniu, że moduł ma miejsce na drugi obraz, można użyć:

```sh
make ota PROFILE=1m OTA_HOST=192.168.1.123
```

`make ota` prosi o hasło w terminalu i przekazuje je uploaderowi PlatformIO; musi być takie samo jak w obrazie aktualnie działającym na urządzeniu. Cel OTA buduje ten sam kod co cel szeregowy. Wymaga miejsca na drugi obraz firmware, dlatego moduły 512 KB pozostają przy kablu. Firmware obsługuje OTA tylko poza aktywnym odtwarzaniem. Adapter USB-UART zachować do odzyskania urządzenia po błędzie sieci lub nieudanej aktualizacji. Pierwszą próbę OTA wykonać dopiero po udanym wczytaniu kasety i pomiarze rozmiaru obrazu na Macu.

## Historia i archiwum

Oryginalny projekt znajduje się obok, w `../esptapuino/`, z nienaruszoną historią. **To archiwum zawiera dawne poświadczenia Wi-Fi i nie powinno być publikowane.** Historia `espsette` została przepisana tak, by usunąć definicje poświadczeń ze wszystkich czterech dawnych commitów przy zachowaniu dat autorów i committerów. Ujawnione hasło sieci należy zmienić. Ta gałąź nie używa lokalnego nagłówka z danymi sieci; portal zapisuje je do LittleFS. Otwarty AP i formularz HTTP przesyłają hasło bez szyfrowania radiowego, dlatego pierwszą konfigurację wykonywać tylko w zaufanym otoczeniu. Konfiguracja pod `/wifi` w sieci lokalnej nie ma jeszcze uwierzytelniania.

Źródła dla procesu: [PlatformIO ESP8266](https://docs.platformio.org/en/latest/platforms/espressif8266.html), [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp8266/index.html), [uv: lokalna instalacja](https://docs.astral.sh/uv/reference/installer/), [Arduino ESP8266 OTA](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html).
