# Budżet cykli i krótsze odstępy

Analiza dotyczy kodu z `receiver.asm` złożonego pod `$0800`, z wygaszonym
ekranem, wyłączonym sprite DMA na C64 i bez wykonywania IRQ/NMI. Liczby
pochodzą z przejścia po **złożonych instrukcjach**, nie z szacunku na podstawie
liczby linii asemblera. `analyze-timing.py` uwzględnia tryby adresowania,
obie ścieżki przeniesień i dodatkowy cykl gałęzi przekraczającej stronę.
Obecne pętle nie mają takiej gałęzi. Po relokacji loadera trzeba przeliczyć
timing ponownie.

## Zegary i PAL/NTSC

| Maszyna | PAL | NTSC |
| --- | ---: | ---: |
| C64 | 985 248 Hz | 1 022 730 Hz |
| C16/Plus/4, szybki zegar | 1 773 447 Hz | 1 789 772 Hz |

Źródła: definicje `c64.h` i `plus4.h` przypiętej rewizji VICE.
Przy nadajniku z odstępami w mikrosekundach **wolniejszy PAL wymaga więcej
czasu na tę samą liczbę instrukcji**. Budżet uniwersalnego profilu liczymy
z PAL. Szybszy NTSC ma wtedy dodatkowy zapas. Liczenie odstępów z szybszego
zegara NTSC bez powiększenia marginesu nie zapewnia zgodności z PAL.

## Ścieżki odbiornika

Przeniesiono `result` do `$FD`, `block_remaining` do `$FE` i C16
`expected_read` do `$FF`. Dzięki temu m.in. `ROL` kosztuje 5 zamiast 6 cykli,
a odczyty/zapisy robocze 3 zamiast 4. C16 jawnie zeruje początkowy oczekiwany
READ. Odbiornik rezerwuje teraz całość `$F7..$FF`.

| Ścieżka | C64 przed → po | C16 przed → po |
| --- | ---: | ---: |
| Jedna iteracja oczekiwania bez strobe | 9 → 9 | 12 → 11 |
| Odczyt strobe → odczyt strobe następnej pary | 53 → 51 | 55 → 50 |
| Czwarta para → pierwsza kolejnego bajtu deskryptora | 101 → 95 | 103 → 94 |
| Czwarta para → pierwsza kolejnego bajtu danych, bez przeniesień | 123 → 117 | 125 → 116 |
| To samo, przeniesienie adresu **albo** pożyczka długości | 127 → 121 | 129 → 120 |
| To samo, oba przeniesienia naraz | 131 → 125 | 133 → 124 |
| Najpóźniejsza próbka danych po strobe, z fazą pętli oczekiwania | 16 → 16 | 37 → 34 |

Ścieżka między bajtami zawiera złożenie ostatniej pary, `DEC` licznika bloku,
powrót z `getbyte`, zapis `(dest),Y`, aktualizację adresu i długości, wywołanie
kolejnego `getbyte`, sprawdzenie licznika bloku i przygotowanie odbioru.
Na granicy bloku nadajnik czeka na MOTOR, więc ten dłuższy handshake nie
wyznacza odstępu między bajtami wewnątrz bloku.

Przeniesienia mogą wystąpić jednocześnie: np. przy ładowaniu 513 B pod `$20FE`
drugi bajt trafia pod `$20FF`, gdy pozostała długość wynosi `$0200`.
Testowanie wyłącznie adresu `$2000` i długości 4096 B pomija tę kombinację.

## TED nadal potrzebuje czasu na refresh

W VICE `plus4/ted.c:ted_delay_clk()` dla fast mode bez character fetch
kradnie pięć ticków na linię, przy pozycjach 92/94/96/98/100. Linia ma 114
ticków. Wygaszenie ekranu usuwa pobieranie grafiki, ale nie ten refresh.

Do bezpiecznego oszacowania ścieżki długości `C` cykli CPU doliczamy
`5 × ceil(C / 109)` ticków. Otrzymujemy:

