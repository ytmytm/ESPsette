# Synchroniczny fastloader PRG — eksperyment

Protokół działa w symulacji z rzeczywistymi odbiornikami 6502 w VICE.
MOTOR żąda teraz bloków po **256 bajtów**, a WRITE i SENSE pozostają wejściami
danych komputera przez cały transfer. READ taktuje kolejne pary bitów.
Sześciobajtowy deskryptor jest osobnym blokiem; ostatni blok danych może być krótszy.
Wyniki i zakres weryfikacji są w [RESULTS.md](RESULTS.md).

To wydzielony eksperyment transportu. Firmware ESPsette i odtwarzacz WWW nie
mają jeszcze opcji fastloadera. Test wgrywa odbiornik przez monitor VICE;
standardowa transmisja bootstrapu, jego autostart oraz uruchomienie docelowego
programu w odtworzonym środowisku BASIC/KERNAL wymagają dalszej implementacji.

## Linie i kierunki

| Linia | Podczas szybkiego transferu | C64 | C16/Plus/4 |
| --- | --- | --- | --- |
| READ | ESP → komputer, strobe | Zdarzenie /FLAG w CIA1, `$DC0D` bit 4 | Poziom `$01` bit 4 |
| WRITE | ESP → komputer, starszy bit pary | `$01` bit 3 | `$01` bit 1 |
| SENSE | ESP → komputer, młodszy bit pary | `$01` bit 4 | `$FD10` bit 2 |
| MOTOR | Komputer → ESP, żądanie bloku | `$01` bit 5, odwrócony względem MOTOR | `$01` bit 3, odwrócony względem MOTOR |

WRITE musi być wejściem komputera przez cały szybki transfer. ESP zaczyna
sterować danymi dopiero po tym przełączeniu. Stan MOTOR zapisujemy z własnej
kopii rejestru wyjściowego, ponieważ odczyt `$01` zawiera już bity danych.
Nie wolno robić `INC/DEC/ASL $01` ani budować nowego wyjścia z odczytanych danych.

Na C64 READ nie jest dostępnym poziomem portu CPU. Odczyt `$DC0D` daje
zatrzaśnięte zdarzenie na /FLAG i kasuje źródła przerwań CIA1. Bit 4 działa
także z zamaskowanym przerwaniem FLAG. Dwa zbocza przed potwierdzeniem przez
odczyt mogą zlać się w jedno zdarzenie: to nie jest kolejka impulsów.

Na C16 para wymaga osobnych odczytów WRITE i SENSE. Ich wartości muszą być
stabilne do zakończenia obu odczytów. C16 nie może stosować maski `$18` z C64.

## Przebieg

1. Odbiornik wykonuje `SEI`, wygasza ekran i przełącza WRITE na wejście.
   C64 dodatkowo wyłącza sprite DMA i maskuje CIA1/CIA2. Samo `SEI` nie
   zatrzymuje NMI, a samo wygaszenie ekranu nie wyłącza sprite DMA.
   Początkowy stan READ jest wysoki, a MOTOR włączony.
2. Pierwsza **zmiana** MOTOR żąda sześciobajtowego deskryptora. Każda następna
   żąda do 256 bajtów danych. Nowy poziom MOTOR pozostaje aż do odebrania całego
   bloku; nie jest to krótki impuls off/on. Odbiornik zna długość pliku, więc
   liczy bajty i rozpoznaje ostatni niepełny blok bez znaczników na liniach danych.
3. Nadajnik czeka na wykrycie zmiany MOTOR i dostępność danych. Pomiędzy
   blokami może uzupełniać bufor i obsługiwać sieć/watchdog. Zwłoka nadajnika
   wewnątrz bloku również jest dopuszczalna przy zachowaniu stabilnych linii.
4. Nadaje pary `b4:b5`, `b6:b7`, `b0:b1`, `b2:b3`: WRITE jest pierwszym,
   SENSE drugim bitem. Najpierw ustawia dane, następnie zmienia READ.
5. Na C64 każda para ma osobne opadające zbocze READ; pomiędzy nimi READ
   wraca wysoko. Na C16 kolejne pary mają poziomy READ low/high/low/high.
   Odbiornik czeka na oczekiwany poziom, po czym go zmienia dla następnej pary.
6. Po czwartej parze odbiornik zapisuje bajt. Nadajnik zachowuje dodatkowy
   odstęp przed kolejnym bajtem, aby CPU zdążył zapisać dane, zaktualizować
   adres/liczniki i wrócić do oczekiwania na READ. Po końcu bloku nadajnik
   zatrzymuje się do następnej zmiany MOTOR. Odbiornik żąda następnego bloku
   dopiero po odebraniu całego poprzedniego; po ostatnim bloku kończy odbiór.
   Żądanie zgłoszone podczas hold ostatniej pary pozostaje widoczne.

