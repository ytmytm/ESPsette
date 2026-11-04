# Wyniki odbioru półbajtami i pętli Y — 2026-11-04

VICE `eaf9b61f5560f921ebaa25349b09e797f351eb1e`, headless `x64sc` / `xplus4`, ACME i przenośny nadajnik C.
Osobny deskryptor 6 B, potem bloki danych po maksymalnie 256 B. Y jest indeksem i licznikiem bloku; ostatni blok używa ujemnego początkowego Y i skorygowanej bazy.
WRITE/SENSE pozostają wejściami komputera przez cały transfer. READ taktuje pary bitów.

## Przepustowość

Ładunek 4096 B, setup/gap 3 µs, hold 16 µs, dodatkowa przerwa po hold czwartej pary 19 µs (C64) / 11 µs (TED).
Czas od pierwszego żądania MOTOR do strobe ostatniej pary. Bez standardowego bootstrapu.
W próbach z blokami 256 B wystąpiło dokładnie **17 żądań MOTOR**: deskryptor i 16 bloków danych.

| Wariant | Komputer | MOTOR OFF / ON | B/s | Żądania |
| --- | --- | --- | ---: | ---: |
| sync co 256 B | C64 PAL | 1000 / 5 µs | 9248.7 | 17 |
| sync co 256 B | PLUS4 PAL | 1000 / 5 µs | 9723.3 | 17 |
| sync co 256 B | C16 PAL | 1000 / 5 µs | 9723.3 | 17 |
| sync co 256 B | C64 NTSC | 1000 / 5 µs | 8621.3 | 17 |
| sync co 256 B | PLUS4 NTSC | 1000 / 5 µs | 9810.8 | 17 |
| sync co 256 B | C16 NTSC | 1000 / 5 µs | 9810.8 | 17 |
| sync co 256 B | C64 PAL | 0 / 0 µs | 9442.1 | 17 |
| sync co 256 B | PLUS4 PAL | 0 / 0 µs | 9936.3 | 17 |
| sync co 256 B | C16 PAL | 0 / 0 µs | 9936.3 | 17 |
| sync co bajt | C64 PAL | 1000 / 5 µs | 1425.0 | 4097 |
| sync co bajt | PLUS4 PAL | 1000 / 5 µs | 1516.8 | 4097 |
| sync co bajt | C16 PAL | 1000 / 5 µs | 1516.8 | 4097 |

W porównaniu co bajt deskryptor nadal jest osobnym blokiem 6 B; payload 4096 B wymaga 4096 żądań. Oba warianty używają tego samego kodu i odstępów czasowych.
Założone opóźnienie MOTOR na TED jest scenariuszem testowym, nie pomiarem sprzętu.
Zerowe opóźnienie MOTOR poprawia wynik blokowy tylko nieznacznie: przy obecnym timingu ograniczają nas już odbiornik i odstępy nadajnika.

## Porównanie z commitem bazowym `cc15d15`

| Komputer PAL | Przed optymalizacją | Teraz | Wzrost |
| --- | ---: | ---: | ---: |
| C64 | 3340,6 B/s | 9248,7 B/s | 2,77× |
| C16/Plus/4 | 5529,5 B/s | 9723,3 B/s | 1,76× |

W obu pomiarach 4096 B i MOTOR 1000/5 µs. Przestawiono pary na
b4/b5, b6/b7, b0/b1, b2/b3; maska/OR składa je półbajtami. Odbiór jest inline,
a Y kończy pełne i częściowe bloki. Dokładny rachunek jest w [TIMING.md](TIMING.md).

Każdy odstęp w modelu VICE jest zaokrąglany w górę do ticka CPU. Z tego powodu
C64 NTSC może w tabeli osiągać mniej niż PAL pomimo szybszego CPU: np. 3 µs
setup to w modelu 4 ticki zamiast 3. Budżety czasu ciągłego sprawdzono osobno;
przyszły nadajnik ESP nie ma polegać na tym zaokrągleniu. Tabela nie określa
maksymalnej przepustowości sprzętu.

## Macierz regresji

| Przypadek | Maszyny | Wynik |
| --- | --- | --- |
| 4096 B, MOTOR 1000/5 µs | C64, Plus/4, C16; PAL i NTSC | 6 × poprawny odbiór |
| 4096 B, MOTOR 0/0 µs | C64, Plus/4, C16 PAL | 3 × poprawny odbiór |
| Porównanie sync co bajt, 4096 B | C64, Plus/4, C16 PAL | 3 × poprawny odbiór |
| 513 B od $20F9, przerwa 50 ms/bajt, jitter do 50 µs | C64, Plus/4, C16 PAL | 3 × poprawny odbiór |
| 513 B, MOTOR 5000/200 µs | C64, Plus/4, C16 PAL | 3 × poprawny odbiór |
| 1 B pod $3FFF | C64, Plus/4, C16 PAL | 3 × poprawny odbiór |
| 255, 256, 257, 512, 513 B od $20F9 | C64, Plus/4, C16 PAL | 15 × poprawny odbiór |
| Zbyt krótki hold 1 µs | C64 i C16 PAL | 2 × oczekiwany brak odbioru |
| Brak dodatkowej przerwy między bajtami, 257 B | C64 PAL | 1 × oczekiwany brak odbioru |

