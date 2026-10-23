# Testy kasetowe bez ESP-01

Zestaw korzysta z testów projektu Pi1551 jako niezależnego punktu odniesienia:

- `.../Pi1551/Pi1541/tests/tape_native.cpp`: C16 `RecordedBit` i `RecordedByte` z półokresami 240/480/960 µs, dwie kopie nagłówka, przypadki zapisu bez jawnego znacznika końca oraz granice rekordów rozszerzonych.
- `.../Pi1551/Pi1541/tests/oled_menu_native.cpp`: tekstowe wpisy Ultimate IDX, komentarze, zapisy hex/dec/octal i błędne offsety.
- `.../Pi1551/Pi1541/tests/test_vice_tape_interop.py`: mocniejszy test VICE↔Pi1551, który zapisuje PRG przez emulator C16 i sprawdza bajty po ponownym `LOAD`. Zależy od zewnętrznego VICE, ROM-ów i binarki Pi1551; nie uruchamia się jako część tego zestawu.

Na Macu, bez ESP-01, można uruchomić:

```sh
node tests/player_index.mjs
c++ -std=c++17 -Isrc tests/write_decoder_test.cpp src/write_decoder.cpp -o /tmp/espsette-write-test
/tmp/espsette-write-test
c++ -std=c++17 -Itests/support -Isrc tests/prg_vectors_test.cpp src/prg_encoder.cpp src/tape_source.cpp -o /tmp/espsette-prg-test
/tmp/espsette-prg-test
```

`prg_vectors_test.cpp` porównuje każdy impuls produkcyjnego generatora C16 i C64 z niezależną sekwencją bloków. Dla C16 dodatkowo porównuje czasy z impulsem testowym Pi1551 z tolerancją dla wartości cykli wybranych przez WAV-PRG; dla C64 używa tej samej struktury bitów oraz długości 384/536/680 cykli. `write_decoder_test.cpp` sprawdza też błędną parzystość i sumę XOR dla obu maszyn. `player_index.mjs` odtwarza przypadki IDX z Pi1551, sprawdza nagłówki C64 i C16 v1/v2, granice rekordów, ucięte i zerowe impulsy, fazę półokresu po skoku, skoki REW/FF według licznika VICE/Pi1551, autoplay, domyślny komputer i PLAY/STOP w lokalnej symulacji oraz uzgodnienie offsetu po utraconej odpowiedzi HTTP. Nie testuje przebiegu I2S na pinie READ.

Testy nie potwierdzają rzeczywistego tempa I2S, polaryzacji READ/WRITE, opóźnień DMA ani działania `LOAD` na komputerze. Pierwsza kompilacja firmware i test sprzętowy pozostają osobnym krokiem na Macu.
