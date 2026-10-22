# espsette

Firmware ESP-01 do odtwarzania sygnału kasetowego przez port datasette C64. Projekt jest w trakcie migracji ze starego szkicu Arduino; [analiza i plan](ANALIZA_PROJEKTU.md) opisują kolejne etapy.

## Stan kodu

- `src/main.ino` zawiera historyczny odtwarzacz TAP z nałożoną poprawką tempa I2S, obsługi MOTOR i kończenia transmisji.
- `include/draxcaught.h` to tymczasowy, osadzony obraz TAP. Docelowo plik będzie dostarczany przez przeglądarkę.
- W kodzie nie ma domyślnych poświadczeń Wi-Fi. Na pierwszy test można podać je w lokalnym, ignorowanym przez Git `include/local_wifi.h`. Portal konfiguracji i zapis w LittleFS są kolejnym etapem.
- Połączenia WRITE, dekoder komend, strumieniowanie z przeglądarki i strona kasety są zaplanowane, ale niezaimplementowane.
- Ta reorganizacja nie była kompilowana ani sprawdzana na ESP-01. Pierwszą próbę należy wykonać na Macu z właściwym modułem i adapterem.

## Układ katalogów

| Ścieżka | Znaczenie |
| --- | --- |
| `src/main.ino` | firmware |
| `include/` | nagłówki i tymczasowy obraz TAP |
| `include/local_wifi.h.example` | wzór lokalnych danych Wi-Fi do pierwszego testu |
| `scripts/` | lokalny bootstrap i uruchamianie PlatformIO |
| `platformio.ini` | przypięta platforma ESP8266, profile flashowania |
| `tools/` | ignorowane przez Git: `uv`, Python, PlatformIO, pakiety i toolchainy |
| `.pio/` | ignorowane wyniki budowania |

## Mac z USB-C: pierwszy flash

Potrzebny jest programator USB-UART 3,3 V z gniazdem ESP-01 oraz przewód lub przejściówka USB-C. Powinien zapewniać stabilne zasilanie 3,3 V i przełączenie GPIO0 w tryb programowania. Programowanie szeregowe wykonujemy z modułem odłączonym od obwodu C64. Dla adaptera bez automatycznego resetu trzeba ręcznie ustawić tryb FLASH, zresetować ESP i po wgraniu wrócić do RUN.

Przed kompilacją skopiować `include/local_wifi.h.example` do `include/local_wifi.h` i wpisać własne `ESPSETTE_WIFI_SSID` oraz `ESPSETTE_WIFI_PASS`. Plik `local_wifi.h` jest ignorowany przez Git. Bez niego odtwarzanie z osadzonego TAP nadal startuje, lecz nie ma połączenia sieciowego ani RemoteDebug. Przy błędnych danych połączenie czeka do 20 sekund, po czym odtwarzacz może działać bez sieci. Na tym etapie dane trafiają do **lokalnie zbudowanego obrazu firmware**; docelowy portal zapisze je do LittleFS.

```sh
make setup
ls /dev/cu.*
make flash-id PORT=/dev/cu.usbserial-XXXX
cp include/local_wifi.h.example include/local_wifi.h
# Wpisać własne SSID i hasło w include/local_wifi.h.
make build PROFILE=1m
make flash PROFILE=1m PORT=/dev/cu.usbserial-XXXX
```

`make setup` pobiera `uv` i lokalnego Pythona 3.12 do `tools/`, a następnie instaluje w `tools/venv` przypięte PlatformIO i esptool. Pakiety platformy, biblioteki i toolchainy PlatformIO trafią do `tools/pio-core` przy pierwszym `make build`. Nie dodaje poleceń do profilu powłoki ani nie wymaga globalnego PlatformIO. Potrzebny jest `curl`, `make`, dostęp do internetu przy pierwszej instalacji i ewentualnie sterownik użytego adaptera USB-UART. Katalog `tools/` należy tworzyć osobno na Macu; nie przenosić do niego plików binarnych z WSL.

Profil `PROFILE=512k` jest przewidziany tylko do flashowania kablem i może nie pomieścić obecnej binarki. `PROFILE=4m` używa ogólnego profilu pamięci ESP8266; fizyczny moduł i połączenia pinów pozostają ESP-01. Faktyczną pojemność ustalać przez `make flash-id`. Parametry flash i zgodność API rdzenia/I2S trzeba potwierdzić pierwszą kompilacją oraz próbą na sprzęcie.

## Pierwsza próba C64 i zdalny debug

Po starcie ESP trzyma SENSE w stanie zwolnionego PLAY podczas konfiguracji Wi-Fi i sprawdzania osadzonego TAP. Po poprawnej walidacji naciska wirtualny PLAY (SENSE niski) i czeka na MOTOR. Wpisanie `LOAD` na C64 uruchamia MOTOR; dopiero wtedy firmware rozpoczyna impulsy READ. Zatrzymanie MOTOR wstrzymuje odtwarzanie i po wznowieniu ma kontynuować z zapamiętanej pozycji.

Po wgraniu i uruchomieniu modułu odczytać jego IP w panelu routera. Na Macu otworzyć `http://ADRES_IP/` — obecna prosta strona podaje stan GPIO, MOTOR, odtwarzacza i długość TAP. Kolorowe logi RemoteDebug są na porcie TCP 23; przed wpisaniem `LOAD` na C64 połączyć terminal:

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