| Budżet po optymalizacji | C64 | C16/Plus/4 z refresh |
| --- | ---: | ---: |
| Para → para | 51 | **55** ticków |
| Czwarta para → pierwszy strobe kolejnego bajtu | 125 | **134** ticki |
| Najpóźniejsza próbka danych po strobe | 16 | **39** ticków |

Budżet pary dotyczy czasu obsługi, a budżet próbki — momentu odczytu pinów.
Samo utrzymywanie danych przez czas próbki nie wystarczy: jeśli kolejne pary
przychodzą szybciej, niż CPU je obsługuje, opóźnienie odbiornika narasta.

## Wybrane odstępy

Nadajnik ma `S = setup`, `H = hold`, `G = gap` i `B = byte_pause`.
Wewnątrz bajtu strobe→strobe wynosi `H + G + S`. Od czwartego strobe do
pierwszego strobe kolejnego bajtu wynosi `H + B + S`, nie samo `B`.
Łącznie na bajt przypada `3 × (H + G + S) + H + B + S`.

| Parametr, µs | C64 wcześniej → teraz | C16 wcześniej → teraz |
| --- | ---: | ---: |
| Setup | 3 → 3 | 3 → 3 |
| Hold | 64 → **48** | 32 → **26** |
| Gap | 3 → 3 | 3 → 3 |
| Dodatkowe byte_pause po hold ostatniej pary | 64 → **80** | 32 → **48** |
| Strobe→strobe między parami | 70 → **54** | 38 → **32** |
| Czwarty strobe→pierwszy kolejnego bajtu | 131 → **131** | 67 → **77** |
| Cały bajt bez handshake MOTOR | 341 → **293** | 181 → **173** |

Byte_pause rośnie celowo. Skracamy odstępy wewnątrz bajtu i zachowujemy
pełny budżet najdłuższego zapisu, zamiast polegać na zapasie z wcześniejszych
par, który zależy od fazy odbiornika.

Rzeczywiste mikrosekundy przeliczone na PAL, **bez** zaokrąglania przez VICE,
dają następujące marginesy:

| Budżet | C64 PAL | C16 PAL |
| --- | ---: | ---: |
| Para → para | 53,203 − 51 = **2,203 cyklu** | 56,750 − 55 = **1,750 ticka** |
| Między bajtami | 129,067 − 125 = **4,067 cyklu** | 136,555 − 134 = **2,555 ticka** |

W modelu VICE każdy pojedynczy odstęp jest zaokrąglany w górę do ticka CPU.
Analizator sprawdza zarówno ten model, jak i powyższy czas ciągły, żeby
przyszła implementacja ESP nie polegała na zapasie pochodzącym z zaokrągleń.
Pozostawione 3 µs setup nie jest wynikiem pomiaru elektrycznego.

## Sprawdzenie

```sh
python3 experiments/fastloader/analyze-timing.py
python3 experiments/fastloader/run-timing-matrix.py
python3 experiments/fastloader/run-matrix.py
```

Analizator sprawdza budżety na faktycznym PRG. Macierz timingu obejmuje:

- 18 opóźnień początkowych dla C64 oraz 114 dla C16 i Plus/4, osobno PAL/NTSC;
  513 B pod `$20FE` wymusza najdłuższe przeniesienia także wewnątrz bloku.
- 8192 B pod `$10FF`, pięć wzorów danych, jitter 0/1/50 µs i zerowe opóźnienie
  MOTOR, aby sprawdzić też najwcześniejszy możliwy początek kolejnego bloku.
- Porównanie całej pamięci i dokładnej liczby żądań MOTOR.

**582/582 prób timingu i 39/39 testów regresji** dało oczekiwany wynik.
Wcześniejsze próby z pierwotnym kodem wykazały błędy m.in. dla C64 hold/pause
46/64 µs oraz C16 26/40 µs; samo zmniejszanie obu wartości nie było bezpieczne.
Testy faz są dodatkową weryfikacją budżetu, nie dowodem analogowego timingu
sprzętu. Przepustowości i hashe bieżącego kodu są w [RESULTS.md](RESULTS.md).
