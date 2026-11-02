# Plan połączeń KiCad (C64 datasette ↔ ESP-01)

Stan: 2026-11-02. Poniższe decyzje zastępują wcześniejszą propozycję zmiany
polaryzacji MOTOR i dodania inwertera READ. Autor potwierdził, że wariant
MOTOR oraz bezpośredni READ działały na rzeczywistym sprzęcie z obecnym
firmware podczas testów 2026-11-01. To zapis wyniku testów przekazanego przez
autora, bez nowego testu sprzętowego w tej sesji.

Założenie autora dotyczące bezpośrednich połączeń wejść ESP-01 z portem
kasety pozostaje świadomą decyzją projektu, nie deklaracją parametrów producenta.

## Docelowy graf

```text
C64 tape GND ─────────────────────────── wspólne GND
C64 tape +5V ──► [moduł lub AMS1117-3.3] ──► 3V3 ──► ESP-01 VCC
                                               └──► CH_PD / 4N35 pin5

C64 MOTOR ── 470Ω ──► 4N35 pin1 (anoda LED)
4N35 pin2 (katoda LED) ──► GND
4N35 pin3 (NC) ── NC
4N35 pin6 (baza) ── NC
4N35 pin5 (kolektor) ──► 3V3
4N35 pin4 (emiter) = MOTOR_3V3 ──► ESP-01 GPIO0
MOTOR_3V3 ── 10k pull-down ──► GND

ESP-01 GPIO3 (RXD, wyjście I2S) ── bezpośrednio ──► C64 READ
C64 WRITE ── bezpośrednio ──► ESP-01 GPIO2
C64 SENSE ── 10k pull-up ──► 3V3
ESP-01 GPIO1 (TXD, wyjście) ──► C64 SENSE

ESP-01 CH_PD ── zwarte z 3V3
ESP-01 RST ──► przycisk RESET ──► GND
ESP-01 GPIO0 ──► JP_FLASH ──► GND
```

## Mapowanie i ustalone warianty

| Funkcja | ESP-01 | Połączenie / zachowanie |
| --- | --- | --- |
| MOTOR_3V3 | GPIO0, wejście | 4N35 pin5→3V3, pin4→GPIO0, 10k do GND; HIGH = MOTOR on; obecna obsługa `motorIsOn() == HIGH` i przerwanie `FALLING` pozostają |
| SENSE | GPIO1 TXD, wyjście | Bezpośrednio do SENSE, pull-up 10k do 3V3; LOW = PLAY |
| WRITE | GPIO2, wejście opcjonalne | Bezpośrednio do WRITE; profil przechwytywania pozostaje opcjonalny |
| READ | GPIO3 RXD, wyjście I2S | Bezpośrednio do READ, bez tranzystora odwracającego; zgodność z obecnym firmware potwierdzona przez autora na sprzęcie |

Nie zmieniamy MOTOR na kolektor otwarty z pull-upem ani jego polaryzacji
w firmware. Nie dodajemy inwertera READ ani nie zmieniamy polaryzacji
odtwarzania na podstawie wcześniejszych komentarzy o tranzystorze.
Pozostający w `idleReadPin()` komentarz o zewnętrznym tranzystorze jest
niezgodny z przyjętym sprzętem; nie stanowi wymagania dla schematu.

## Zasilanie i elementy świadomie pominięte

- Zachowujemy oba warianty montażu regulatora z TCBM2SD: moduł na J4 albo
  AMS1117-3.3 w SOT-223. Montowany jest jeden regulator.
- C6 i C7: po 10 µF, footprint `CP_EIA-3216-12_Kemet-S_HandSolder`.
- C1: 100 µF, footprint `CP_Radial_D8.0mm_P3.50mm`.
- Nie dodajemy osobnego lokalnego zestawu 100 nF + 10 µF przy ESP.
- Nie dodajemy zewnętrznych pull-upów na RST ani GPIO2.
- CH_PD jest połączone bezpośrednio z 3V3.

## Programowanie

ESPsette jest podczas programowania całkowicie odłączony od C64/C16.
Programator jest zawsze odpinany po flashowaniu; przy podłączonym
programatorze autor nie naciska przycisku RESET.

Złącze UART: GND / CTS (NC) / 3V3 / TXD / RXD / DTR, z perspektywy adaptera.
TXD adaptera → GPIO3/RXD ESP, RXD adaptera ← GPIO1/TXD ESP.
Pin 3 złącza UART podaje 3,3 V programatora bezpośrednio na wspólną szynę
3V3 zasilającą ESP i połączoną z wyjściem regulatora. Nie stosujemy selektora
zasilania ani odłączania wyjścia regulatora.
DTR jest podłączone bezpośrednio do RST: bez zworki odłączającej i bez
rezystora szeregowego. Nie przewidujemy zworek izolujących sygnały kasety.
JP_FLASH zwiera GPIO0 do GND na czas programowania.

## Złącza kasety

| Sygnał | C64 | C16 / Plus/4 Mini-DIN 7 |
| --- | --- | --- |
| GND | A / 1 | 1, 7 |
| +5V | B / 2 | 2 |
| MOTOR | C / 3 | 3 |
| READ | D / 4 | 4 |
| WRITE | E / 5 | 5 |
| SENSE | F / 6 | 6 |

Podłączamy jeden komputer naraz. Szczegóły bibliotek, modeli i mechaniki:
[WIRING.md](WIRING.md) oraz [libraries/README.md](libraries/README.md).
