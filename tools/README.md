# 🖥️ Zwift Buttons – konfiguráló program

Ablakos (Tkinter) segédprogram, amivel a **3 üzemmód × 5 gomb × 3 esemény**
(rövid nyomás / dupla kattintás / hosszú nyomás) billentyű-kombinációi
állíthatók be, USB soros porton elküldhetők az eszköznek, és elmenthetők annak
belső flash memóriájába.

Cellánként állítható:

- a **parancs** (billentyű-kombináció, média billentyű, üzemmód váltás,
  nézetváltás),
- a **cél eszköz** (ha az eszköz egyszerre két géphez csatlakozik, pl. Windows
  PC + telefon) – üzemmódonként és gombonként is,
- **rövid / dupla nyomásnál**: meddig menjen ki a parancs és milyen ismétléssel,
- **hosszú nyomásnál**: az ismétlés üteme és módja.

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
   - **Rövid nyomásnál és dupla kattintásnál** megadható, hogy a parancs
     **meddig menjen ki** (küldés hossza ms-ban), és hogy közben ismételjen-e
     (lásd lentebb).
   - **Hosszú nyomásnál** beállítható az **ismétlés**, annak sebessége (ms), és
     hogy **külön leütésekként** menjen-e.
   - Az ablak alján a cella **cél eszköze** is megadható – alapból az
     üzemmódnál beállított célpontot örökli (lásd *Egyedi cél gombonként*).
5. Minden üzemmód fülén felül állítható a **cél eszköz**: csak a Windows PC,
   csak a telefon, vagy mindkettő.
6. **Küldés az eszközre** – a kiosztás azonnal érvénybe lép.
7. **Mentés az eszköz memóriájába** – hogy újraindítás után is megmaradjon.

A kiosztás **JSON fájlba** is menthető és onnan visszatölthető
(*Mentés fájlba… / Megnyitás fájlból…*). A régebbi programmal mentett fájlok is
betölthetők – az átalakításról a program tájékoztat, lásd a
*Formátum-verziók* fejezetet.

### Hol van a gyári kiosztás?

**Két helyen, és a kettőnek egyeznie kell:**

| Hol | Mi | Mire jó |
|-----|----|---------|
| A firmware kódjában (`loadDefaultKeymap()`) | a 45 bejegyzés **bele van fordítva** a programba | ez az igazi forrás: ezt tölti be az eszköz, ha nincs mentés, ha a mentés sérült, és ezt állítja vissza a `DEFAULTS` |
| `tools/default_keymap.json` | ugyanaz **adatfájlként** | ebből indul a konfiguráló program, hogy eszköz nélkül is lehessen kiosztást szerkeszteni |

A gyári kiosztás tehát nem a flash memóriában lakik, hanem a programkódban –
ezért nem is veszhet el: formázás vagy sérült mentés után is pontosan ugyanaz
áll vissza. A `DEFAULTS` parancs csak a RAM-ba tölti be; hogy megmaradjon,
utána **menteni kell** az eszköz memóriájába.

A két példány egyezését az integrációs teszt **tételesen ellenőrzi** (mind a 45
bejegyzés és a 3 üzemmód-célpont), a valódi firmware-kódtól lekérdezve – így
nem csúszhatnak szét észrevétlenül.

> A Win (GUI) billentyűt az operációs rendszer gyakran elkapja, ezért azt a
> „Win” pipával érdemes beállítani a felvétel helyett. Ugyanez igaz az
> `Alt+Tab`-ra.

### A cellák felirata a főképernyőn

A táblázat celláin csak a lényeg látszik, ebben a sorrendben:

```
Alt+Tab  (ismétlő)  → PC
Hangasszisztens (Siri/Google)  (2000 ms)  → telefon
Enter  → mindkettő
```

1. a **kiküldött parancs**,
2. zárójelben a **küldési hossz** (ha be van állítva) és hogy **ismétlődik-e**,
3. nyíl után a **parancs célpontja** – ez mindig ott van, öröklődés esetén is.

