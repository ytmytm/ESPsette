# Audyt startu ESP-01: C64, C16 i Plus/4

## Eksperyment sprzętowy — 2026-11-15, C16

**Wniosek projektowy autora:** obsługa C16 z samoczynnym startem wymagałaby
raczej modułu z większą liczbą dostępnych GPIO, np. ESP-12 albo Wemos D1.
Pozwoliłoby to przenieść MOTOR, SENSE i WRITE na dostępne piny bez funkcji
wyboru trybu startowego, z uwzględnieniem pozostałych połączeń modułu.
READ nadal wymaga GPIO3 ze względu na używany nadajnik I2S.
Jest to kierunek alternatywnej konstrukcji, a nie wynik testu takiego modułu
ani dowód usunięcia wszystkich przyczyn zaobserwowanego problemu.
Projekt pozostaje przy ESP-01; wariant ESP-12/Wemos D1 nie jest obecnie
rozwijany. Dla C16 rozważanym kompromisem pozostaje ręczne rozłączanie MOTOR
i reset, zgodnie z wynikiem poniżej.

Relacja autora, po rozcięciu połączenia gałęzi MOTOR z GPIO1 przy R6:

- ESP-01 uruchamia się po ręcznym resecie przyciskiem. **Samo rozłączenie
  nie zapewnia samoczynnego startu po włączeniu zasilania**; reset nadal
  jest potrzebny.
- Do normalnego działania trzeba przywrócić połączenie już podczas pracy
  (np. przełącznikiem), ponieważ firmware musi odczytywać stan MOTOR.
- Podczas tej próby na WRITE/GPIO2 był obecny wcześniej omawiany pull-up.
  Relacja nie precyzuje, czy po wcześniejszym rozcięciu przywrócono również
  połączenie WRITE z komputerem; nie zakładamy tego na podstawie samej
  obecności pull-upu. Nie zgłoszono też dodania pull-upu na GPIO1 w tej próbie.

Wynik potwierdza możliwość uruchomienia po ręcznym resecie z odłączoną
gałęzią MOTOR. Nie dowodzi, że obciążenie GPIO1 jest jedyną przyczyną
problemów startu, ani że przełącznik sam rozwiązuje start po zasilaniu.
Przyczyna pozostałej konieczności resetu nie została ustalona.
Nie utożsamiamy udanego uruchomienia z potwierdzeniem konkretnego trybu ROM
podczas wcześniejszej awarii.

Na aktualnym schemacie miejsce na przełącznik stanowi JP2:
`4N35 pin 4 / R2 10k do GND → R6 1k → MOTOR_3V3 → JP2 → GPIO1_TXD`.
Po otwarciu JP2 GPIO1 jest odłączone również od pulldownu R2.

## Korekta krytyczna — 2026-11-12