**39/39 sprawdzeń zgodnych z oczekiwaniem**. Weryfikowano całą pamięć bajt po bajcie, dojście do końca odbiornika i dokładną liczbę żądań MOTOR.
Negatywne próby uruchomiły nadajnik i zakończyły się limitem cykli emulacji; nie były błędami uruchomienia ani timeoutami hosta.
Test C11 z AddressSanitizer/UndefinedBehaviorSanitizer również przeszedł: oba warianty READ, długości 1/255/256/257/512/513 B, wszystkie wartości bajtu, sync 1/256, długi zastój i żądanie kolejnego bloku podczas hold ostatniej pary.

Brak przerwy między bajtami to nowy, istotny negatywny przypadek: CPU potrzebuje czasu na zapis i aktualizację adresów, mimo że kolejne pary wewnątrz bajtu mają wystarczający hold.

## Testy poza VICE i dodatkowa macierz timingu

- Python: siedem rodzin dekoderów, 40 320 permutacji dla każdej,
  **2 361 088** sprawdzeń wszystkich bajtów i niezwiązanych bitów portów.
- Python: **9202** przypadki indeksowania i końcowego krótkiego bloku,
  w tym wszystkie młodsze bajty adresu oraz zawijanie 16-bitowych adresów.
- VICE: **672/672 prób** timingu. PAL/NTSC, C64 CIA 6526/8521, C16 i Plus/4,
  fazy READ, wzory danych, jitter 0/1/50 µs i MOTOR 0/0 µs.
- Dodatkowe długości: 255/256/257, 511/512/513, 8191/8193, maksymalnie 28672 B
  pod $1000 na C64/Plus/4 i 12288 B pod $1000 na C16.

Każda próba VICE porównuje całą pamięć i dokładną liczbę żądań MOTOR.
`tools/fastloader-vice/timing-matrix-unrolled/summary.json` zawiera wyniki.
`analyze-timing.py` liczy instrukcje rzeczywistego PRG, sprawdza wyrównanie
oraz marginesy czasu ciągłego i ticków VICE.

## Odtwarzalność

```sh
make fastloader-bit-test
make fastloader-test
python3 experiments/fastloader/analyze-timing.py
python3 experiments/fastloader/run-timing-matrix.py --roms /usr/local/share/vice --out tools/fastloader-vice/timing-matrix-unrolled
python3 experiments/fastloader/run-matrix.py --roms /usr/local/share/vice --out tools/fastloader-vice/matrix-unrolled
```

`tools/fastloader-vice/matrix-unrolled/summary.json` zawiera szczegółowe metryki. Podkatalogi mają log VICE, skrypt monitora, odbiornik, strumień i zrzut pamięci. Artefakty są ignorowane przez Git.
[Historyczne wyniki pierwszej wersji z sync co bajt](RESULTS-byte-sync.md) dotyczyły wcześniejszej pętli i innych odstępów.

SHA-256 źródeł użytych w próbie:

- `sender.c`: `be71febfae1d47adab4685a87c699c94f1d770f7d2700cad9c663e82beead8f1`
- `sender.h`: `a4f021c290dfa77f0971749a6ba454f53a8e4e22e8a6031cdfc5495b6a4a5a3c`
- `receiver.asm`: `9e1dfb8f5833256f599af0f0ce85523e705a6ee0b87c3179f299e9135b40256d`
- `vice-device.c`: `74ae0eb6768cccc32912e0019c5bcd87716726729867a7f0652f93f895636084`

SHA-256 ROM-ów komputera (ROM-y nie są dołączane do repozytorium):

- `C64/kernal-901227-03.bin`: `83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721`
- `C64/basic-901226-01.bin`: `89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d`
- `C64/chargen-901225-01.bin`: `fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420`
- `PLUS4/kernal-318004-05.bin`: `1f07270c43fc84d1556978e5bb3a6b08aa7b1253f2e46d1a452c89c972ae506b`
- `PLUS4/kernal-318005-05.bin`: `5c094751e8bbac217726e25e0864769aacb76f606f2ab807e10d420a5209aaf3`
- `PLUS4/basic-318006-01.bin`: `cbf0c4dec44e3e203beaf09690e5c7f859b0eca164e789884161c8cb0e596567`

Te testy potwierdzają transport na poziomie emulowanych linii i instrukcji. Nie potwierdzają bootstrapu, autostartu, końcowego RUN, timingu ESP8266 ani parametrów elektrycznych.