A részletek (ismétlési idő, külön leütések, módosító nyomva tartása)
szándékosan csak a szerkesztő ablakban jelennek meg. Az üzemmódváltás és a
„nincs művelet" cella nem kap nyilat, mert nem küld semmit egyik eszközre sem.

## Fájlok

| Fájl | Leírás |
|------|--------|
| `zwift_config.bat` | Windows indító parancsfájl (függőség-ellenőrzéssel és -telepítéssel) |
| `zwift_config_gui.py` | A konfiguráló program (ablakos felület + soros kommunikáció) |
| `hid_tables.py` | HID billentyűkódok, média usage-ek, Tkinter keysym → HID leképezés |
| `default_keymap.json` | A firmware gyári kiosztása |
| `requirements.txt` | Python függőségek |
| `tests/` | Hardver nélkül futtatható tesztkészlet – lásd [`tests/README.md`](tests/README.md) |

## Soros protokoll

Az eszköz USB soros porton (115200 baud), sor alapú ASCII protokollal
konfigurálható. Terminálból (pl. Arduino Serial Monitor, `screen`, PuTTY) is
használható – minden parancs `Enter`-rel zárul.

| Parancs | Válasz | Leírás |
|---------|--------|--------|
| `PING` | `OK ZWIFT_BUTTONS PROTO=6 MODES=3 BUTTONS=5 EVENTS=3 SLOTS=2 CONNS=2` | Eszköz azonosítás |
| `GET` | 45 db `MAP …` (10 mező), 3 db `TARGET …` sor, majd `END` | A teljes konfiguráció lekérése |
| `SETTARGET <m> <maszk>` | `OK` / `ERR …` | Üzemmód cél-eszköze (1 = PC, 2 = telefon, 3 = mindkettő) |
| `PEERS` | `SLOT …` / `CONN …` sorok, majd `END` | Fiókok és élő BLE kapcsolatok |
| `ASSIGN <slot> <conn_hdl>` | `OK` / `ERR NOTBONDED` / `ERR …` | Élő kapcsolat hozzárendelése fiókhoz (csak párosítás után) |
| `CLEARSLOT <slot>` | `OK` / `ERR …` | Fiók-hozzárendelés törlése |
| `SET <m> <b> <e> <t> <mod> <code> <rep> <ms> [<tgt> [<hold>]]` | `OK` / `ERR …` | Egy bejegyzés beállítása |
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
| `rep` | ismétlés bitmaszk. Rövid/dupla nyomásnál csak `hold` mellett érvényes. Érvényes értékek: `0` = nincs, `1` = nyomva tartva, `3` = külön leütések, `7` = külön leütések + módosító nyomva. Egyéb kombináció `ERR VALUE` |
| `ms` | ismétlési idő ezredmásodpercben. Külön leütéseknél (`rep` 3/7) a firmware 30 ms alá nem megy |
| `tgt` | cél-felülbírálás: `0` = az üzemmód célpontja, egyébként `1`/`2`/`3`. A `SET`-nél elhagyható |
| `hold` | **csak rövid és dupla nyomásnál, billentyű vagy média műveletnél**: meddig menjen ki a parancs (ms). `0` = a szokásos rövid impulzus, egyébként `50`…`5000` – a firmware a tartományon kívüli értéket `ERR VALUE`-val utasítja vissza, ahogy a hosszú nyomásra vagy más művelet-típusra adott hosszt is. A `SET`-nél elhagyható |

Példa: `SET 0 0 2 1 12 21 0 60` → Normál üzemmód, Gomb 1, hosszú nyomás =
`Alt+Win+R` (mod 12 = 4|8, code 21 = 0x15 = `R`).

A kiosztás CRC32-vel védve, a `/keymap.bin` fájlban tárolódik. Sérült vagy
hiányzó fájl esetén a firmware automatikusan a gyári kiosztást használja.

### Ha mentés közben megszakad az áram

