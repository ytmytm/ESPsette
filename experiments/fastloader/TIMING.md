# Składanie par bitów i budżet zoptymalizowanego odbiornika

Punkt odniesienia: commit `cc15d15` na `feature/prg-fastloader-sim`.
Odbiornik działa pod `$0800`, z wygaszonym ekranem, wyłączonym sprite DMA
na C64 i bez wykonywania IRQ/NMI. Przepustowości są wynikami VICE, nie pomiarem
sprzętu. [Wyniki](RESULTS.md) zawierają metryki oraz hashe bieżących źródeł.

## Najpierw test arytmetyki w Pythonie

`search-bit-layout.py` porównuje siedem rodzin dekoderów. Dla każdej sprawdza
40 320 permutacji ośmiu bitów na kolejnych pozycjach WRITE/SENSE, następnie
wszystkie 256 wartości bajtu i zakłócenia niezwiązanych bitów portów.
To łącznie 2 361 088 sprawdzeń bajtu/pinów, bez uruchamiania VICE.
Weryfikacja obejmuje zero i wektory bazowe, a potem pełne wartości bajtów.
Test OR z ROL/ROR dodatkowo przeszukuje przesunięcia dziewięciobitowego A/C
między wstawieniami kolejnych par; nie wolno zgubić bitu w carry.

| Rodzina | Koszt samego składania bajtu, bez oczekiwania |
| --- | ---: |
| C64: osobne bity, PHA/PLA | 152 cykle |
| C64: osobne bity, TAX/TXA, bez pętli | 140 cykli |
| **C64: maska/OR, dwa półbajty** | **50 cykli** |
| C64: EOR, z zachowaniem A pomiędzy parami | 40 cykli, dodatkowy warunek opisany niżej |
| C64: OR i rotacje przez carry | 56 cykli |
| **C16: maska/OR, dwa półbajty** | **94 cykle** |
| C16: OR i rotacje przez carry | 102 cykle |

To porównanie wymienionych rodzin, nie dowód globalnego minimum wszystkich
możliwych programów 6502. Wybrano maskę/OR: obie maszyny mają tę samą kolejność
na kablu, dekoder nie zależy od wartości pozostałych bitów CPU portu i nie
wymaga zachowania A podczas oczekiwania na następne dane.

Wariant EOR oszczędza operacje danych, ale zakłada stałe bity 1/2 portu C64
oraz zachowanie A podczas oczekiwania. Polling bezpośredniego FLAG wymaga
załadowania maski do A. Podsumowanie IRQ w bicie 7 ICR ma osobną ścieżkę
opóźnienia w CIA, więc nie zastępujemy nim bezpośredniego FLAG w bicie 4.
W wybranym dekoderze maska/OR najdłuższa ścieżka pary to 20 cykli i nie wymaga
zmiany kierunku linii ani włączania maski IRQ FLAG.

## Nowa kolejność na kablu

| Strobe | WRITE | SENSE | Wynik cząstkowy |
| --- | --- | --- | --- |
| 0 | b4 | b5 | dwa młodsze bity górnego półbajtu |
| 1 | b6 | b7 | cały górny półbajt → X |
| 2 | b0 | b1 | dwa młodsze bity dolnego półbajtu |
| 3 | b2 | b3 | dolny półbajt OR tablica górnego |

Na C64 maska `$18` pobiera oba bity jednocześnie. Pierwsza para wymaga dwóch
LSR, druga jednego LSR. Na cały bajt przypada sześć LSR, zamiast 36 LSR i ośmiu
ROL wykonywanych poprzednio. Odczyt SENSE na C16 nadal jest osobną operacją:
WRITE z `$01` i SENSE z `$FD10` tworzą parę w bitach 1/2. Pierwsza para
półbajtu wymaga jednego LSR, druga jednego ASL: cztery przesunięcia na bajt.

Tablica 16 B przesuwa górny półbajt w gotowe miejsce. Jej wyrównanie do 16 B
zapewnia brak przekroczenia strony dla `ORA upper_table,X`, gdzie X jest 0..15.

Obie procedury rozwinięto do czterech próbek. Y jest zachowany. C64 używa
`LDA #$10 / BIT $DC0D / BEQ`, C16 `LDA #$10 / BIT $01` z naprzemiennym
BNE/BEQ dla low/high. Przerwania CIA pozostają zamaskowane; `BIT` na C64
czyta i potwierdza rzeczywisty bit 4 FLAG.

## Pętla zapisu: Y jest również licznikiem bloku

Odbiór jest wstawiony inline, osobno w pętli deskryptora i danych. Nie ma
JSR/RTS na bajt. Rdzeń pętli danych po złożeniu A to:

```asm
    sta (dest),y
    iny
    bne load
    inc dest+1
    jmp next_block
```

Długość 16-bitowa jest pomniejszana o 256 raz na pełny blok. Nie ma już DEC
licznika bloku ani aktualizacji pozostałej długości przy każdym bajcie.

Dla ostatniego bloku długości `n < 256` ustawiamy:

```text
Y = 256 - n
base = destination + n - 256
```

Pierwszy zapis trafia w `base + Y = destination`. Kolejne adresy rosną po
jednym; Y przewija się po dokładnie n zapisach, a zwiększenie starszego bajtu
bazy daje adres końca danych. Działa to również dla niezerowego młodszego
bajtu adresu i długości 1. W trybie porównawczym `--block-bytes 1` stosujemy
tę samą korektę dla każdego pojedynczego bajtu.

