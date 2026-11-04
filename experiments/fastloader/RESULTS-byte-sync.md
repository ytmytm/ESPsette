# Historyczne wyniki synchronizacji co bajt — 2026-11-04

VICE `eaf9b61f5560f921ebaa25349b09e797f351eb1e`, headless `x64sc` / `xplus4`.
Zbudowano czyste źródła z archiwum Git, z lokalnym adapterem tape-port i poprawką wejścia SENSE na C16.
Odbiorniki: ACME, kod 6502. Nadajnik: przenośny C, wykonywany przez alarmy VICE.
Wynik pozytywny wymaga dojścia do końca odbiornika i porównania całej pamięci z danymi wejściowymi.

## Przepustowość

Ładunek 4096 B, sześciobajtowy deskryptor, setup/gap 3 µs, hold 64 µs (C64) / 32 µs (TED), przerwa nadajnika 20 µs/bajt.
Czas od pierwszego żądania MOTOR do strobe ostatniej pary. Bez standardowego bootstrapu.

| Komputer | MOTOR OFF / ON | Wynik | B/s |
| --- | --- | --- | ---: |
| C64 PAL | 1000 / 5 µs | PASS | 1165.0 |
| PLUS4 PAL | 1000 / 5 µs | PASS | 1393.5 |
| C16 PAL | 1000 / 5 µs | PASS | 1393.5 |
| C64 NTSC | 1000 / 5 µs | PASS | 1159.9 |
| PLUS4 NTSC | 1000 / 5 µs | PASS | 1393.8 |
| C16 NTSC | 1000 / 5 µs | PASS | 1393.8 |
| C64 PAL | 0 / 0 µs | PASS | 2851.7 |
| PLUS4 PAL | 0 / 0 µs | PASS | 4677.5 |
| C16 PAL | 0 / 0 µs | PASS | 4677.5 |

Założone opóźnienie 1 ms na TED jest scenariuszem testowym, nie pomiarem C16/Plus/4.
Wariant 0/0 µs pokazuje wpływ usunięcia opóźnienia MOTOR przy tej samej implementacji odbiornika; nie określa maksymalnej prędkości komputera.

## Macierz regresji

| Przypadek | Maszyny | Oczekiwany i uzyskany wynik |
| --- | --- | --- |
| 4096 B, MOTOR 1000/5 µs | C64, Plus/4, C16; PAL i NTSC | 6 × poprawny odbiór |
| 4096 B, MOTOR 0/0 µs | C64, Plus/4, C16 PAL | 3 × poprawny odbiór |
| 513 B od $20F9, przerwa 50 ms/bajt, jitter do 50 µs | C64, Plus/4, C16 PAL | 3 × poprawny odbiór |
| 513 B, MOTOR 5000/200 µs | C64, Plus/4, C16 PAL | 3 × poprawny odbiór |
| 1 B pod $3FFF | C64, Plus/4, C16 PAL | 3 × poprawny odbiór |
| Zbyt krótki hold 1 µs | C64 i C16 PAL | 2 × brak poprawnego odbioru, limit cykli |

**20/20 sprawdzeń zgodnych z oczekiwaniem**. Negatywne próby uruchomiły nadajnik i zakończyły się limitem cykli emulacji; nie były błędami uruchomienia ani timeoutami hosta.
Test C11 z AddressSanitizer/UndefinedBehaviorSanitizer również przeszedł: wszystkie wartości bajtu, oba warianty READ, przerwa 10 milionów ticków i żądanie kolejnego bajtu w trakcie końcowego hold.

## Artefakty historycznej próby

Poniższe hashe dotyczą pierwszej wersji kodu. Obecna wersja domyślnie synchronizuje bloki 256 B; aktualne wyniki i komendy są w [RESULTS.md](RESULTS.md).

## Odtwarzalność

```sh
python3 experiments/fastloader/run-matrix.py --roms /usr/local/share/vice
```

`tools/fastloader-vice/matrix/summary.json` zawiera szczegółowe metryki; podkatalogi mają log VICE, skrypt monitora, odbiornik, strumień i zrzut pamięci. Te generowane artefakty są ignorowane przez Git.

SHA-256 źródeł użytych w próbie:

- `sender.c`: `8ba2506beb3bdadd6c21991b3e22343265805a94da83ec1362b08be3a2085edf`
- `sender.h`: `edc911426c9d0c243600ce6abf17c094e57faf54601728554c3c25276492094e`
- `receiver.asm`: `ca8017f547c3c7192497e6b5251b5c7c0198cdd0032a5086bc025b7759f21d87`
- `vice-device.c`: `80a7576354147f2a8068223a6df529e8c5d395c2ece3a660cd952b4ef7f49a1c`

SHA-256 ROM-ów komputera (ROM-y nie są dołączane do repozytorium):

- `C64/kernal-901227-03.bin`: `83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721`
- `C64/basic-901226-01.bin`: `89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d`
- `C64/chargen-901225-01.bin`: `fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420`
- `PLUS4/kernal-318004-05.bin`: `1f07270c43fc84d1556978e5bb3a6b08aa7b1253f2e46d1a452c89c972ae506b`
- `PLUS4/kernal-318005-05.bin`: `5c094751e8bbac217726e25e0864769aacb76f606f2ab807e10d420a5209aaf3`
- `PLUS4/basic-318006-01.bin`: `cbf0c4dec44e3e203beaf09690e5c7f859b0eca164e789884161c8cb0e596567`

Te testy potwierdzają transport na poziomie emulowanych linii i instrukcji. Nie potwierdzają bootstrapu, autostartu, końcowego RUN, timingu ESP8266 ani parametrów elektrycznych.