A **Mentés az eszköz memóriájába** nem írja felül közvetlenül a meglévő
konfigurációt. A firmware előbb egy **ideiglenes fájlba** ír, és csak hibátlan
kiírás után cseréli le a régit egy **átnevezéssel** – a littlefs átnevezése
áramszünet-biztos (a fájlrendszer a bekapcsoláskor felismeri és befejezi vagy
visszavonja a félbemaradt műveletet).

Ezért bármikor is szakad meg az áram, a flashben **vagy a régi, vagy az új
konfiguráció** van – félig felülírt tartalom nem keletkezhet:

| Mikor szakad meg | Mi marad a flashben |
|------------------|---------------------|
| Az ideiglenes fájl írása közben | A **régi** konfiguráció, sértetlenül |
| Az átnevezés előtt | A **régi** konfiguráció; az ideiglenes fájl ottmarad, de a következő mentés törli |
| Az átnevezés közben | A fájlrendszer rendezi: a régi vagy az új, sosem keverék |
| Az átnevezés után | Az **új** konfiguráció |

Két további védelem:

- A konfiguráció **CRC32-vel** védett, és a mérete is ellenőrzött. Ha a fájl
  mégis sérülne (csonka tartalom, bitbillenés), a firmware nem tölti be, hanem
  a **gyári kiosztással** indul – a soros porton ki is írja. A sérült fájlt nem
  írja felül, tehát nem tesz kárt.
- Ha maga a fájlrendszer sérül meg annyira, hogy nem csatolható, az
  `InternalFS.begin()` **automatikusan formáz** – az eszköz ilyenkor gyári
  kiosztással és Normál üzemmódban indul, de működőképes marad.

Ugyanez a mechanizmus védi az **üzemmód** mentését is.

> A gomb-kiosztást érdemes a konfiguráló programból **JSON fájlba is
> elmenteni**. Az eszközön tárolt példány elvesztése (sérülés, formázás,
> gyári visszaállítás) így nem jelent újrakattintgatást.

### Formátum-verziók

| Mi | Jelenlegi | Hol jelenik meg |
|----|-----------|-----------------|
| Soros protokoll | **6** | `PING` válasza (`PROTO=6`); a konfiguráló program ennél régebbi firmware-hez nem csatlakozik |
| Mentés az eszközön (`/keymap.bin`) | **4** | csak belül; a 2-es és 3-as mentést a firmware betöltéskor átalakítja |
| Kiosztás-fájl (JSON) | **5** | a fájl `version` mezője |

**Régi mentés az eszközön:** a 2-es és 3-as formátumú `keymap.bin` bejegyzései
8 bájtosak voltak (nem volt bennük küldési hossz). A firmware felismeri a
méretükből, átveszi a tartalmat, és a hiányzó küldési hosszt 0-nak veszi – azaz
a korábbi működés marad. A 2-es mentésnél az ismétlődő bejegyzések a „külön
leütések" módra is átállnak, mert abban a formátumban az ismétlés még végig
lenyomva tartotta a billentyűt.

**Régi kiosztás-fájl:** ugyanez a JSON-nál is megtörténik, de ott **nem néma** –
a program kiírja, hány bejegyzést alakított át. Erre azért van szükség, mert a
3-as fájlverzió kétértelmű: a „külön leütések" mód még ezzel a verziószámmal
jelent meg, tehát egy 3-as fájlban a `repeat: 1` jelenthet szándékos „nyomva
tartva" beállítást is.

## A parancs küldésének hossza (rövid és dupla nyomás)

Alapesetben egy rövid vagy dupla nyomás egy pillanatnyi leütést küld. Néhány
funkcióhoz viszont **nyomva tartott** gomb kell – például a telefon
asszisztensének indításához. Ezért mind a 30 rövid/dupla cellánál beállítható,
hogy a parancs meddig menjen ki:

| Beállítás | Mit csinál |
|-----------|------------|
| **Küldés hossza = 0** *(gyári)* | egyetlen rövid impulzus, a korábbi működés |
| **Küldés hossza = 50…5000 ms**, ismétlés nélkül | a billentyű végig **lenyomva marad** ennyi ideig, majd felengedődik |
| **Küldés hossza + ismétlés** | a beállított ideig ismételget az *Ismétlési idő* szerint |
| **+ Külön leütésekként** | minden ismétlés teljes leütés + felengedés |
| **+ Módosító nyomva** | az `Alt`/`Ctrl`/`Win` a küldés végéig nyomva marad |

**Amíg egy ilyen küldés tart, az eszköz semmilyen más parancsot nem küld ki.**
Ez szándékos: így soha nem torlódik egymásra két parancs. Hogy közben mégse
tűnjön süketnek az eszköz, **bármelyik gombnyomás megszakítja a futó küldést** –
a megszakító gombnyomás saját parancsa viszont nem megy ki. Egy hosszúra
állított küldésből tehát egyetlen gombnyomással ki lehet lépni, a következő
nyomás pedig már a szokásos módon működik.

Fordítva is igaz: amíg egy hosszú nyomás ismétlése fut, addig nem indul
időzített küldés. Ha pedig a beállított célpont épp nincs csatlakozva, a küldés
el sem indul – ilyenkor a többi gomb változatlanul használható marad.

A hosszú nyomásnál ez a mező **nem** jelenik meg, és a firmware vissza is
utasítja: ott a küldést a gomb elengedése zárja le (lásd a következő fejezetet).

## Ismétlés nyomva tartáskor

A hosszú nyomás **működése változatlan**: amíg a gombot nyomva tartod, az eszköz
ismétel. Az ismétlés itt is kétféleképp működhet:

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

A főképernyő celláin a `→ PC` / `→ telefon` / `→ mindkettő` utótag **mindig**
látszik: felülbírálásnál a beállított célt, egyébként az üzemmódtól örökölt
célt mutatja. Az üzemmód célpontjának átállítása azonnal frissíti az öröklődő
cellák feliratát. A választó letiltva jelenik meg a „Nincs művelet" és
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

### Ha megszakad egy kapcsolat

| Helyzet | Mi történik |
|---------|-------------|
| Az egyik eszköz kiesik, a másik marad | A megmaradt eszköz **változatlanul kap** mindent; a kiesett célpont kikerül a lenyomás-nyilvántartásból, így rá már nem megy felengedés |
| **Mindkét** eszköz kiesik | Az eszköz elengedi a kiküldött billentyűket, a folyamatban lévő ismétlést és időzített küldést lezárja – a következő gombnyomás tiszta állapotból indul |
| Újracsatlakozás | A hirdetés **azonnal újraindul**, amint felszabadul egy hely; a kiesett eszköz magától vissza tud jönni |
| Más kapcsolat-azonosítóval jön vissza | A fiók (PC / telefon) a **BLE címet** követi, nem a kapcsolat sorszámát – a parancsok oda mennek, ahová valók |
| Párosítás nélkül csatlakozik | Amíg legalább egy fiók hozzá van rendelve, a hozzá nem rendelt eszköz **egyetlen parancsot sem kap**; a konfiguráló program „Eszközök hozzárendelése…" ablaka jelzi is |

A hirdetés újraindítását a firmware maga végzi: a Bluefruit könyvtár csak akkor
indítja újra magától, ha **minden** kapcsolat megszűnt, két eszköznél tehát
enélkül a kieső eszköz nem tudna visszajönni, amíg a másik kapcsolat él.

### Ha egyszerre nyomsz meg több gombot

Az eszköz egyszerre mindig **egyetlen** parancsot tart a levegőben – így nem
fordulhat elő, hogy két billentyű állapota egymásra torlódik a számítógépnél.
A gombokat a firmware sorban kérdezi le (Gomb 1-től Gomb 5-ig), tehát ha két
esemény ugyanabba a körbe esik, az **alacsonyabb sorszámú gomb nyer**:

| Helyzet | Mi történik |
|---------|-------------|
| Két **rövid** nyomás egy körben | Az alacsonyabb sorszámú gomb parancsa megy ki, a másik **elmarad**. Kb. 100 ms elteltével (a felengedés után) a következő gombnyomás már működik. |
| Két **hosszú** nyomás egyszerre | A **később induló** veszi át az ismétlést; a korábbi tisztán lezárul (a billentyűje felengedődik), és a gomb elengedése sem hagy maga után semmit. Ha újra akarod, engedd el és nyomd meg megint. |
| Rövid nyomás, majd rögtön egy hosszú | A hosszú nyomás átveszi: az előző lenyomás **felengedődik a saját célpontján**, csak utána indul az ismétlés. |
| **Üzemmódváltás** bármi mással egyszerre | Az üzemmódváltás **mindig lefut** – nem küld semmit BLE-n, ezért nem torlódhat semmivel. Ez egyben a biztos kiút is, ha épp egy hosszú küldés zajlik. |

Ez szándékos: egy elmaradt gombnyomás bosszantó, egy beragadt billentyű viszont
sokkal rosszabb. A firmware ezért inkább kihagy egy parancsot, mint hogy két
billentyű-állapotot keverjen össze.

### Ha egy gomb fizikailag beragad

Egy mechanikusan beragadt (vagy zárlatos) gomb esetén az eszköz soha nem kapja
meg a felengedést. Ez kezelés nélkül háromszorosan is rossz lenne: a beállított
parancsot végtelenül szórná a számítógépnek, közben a **többi gomb is néma
maradna** (egyszerre csak egy parancs lehet a levegőben), és az eszköz soha nem
aludna el, tehát lemerülne az akkumulátor.

Ezért a firmware **30 másodperc** folyamatos nyomva tartás után beragadtnak
tekinti a gombot, és figyelmen kívül hagyja, amíg fel nem engedik:

- az ismétlés leáll, a billentyű felengedődik,
- a többi gomb újra használható,
- az inaktivitás-számláló újra nő, tehát az **alvó mód is működik**,
- `DBG 1` mellett a soros porton megjelenik, melyik gombot tekintette
  beragadtnak.

Amint a gomb ténylegesen felengedett, minden korlátozás megszűnik, és a gomb
újra a szokásos módon működik. A 30 másodperc bőven a valós használat fölött
van: a leghosszabb értelmes tartás (hangerő, ablakváltás) néhány másodperc.

#### Ha már bekapcsoláskor be van ragadva

Az ébredés az alvó módból a chip szintjén **újraindulás**, ezért a bekapcsolás
és az ébredés ugyanaz az eset. A gombkezelő könyvtár a bekapcsoláskor már
lenyomott gombot friss gombnyomásnak látná, és 0,8 másodperc múlva elsütné a
hozzá tartozó hosszú nyomás parancsát.

Ezért a firmware **indulásnál megnézi a gombok állapotát**, és amelyik már
nyomva van, azt eleve figyelmen kívül hagyja – nem kell megvárni a 30
másodperces felismerést, és a parancs egyszer sem megy ki tévedésből. A
soros porton ez meg is jelenik:

```
Gomb 4 mar indulaskor nyomva - figyelmen kivul hagyom, amig fel nem engedik
```

A gomb az első tényleges felengedés után azonnal újra használható. (Ha csak
azért tartottad nyomva, mert épp bedugtad az USB-t, semmit nem veszítesz:
engedd el, és onnantól normálisan működik.)

#### Ha az ébresztő gomb (Gomb 2) ragad be

Az alvás ilyenkor **azonnali ébredéssel és újraindulással** járna, mert a chip
az ébresztési feltételt már az elalvás pillanatában teljesítettnek látja. A
firmware ezért **nem alszik el, amíg az ébresztő gomb nyomva van**, hanem
másodpercenként újrapróbálja – amint a gomb felenged, elalszik. Így nem
keletkezik újraindítási hurok; a gomb mechanikai javítása viszont ettől még
szükséges, mert addig az eszköz ébren marad.

### Ha a soros kapcsolat megszakad művelet közben

