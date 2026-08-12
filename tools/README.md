# 🖥️ Zwift Buttons – konfiguráló program

Ablakos (Tkinter) segédprogram, amivel a **3 üzemmód × 5 gomb × 3 esemény**
(rövid nyomás / dupla kattintás / hosszú nyomás) billentyű-kombinációi
állíthatók be, USB soros porton elküldhetők az eszköznek, és elmenthetők annak
belső flash memóriájába.

Emellett üzemmódonként megadható, hogy a parancsok **melyik eszközre** menjenek,
ha az eszköz egyszerre két géphez (pl. Windows PC + telefon) csatlakozik.

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
   - **Média billentyű** – play/pause, hangerő, szám váltás, hangasszisztens
     (lásd a *Hangasszisztens* fejezetet) stb.
   - **Üzemmód váltás** – a következő üzemmódra léptet.
   - **Zwift nézetváltás** – az 1…9 billentyűket küldi körbe.
   - Hosszú nyomásnál beállítható az **ismétlés**, annak sebessége (ms), és
     hogy **külön leütésekként** menjen-e (lásd lentebb).
   - Az ablak alján a cella **cél eszköze** is megadható – alapból az
     üzemmódnál beállított célpontot örökli (lásd *Egyedi cél gombonként*).
5. Minden üzemmód fülén felül állítható a **cél eszköz**: csak a Windows PC,
   csak a telefon, vagy mindkettő.
6. **Küldés az eszközre** – a kiosztás azonnal érvénybe lép.
7. **Mentés az eszköz memóriájába** – hogy újraindítás után is megmaradjon.

A kiosztás **JSON fájlba** is menthető és onnan visszatölthető
(*Mentés fájlba… / Megnyitás fájlból…*). A régebbi programmal mentett fájlok is
betölthetők: a 4-esnél régebbi fájlverziónál az ismétlődő bejegyzések a
*„külön leütések"* módra alakulnak, ahogy a firmware is teszi a saját mentésével.

A `default_keymap.json` a firmware gyári kiosztását tartalmazza; a program
indításkor ezt tölti be, így eszköz nélkül is szerkeszthető egy kiosztás.

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
| `PING` | `OK ZWIFT_BUTTONS PROTO=5 MODES=3 BUTTONS=5 EVENTS=3 SLOTS=2 CONNS=2` | Eszköz azonosítás |
| `GET` | 45 db `MAP …`, 3 db `TARGET …` sor, majd `END` | A teljes konfiguráció lekérése |
| `SETTARGET <m> <maszk>` | `OK` / `ERR …` | Üzemmód cél-eszköze (1 = PC, 2 = telefon, 3 = mindkettő) |
| `PEERS` | `SLOT …` / `CONN …` sorok, majd `END` | Fiókok és élő BLE kapcsolatok |
| `ASSIGN <slot> <conn_hdl>` | `OK` / `ERR NOTBONDED` / `ERR …` | Élő kapcsolat hozzárendelése fiókhoz (csak párosítás után) |
| `CLEARSLOT <slot>` | `OK` / `ERR …` | Fiók-hozzárendelés törlése |
| `SET <m> <b> <e> <t> <mod> <code> <rep> <ms> [<tgt>]` | `OK` / `ERR …` | Egy bejegyzés beállítása |
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
| `code` | HID keycode (típus 1, max. `255`) vagy consumer usage (típus 2, max. `1023` = `0x03FF`, mert a HID leíró eddig hirdet). Fölötte `ERR VALUE` |
| `rep` | ismétlés bitmaszk (csak hosszú nyomásnál). Érvényes értékek: `0` = nincs, `1` = nyomva tartva, `3` = külön leütések, `7` = külön leütések + módosító nyomva. Egyéb kombináció `ERR VALUE` |
| `ms` | ismétlési idő ezredmásodpercben. Külön leütéseknél (`rep` 3/7) a firmware 30 ms alá nem megy |
| `tgt` | cél-felülbírálás: `0` = az üzemmód célpontja, egyébként `1`/`2`/`3`. A `SET`-nél elhagyható |

Példa: `SET 0 0 2 1 12 21 0 60` → Normál üzemmód, Gomb 1, hosszú nyomás =
`Alt+Win+R` (mod 12 = 4|8, code 21 = 0x15 = `R`).

A kiosztás CRC32-vel védve, a `/keymap.bin` fájlban tárolódik. Sérült vagy
hiányzó fájl esetén a firmware automatikusan a gyári kiosztást használja.

## Ismétlés nyomva tartáskor

A hosszú nyomáshoz beállítható ismétlés kétféleképp működhet:

| Mód | `rep` | Mit lát a számítógép |
|-----|-------|----------------------|
| **Külön leütések** *(gyári)* | `3` | Minden ismétlés egy teljes leütés + felengedés. Az ismétlési idő pontosan azt jelenti, amit beállítottál. |
| **Külön leütések, módosító nyomva** | `7` | Mint fent, de az `Alt` / `Ctrl` / `Win` végig nyomva marad, és csak a gomb elengedésekor jön fel. Csak módosítós billentyűnél. |
| **Nyomva tartva** | `1` | A billentyű végig lenyomva marad, és a számítógép a **saját** ismétlési sebességével pörgeti. |

Külön leütéseknél az ismétlési idő alsó határa **30 ms**: a leütés-impulzusnak
(20 ms) és a felengedésnek is be kell férnie két ismétlés közé. A konfiguráló
program ennél kisebb értéket automatikusan 30 ms-ra emel, hogy a cella felirata
ne ígérjen olyan sebességet, amit az eszköz nem tud tartani.

A különbség oka, hogy a HID billentyűzet-jelentés a billentyű **állapotát**
írja le, nem egy leütést. Felengedés nélkül újraküldve a Windows folyamatosan
lenyomva tartottnak látja a billentyűt — ilyenkor hiába állítasz be pl. 2000
ms-ot, a billentyű „beragadtnak" tűnik, és a gép a saját ütemében ismétel.

Ezért a gyári kiosztásban mind a négy ismétlődő művelet (a le nyíl és a
hangerő gombok) **külön leütésekként** megy ki. A szerkesztő ablakban a
*„Külön leütésekként"* pipával váltható.

### Alt+Tab: miért kell a módosítót nyomva tartani

A Windows ablakváltója **csak addig lépked tovább, amíg az `Alt` nyomva van**.
Ha az `Alt+Tab` teljes leütés+felengedésként megy ki, az mindig csak a két
legutóbbi ablak között vált oda-vissza.

Ezért a gyári kiosztásban a Média vezérlő üzemmód **Gomb 2 hosszú nyomása**
ismétlődő, külön leütésekkel, **nyomva tartott módosítóval**, 500 ms-onként:
lenyomva tartva az ablakváltó nyitva marad és lépked tovább, a gomb
elengedésekor pedig az `Alt` felenged, és a kiválasztott ablak kerül előtérbe.

A szerkesztő ablakban ez a *„A módosító (Alt, Ctrl, …) maradjon nyomva"*
pipával kapcsolható; csak a *Külön leütésekként* mellett választható.

> **A régi mentések automatikusan átalakulnak.** A firmware a betöltéskor a
> korábbi (2-es formátumú) kiosztásokban a „nyomva tartva" ismétlést átállítja
> külön leütésekre, tehát a javítás akkor is érvényre jut, ha már van mentett
> konfigurációd — nem kell kézzel visszaállítanod semmit. A gyári célpontok és
> az Alt+Tab módosító-beállítás viszont a te választásod marad; ha azokat is
> szeretnéd, nyomj **Gyári alapértelmezés**-t, majd mentsd az eszközre.

## Két eszköz egyszerre (PC + telefon)

Az eszköz egyszerre két géphez tud csatlakozni. Mivel a BLE
kapcsolat-azonosítók csatlakozási sorrendben keletkeznek, a firmware a **BLE
cím** alapján jegyzi meg, melyik a PC és melyik a telefon — így újracsatlakozás
után is jó marad a hozzárendelés.

> **Fontos:** a hozzárendelés csak **párosítás (bonding) után** lehetséges.
> Párosítás előtt a telefonok változó (resolvable private) címet használnak,
> amit elmentve soha többé nem egyezne. A firmware ezért az `ASSIGN` parancsra
> `ERR NOTBONDED` hibát ad, a konfiguráló programban pedig a gombok tiltva
> maradnak, amíg a párosítás nem kész.

**Beállítás:**

1. Párosítsd az eszközt mindkét géppel (mindkettő maradjon csatlakoztatva).
2. A konfiguráló programban nyomd meg az **„Eszközök hozzárendelése…"** gombot.
3. A listában megjelenik mindkét csatlakozott eszköz a BLE címével; kattints
   a megfelelő **„Ez a Windows PC"** / **„Ez a telefon"** gombra.
4. Zárd be az ablakot, majd **Mentés az eszköz memóriájába**, hogy a
   hozzárendelés újraindítás után is megmaradjon.

### Egyedi cél gombonként

Minden cella – **mind a 3 üzemmód, mind az 5 gomb, mind a 3 esemény** (rövid /
dupla / hosszú nyomás), összesen 45 – kaphat saját célt, ami felülírja az
üzemmódét. A művelet szerkesztő ablakában alul van a **Cél eszköz** választó:

| Választás | Jelentés |
|-----------|----------|
| **Az üzemmódnál beállított célpont** | alapértelmezés: örökli az üzemmód célját |
| Csak a Windows PC | mindig a PC-re megy, akkor is, ha az üzemmód mindkettőre küld |
| Csak a telefon | mindig a telefonra megy |
| Mindkét eszköz | mindig mindkettőre megy |

