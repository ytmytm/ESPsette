# Notatka do schematu KiCad (C64 datasette ↔ ESP-01)

Źródła: opis połączeń poniżej + komentarze pinów w `src/main.cpp` (GPIO0…3) oraz tabela w README. To **propozycja sieci**, nie zatwierdzony PCB — przed płytką potwierdzić polaryzacje pomiarem.

Założenie autora: wejścia ESP-01 są **5 V tolerant** (przyjęte świadomie).

## Proponowany graf (stan na 2026-10)

```text
C64 tape GND ─────────────────────────── wspólne GND (C64 + regulator + ESP-01)

C64 tape +5V ──► [moduł zasilania] ──► 3V3 ──► ESP-01 VCC
                                           └──► pullupy / CH_PD / 4N35 pin5

C64 MOTOR (~9 V gdy włączony) ── 470Ω ──► 4N35 pin1 (anoda LED)
4N35 pin2 (katoda LED) ──► GND
4N35 pin3 (baza) ── NC
4N35 pin6 (baza) ── NC
4N35 pin4 (emiter) ──► GND
4N35 pin5 (kolektor) = MOTOR_3V3 ──► ESP-01 GPIO0
MOTOR_3V3 ── 10k pull-up ──► 3V3
  // Boot-safe OC: MOTOR off ⇒ LED off ⇒ pin5 otwarty ⇒ 10k↑ ⇒ GPIO0 HIGH (RUN).
  // MOTOR on ⇒ LED pin1→pin2 ⇒ pin5→pin4 do GND ⇒ GPIO0 LOW.
  // Soft musi czytać odwrócenie (on = LOW); ISR pauzy na RISING (powrót do HIGH).

C64 READ  ── (bezpośrednio) ──► ESP-01 GPIO3 (RXD)     ※ kierunek: zob. uwagi
C64 WRITE ── (bezpośrednio) ──► ESP-01 GPIO2
C64 SENSE ── 10k pull-up do 3V3
C64 SENSE ──► ESP-01 GPIO1 (TXD)                       ※ ESP steruje SENSE

ESP-01 CH_PD ── zwarte z 3V3
ESP-01 RST ──► pushbutton ──► GND
```

Mapowanie firmware (`main.cpp`):

| Net / funkcja | ESP-01 | Kierunek w kodzie | Uwaga w kodzie |
| --- | --- | --- | --- |
| MOTOR_3V3 | GPIO0 | wejście | 4N35 pin5→GPIO0, pin4→GND, **10k↑**; **GPIO0 1=off / 0=on** (soft do zmiany) |
| SENSE (PLAY) | GPIO1 TXD | **wyjście** | pull-up 10k do 3V3; **0 = PLAY wciśnięte** |
| WRITE | GPIO2 | wejście (opcjonalnie) | „buffered”; profil WRITE |
| READ | GPIO3 RXD | **wyjście I2S** | spoczynek GPIO LOW ⇒ zakłada **tranzystor odwracający** na C64 READ |

## Czy coś brakuje w grafie?

Warto dopisać na schemacie (nawet jako TBD):

1. **Kondensatory na 3V3** przy ESP (np. 100 nF + 10–47 µF) — Wi‑Fi ma szczyty prądu.
2. **Złącze / testpointy UART** (TXD/RXD, GPIO0 strap, RST) do programowania USB‑serial; przy flashu GPIO0≠MOTOR i odłączenie torów SENSE/READ od C64 (albo zworki).
3. **Prąd z +5 V portu kasety** — czy regulator + ESP (STA/AP) mieści się w limicie portu; jeśli nie: osobne 5 V/USB i tylko wspólne GND.
4. **Opcjonalnie** szeregowe rezystory (np. 100–330 Ω) na READ/WRITE/SENSE mimo 5 V‑tolerant — limitują prąd przy konfliktach / ESD.
5. **LED** status (opcjonalnie, bez obciążania READ/SENSE).
6. **C16/1531** — inny wtyk; te same role sygnałów, inny mechaniczny connector.

## Uwagi i pytania (rozjazdy z firmware / bootem)

### 1. MOTOR + GPIO0 — boot-safe: zamiana pin4/pin5 + pull-up

**Dla KiCad (docelowo)** względem wariantu „pin5=3V3, pin4=GPIO0, 10k↓”:

1. **Zamienić miejscami** wyjście fototranzystora: **4N35 pin4 (emiter) → GND**, **4N35 pin5 (kolektor) → MOTOR_3V3 / GPIO0** (zamiast pin5→3V3, pin4→GPIO0).
2. **Przestawić 10k** z pull-down (GPIO0→GND) na **pull-up (GPIO0→3V3)**.
3. Strona LED bez zmian: C64 MOTOR → 470 Ω → **4N35 pin1 (anoda)**; **4N35 pin2 (katoda) → GND**; **pin3 i pin6 NC**.

