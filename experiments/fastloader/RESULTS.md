# Wyniki bloków 256 B po analizie timingu — 2026-11-04

VICE `eaf9b61f5560f921ebaa25349b09e797f351eb1e`, headless `x64sc` / `xplus4`, ACME i przenośny nadajnik C.
Odbiornik liczy bajty: osobny deskryptor 6 B, potem bloki danych po maksymalnie 256 B.
WRITE/SENSE pozostają wejściami komputera przez cały transfer. READ taktuje pary bitów.

## Przepustowość

Ładunek 4096 B, setup/gap 3 µs, hold 48 µs (C64) / 26 µs (TED), dodatkowa przerwa po hold czwartej pary 80/48 µs.
Czas od pierwszego żądania MOTOR do strobe ostatniej pary. Bez standardowego bootstrapu.
W próbach z blokami 256 B wystąpiło dokładnie **17 żądań MOTOR**: deskryptor i 16 bloków danych.

| Wariant | Komputer | MOTOR OFF / ON | B/s | Żądania |
| --- | --- | --- | ---: | ---: |
| sync co 256 B | C64 PAL | 1000 / 5 µs | 3340.6 | 17 |
| sync co 256 B | PLUS4 PAL | 1000 / 5 µs | 5529.5 | 17 |
| sync co 256 B | C16 PAL | 1000 / 5 µs | 5529.5 | 17 |
| sync co 256 B | C64 NTSC | 1000 / 5 µs | 3267.2 | 17 |
| sync co 256 B | PLUS4 NTSC | 1000 / 5 µs | 5579.5 | 17 |
| sync co 256 B | C16 NTSC | 1000 / 5 µs | 5579.5 | 17 |
| sync co 256 B | C64 PAL | 0 / 0 µs | 3365.6 | 17 |
| sync co 256 B | PLUS4 PAL | 0 / 0 µs | 5597.7 | 17 |
| sync co 256 B | C16 PAL | 0 / 0 µs | 5597.7 | 17 |
| sync co bajt | C64 PAL | 1000 / 5 µs | 1130.9 | 4097 |
| sync co bajt | PLUS4 PAL | 1000 / 5 µs | 1361.7 | 4097 |
| sync co bajt | C16 PAL | 1000 / 5 µs | 1361.7 | 4097 |

W porównaniu co bajt deskryptor nadal jest osobnym blokiem 6 B; payload 4096 B wymaga 4096 żądań. Oba warianty używają tego samego kodu i odstępów czasowych.
Założone opóźnienie MOTOR na TED jest scenariuszem testowym, nie pomiarem sprzętu.
Zerowe opóźnienie MOTOR poprawia wynik blokowy tylko nieznacznie: przy obecnym timingu ograniczają nas już odbiornik i odstępy nadajnika.

## Porównanie z wcześniejszym timingiem

| Komputer PAL | Wcześniej | Teraz | Zmiana |
| --- | ---: | ---: | ---: |
| C64 | 2864,5 B/s | 3340,6 B/s | +16,6% |
| C16/Plus/4 | 5345,9 B/s | 5529,5 B/s | +3,4% |

W obu pomiarach 4096 B i MOTOR 1000/5 µs. Odbiornik ma teraz robocze zmienne
w `$FD..$FF`, a odstępy wyznaczono z najdłuższych ścieżek instrukcji i refreshu
TED. Pełny rachunek jest w [TIMING.md](TIMING.md).

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

## Dodatkowa macierz timingu

**582/582 prób** przeszło dla C64/Plus/4/C16, PAL/NTSC: zmiana początkowej fazy
READ o 0..17 ticków na C64 i 0..113 na TED, 513 B pod `$20FE` (oba przeniesienia
jednocześnie), pięć wzorów danych po 8192 B pod `$10FF`, jitter 0/1/50 µs
oraz zerowe opóźnienie MOTOR. Każda próba porównuje pamięć i liczbę żądań.

`tools/fastloader-vice/timing-matrix/summary.json` zawiera wyniki tych prób.
`analyze-timing.py` dodatkowo sprawdza wyliczone cykle rzeczywistego PRG oraz
marginesy dla czasu ciągłego i dla ticków VICE. Nie polegamy na zaokrągleniach
w emulatorze przy wyborze odstępów w mikrosekundach.

## Odtwarzalność

```sh
make fastloader-test
python3 experiments/fastloader/analyze-timing.py
python3 experiments/fastloader/run-timing-matrix.py --roms /usr/local/share/vice
python3 experiments/fastloader/run-matrix.py --roms /usr/local/share/vice --out tools/fastloader-vice/matrix-timing
```

`tools/fastloader-vice/matrix-timing/summary.json` zawiera szczegółowe metryki. Podkatalogi mają log VICE, skrypt monitora, odbiornik, strumień i zrzut pamięci. Artefakty są ignorowane przez Git.
[Historyczne wyniki pierwszej wersji z sync co bajt](RESULTS-byte-sync.md) dotyczyły wcześniejszej pętli i innych odstępów.

SHA-256 źródeł użytych w próbie:

- `sender.c`: `63848a81f48ee5ad22ea78b3c0ed1f695eba2368da5f290cc9db20b8f392ab3d`
- `sender.h`: `a4f021c290dfa77f0971749a6ba454f53a8e4e22e8a6031cdfc5495b6a4a5a3c`
- `receiver.asm`: `d489c335fed21ef907c730c3cf1c12c8e24e8da672d2f6cd8c8b6a8909d4a054`
- `vice-device.c`: `379dabb700cc95c2b5b923c890d582c749c121a62fd24b6b5d53e1dbbb2cbf44`

SHA-256 ROM-ów komputera (ROM-y nie są dołączane do repozytorium):

- `C64/kernal-901227-03.bin`: `83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721`
- `C64/basic-901226-01.bin`: `89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d`
- `C64/chargen-901225-01.bin`: `fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420`
- `PLUS4/kernal-318004-05.bin`: `1f07270c43fc84d1556978e5bb3a6b08aa7b1253f2e46d1a452c89c972ae506b`
- `PLUS4/kernal-318005-05.bin`: `5c094751e8bbac217726e25e0864769aacb76f606f2ab807e10d420a5209aaf3`
- `PLUS4/basic-318006-01.bin`: `cbf0c4dec44e3e203beaf09690e5c7f859b0eca164e789884161c8cb0e596567`

Te testy potwierdzają transport na poziomie emulowanych linii i instrukcji. Nie potwierdzają bootstrapu, autostartu, końcowego RUN, timingu ESP8266 ani parametrów elektrycznych.