```text
MOTOR:    on ----- off (blok 0) -------------------- on (blok 1) --------
dane:                   b4b5      b6b7      b0b1      b2b3
READ C64:  HIGH ----------\__/------\__/------\__/------\__/-------------
READ C16:  HIGH ----------\_________/---------\_________/---------------
                          próbka    próbka    próbka    próbka
```

To synchronizacja bloków z taktowaniem par przez nadajnik. Nie ma osobnego ACK
dla każdej pary, więc pozostaje **minimalny czas utrzymania danych**. ESP może
wydłużyć odstęp, ale nie może skrócić go poniżej najwolniejszej ścieżki odbioru,
łącznie z fazą pętli oczekiwania, złożeniem bajtu i opóźnieniami magistrali.
W szczególności przedwcześnie zmienione dane mogą zepsuć próbkę nawet wtedy,
gdy CIA zdążyła poprawnie zatrzasnąć zbocze.

[Analiza cykli i test kolejności bitów](TIMING.md) doprowadziły do odbioru
półbajtami, rozwinięcia czterech próbek i wstawienia odbioru inline. Y indeksuje
dane i kończy blok po zawinięciu; długość 16-bitowa jest aktualizowana raz na
blok. Nie ma per-byte JSR/RTS ani osobnego DEC licznika.

Setup/gap pozostają po 3 µs, hold wynosi 16 µs na obu maszynach, a dodatkowa
pauza po hold czwartej pary 19 µs na C64 / 11 µs na C16. Odstęp między parami
wynosi 22 µs, a czwarta para→pierwsza kolejnego bajtu 38/30 µs.
Budżet obejmuje dane i deskryptor oraz refresh TED. Liczymy z wolniejszego PAL;
NTSC ma zapas. To nowa kolejność par, zgodna z aktualnym nadajnikiem C.

Odbiornik rezerwuje `$F7..$FF` i tablicę 16 B; Y pozostaje nietknięty podczas
składania bajtu. Niezależne testy Pythona sprawdzają permutacje/operacje bitowe
oraz korektę adresu ostatniego bloku. W VICE przeszły 672 próby timingu i 39
regresji. To budżety dla tego kodu, nie pomiar elektryczny. ESP nie może
nadrabiać opóźnień przez skracanie kolejnych odstępów.

## Narzut MOTOR przy blokach 256 B

