# Wstępny opis połączeń (schemat odłożony)

To jest lista sygnałów do przyszłego schematu KiCad, **nie projekt do
podłączenia do komputera**. Polaryzacje, zasilanie, napięcia i stany podczas
resetu trzeba sprawdzić pomiarem. Numery styków złącza należy potwierdzić
na rysunku portu i fizycznym adapterze przed wykonaniem PCB.

```text
             PORT KASETY                         ESP-01 (3,3 V)
  C64/C16 ── MOTOR ── izolacja/poziomy ── TP_M ── GPIO0  wejście
          ← SENSE ── bufor otwartego wyjścia ← TP_S ← GPIO1  PLAY
          ← READ  ── tranzystor odwracający ← TP_R ← GPIO3  I2S
          ↔ WRITE ── izolacja 5/3,3 V      ↔ TP_W ↔ GPIO2  opcjonalnie
              GND ────────────────────────────────── GND (według układu)
              5 V ── zabezpieczenie/regulator 3,3 V ── VCC ESP

  UART programatora USB-C/USB-serial ↔ GPIO1/GPIO3 tylko po odłączeniu
  torów SENSE/READ od portu kasety; GPIO0 ma osobny tryb flash/reset.
```

## Warunki, które schemat musi spełnić

- MOTOR ma dawać na GPIO0 stan HIGH podczas pracy i nie może ściągać GPIO0
  do LOW przy rozruchu ESP. Dodać punkt pomiarowy po stronie ESP.
- SENSE po stronie komputera ma odpowiadać wciśniętemu PLAY, gdy firmware
  ustawia GPIO1 LOW. Stopień wyjściowy musi odseparować 5 V od GPIO1;
  impulsów UART z ROM podczas startu nie wolno bez sprawdzenia podać na SENSE.
- READ ma być HIGH w spoczynku. Obecny kod ustawia wtedy GPIO3 LOW, bo zakłada
  odwrócenie w tranzystorze. Zmierzyć poziom i polaryzację przy pracy I2S.
- WRITE to etap późniejszy. GPIO2 musi pozostać HIGH podczas startu ESP,
  nie może dostać 5 V ani zegara I2S. C64 nadaje aktywne HIGH i LOW, więc
  sam BSS138 z typowego konwertera I2C nie jest jeszcze zatwierdzonym
  rozwiązaniem. Dla przyszłego turbo wymagany będzie tor dwukierunkowy bez
  konfliktu wyjść; do samej diagnostyki SAVE wystarczy bezpieczne wejście.
- Osobne diody dla SENSE/READ/WRITE mogą mieć bufory lub wydłużanie błysku,
  tak aby nie obciążały linii i nie zmieniały kształtu impulsów.
- Zasilanie ESP wymaga stabilnych 3,3 V także podczas szczytów poboru Wi-Fi.
  Przed wyborem zasilania z portu kasety sprawdzić jego dopuszczalny prąd;
  w przeciwnym razie przewidzieć osobne źródło i świadomie rozwiązać masy.
- Pomiary: poziomy przy zimnym starcie ESP i C64/C16 w różnej kolejności,
  podczas flashowania, z aktywnym i wyłączonym MOTOR, a potem czasy READ.

Zobacz [analizę sprzętu i WRITE](../ANALIZA_PROJEKTU.md#opcjonalny-kanał-komend-z-c64-przez-write)
oraz [analizę turbo](../ANALIZA_TURBO_TAPECART.md). Rysunek elektryczny i PCB
pozostają odłożone do czasu pierwszych prób odtwarzania.