Poprzednie stwierdzenie, że GPIO1 nie jest strapem, było błędne.
[Espressif_Kelly, odpowiedź producenta z 24.07.2015](https://bbs.espressif.com/viewtopic.php?t=796)
opisuje U0TXD/GPIO1 jako wejście wyboru **chip_test_mode** przy włączaniu
zasilania: HIGH oznacza normalną pracę, LOW tryb testowy. To osobny wybór
od boot_sel na GPIO0/2/15. Brak GPIO1 w skróconej instrukcji esptool nie
uprawniał do wykluczenia jego wpływu na start.

MOTOR z wyjściem emiterowym i pulldownem przeniesiony z GPIO0 na GPIO1
nadal obciąża pin wpływający na start. Rezystor 1 kΩ ogranicza konflikt
z nadajnikiem UART, ale nie zapewnia HIGH w chwili próbkowania.
To najważniejszy nowy mechanizm do sprawdzenia po otrzymaniu nowych płytek;
nie potwierdzono jeszcze wejścia konkretnego egzemplarza w chip_test_mode.
Znalezione źródło nie podaje dokładnego okna próbkowania ani zachowania
tej funkcji dla wszystkich rodzajów resetu.

Kwerenda z przypadkami użytkowników: [KWERENDA_STARTU_ESP01.md](KWERENDA_STARTU_ESP01.md).

2026-11-11. Analiza płytki opisanej w PLAN_POLACZEN.md i WIRING.md,
po zamianie MOTOR→GPIO1 przez 1 kΩ oraz SENSE→GPIO0.
READ=GPIO3, WRITE=GPIO2. Schemat samego modułu przyjęty zgodnie ze wskazaniem
autora: ESP-01 z LED od 3V3 przez 2,2 kΩ do TX/GPIO1.
To analiza dokumentacji, kodu i emulacji, nie pomiar analogowy egzemplarza.

## Wynik

Nie można uznać obecnego połączenia za projekt z gwarantowanym startem.
Stwierdzone są trzy grupy problemów: zewnętrzne sterowanie strapami
GPIO0/GPIO2 oraz GPIO1 (chip_test_mode) podczas startu, bezpośrednie połączenia z obwodami podciągniętymi
do 5 V oraz słaby margines LOW na GPIO1 obciążonym LED modułu.
Używanie wszystkich czterech GPIO jest dopuszczalne; problemem są konkretne
obciążenia i stany podczas resetu, nie liczba wykorzystanych GPIO.

Schematy i wykonanie KERNAL-i wyjaśniają, dlaczego C64 i C16 nie są równoważnymi
testami tego adaptera. Nie dowodzą jednak jednej przyczyny całej sekwencji
„dotknięcie TX → LED gaśnie → reset pomaga → kolejny reset nie pomaga”.
Przypisanie tej sekwencji wyłącznie rezystorowi, pojemności albo GPIO2 byłoby
wyjściem poza zgromadzone dowody.

## Źródła i zakres weryfikacji

Źródła producentów:

- [Espressif: ESP8266EX Datasheet, tabela 5-1 i 5-2](https://documentation.espressif.com/0a-esp8266ex_datasheet_en.html).
- [Espressif: wybór trybu startu i log ROM](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html).
- [Commodore C16, 251788, arkusz 1](https://www.zimmers.net/anonftp/pub/cbm/schematics/computers/plus4/c16-251788-1of3-left-right-merged.png)
  i [arkusz 2](https://www.zimmers.net/anonftp/pub/cbm/schematics/computers/plus4/c16-251788-2of3-left-right-merged.png).
- [Commodore Plus/4, 310164, arkusz 1](https://www.zimmers.net/anonftp/pub/cbm/schematics/computers/plus4/plus4-310164-1of4.gif),
  [arkusz 2](https://www.zimmers.net/anonftp/pub/cbm/schematics/computers/plus4/plus4-310164-2of4.gif),
  [arkusz 3](https://www.zimmers.net/anonftp/pub/cbm/schematics/computers/plus4/plus4-310164-3of4.gif).
- [Commodore C64 Service Manual 314001-02](https://oldcrap.org/wp-content/uploads/2023/04/commodore-c64-service-manual-314001-02.pdf).
- [TI SN7406](https://www.ti.com/lit/ds/symlink/sn7406.pdf): inwerter z wyjściem otwartego kolektora.
- [Vishay 4N25](https://www.vishay.com/docs/83725/4n25.pdf).

Znalezione i odczytane projekty KiCad, użyte do śledzenia połączeń:

- [C64 250407 Replica](https://github.com/bwack/C64-250407-Replica-KiCad),
  commit `9bf17bb868248b12fb12210d66be3a37ec4ab832`, pliki
  `250407_.kicad_sch`, `250407_.kicad_pcb`, `250407_.pdf`.
- [C16 LittleSixteen](https://github.com/janaite/c16-LittleSixteen),
  commit `4480843f16f176787c7a538def2686f195c07049`, `datasette.sch`, `cpu.sch`
  oraz kopia oryginalnego `doc/c16_schematics_251788.pdf`.

Repliki nie są dokumentacją producenta. Dla C16 i Plus/4 sprawdzono także
oryginalne skany; dodatkowych elementów LittleSixteen nie przypisano oryginałowi.
Szczegółowe wartości dotyczą wymienionych rewizji, nie wszystkich płyt C64.

[Matthew Ford: używanie GPIO ESP-01](https://www.forward.com.au/pfod/ESP8266/GPIOpins/ESP8266_01_pin_magic.html)
jest relacją autora konstrukcji, nie specyfikacją Espressif. Pokazuje LED na TX
w ESP-01, wejście na GPIO1 po pinMode(INPUT), ochronny rezystor szeregowy
330 Ω oraz wymaganie HIGH na GPIO0/2 podczas startu. Przykłady I²C zakładają,
że urządzenia zewnętrzne nie wymuszają LOW w tej chwili. Nasza linia WRITE
nie spełnia tego założenia we wszystkich stanach.

## Wymagania ESP i rzeczywiste przypisanie

Tryb startu wybierają GPIO0, GPIO2 i GPIO15. Dla flash boot wymagane są
odpowiednio HIGH, HIGH, LOW. GPIO15 jest obsłużony na module.
GPIO1 dodatkowo wybiera chip_test_mode przy power-on (korekta powyżej).
ROM używa go także jako wyjścia UART;
przełączenie na wejście w setup() nie wyłącza wcześniejszych komunikatów ROM.

| GPIO | Obecna funkcja | Problem przy resecie |
|---|---|---|
| 0 | SENSE, wyjście | Podciąganie z komputera może przekroczyć 3,6 V. Stan musi zostać zwolniony na czas próbkowania strapów. |
| 1 | MOTOR, wejście przez 1 kΩ | Wybór chip_test_mode przy power-on wymaga HIGH; następnie UART, potem wejście z LED do 3V3 i pulldownem 11 kΩ do masy. |
| 2 | WRITE, dwukierunkowo | Komputer może aktywnie wymusić LOW, niezależnie od pull-upu ESP. W innych stanach podaje HIGH z domeny 5 V. |
| 3 | READ, wyjście I²S | UART RX podczas programowania; na C64 obciążenie obejmuje pull-up do 5 V. |

Firmware w setupPins() zwalnia UART na GPIO1, ustawia MOTOR jako INPUT,
WRITE jako INPUT_PULLUP, SENSE jako wyjście HIGH, a READ w stan spoczynkowy.
To następuje **po** wyborze trybu startu. Żaden pinMode w setup() nie naprawi
nieprawidłowych strapów przy resecie.

Podciągnięcie GPIO2 nie przeciwdziała skutecznie aktywnemu wyjściu LOW
komputera. Sam test z dodaniem pull-upu nie wyklucza problemu WRITE.

## Czym różnią się porty komputerów

| Linia | C64 250407 | C16 251788 | Plus/4 310164 |
|---|---|---|---|
| SENSE | CPU P4, R1 3,3 kΩ do +5 V | R12 3,3 kΩ do +5 V, wejście 74LS125, odczyt bitu 2 przez dekoder | P2 układu 6529B na $FD10; nie należy przypisywać mu automatycznie R12 z C16 |
| WRITE | CPU P3 przez ferryt, bez inwertera | P1 → 7406 → wspólna linia IEC CLK/P6 → R29 100 Ω i ferryt → złącze; pull-up 1 kΩ | P1 → 7406 → wspólna linia IEC CLK/P6, pull-up R24 1 kΩ; inny filtr niż C16 |
| READ | CIA1 /FLAG, pull-up RP3 3,3 kΩ do +5 V | CPU P4 przez ferryt; 470 pF do GND, brak analogicznego pull-upu na rysunku | CPU P4 przez ferryt |
| MOTOR | P5 → stopnie tranzystorowe, aktywne LOW po stronie sterowania | P3 → 7406 → baza Q5, aktywne LOW po stronie P3 | P3 → 7406 → baza Q4, taka sama polaryzacja |

W C16 WRITE jest elektrycznie również zegarem IEC. Zewnętrzny napęd lub
urządzenie IEC może ściągnąć tę linię, nawet gdy lokalny P1 zwalnia 7406.
Stan GPIO2 nie zależy więc wyłącznie od KERNAL-a ani od funkcji kasety.

Przy DDR procesora równym zero C64 nie steruje aktywnie P3/WRITE. Możliwe
jest podciągnięcie przez stronę ESP. W C16 wejście 7406 przy zwolnionym P1
ma tendencję do stanu HIGH typową dla wejścia TTL, a wyjście 7406 ściąga WRITE.
Nie jest to zewnętrzny rezystor pull-up na P1; nie wolno mylić go z pull-upem
1 kΩ na wyjściu 7406. Zachowanie przy narastających, jeszcze nieprawidłowych
napięciach zasilania nie wynika z tabeli logicznej TTL.

MOTOR również może mieć inny stan przed uruchomieniem KERNAL-a: w C64
zwolniony P5 i rezystor bazy Q2 do GND sprzyjają włączeniu stopnia MOTOR,
a w C16 zwolnione wejście TTL 7406 sprzyja wyłączeniu MOTOR. Są to wnioski
ze schematów dla zasilonych układów, nie zarejestrowane przebiegi narastania.
Dlatego zdanie „MOTOR włącza się dopiero po SENSE” opisuje normalną obsługę
programową, ale nie gwarantuje stanu w czasie resetu i narastania zasilania.

## KERNAL: wykonana emulacja, nie domysł

Uruchomiono x64sc i xplus4 z lokalnej kompilacji VICE 3.10, od resetu,
bez cartridge'a, z domyślnym zwolnionym SENSE. Monitor śledził zapisy
`$0000–$0001` do limitu 2 500 000 taktów licznika VICE.

| Model | Licznik VICE | PC | Zapis | Znaczenie |
|---|---:|---|---|---|
| C64 PAL | 121 | $FDD7 | $01 ← $E7 | Ustawienie zatrzasku: P5=1, P3=0, jeszcze przed DDR |
| C64 PAL | 126 | $FDDB | $00 ← $2F | P5 i P3 stają się wyjściami: MOTOR OFF, WRITE LOW |
| C16 PAL | 304 | $F30D | $00 ← $0F | Najpierw DDR: P0–P3 wyjściami |
| C16 PAL | 309 | $F311 | $01 ← $08 | P3=1: MOTOR OFF; P1=0: 7406 zwolniony, WRITE HIGH |
| Plus/4 PAL | 359 | $F30D | $00 ← $0F | Ta sama kolejność jak C16 |
| Plus/4 PAL | 364 | $F311 | $01 ← $08 | MOTOR OFF, WRITE HIGH |

W modelu VICE zatrzask portu TED zaczyna od zera, więc pomiędzy zapisami DDR
i DATA istnieje pięciotaktowe okno P3=0, czyli żądania MOTOR ON. Nie oznacza
to, że fizyczne MOTOR ani transoptor zdążą osiągnąć HIGH w tak krótkim oknie.

Później C64 zapisuje $37 z $EA79 (pierwszy raz licznik 2060564), C16 $D8
z $E3B4 (134296), Plus/4 $D8 (134520). Bity sterujące zachowują opisane
stany. Górne bity odczytu TED zależą od wejść: realny ESP wymuszający READ
LOW może zmienić odczytaną wartość, ale nie zmienia w tym kodzie P1=0/P3=1.

Istotne fragmenty ROM:

```asm
; C64 901227-03
FDD5  LDA #$E7
FDD7  STA $01
FDD9  LDA #$2F
FDDB  STA $00

; C16/Plus4 PAL 318004-05
F30B  LDA #$0F
F30D  STA $00
F30F  LDA #$08
F311  STA $01
F313  LDX #$FF
F315  STX $FD10       ; zwolnienie wyjść 6529 w Plus/4
```

SHA-256 ROM-ów:

```text
901227-03: 83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721
318004-05: 1f07270c43fc84d1556978e5bb3a6b08aa7b1253f2e46d1a452c89c972ae506b
```

[Skrypt powtórzenia](../experiments/boot-audit/trace-startup.py) i
[logi](../experiments/boot-audit/results/) zachowano w repozytorium.
VICE opiera się na rewizji `eaf9b61f5560f921ebaa25349b09e797f351eb1e`
z istniejącymi poprawkami projektu do fizycznej ścieżki WRITE i SENSE TED.
Nie użyto symulowanego urządzenia ESP jako źródła danych. Kod wyjścia 1
oznacza w tych logach osiągnięcie zadanego limitu cykli, nie błąd KERNAL-a.

Liczników nie należy utożsamiać z czasem od włączenia zasilacza. VICE nie
symuluje tu regulatora ESP, analogowego POR, LED, transoptora ani obwodu
555/556 resetującego komputer. Trace potwierdza kolejność instrukcji;
stan GPIO w chwili próbkowania strapów wymaga dodatkowo czasu resetu ESP
względem tych zdarzeń. Inny KERNAL/cartridge wymaga osobnego trace.

## Dlaczego C64 może startować, a C16 nie

**Przed zamianą pinów:** MOTOR był na GPIO0 z pulldownem. Gdy transoptor
nie przewodzi, projekt żąda bootloadera zamiast uruchomienia firmware.
Przed-KERNAL-owe włączenie MOTOR w C64 może zapewnić potrzebne HIGH;
inne sterowanie MOTOR w C16 może pozostawić LOW. To konkretny mechanizm
zgodny z różnicą komputerów, ale bez zapisu strapów nie dowodzi przyczyny
każdego nieudanego uruchomienia.

**Po zamianie:** ryzyko MOTOR na GPIO0 znika, ale MOTOR obciąża teraz strap
chip_test_mode na GPIO1 (ustalenie z 12.11), a GPIO2 nadal jest WRITE.
C64 przed ustawieniem DDR może pozwolić ESP wystartować z podciągniętym
GPIO2. C16 w tym okresie może aktywnie ściągać WRITE przez 7406. Po starcie
KERNAL-a sytuacja się odwraca: C64 wymusza WRITE LOW, C16 zwalnia je do HIGH.
To uzasadnia wrażliwość na kolejność zasilania i resetów.

Ważna konsekwencja falsyfikowalna: na standardowym C64 w READY, przy
bezpośrednim WRITE→GPIO2 i rzeczywistym LOW na tym pinie, reset ESP nie
powinien dać prawidłowego flash boot. Jeżeli taki reset wielokrotnie działa,
trzeba zweryfikować model fizycznych stanów/konfigurację komputera, zamiast
uznawać powyższe wyjaśnienie za potwierdzoną diagnozę. Cold start i reset
samego ESP przy działającym komputerze to różne testy.

Analogicznie: gdy C16 jest już w READY i żadne urządzenie IEC nie ściąga
zegara, początkowe LOW przed inicjalizacją jego CPU już nie występuje.
Nie może więc samo wyjaśnić kolejnych nieudanych resetów wyłącznie ESP.
Pozostają wtedy obciążenie WRITE napięciem 5 V, stan innych pinów, reset/
zasilanie i etap działania firmware — do rozdzielenia pomiarem lub izolacją.

**Samo wydłużenie resetu nie naprawia obu maszyn:** może ominąć początkowe
LOW w C16, ale przenieść próbkowanie GPIO2 właśnie na LOW ustawione przez
KERNAL C64. Do pewnego startu potrzebna jest niezależność strapów od stanu
komputera podczas resetu, a nie wyłącznie odpowiednio dobrana zwłoka.

## TX, LED i rezystor 1 kΩ

Po wyłączeniu UART, przy nieprzewodzącym transoptorze:

```text
3V3 ── 2,2k ── LED ── GPIO1 ── 1k ── emiter 4N25 ── 10k ── GND
```

Kolektor 4N25 jest połączony z 3V3, ale gdy opto jest wyłączone, prąd przez
LED modułu wystarcza do wytworzenia napięcia na obu rezystorach do GND.

Z pomiaru emitera 0,79 V wynika prąd w 10 kΩ równy 79 µA. Taki prąd daje
na 1 kΩ około 0,079 V, czyli przewiduje GPIO1 około 0,869 V. To blisko
zmierzonych 0,89 V. Różnica może obejmować upływy, obciążenie miernikiem,
niestacjonarność i to, że pomiary nie były jednoczesne.
Przy około 80 µA spadek na LED wynikający z bilansu wynosi około 2,23 V.
To model zgodny z obserwacją, nie pomierzona charakterystyka tej LED.

Nie trzeba zakładać dwóch walczących wyjść, żeby uzyskać ten wynik.
Sam prąd ciemny 4N25 według wskazanej karty jest rzędu nA w podanych
warunkach, więc nie tłumaczy około 80 µA w 10 kΩ.

Przy VIO=3,3 V Espressif gwarantuje LOW do 0,825 V i HIGH od 2,475 V.
0,89 V nie jest gwarantowanym LOW, ale również nie jest poprawnym HIGH.
0,67 V spełnia warunek LOW. Poziom podczas pracy nie dowodzi jednak poziomu
w chwili próbkowania chip_test_mode. Nieokreślony poziom wejścia podczas
pracy może także powodować błędny odczyt, a przy szumie przerwania CHANGE.

1 kΩ ma uzasadnienie jako ograniczenie prądu, gdy ROM steruje TX, a opto
jest włączone. Przy 3,3 V ogranicza prąd tej gałęzi do około 3,3 mA.
Przeniesienie 10 kΩ na stronę GPIO skraca drogę pulldownu z 11 kΩ do 10 kΩ,
czyli tylko o około 9%. Może przesunąć napięcie przez granicę gwarancji,
lecz nie usuwa obciążenia LED ani problemów strapów i domen napięć.
W stanie ON tworzy dodatkowo dzielnik 1 kΩ/10 kΩ.

Przeniesienie tego rezystora z GND do VCC przy niezmienionej topologii
emiterowej zniszczyłoby rozróżnienie MOTOR OFF/ON: obydwa dawałyby HIGH.
Wyjście opto z kolektora, z emiterem do GND i pull-upem, to odrębny projekt
z przeciwną polaryzacją, wymagający zmian firmware i ponownego bilansu
LOW z LED i rezystorem szeregowym. Nie jest prostym przełożeniem rezystora.

W dokumentacji płytki występuje 4N35, a ostatnia relacja mówi o 4N25.
Nie wolno przenosić gwarancji CTR między tymi typami. Dla przykładowego
4N25 Vishay minimalny CTR 20% podano przy IF=10 mA i VCE=10 V; nie jest to
gwarancja napięcia emitera w naszym układzie przy VCE bliskim zeru.

## Domeny napięcia i fastloader

Karta ESP podaje górny poziom wejściowy 3,6 V. 4,67 V nie staje się zgodne
ze specyfikacją tylko dlatego, że jest logicznym HIGH. Nie rozstrzyga to
samodzielnie awarii, ale uniemożliwia uznanie interfejsu za zweryfikowany
elektrycznie. Nie zakładamy konkretnego mechanizmu diod zabezpieczających
ESP bez dokumentacji jego padów.

C16 ma WRITE podciągnięte przez 1 kΩ do zasilania komputera, C64 po
inicjalizacji utrzymuje WRITE LOW. Jest to trwała różnica obciążenia GPIO2,
która istnieje również po zamianie MOTOR/SENSE i po zmianie zasilacza.
Na SENSE C64/C16 pull-up 3,3 kΩ do 5 V współistnieje z naszym 10 kΩ do 3V3;
ten drugi rezystor nie jest konwerterem poziomów. Dla 4,67 V i pinu Hi-Z,
pomijając pozostałe ścieżki, dzielnik daje około 4,33 V.

WRITE musi pozostać dwukierunkowe dla fastloadera. W C64 komputer zwalnia
P3 przez DDR. W C16 musi trzymać P1 jako wyjście LOW, aby wyłączyć zewnętrzny
7406; odczyt powrotu odbywa się przez P6. Samo przełączenie P1 na wejście
nie jest odpowiednikiem zwolnienia C64. Istniejący bootstrap już uwzględnia
tę różnicę. Zwykły jednokierunkowy dzielnik napięcia nie rozwiązuje całego
interfejsu. Dwukierunkowy translator musi dodatkowo zapewnić izolację
GPIO2 podczas resetu — sam translator przepuszczający LOW nadal psuje strap.

READ=GPIO3 pozostaje zgodne z I²S. Programowanie po odłączeniu komputera
usuwa konflikt z jego portem. FLASH musi zwierać rzeczywiste GPIO0 od strony
modułu; zakładać go przy odłączonym zasilaniu, ponieważ firmware używa GPIO0
jako wyjścia. Po flashowaniu trzeba usunąć zworkę przed uruchomieniem aplikacji.

## Zasilanie i reset: osobne, niewykluczone ryzyko

W płytce EN jest zwarte z 3V3, a dodatkowe 470 µF poprawia zasób energii,
lecz nie zapewnia sekwencji zwalniania resetu. Nie zastępuje pomiaru krótkich
spadków ani sprawdzenia stabilności zastosowanego regulatora z rzeczywistymi
kondensatorami. Samo napięcie 3,3 V z miernika nie opisuje startu.
[Zalecenia sprzętowe Espressif](https://documentation.espressif.com/esp8266_hardware_design_guidelines_en.html)
opisują załączanie EN po ustaleniu zasilania. Obecna płytka nie wymusza
takiego opóźnienia osobnym układem. To ryzyko projektu, nie dowód przyczyny
tego przypadku. Lepszy zasilacz bez zmiany objawu osłabia prostą hipotezę
niewystarczającej wydajności źródła.

Nie proponujemy w ciemno zwiększenia kondensatora na RST ani EN: EN jest
zwarte z zasilaniem, więc sam kondensator do GND staje się kolejnym
kondensatorem zasilania, a nie niezależnym opóźnieniem. Nadzorca resetu/EN
może zapewnić kontrolowany start po ustaleniu 3V3, ale nie naprawia GPIO2
ściąganego przez komputer. Rozwiązanie tych dwóch problemów musi być spójne.

## Co wykluczają dotychczasowe próby

| Obserwacja | Wniosek, który wolno wyciągnąć |
|---|---|
| Problem istniał przed dodaniem 1 kΩ i zamianą pinów | 1 kΩ nie jest jedyną pierwotną przyczyną historii; może pogorszyć obecny margines LOW. |
| C64 działa | Wyklucza całkowitą niesprawność adaptera, nie konflikt stanów startowych ani różne obciążenia portów. |
| Mocniejszy zasilacz nie pomógł | Osłabia tezę o niewydolności samego źródła, ale nie mierzy przebiegów 3V3/EN/RST przy module. |
| 330 pF nie pomogło | Nie uzasadnia zwiększania pojemności; kondensator nie zmienia statycznego bilansu LED/pulldown. |
| Pull-up GPIO2 nie pomógł | Nie izoluje od aktywnego sinka 7406 ani od 5 V. |
| Następnie fizyczne rozcięcie WRITE od GPIO2 i pozostawienie pull-upu również nie pomogło; C16 nadal nie uruchamia ESP samodzielnie | Bezpośrednie oddziaływanie WRITE na GPIO2 nie jest wystarczającym wyjaśnieniem obecnej awarii. Ryzyko konstrukcyjne tej gałęzi pozostaje, lecz nie można przedstawiać go jako potwierdzonej przyczyny. |
| Dotknięcie TX zmienia LED, dopiero reset uruchamia | To realny objaw zmiany stanu elektrycznego; nie dowód uszkodzenia mechanicznego, brakującej pojemności ani konkretnego trybu ROM. |
| Brak panelu WWW | Nie rozróżnia braku flash boot, restartów i zatrzymania podczas inicjalizacji sieci. |

W kodzie setupNetwork() wykonywane jest przed dołączeniem przerwania MOTOR.
NetworkConfig::begin() nie czeka na stan MOTOR; motorBusy() blokuje inną
operację HTTP. Nie ma podstaw do tezy, że stały odczyt MOTOR HIGH sam blokuje
uruchomienie Wi-Fi. Burza przerwań po attachInterrupt jest możliwą hipotezą
dla niestabilnego wejścia, ale nie została zaobserwowana i nie wyjaśnia etapu
przed attachInterrupt. Poziom LOW obecny również podczas poprawnej pracy
wyklucza diagnozowanie bootu wyłącznie z napięcia TX.

## Najkrótsze testy rozstrzygające

Nie zaleca się teraz kolejnej przypadkowej pojemności ani zwarcia 1 kΩ.
Zmieniać po jednym czynniku; rozdzielić cold start od resetu samego ESP
przy komputerze w READY, wykonać serię powtórzeń i zapisać wynik.

1. **Odłączyć fizycznie WRITE komputera od GPIO2**, zostawiając po stronie
   ESP pull-up 10 kΩ do 3V3. Wszystkie pozostałe przewody jak dotąd.
   Ten test sprawdza jednocześnie strap i obciążenie domeną 5 V przez WRITE.
   Dodatni wynik lokalizuje problem do tej gałęzi, ale nie rozdziela tych
   dwóch mechanizmów. To nie jest powtórzenie próby z samym pull-upem.
2. Po zgłoszonym negatywnym wyniku testu 1 pozostawić WRITE odłączone,
   z pull-upem GPIO2, i odłączyć gałąź opto od GPIO1, podciągając GPIO1
   10 kΩ do 3V3. Porównujemy z testem 1, zmieniając tylko obciążenie GPIO1.
   To test startu WWW, nie
   odtwarzania (MOTOR odczytywany jako ON). Lokalizuje wpływ obwodu TX/MOTOR.
   Nie wymaga ingerencji w LED modułu.
3. Zarejestrować ROM UART przy 74880 baud w **nieudanym** starcie i porównać
   z udanym. Pasywny odbiór: RXD adaptera o wejściu 3,3 V oraz GND; bez TXD,
   VCC, DTR/RTS. Samo dołączenie odbiornika może zmienić objaw, co trzeba
   odnotować. Log `boot mode:(1,...)` wskazuje download; `(3,...)` flash.
   Poprawny flash log bez WWW kieruje badanie do dalszej inicjalizacji.
4. Gdy wejście sondy zmienia stan, najbardziej wartościowy jest zapis od
   zasilania/resetu: 3V3, EN/RST, GPIO0, GPIO2 i GPIO1. Pomiar późniejszych
   stałych HIGH nie wyklucza krótkiego LOW w chwili próbkowania strapów.

Warunek projektu docelowego: GPIO0/1/2 otrzymują prawidłowe poziomy HIGH 3,3 V
niezależnie od komputera podczas resetu, WRITE zachowuje dwukierunkowość
i ma kontrolowane zwolnienie obu nadajników, a wejście MOTOR ma margines
LOW/HIGH uwzględniający LED modułu i CTR rzeczywistego transoptora.
Takie wymagania można spełnić bez przerabiania samego modułu ESP-01.