A cella felirata `→ PC` / `→ telefon` / `→ mindkettő` utótaggal jelzi, ha
felülbírálás van érvényben. A választó letiltva jelenik meg a „Nincs művelet" és
az „Üzemmód váltás" típusnál, mert ezek nem küldenek semmit egyik eszközre sem.

**Gyárilag egyetlen kivétel van**, minden más cella örököl: a **Média vezérlő
üzemmód Gomb 1, Gomb 2 és Gomb 3 hosszú nyomása** gépfüggő parancs
(`Win+Alt+R`, `Alt+Tab`, `Win+Alt+G`), aminek telefonon nincs értelme.

| Média vezérlő, hosszú nyomás | Parancs | Gyári cél |
|------------------------------|---------|-----------|
| Gomb 1 | `Win+Alt+R` | Csak a Windows PC |
| Gomb 2 | `Alt+Tab` | Csak a Windows PC |
| Gomb 3 | `Win+Alt+G` | Csak a Windows PC |
| Gomb 4 | 🔉 Hangerő – | Az üzemmód célpontja (mindkettő) |
| Gomb 5 | 🔊 Hangerő + | Az üzemmód célpontja (mindkettő) |

Soros parancsból ugyanez a `SET` 9. mezője (lásd a protokoll-táblázatot).

**Amíg egyik eszköz sincs hozzárendelve, minden gombnyomás mindkét
kapcsolatra kimegy** — így az eszköz párosítás után azonnal használható.
Amint viszont legalább egy hozzárendelés létezik, a hozzá nem rendelt eszközök
**egyetlen parancsot sem kapnak meg**; ezért érdemes mindkettőt hozzárendelni.
A konfiguráló program figyelmeztet, ha van hozzá nem rendelt csatlakozott eszköz.

A gyári visszaállítás (`DEFAULTS`) a gomb-kiosztást és a célpontokat
alapállapotba hozza, de a PC/telefon hozzárendelést **megtartja**.

Ha valamiért mégis egykapcsolatos működés kell, a `zwift_config.h`-ban állítsd
a `ZW_MAX_CONNECTIONS` értékét 1-re. A mentett konfiguráció formátuma nem
változik, így oda-vissza váltható.

## Hangasszisztens (Siri / Google Segéd)

A média billentyűk listájában három olyan kód van, ami a telefon asszisztensét
célozza:

| Listaelem | Usage | Mit vált ki |
|-----------|-------|-------------|
| **Hangasszisztens (Siri/Google)** | `0x00CF` | HID szabvány „Voice Command" |
| **Asszisztens (Android)** | `0x01CB` | HID „Context-aware Desktop Assistant" |
| **Menü / Home gomb** | `0x0040` | Home gomb (régebbi iPhone-okon) |

Mivel az asszisztens a telefoné, érdemes a cellánál a **Cél eszköz** →
*„Csak a telefonra"* beállítást használni, különben a parancs a PC-re is
kimegy.

### Android

Itt megbízhatóan működik. Az Android bemeneti rétege a `0x00CF`-et
`KEY_VOICECOMMAND`-ként veszi át (Linux `hid-input.c`), amit az alapértelmezett
kiosztás (`Generic.kl`) a `VOICE_ASSIST` gombra képez le – ez indítja a Google
Segédet. A `0x01CB` ugyanígy `KEY_ASSISTANT` → `ASSIST` láncon fut.

**Először a „Hangasszisztens (Siri/Google)" kódot próbáld**; ha a telefonod nem
reagál rá, állítsd át „Asszisztens (Android)"-ra.

### iPhone

A `0x00CF` az a kód, amit a HID szabvány *„intended to start Siri"*
megjegyzéssel lát el, **de BLE HID eszközről nem megbízható**. Az Apple
fejlesztői fórumán több bejelentés szerint sem indul el tőle a Siri, és felmerül,
hogy ehhez MFi tanúsítvány kellene. Nálam nincs iPhone a teszteléshez, ezért
ezt **nem tudom garantálni** – érdemes egyszerűen kipróbálni.

Ha nem megy, ezek a kerülő utak maradnak:

- **Home gomb nyomva tartása.** iOS-en a `0x0040` (Menü) és a billentyűzet
  `Esc` gombja a Home gombként viselkedik; **nyomva tartva** a Siri jön elő.
  Ez viszont csak fizikai Home gombos iPhone-okon működik, a Face ID-s
  modelleken (iPhone X-től) nem.
- **Nyomva tartáshoz** a hosszú nyomásnál kapcsold be az ismétlést
  **„Nyomva tartva"** módban (`rep = 1`, a *„Külön leütésekként"* pipa
  kikapcsolva). Enélkül a firmware 100 ms után felengedi a billentyűt, ami
  rövid egy nyomva tartáshoz.
- **Kimondott parancs helyett Parancsikon:** iPhone-on a „Keresés" média kód
  (`0x0221`) a Spotlightot nyitja meg, ahonnan egy Parancsikon a nevének első
  betűivel indítható.