| Mikor | Mi történik |
|-------|-------------|
| **Beolvasás** (`GET`) közben | A program hibát jelez, és **semmit nem változtat**: a képernyőn maradt kiosztás érintetlen. |
| **Küldés** közben | A hibaüzenet megmondja, **hány beállítás ment ki** a 48-ból, tehát tudod, hogy az eszközön félig alkalmazott kiosztás van – csatlakozz újra és küldd el megint. |
| **Mentés az eszköz memóriájába** közben | A parancs már kiment, csak a válasz veszett el, ezért a **mentés eredménye bizonytalan**: az eszköz akár el is menthette. A program ezt így is írja ki – csatlakozz újra, olvasd be, és ha nem az van rajta, amit vártál, küldd el és mentsd újra. |
| **USB kihúzása** bontás nélkül | A program felismeri, hogy a port eltűnt, **magától lezárja a kapcsolatot**, a gomb visszavált „Csatlakozás"-ra, és a státuszsor kiírja, hogy a kapcsolat megszakadt. |

Az eszköz oldalán a megszakadás nem hagy maga után rendetlenséget: a `SET`
parancsok azonnal hatnak a memóriában, a flashbe csak a `SAVE` ír (az pedig
áramszünet-biztos, lásd fentebb). Ha félbeszakadt a küldés, elég újra
végigküldeni a kiosztást.

### Akkumulátor, LED, watchdog

**Akkumulátor-szint.** Az eszköz szabványos BLE Battery Service-en jelenti a
töltöttséget (percenként frissítve), tehát a telefon Bluetooth-beállításaiban
és a Windows eszközlistájában is látszik. A mérés a XIAO beépített
feszültségosztóján keresztül történik; a százalék a LiPo kisülési görbéjéhez
igazított töréspontos táblázatból jön, nem egyszerű lineáris átváltásból.

**LED.** Az üzemmódot jelző LED **2 másodpercre villan fel** bekapcsoláskor és
minden üzemmódváltáskor, utána elalszik. Folyamatosan égve ez fogyasztaná a
legtöbbet az egész eszközön – nagyságrendekkel többet, mint maga a rádió.

**Hardveres watchdog.** Ha a firmware valaha megakadna (végtelen ciklus,
holtpont), a chip **10 másodperc után magától újraindul**. Mivel az üzemmód és
a kiosztás is mentve van, az újraindulás gyakorlatilag észrevétlen: az eszköz
ugyanabban az üzemmódban jön vissza, és újra hirdeti magát. Alvó módban a
watchdog nem fut, tehát nem ébreszti fel az eszközt.

### Mikor alszik el az eszköz

15 perc (900 másodperc) tétlenség után. A számlálót **bármelyik gombnyomás és
bármelyik soros parancs** nullázza. Mérve:

| Állapot | Elalszik? |
|---------|-----------|
| Tétlen, akár BLE-vel csatlakoztatva | **igen**, 15 perc után |
| Időnkénti gombnyomás | nem (ez a helyes) |
| Időnkénti soros parancs | nem |
| USB csatlakoztatva, de nincs forgalom | **igen** – az USB önmagában nem tartja ébren |
| Beragadt gomb | igen: a beragadás felismerése (30 mp) után indul a 15 perces számláló |
| Az **ébresztő gomb** (Gomb 2) nyomva | nem – szándékosan, mert az elalvás azonnali újraindulást okozna |

> Az USB-ről táplált eszköz is elalszik 15 perc tétlenség után, és ilyenkor a
> BLE kapcsolat is megszűnik. Egy gombnyomás felébreszti.

### Ha egy parancs küldése nem sikerül

A BLE jelentések nem sorbaállított küldések: ha a rádiónak épp nincs szabad
küldési puffere, a könyvtár **eldobja** a jelentést (nincs újrapróbálkozás).
Ezért a firmware maga ellenőrzi a küldést:

| Mi veszik el | Mi történik |
|--------------|-------------|
| A **lenyomás** | Az eszköz nem jegyzi fel célpontnak – így később nem küld felengedést egy le sem nyomott billentyűre. A parancs egyszerűen elmarad. |
| A **felengedés** | **Újrapróbálja** minden főciklus-körben, amíg sikerül. Ez a fontos irány: egy elveszett felengedés a számítógépnél beragadt billentyű lenne. |
| Tartós küldési hiba | Kb. 20 próbálkozás után feladja és tiszta állapotba áll, hogy egy néma kapcsolat ne bénítsa meg az eszközt (a bontást a kapcsolatfigyelés amúgy is észreveszi). |

Ha a kapcsolat még a parancs kiküldése **előtt** szakad meg, semmi nem megy ki
és semmi nem marad nyilvántartva; a nézetváltás számlálója sem lép, tehát a
Zwiftben beállított nézettől sem csúszik el.

**Amíg egyik eszköz sincs hozzárendelve, minden gombnyomás mindkét
kapcsolatra kimegy** – függetlenül attól, hogy az üzemmód vagy az adott cella
melyik célpontot kérné. Ez azért fontos, mert a fájlrendszer sérülése utáni
automatikus formázás a párosításokat és a hozzárendeléseket is törli: ilyenkor
az eszköz nem néma marad, hanem mindent mindkét géppel közöl, amíg újra hozzá
nem rendeled őket. — így az eszköz párosítás után azonnal használható.
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
| **Desktop Assistant** | `0x01CB` | HID „Context-aware Desktop Assistant" |
| **Menü / Home gomb** | `0x0040` | Home gomb (régebbi iPhone-okon) |

Mivel az asszisztens a telefoné, érdemes a cellánál a **Cél eszköz** →
*„Csak a telefonra"* beállítást használni, különben a parancs a PC-re is
kimegy.

### Android

Az Android bemeneti rétege a `0x00CF`-et
`KEY_VOICECOMMAND`-ként veszi át (Linux `hid-input.c`), amit az alapértelmezett
kiosztás (`Generic.kl`) a `VOICE_ASSIST` gombra képez le – ez indítja a Google
Segédet. A `0x01CB` ugyanígy `KEY_ASSISTANT` → `ASSIST` láncon fut.

**Először a „Hangasszisztens (Siri/Google)" kódot próbáld**; ha a telefonod nem
reagál rá, állítsd át „Desktop Assistant"-ra. Ha egy rövid impulzusra csak a
képernyő ébred fel, állíts be küldési hosszt (500 ms), ahogy az iPhone-nál.

*(Ez a leképezés a Linux `hid-input.c` és az Android `Generic.kl` forrásából
következik; Android készüléken nem próbáltam ki – iPhone-on igen.)*

### iPhone

**Működik** – hardveren kipróbálva. A `0x00CF` elindítja a Sirit, de csak akkor,
ha a parancs **elég hosszan** megy ki: egy pillanatnyi impulzusra az iPhone
jellemzően csak a képernyőt ébreszti fel. Épp ezért van a *küldés hossza* mező.

Bevált beállítás:

| Mező | Érték |
|------|-------|
| Művelet | **Média billentyű** → *Hangasszisztens (Siri/Google)* (`0x00CF`) |
| Esemény | rövid nyomás vagy dupla kattintás (hosszú nyomásnál nincs küldési hossz) |
| **Küldés hossza** | **500 ms** |
| Ismétlés | kikapcsolva |
| Cél eszköz | *Csak a telefonra* |

Ennél hosszabb küldés is működhet; rövidebbnél viszont könnyen csak a képernyő
ébred fel, a Siri nem indul el.

> Érdemes tudni: az Apple fejlesztői fórumán több régebbi bejelentés szerint a
> Siri BLE HID eszközről nem indítható, és felmerült, hogy MFi tanúsítvány
> kellene hozzá. A gyakorlat ezt megcáfolta – a különbség a küldés hossza volt.

Ha a te készülékeden mégsem indulna: próbálj hosszabb küldést (pl. 1000 ms),
majd a *Desktop Assistant* (`0x01CB`) kódot. Feloldott, bekapcsolt képernyőn
teszteld, mert lezárt képernyőről az asszisztens indítása gyakran tiltott.
