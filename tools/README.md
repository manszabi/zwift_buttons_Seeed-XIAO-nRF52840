# 🖥️ Zwift Buttons – konfiguráló program

Ablakos (Tkinter) segédprogram, amivel a **3 üzemmód × 5 gomb × 3 esemény**
(rövid nyomás / dupla kattintás / hosszú nyomás) billentyű-kombinációi
állíthatók be, USB soros porton elküldhetők az eszköznek, és elmenthetők annak
belső flash memóriájába.

## Indítás Windows alatt (ajánlott)

Kattints duplán a **`zwift_config.bat`** fájlra.

A parancsfájl mindent elintéz:

- megkeresi a Pythont (`py` launcher, majd `python`),
- ellenőrzi a verziót (3.7 vagy újabb kell),
- ellenőrzi a `tkinter` meglétét,
- ha hiányzik a `pyserial`, **automatikusan telepíti** (ha a rendszerszintű
  telepítés nem megy, `--user` módban újrapróbálja),
- majd elindítja a programot.

A háttérben nyitva marad egy parancssori ablak – ide írja ki a program a
hibaüzeneteket. Hiba esetén az ablak nem záródik be azonnal, hogy elolvashasd,
mi történt.

Ha a Python egyáltalán nincs telepítve, a parancsfájl kiírja a teendőt: töltsd
le a <https://www.python.org/downloads/> oldalról, és a telepítőben pipáld be
az **„Add python.exe to PATH”** opciót, valamint a **„tcl/tk and IDLE”**
komponenst.

## Indítás kézzel (Linux / macOS / Windows)

```bash
pip install -r requirements.txt
python zwift_config_gui.py
```

Szükséges: **Python 3.7+**, `tkinter` (Windows/macOS telepítőben benne van,
Linuxon `sudo apt install python3-tk`), és a `pyserial` csomag.

## Használat

1. Csatlakoztasd az eszközt USB-n, és indítsd el a programot.
2. Válaszd ki a soros portot (Windows: `COM3`…, Linux: `/dev/ttyACM0`,
   macOS: `/dev/cu.usbmodem…`), majd **Csatlakozás**.
3. A program felajánlja az eszközön lévő kiosztás beolvasását.
4. Kattints bármelyik cellára a táblázatban → megnyílik a szerkesztő ablak:
   - **Billentyű / billentyű-kombináció** – kattints a fehér mezőbe, és
     egyszerűen **nyomd le a kívánt kombinációt** (pl. `Ctrl+Shift+F9`).
     A módosítók külön pipákkal is állíthatók, a billentyű listából is
     választható.
   - **Média billentyű** – play/pause, hangerő, szám váltás stb.
   - **Üzemmód váltás** – a következő üzemmódra léptet.
   - **Zwift nézetváltás** – az 1…9 billentyűket küldi körbe.
   - Hosszú nyomásnál beállítható az **ismétlés** és annak sebessége (ms).
5. **Küldés az eszközre** – a kiosztás azonnal érvénybe lép.
6. **Mentés az eszköz memóriájába** – hogy újraindítás után is megmaradjon.

A kiosztás **JSON fájlba** is menthető és onnan visszatölthető
(*Mentés fájlba… / Megnyitás fájlból…*). A `default_keymap.json` a firmware
gyári kiosztását tartalmazza; a program indításkor ezt tölti be, így eszköz
nélkül is szerkeszthető egy kiosztás.

> A Win (GUI) billentyűt az operációs rendszer gyakran elkapja, ezért azt a
> „Win” pipával érdemes beállítani a felvétel helyett. Ugyanez igaz az
> `Alt+Tab`-ra.

## Fájlok

| Fájl | Leírás |
|------|--------|
| `zwift_config.bat` | Windows indító parancsfájl (függőség-ellenőrzéssel és -telepítéssel) |
| `zwift_config_gui.py` | A konfiguráló program (ablakos felület + soros kommunikáció) |
| `hid_tables.py` | HID billentyűkódok, média usage-ek, Tkinter keysym → HID leképezés |
| `default_keymap.json` | A firmware gyári kiosztása |
| `requirements.txt` | Python függőségek |

## Soros protokoll

Az eszköz USB soros porton (115200 baud), sor alapú ASCII protokollal
konfigurálható. Terminálból (pl. Arduino Serial Monitor, `screen`, PuTTY) is
használható – minden parancs `Enter`-rel zárul.

| Parancs | Válasz | Leírás |
|---------|--------|--------|
| `PING` | `OK ZWIFT_BUTTONS PROTO=1 MODES=3 BUTTONS=5 EVENTS=3` | Eszköz azonosítás |
| `GET` | 45 db `MAP …` sor, majd `END` | A teljes kiosztás lekérése |
| `SET <m> <b> <e> <t> <mod> <code> <rep> <ms>` | `OK` / `ERR …` | Egy bejegyzés beállítása |
| `SAVE` | `OK SAVED` / `ERR SAVE` | Mentés a flash memóriába |
| `LOAD` | `OK LOADED` / `ERR LOAD` | Visszatöltés a flash memóriából |
| `DEFAULTS` | `OK DEFAULTS` | Gyári kiosztás betöltése (mentés nélkül) |
| `MODE [n]` | `OK MODE <n>` | Aktuális üzemmód lekérdezése / beállítása |
| `DBG <0\|1>` | `OK DBG <n>` | A gombok debug kiírásainak ki/be kapcsolása |

A `MAP` / `SET` mezői:

| Mező | Értékek |
|------|---------|
| `m` – üzemmód | 0 = Normál, 1 = Verseny/Edzés, 2 = Média |
| `b` – gomb | 0…4 (Gomb 1…5) |
| `e` – esemény | 0 = rövid, 1 = dupla, 2 = hosszú |
| `t` – típus | 0 = nincs, 1 = billentyű, 2 = média, 3 = üzemmód váltás, 4 = nézetváltás |
| `mod` | módosító bitmaszk: 1 = Ctrl, 2 = Shift, 4 = Alt, 8 = Win (jobb oldali: 16/32/64/128) |
| `code` | HID keycode (típus 1, max. `255`) vagy consumer usage (típus 2, max. `65535`) |
| `rep` | 0/1 – ismétlés nyomva tartás közben (csak hosszú nyomásnál) |
| `ms` | ismétlési idő ezredmásodpercben |

Példa: `SET 0 0 2 1 12 21 0 60` → Normál üzemmód, Gomb 1, hosszú nyomás =
`Alt+Win+R` (mod 12 = 4|8, code 21 = 0x15 = `R`).

A kiosztás CRC32-vel védve, a `/keymap.bin` fájlban tárolódik. Sérült vagy
hiányzó fájl esetén a firmware automatikusan a gyári kiosztást használja.
