# Kwerenda: ESP-01 / ESP8266, obciążenie TX, dotyk i problemy startu

Data kwerendy: 2026-11-12. Wówczas dalsze próby sprzętowe były odłożone
do otrzymania nowych płytek. Odłączenie WRITE od GPIO2 z pozostawionym
pull-upem nie usunęło problemu na C16.

**Aktualizacja sprzętowa 2026-11-15:** po rozcięciu gałęzi MOTOR/GPIO1 przy
R6 ESP-01 uruchamia się, ale nadal wymaga ręcznego resetu przyciskiem.
Do odczytu MOTOR podczas pracy połączenie trzeba przywrócić przełącznikiem.
Próba odbyła się z pull-upem na WRITE. To nie jest potwierdzenie naprawy
samoczynnego startu ani dowód jednej przyczyny całej awarii.
Szczegóły i ograniczenia zapisu: [analiza](ANALIZA_STARTU_ESP01.md#eksperyment-sprzętowy--2026-11-15-c16).

## Najważniejsze źródło pierwotne: GPIO1 wybiera tryb testowy

[Espressif Developer Zone: U0TXD=GPIO1](https://bbs.espressif.com/viewtopic.php?t=796),
odpowiedź konta Espressif_Kelly, 24.07.2015:

> U0TXD is also a strapping pin for chip_test_mode

Producent rozróżnia HIGH jako normalny tryb i LOW jako chip_test_mode;
odradza ściąganie TX przy włączeniu zasilania. Dopuszcza LED do VDD.
Jest to bezpośrednia odpowiedź na pytanie o obciążanie GPIO1 przed user_init.
W tym samym wątku użytkownik w 2017 zgłasza brak startu po dołączeniu diody
z U0TXD do GND. Nie podaje zakończonego rozwiązania.

To koryguje wcześniejszą analizę ESPsette. GPIO1 nie należy do trzech bitów
boot_sel GPIO0/2/15, ale **nie jest wolny od funkcji strappingu**.
Nie należy utożsamiać chip_test_mode z UART download mode wybieranym GPIO0.
Źródło nie rozstrzyga dokładnej chwili próbkowania ani różnicy między
power-on, impulsem RST, EN i programowym restartem.

Informacja występuje również w publikowanej kopii tabeli ESP8266 Pin List
z 2014 r. Oryginalny katalog Espressif wykazuje Pin List V1.3 z 13.12.2018,
ale w tej kwerendzie nie udało się pobrać właściwego arkusza z serwera
producenta. Podstawą wniosku jest bezpośrednia wypowiedź Espressif powyżej,
nie sam powielany opis pinów ani niezweryfikowany plik XLSX.

## Przypadki z pierwszej ręki

### 1. Sonoff/Tasmota: LOW na TX uniemożliwia start, 2018–2019

[Issue #2902](https://github.com/arendst/Tasmota/issues/2902).
Autor zgłasza brak startu przy zamkniętym przełączniku TX→GND; RX i GPIO14
w jego układzie nie powodują tego samego. Na końcu potwierdza działające
rozwiązanie z podciągnięciem pinu. Komentarz z 2019 wskazuje powyższe źródło
Espressif. Inny uczestnik opisuje różnicę między cold start a restartem
programowym oraz przypadek pulldownu 10 kΩ, ale część jego prób jest
niejednoznaczna. Nie traktujemy wszystkich komentarzy jako zgodnych pomiarów.

Znaczenie: obciążenie TX może zatrzymać uruchamianie, nie tylko zniekształcić
log. Zwarcie 0 Ω z pierwszego zgłoszenia nie jest naszym pulldownem 11 kΩ.

### 2. ESP-01 + LD1125H: różne zasilacze nie pomogły, odłączenie TX pomogło, 2024

[Relacja autora i potwierdzenie rozwiązania](https://www.reddit.com/r/esp8266/comments/1eqv8r0/).
Start udawał się mniej więcej co piąte włączenie. Wymiana ESP, płytki
zasilającej oraz źródła 1 A na 5 A nie pomogła. Odłączenie zasilania sensora
na czas startu pomagało; ostatecznie autor odłączył zbędną linię TX ESP
i potwierdził ustąpienie problemu. Ten sam projekt opisano także w r/Esphome;
nie liczymy go jako niezależnego drugiego przypadku.

Znaczenie: bardzo podobna zależność od peryferium mimo poprawnego wskazania
3,3 V i zmiany źródła. Brakuje przebiegów, więc nie dowodzi mechanizmu
chip_test_mode ani identyczności z naszym układem.

### 3. NodeMCU + niezasilone Nano: odłącz TX na start, potem działa, 2020

[Arduino Forum](https://forum.arduino.cc/t/esp8266-not-booting-when-connected-to-arduino-nano-with-tx-pin/686091).
Autor zastosował konwertery BSS138 i przełączniki linii UART. ESP nie startuje
z TX połączonym do RX niezasilonego Nano; startuje po rozłączeniu tej linii.
Po jej ponownym połączeniu działa, a esp.restart() również działa; następne
odłączenie i przywrócenie zasilania odtwarza problem.

Znaczenie: start zasilania i restart programowy nie muszą być równoważne.
Sam translator BSS138 nie gwarantuje odizolowania od obciążenia przy wyłączonym
zasilaniu drugiej strony. Nie jest to dowód, że C16 ma identyczny mechanizm.

### 4. ESP-01/esp-link: niebieska LED, brak startu z obciążonym TX, 2019

[Issue #449](https://github.com/jeelabs/esp-link/issues/449).
Autor zgłasza poprawną pracę po odłączeniu U0TX na start; z połączeniem
świeci niebieska LED i brak logu aplikacji na GPIO2. Raportuje poprawę po
podciągnięciu wejścia odbiornika rezystorem 1 kΩ; 15 kΩ nie wystarczało.

Znaczenie: kolejne potwierdzenie zależności od obciążenia TX. Wyjaśnienie
autora o zamianie fizycznych strapów GPIO1/GPIO2 przez przekierowanie logu
nie jest dowodem i nie jest przyjęte w naszej analizie. Nie kopiujemy też
1 kΩ jako uniwersalnej naprawy: zmieniłby działanie naszego czujnika MOTOR.

### 5. Dotyk pomagał: ESP-M3/ESP8285, ostatecznie płytka stykowa, 2021–2022

[Wątek z końcowym wynikiem autora](https://www.reddit.com/r/esp8266/comments/rsb92p/).
Układ nie dochodził do programu, LED migała, dotknięcie rezystora pomiędzy
GPIO16 i RST umożliwiało działanie. Wymiana modułu nie pomagała. Autor
ostatecznie znalazł uszkodzony rząd płytki stykowej; wymiana płytki rozwiązała
problem. To ESP8285 i inna ścieżka sygnałowa, nie dokładny odpowiednik ESPsette.

Znaczenie: podobieństwo „pomaga palec” samo nie identyfikuje pojemności ani
uszkodzenia krzemu. Nie przenosimy diagnozy mechanicznej na płytkę użytkownika.

### 6. Dotyk/miernik wywołuje reset ESP-01, 2015

[Relacja GregM na SparkFun](https://community.sparkfun.com/t/esp-8266/30638),
wpis z 04.04.2015: dotknięcie przewodu RST oraz samo próbkowanie miernikiem
potrafiło resetować moduł. Autor opisuje obejście z przyciskiem i rezystorem,
ale bez przebiegów ani potwierdzonej pełnej diagnozy.

Znaczenie: miernik i dotyk mogą wywołać zdarzenie, a nie tylko biernie zmierzyć
napięcie. W naszym przypadku dotykany jest TX, więc to słabsza analogia.

### 7. Dotyk okolicy RX/TX blokuje WWW, 2022 — bez rozwiązania

[ESP8266 Community Forum](https://esp8266.com/viewtopic.php?f=13&t=23910).
Autor zgłasza utratę responsywności GUI po umieszczeniu modułu w płytce
stykowej lub dotknięciu pinów przy RX/TX/D1/D2. Testował kilka płytek i drugi
moduł. Brak zakończonej diagnozy; nie jest to dowód przyczyny naszego przypadku.

## Wnioski dla ESPsette

1. Trzeba wycofać stwierdzenie „GPIO1 nie wpływa na boot”. Producent opisuje
   funkcję chip_test_mode, a liczne relacje potwierdzają praktyczne problemy
   po obciążeniu TX. Nie trzeba sprowadzać ich wyłącznie do zwarcia wyjścia UART.
2. Przeniesienie MOTOR z GPIO0 na GPIO1 mogło przenieść to samo wymuszenie
   LOW z wyboru UART download na wybór trybu testowego. Pasuje to do tego,
   że problem był przed zamianą i po niej. Różne początkowe sterowanie MOTOR
   w C64/C16 daje konkretny mechanizm różnicy maszyn, ale nadal brakuje
   rzeczywistych przebiegów w chwili próbkowania.
3. Pomiar 0,8–0,9 V na TX nie dowodzi już wyłącznie złego marginesu wejścia
   aplikacji. Jest również sygnałem ryzyka podczas startu. Sam pomiar wykonany
   po zawieszeniu nie dowodzi, jakie napięcie zostało wcześniej spróbkowane.
4. Brak poprawy po odłączeniu WRITE jest zgodny z problemem na GPIO1.
   Nie potwierdza go samodzielnie. Sekwencja dotknięcia, zgaśnięcia LED i resetu
   nadal nie ma jednoznacznie potwierdzonego mechanizmu.
5. Nie znaleziono zakończonego, wiarygodnie opisanego przypadku identycznego
   we wszystkich szczegółach: C16, TX około 0,8 V, luźny przewód, potem konieczny
   reset. Nie ma podstaw, by na tej kwerendzie dobrać „naprawczy kondensator”.
6. Na nowych płytkach priorytetem jest możliwość zwolnienia gałęzi MOTOR
   i zapewnienia HIGH na GPIO1 podczas startu. Open collector/open drain
   z pull-upem może zapewnić HIGH przy MOTOR OFF, ale gdy tranzystor przewodzi,
   ponownie daje LOW. Sama nazwa topologii nie zapewnia startu dla obu stanów
   MOTOR; projekt musi uwzględnić moment startu i zmianę polaryzacji firmware.

Źródła pozwalają wskazać konkretny pominięty mechanizm sprzętowy. Nie pozwalają
jeszcze oznaczyć diagnozy tego egzemplarza jako potwierdzonej ani zignorować
pozostałych ryzyk napięciowych i resetu opisanych w głównej analizie.
