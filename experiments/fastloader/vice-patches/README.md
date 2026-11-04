# Lokalny patch VICE

`espsette-fastloader.patch` jest osobnym patchem integracyjnym, na wzór
`vice/patches/` w Parobku. Bazuje na rewizji VICE
`eaf9b61f5560f921ebaa25349b09e797f351eb1e`.

Patch rejestruje urządzenie eksperymentalne, dołącza adapter tape-port i
poprawia zwolniony SENSE na C16. Kod nadajnika i adaptera pozostaje osobno
w `sender.c`, `sender.h` i `vice-device.c`, żeby rozwijać go bez dublowania
implementacji w patchu. `patch-vice.py` nakłada patch, a następnie kopiuje
te trzy pliki do roboczego `vice/src/tapeport/`.

Ścieżki w patchu mają prefiks `a/vice/` i `b/vice/`, jak w patchach Parobka.
Ze źródłowego katalogu `vice/` stosujemy `patch -p2`; helper robi to sam:

```sh
python3 experiments/fastloader/patch-vice.py /path/to/disposable/vice-source
```

Helper sprawdza cały patch przed zmianą i rozpoznaje już nałożony patch.
Źródła i binaria emulatora pozostają lokalnie w ignorowanym
`tools/fastloader-vice/`; źródeł Parobka nie modyfikujemy.

Patch pozostaje roboczy do zakończenia developmentu. Wtedy przygotujemy jego
ostateczną postać i commit do repozytorium razem z uzgodnionymi plikami modelu.