[Tapecart Programmer's Reference](https://github.com/ikorb/tapecart/blob/master/doc/ProgRef.md#streaming-mode)
opisuje wyłączanie MOTOR trwające około 1 ms na niektórych płytach C64.
Włączenie jest znacznie szybsze. Dochodzi tor detekcji naszego urządzenia,
w tym 4N35. Nie przenosimy wartości 1 ms na C16 jako ustalonego faktu:
w teście to konfigurowalny scenariusz opóźnienia, wymagający pomiaru.

Przy zmianie stanu raz na blok średni narzut na bajt wynosi
`(t_off + t_on) / (2 × 256)`. Dla 1 ms / 5 µs to około **1,96 µs/bajt**.
Powolne zbocze przestaje być głównym ograniczeniem. Przy hipotetycznym samym
transporcie 9500 B/s odpowiada to około 2% narzutu na długim strumieniu.
Prędkości z obecnego odbiornika są zmierzone w [RESULTS.md](RESULTS.md);
nie są deklaracją osiągnięcia timingu oryginalnego Tapecart.

Pierwsza wersja z synchronizacją co bajt osiągała około 1165/1394 B/s:
[wyniki wcześniejszej próby](RESULTS-byte-sync.md). Przełącznik
`--block-bytes 1` pozwala porównać obie częstotliwości synchronizacji z tym
samym, aktualnym kodem i tymi samymi odstępami między bajtami.
WRITE/SENSE nie zmieniają kierunku przy synchronizacji bloków.
Opisany przez użytkownika dwubitowy punkt odniesienia odpowiada
[Tapecart](https://github.com/ikorb/tapecart), którego protokół fastload używa
WRITE/SENSE i **nie** taktuje par linią READ. Nie skopiowano jego loadera.

## Uruchomienie

Test nadajnika wymaga C11; test kolejności bitów i adresowania wymaga tylko Pythona:

```sh
make fastloader-test
make fastloader-bit-test
```

Test integracyjny wymaga ACME, Python 3, narzędzi do budowy VICE
(m.in. autoconf/automake, flex, bison, kompilatory C/C++) i lokalnych ROM-ów.
Budujemy osobną kopię źródeł z repozytorium oficjalnego VICE; robocze zmiany
w podanym repozytorium nie są kopiowane ani modyfikowane:

```sh
python3 experiments/fastloader/build-vice.py --repository /path/to/VICE-git-mirror
python3 experiments/fastloader/analyze-timing.py
python3 experiments/fastloader/run-matrix.py --roms /usr/local/share/vice
python3 experiments/fastloader/run-timing-matrix.py --roms /usr/local/share/vice
```

Przypięta rewizja VICE: `eaf9b61f5560f921ebaa25349b09e797f351eb1e`.
Zmiany emulatora są wydzielone jako [osobny patch integracyjny](vice-patches/README.md),
nakładany przez `patch-vice.py`. Finalizacja patcha i commit do repozytorium
pozostają na koniec developmentu.
Podane repozytorium musi zawierać tę rewizję i podkatalog `vice/`.
Domyślne źródła, binaria, logi i wyniki trafiają do ignorowanego
`tools/fastloader-vice/`. `--root`, `--build` i `--out` pozwalają zmienić
lokalizacje. Wyniki macierzy zawierają liczbę bajtów, zegar, czas symulowany,
SHA-256 danych i wynik porównania pamięci bajt po bajcie. `--trace` pojedynczego
testu zapisuje również zdarzenia linii w CSV.

```sh
python3 experiments/fastloader/run-vice.py --machine c64 --trace
python3 experiments/fastloader/run-vice.py --machine c16 --ntsc
python3 experiments/fastloader/run-vice.py --block-bytes 1  # porównanie z sync co bajt
python3 experiments/fastloader/run-vice.py --motor-off-us 5000 --jitter-us 50
python3 experiments/fastloader/run-vice.py --machine plus4 --prg /path/to/program.prg
```

W eksperymencie odbiornik zajmuje `$0800`, odbiornik rezerwuje `$F7..$FF`,
a testowane dane muszą mieścić się w `$1000..$7FFF` lub `$1000..$3FFF` na C16.
Deskryptor to trzy słowa little endian: adres ładowania, niezerowa długość,
adres wykonania. Deskryptor zajmuje własny blok 6 B. Potem idą bloki danych
bez dwubajtowego adresu z pliku PRG: do 256 B na żądanie. Dla 4096 B danych
występuje dokładnie 17 zmian MOTOR: jedna dla deskryptora i 16 dla danych.
Test kończy się przed skokiem do programu: potwierdza pamięć i dojście do
końca odbiornika, nie poprawność procedury `RUN` ani działania programu.

## Granica między modelem a sprzętem

`sender.c` nie widzi PC ani rejestrów komputera. Jego wejściem jest MOTOR,
wyjściami fizyczne poziomy trzech linii i termin następnego zdarzenia.
`vice-device.c` wykonuje zdarzenia przez alarmy VICE i zwykłe API tape-port.
Nie podmienia instrukcji 6502 ani wartości odczytywanych przez loader.
Monitor ustawia urządzenie testowe i wgrywa kod przed rozpoczęciem próby.

Adapter uwzględnia konwencje API VICE: SENSE i C64 WRITE oznaczają wymuszenie
zera; C16 WRITE oznacza poziom wejścia; C64 `read_in(1)` oznacza zdarzenie FLAG,
nie fizyczny poziom wysoki. Patch poprawia również istniejący model SENSE
w `plus4pio1.c`: dla C16 bez portu użytkownika zwolniony bit 2 musi być wysoki.
To oddzielna poprawka modelu wejścia, nie obejście dekodera danych.

Symulacja nie dowodzi czasów narastania GPIO, odporności elektrycznej ani
timingu ESP8266 z Wi-Fi. Opóźnienie MOTOR to model opóźnienia detekcji stanu,
nie symulacja analogowego RC/transoptora. Przed użyciem sprzętowym potrzebne są:

- Standardowy bootstrap i autostart osobno dla C64 i C16, wybór trybu oraz
  uzgodnienie początkowego poziomu MOTOR po przejściu z KERNAL-a.
- Obsługa mapy pamięci większych PRG i ochrony obszaru loadera, checksum/CRC,
  błędnego nagłówka, timeoutów i przerwania transmisji.
- Odtworzenie ekranu, portów i środowiska BASIC/KERNAL oraz jawna decyzja
  RUN albo adres wykonania. Adres ładowania PRG nie określa adresu startu.
- Nadajnik ESP w IRAM, przełączenie I2S→GPIO i sterowanie WRITE dopiero po
  zwolnieniu linii przez komputer. Przerwy na sieć/watchdog można robić między
  blokami oraz przy stabilnym READ/danych. Nie wyłączać przerwań na cały
  blok 256 B. Nie nadrabiać opóźnionych zdarzeń przez skracanie setup/hold
  ani odstępów między bajtami; opóźnienie może tylko wydłużać transmisję.
- Pomiar setup/hold oraz obu zboczy MOTOR dla konkretnych C64/C16 i naszego
  układu, następnie próby przy aktywnym Wi-Fi i zanikach danych z przeglądarki.
