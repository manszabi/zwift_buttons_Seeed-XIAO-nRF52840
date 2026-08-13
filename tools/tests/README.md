# 🧪 Tesztek

Ezek a tesztek **valódi hardver nélkül** futnak, mégis a **valódi kódot**
vizsgálják: a firmware `.ino` fájlját és a konfiguráló program Python
moduljait – nem másolatot, nem újraírt logikát.

## Futtatás

```bash
tools/tests/run_all.sh
```

Szükséges: `g++`, `python3` a `tkinter` modullal, és fejnélküli (kijelző
nélküli) futtatáshoz `xvfb-run`.

Debian / Ubuntu:

```bash
sudo apt install g++ python3-tk xvfb
```

Ha a rendszer alapértelmezett `python3`-ában nincs tkinter, a parancsfájl
magától megkeres egy alkalmasat; kézzel is megadható:

```bash
PYTHON=/usr/bin/python3.12 tools/tests/run_all.sh
```

## Mit futtat

| Teszt | Mit vizsgál |
|-------|-------------|
| `firmware_test.cpp` | A teljes `.ino` a `stubs/` alatti Arduino/BLE/LittleFS utánzatok ellen: gombkezelés, ismétlés, időzített küldés, cél-eszközök, mentés/betöltés, formátum-migráció, soros protokoll. ~90 tesztcsoport. |
| `single_conn.cpp` | Ugyanaz a firmware `ZW_MAX_CONNECTIONS=1`-gyel fordítva – az egykapcsolatos mód is működőképes marad. |
| `gui_test.py` | A valódi Tkinter felület felépítése és végigkattintgatása: szerkesztő ablak, billentyű-felvétel, ellenőrzések, JSON mentés/betöltés, cella-feliratok. ~190 ellenőrzés. |
| `integration_test.py` | A valódi Python `DeviceLink` a valódi firmware-kóddal beszélget: a `bridge.cpp` a lefordított firmware-t soros hídként futtatja, a program pedig ezen keresztül olvassa/írja a kiosztást. |
| `conn_test.py` | Kapcsolat-hibák: nem nyitható port, néma eszköz, régi protokoll-verzió, bontás. |
| `proto_test.py` | A soros protokoll mezői és a `default_keymap.json` egyezése a firmware gyári kiosztásával. |

## Amit a stubok tudnak

A `stubs/` könyvtár minimális, de a lényeges viselkedésben **hűséges**
utánzata az Arduino, a Bluefruit BLE és a LittleFS API-nak:

- a HID jelentések **állapotként** viselkednek (a felengedés külön esemény),
- a `FILE_O_WRITE` nem csonkol és a fájl végére pozicionál – ahogy a valódi
  LittleFS,
- a BLE kapcsolatok külön címmel és „bonded" állapottal rendelkeznek,
- `millis()` és `delay()` a tesztből léptethető, így az időzítések
  ellenőrizhetők valós várakozás nélkül.

## Fordítási szabvány

A firmware-teszt **`-std=gnu++11`**-gyel fordul, mert a Seeed/Adafruit nRF52
board csomag is ezzel fordít (`platform.txt`). Így nem csúszhat be olyan kód,
ami a fejlesztőgépen lefordul, az eszközön viszont nem.

## Amit ezek a tesztek NEM helyettesítenek

Ezek a tesztek a fejlesztőgépen futnak, nem az eszközön. Nem fedik le az
Arduino IDE-s fordítást, a LittleFS viselkedését igazi flash memórián, a BLE
párosítást és RAM-igényt, sem az USB soros időzítést – ezeket feltöltés után
kézzel kell kipróbálni.

A firmware jelenlegi állapota **valódi hardveren ellenőrzött**: Windows PC és
telefon egyidejű kapcsolattal, a Siri indításával együtt. A tesztkészlet ettől
függetlenül hasznos marad: a további módosításokat fogja meg, mielőtt az
eszközre kerülnének.