Skutek: MOTOR off → GPIO0 **HIGH** (cold start z C64 = RUN); MOTOR on → GPIO0 **LOW**.

**Firmware:** obecny `main.cpp` ma non-inverted (`motorIsOn() = HIGH`, ISR `FALLING` = motor off). Po OC trzeba: `motorIsOn() = (read == LOW)`, ISR pauzy na **`RISING`** (GPIO0 wraca HIGH = motor off). Do czasu zmiany softu nie łączyć tej topologii z `GPIO_DISABLED=0`.

Stary wariant (**pin5→3V3**, **pin4→GPIO0**, 10k↓) był OK dla non-inverted, ale MOTOR off ⇒ GPIO0 LOW ⇒ flash mode przy zasilaniu z C64.

### 1b. SENSE LOW ≠ automatyczny MOTOR przy starcie (nie ratuje bootu)

C64 **nie** łączy sprzętowo SENSE→MOTOR. MOTOR to wyjście CIA (bit, ~9 V przez tranzystor) włączane przez KERNAL/`LOAD` itd.; SENSE to osobne wejście „PLAY wciśnięte”.

W firmware przy `GPIO_DISABLED=0`:

- `setupPins()`: SENSE (GPIO1) startuje **HIGH** (PLAY zwolnione); na końcu `setup()` znowu `setSensePressed(false)`.
- SENSE idzie w **LOW** dopiero przy **autoplay + włożona kaseta** (sesja `/session`, demo po walidacji TAP, itd.) — **po** tym, jak ESP już wystartował w RUN.
- Domyślnie `autoPlay = true`, ale to nie działa, zanim firmware w ogóle wstał.

Wniosek: nawet gdyby C64 przy `LOAD` szybko włączył MOTOR po SENSE=on, **nie pomoże to cold bootowi ESP** — GPIO0 jest próbkowane przy power-on/RST, zanim soft zdąży ściągnąć SENSE. Boot-safe zostaje topologia OC (**pin4→GND**, **pin5→GPIO0**, 10k↑).

### 2. READ — kierunek i inwersja

- W grafie brzmi jak wejście do ESP. W firmware GPIO3 to **wyjście** impulsów taśmy **do** C64 READ.
- Kod w spoczynku trzyma GPIO3 **LOW** i zakłada **tranzystor odwracający** (GPIO LOW ⇒ READ HIGH na porcie).  
  **Bezpośrednie** GPIO3↔READ **bez** inwertera = odwrotna polaryzacja względem obecnego I2S.  
  **Pytanie:** zostawiamy bezpośrednie połączenie i zmieniamy idle/I2S w softie, czy na schemacie wraca NPN/MOSFET jak w starym planie?

### 3. WRITE „bezpośrednio” na GPIO2

- GPIO2 musi być **HIGH przy boocie**. Linia WRITE C64 w spoczynku bywa w stanie, który to psuje — warto zmierzyć.
- README / stary plan: WRITE raczej z buforem; bezpośrednie OK tylko jeśli świadomie polegamy na 5 V‑tolerant i HIGH w idle. Profil `ESPSETTE_WRITE_CAPTURE` nadal opcjonalny.

### 4. SENSE na GPIO1 (UART TX)

- Pull-up 10k + ESP jako wyjście: zgodne z kodem (LOW = PLAY).
- Przy starcie ROM/UART może migać TX → krótko „wciskać” PLAY na C64. Zworka / bufor / start z odłączonym SENSE nadal wskazane przy debugu.

### 5. Izolacja

- Galwanicznie izolowany jest tylko tor MOTOR przez **4N35** (LED **pin1–pin2**, fototranzystor **pin4–pin5**). READ/WRITE/SENSE + wspólne GND = **brak** pełnej izolacji (OK, jeśli tak ma być).

### 6. Napięcie MOTOR C64

- MOTOR to nie „logika 5 V” — bywa ~9 V przy włączonym silniku. **470 Ω** przed **4N35 pin1**: sprawdzić \(I_f\) przy realnym MOTOR (\(\approx (V_\mathrm{MOTOR}-V_f)/470\)).

## Minimalna checklista przed PCB

- [ ] MOTOR: 4N35 pin4→GND, pin5→GPIO0, 10k↑; pomiar off=HIGH / on=LOW; soft odwrócony
- [ ] Czy READ ma inwerter, czy zmiana softu
- [ ] Poziom WRITE w idle vs GPIO2 @ reset
- [ ] Prąd 3V3 przy Wi‑Fi z +5 V portu
- [ ] Ścieżka programowania bez konfliktu z C64

Zobacz też [analizę sprzętu](../ANALIZA_PROJEKTU.md) i tabelę pinów w [README](../README.md#piny-kasety-c64--c16).
