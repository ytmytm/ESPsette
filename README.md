# espsette

Firmware ESP-01 do odtwarzania sygnału kasetowego przez port datasette C64. Projekt jest w trakcie migracji ze starego szkicu Arduino; [analiza i plan](ANALIZA_PROJEKTU.md) opisują kolejne etapy.

## Stan kodu

- `src/main.ino` zawiera historyczny odtwarzacz TAP z nałożoną poprawką tempa I2S, obsługi MOTOR i kończenia transmisji.
- `include/draxcaught.h` to tymczasowy, osadzony obraz TAP. Docelowo plik będzie dostarczany przez przeglądarkę.
- W kodzie nie ma domyślnych poświadczeń Wi-Fi. Portal konfiguracji i zapis w LittleFS są kolejnym etapem. Obecne `WiFi.begin()` z pustymi danymi nie połączy urządzenia z siecią, więc OTA i RemoteDebug nie są jeszcze osiągalne.
- Połączenia WRITE, dekoder komend, strumieniowanie z przeglądarki i strona kasety są zaplanowane, ale niezaimplementowane.
- Ta reorganizacja nie była kompilowana ani sprawdzana na ESP-01. Pierwszą próbę należy wykonać na Macu z właściwym modułem i adapterem.

## Układ katalogów

| Ścieżka | Znaczenie |
| --- | --- |
| `src/main.ino` | firmware |
| `include/` | nagłówki i tymczasowy obraz TAP |
| `scripts/` | lokalny bootstrap i uruchamianie PlatformIO |
| `platformio.ini` | przypięta platforma ESP8266, profile flashowania |
| `tools/` | ignorowane przez Git: `uv`, Python, PlatformIO, pakiety i toolchainy |
| `.pio/` | ignorowane wyniki budowania |

## Mac z USB-C: pierwszy flash

Potrzebny jest programator USB-UART 3,3 V z gniazdem ESP-01 oraz przewód lub przejściówka USB-C. Powinien zapewniać stabilne zasilanie 3,3 V i przełączenie GPIO0 w tryb programowania. Programowanie szeregowe wykonujemy z modułem odłączonym od obwodu C64. Dla adaptera bez automatycznego resetu trzeba ręcznie ustawić tryb FLASH, zresetować ESP i po wgraniu wrócić do RUN.

```sh
make setup
ls /dev/cu.*
make flash-id PORT=/dev/cu.usbserial-XXXX
make build PROFILE=1m
make flash PROFILE=1m PORT=/dev/cu.usbserial-XXXX
```

`make setup` pobiera `uv` i lokalnego Pythona 3.12 do `tools/`, a następnie instaluje w `tools/venv` przypięte PlatformIO i esptool. Pakiety platformy, biblioteki i toolchainy PlatformIO trafią do `tools/pio-core` przy pierwszym `make build`. Nie dodaje poleceń do profilu powłoki ani nie wymaga globalnego PlatformIO. Potrzebny jest `curl`, `make`, dostęp do internetu przy pierwszej instalacji i ewentualnie sterownik użytego adaptera USB-UART. Katalog `tools/` należy tworzyć osobno na Macu; nie przenosić do niego plików binarnych z WSL.

Profil `PROFILE=512k` jest przewidziany tylko do flashowania kablem i może nie pomieścić obecnej binarki. `PROFILE=4m` używa ogólnego profilu pamięci ESP8266; fizyczny moduł i połączenia pinów pozostają ESP-01. Faktyczną pojemność ustalać przez `make flash-id`. Parametry flash i zgodność API rdzenia/I2S trzeba potwierdzić pierwszą kompilacją oraz próbą na sprzęcie.

## OTA po uruchomieniu sieci

Po zaimplementowaniu portalu konfiguracji, połączenia STA i skutecznej obsługi `ArduinoOTA` można użyć:

```sh
make ota PROFILE=1m OTA_HOST=192.168.1.123
```

Cel OTA buduje ten sam kod co cel szeregowy. Wymaga miejsca na drugi obraz firmware, dlatego moduły 512 KB pozostają przy kablu. Mechanizm aktualizacji musi odrzucać próbę podczas aktywnego MOTOR lub sesji kasety. Adapter USB-UART zachować do odzyskania urządzenia po błędzie sieci lub nieudanej aktualizacji. Obecny kod nie ma jeszcze działającej konfiguracji Wi-Fi ani uwierzytelnienia OTA; celu `make ota` na tym etapie nie używać.

## Historia i archiwum

Oryginalny projekt znajduje się obok, w `../esptapuino/`, z nienaruszoną historią. **To archiwum zawiera dawne poświadczenia Wi-Fi i nie powinno być publikowane.** Historia `espsette` została przepisana tak, by usunąć definicje poświadczeń ze wszystkich czterech dawnych commitów przy zachowaniu dat autorów i committerów. Ujawnione hasło sieci należy zmienić. Nowe poświadczenia będą podawane wyłącznie przez portal konfiguracji i zapisane lokalnie w urządzeniu.

Źródła dla procesu: [PlatformIO ESP8266](https://docs.platformio.org/en/latest/platforms/espressif8266.html), [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp8266/index.html), [uv: lokalna instalacja](https://docs.astral.sh/uv/reference/installer/), [Arduino ESP8266 OTA](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html).