`test-indexing.py` sprawdza 9202 kombinacje adresu/długości/wielkości bloku
wobec liniowego orakla adresów, w tym wszystkie młodsze bajty adresu,
granice 255/256/257 i 511/512/513, 8192/12288/28672 B oraz zawinięcie 16 bitów.
W VICE osobno porównujemy całą zapisaną pamięć, do końca obsługiwanego RAM.

## Cykle rzeczywistych instrukcji

`analyze-timing.py` przechodzi po złożonym PRG: liczy tryby adresowania,
gałęzie i przekroczenia stron. Każdy odcinek unrolled i jego pętla mieści się
w oknie 128 B. Obecne gałęzie nie przekraczają stron; tablica również ich nie
przekracza. Po zmianie kodu lub relokacji trzeba ponownie uruchomić analizę.

| Ścieżka | C64 przed → po | C16 przed → po |
| --- | ---: | ---: |
| Pętla oczekiwania bez strobe | 9 → 7 | 11 → 6 |
| Para → następna para | 51 → **20/20/20** | 50 → **29/31/29** |
| Czwarta para → pierwszy strobe kolejnego bajtu deskryptora | 95 → **34** | 94 → **45** |
| Czwarta para → pierwszy strobe kolejnego bajtu danych | do 125 → **33** | do 124 → **44** |
| Najpóźniejsza próbka danych po strobe, z fazą pollingu | 16 → **12** | 34 → **20** |

Zawinięcie Y zachodzi na końcu bloku. Wtedy nadajnik czeka na MOTOR, więc
zwiększanie `dest+1` i wybór następnego bloku nie wyznaczają stałego odstępu.
Budżet między bajtami musi jednak objąć również deskryptor: 34 cykle na C64,
45 na C16, choć zwykła ścieżka danych jest o cykl krótsza.

W blank-screen fast mode TED kradnie pięć ticków na 114-tickową linię
(`plus4/ted.c:ted_delay_clk()`). Doliczamy `5 × ceil(C / 109)` do ścieżki
C cykli. Daje to następujące maksima:

| Budżet | C64 | C16/Plus/4 z refresh |
| --- | ---: | ---: |
| Para → para | **20** | **36** ticków |
| Między bajtami, dane i deskryptor | **34** | **50** ticków |
| Najpóźniejsza próbka danych | **12** | **25** ticków |

## Zegary i wybrane odstępy

| Maszyna | PAL | NTSC |
| --- | ---: | ---: |
| C64 | 985 248 Hz | 1 022 730 Hz |
| C16/Plus/4, szybki zegar | 1 773 447 Hz | 1 789 772 Hz |

Stałe mikrosekundy dobieramy do wolniejszego PAL. Szybszy NTSC ma zapas.
Źródłem zegarów są `c64.h` i `plus4.h` przypiętej rewizji VICE.

Setup i gap wynoszą po 3 µs. Dla `H = hold`, `B = byte_pause` strobe→strobe
wewnątrz bajtu to `H+6`, a od czwartej pary do pierwszej następnego bajtu
`H+B+3`. Czas bajtu to `3×(H+6)+H+B+3`, przed handshake MOTOR.

| Parametr, µs | C64 przed → po | C16 przed → po |
| --- | ---: | ---: |
| Hold | 48 → **16** | 26 → **16** |
| Byte_pause po hold ostatniej pary | 80 → **19** | 48 → **11** |
| Para → para | 54 → **22** | 32 → **22** |
| Czwarta para → pierwsza kolejnego bajtu | 131 → **38** | 77 → **30** |
| Cały bajt bez handshake | 293 → **104** | 173 → **96** |

Marginesy PAL bez zaokrągleń do ticków VICE:

| Budżet | C64 | C16 |
| --- | ---: | ---: |
| Para → para | 21,675 − 20 = **1,675 cyklu** | 39,016 − 36 = **3,016 ticka** |
| Między bajtami | 37,439 − 34 = **3,439 cyklu** | 53,203 − 50 = **3,203 ticka** |
| Dane stabilne do zmiany następnej pary | 18,720 − 12 = **6,720 cyklu** | 33,695 − 25 = **8,695 ticka** |

Analizator sprawdza czas ciągły oraz model VICE, który zaokrągla każdy odstęp
w górę. Nie polegamy na tym zaokrągleniu przy wyborze profilu. Setup nie jest
pomiarem elektrycznym. ESP może się spóźnić, ale nie może nadrabiać opóźnień
przez skracanie kolejnych odstępów.

## Weryfikacja i zakres

```sh
make fastloader-bit-test
make fastloader-test
python3 experiments/fastloader/analyze-timing.py
python3 experiments/fastloader/run-timing-matrix.py
python3 experiments/fastloader/run-matrix.py
```

Przeszło **672/672 prób timingu i 39/39 regresji**. Próby obejmują PAL/NTSC,
C64 z CIA 6526 i 8521, C16 i Plus/4, fazy początkowe READ, wzory danych,
jitter 0/1/50 µs, zerową zwłokę MOTOR i pełne zakresy danych testowych:
28672 B pod `$1000` na C64/Plus/4 i 12288 B pod `$1000` na C16.

Kod jest większy wskutek rozwinięcia pętli i wyrównania: 368 B na C64 i 528 B
na C16, wraz z paddingiem i tablicą. To nadal odbiornik eksperymentalny
wgrywany monitorem. Standardowy bootstrap wymaga osobnego małego stubu lub
relokacji; nie deklarujemy zmieszczenia całego odbiornika w nagłówku kasety.
