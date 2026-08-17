# 🎮 Zwift Buttons – Seeed XIAO nRF52840

Bluetooth Low Energy (BLE) HID billentyűzet a **Zwift** kerékpáros edzőprogram vezérléséhez, **Seeed XIAO nRF52840** mikrokontrollerre építve.

Az eszköz 5 fizikai gombbal rendelkezik, és 3 különböző üzemmódot támogat. BLE-n keresztül billentyűzet-eszközként jelentkezik be, így vezeték nélkül küldhetsz billentyűparancsokat a Zwiftnek (vagy bármely más alkalmazásnak).

---

## 📋 Jellemzők

- **BLE HID billentyűzet** – Az eszköz Bluetooth billentyűzetként párosítható bármely számítógéphez, telefonhoz vagy tablethez
- **Két eszköz egyszerre** – Egyidejűleg csatlakozhat pl. a Windows PC-hez és a telefonhoz; üzemmódonként állítható, hogy a parancsok melyikre menjenek, és gombonként (mind a 45 cellánál) felül is bírálható
- **5 fizikai gomb** – Mindegyik gombhoz 3 művelet tartozik: rövid nyomás (click), dupla kattintás (double-click), hosszú nyomás (long press – 800 ms tartás után indul)
- **3 üzemmód** – Normál (Zwift), Verseny/Edzés, Média vezérlő
- **Szabadon konfigurálható kiosztás** – Mind a 45 billentyű-kombináció (3 üzemmód × 5 gomb × 3 esemény) átállítható a mellékelt [Python konfiguráló programmal](tools/README.md), USB-n keresztül, újraprogramozás nélkül
- **Állítható ismétlés** – Nyomva tartásnál beállítható az ismétlés sebessége, hogy külön leütésekként menjen-e (különben a számítógép a saját ütemében ismétel), és hogy a módosító nyomva maradjon-e (Alt+Tab ablakváltáshoz)
- **Állítható küldési hossz** – Rövid nyomásnál és dupla kattintásnál megadható, hogy a parancs meddig menjen ki (50–5000 ms), ismétléssel vagy anélkül – például a telefon asszisztensének indításához, ami nyomva tartott gombot vár
- **Üzemmód-mentés** – Az aktuális üzemmód a belső flash-memóriába mentődik, újraindítás után is megmarad
- **Kiosztás-mentés** – A gomb-kiosztás is a belső flash-memóriába kerül (CRC-vel védve), újraindítás után is megmarad
- **LED visszajelzés** – 3 szín (piros, kék, zöld) jelzi az aktuális üzemmódot; a LED **2 másodpercre villan fel** bekapcsoláskor és üzemmódváltáskor, utána elalszik (folyamatosan égve ez fogyasztaná a legtöbbet). Az eszköz sötéten indul: a felvillanás már az üzemmód valódi színe
- **Akkumulátor-szint** – az eszköz BLE-n jelenti a töltöttséget, így a telefon és a Windows is mutatja; két csatlakozott gép esetén mindkettő megkapja a frissítést ([ha nem látszik](#-akkumulátor-szint))
- **Hardveres watchdog** – ha a firmware valaha megakadna, a chip **10 másodperc után** magától újraindul; a mentett üzemmód és kiosztás miatt ez észrevétlen
- **Hibatűrés** – a megszakadó BLE kapcsolat, az elveszett HID jelentés és a fizikailag beragadt gomb is kezelve van, hogy ne maradjon beragadt billentyű a számítógépnél ([részletek](tools/README.md#ha-megszakad-egy-kapcsolat))
- **Automatikus kikapcsolás** – 900 másodperc (15 perc) inaktivitás után alvó módba lép az energiatakarékosság érdekében
- **Gombnyomásra ébredés** – Alvó módból a 2-es (WAKEUP_PIN) gomb megnyomásával kelthető fel
- **Alacsony fogyasztás** – DC-DC konverter engedélyezve, QSPI flash alvó módba helyezve kikapcsoláskor

---

## 🔧 Hardver követelmények

| Alkatrész | Leírás |
|-----------|--------|
| **Mikrokontroller** | [Seeed XIAO nRF52840](https://wiki.seeedstudio.com/XIAO_BLE/) |
| **Gombok** | 5 db nyomógomb (pull-up ellenállással, aktív LOW) |
| **LED-ek** | 3 db LED (piros, kék, zöld) – beépített LED-ek használhatók |

### Lábkiosztás (Pin mapping)

| Pin | Funkció |
|-----|---------|
| D0 (A0) | Gomb 5 |
| D1 (A1) | Gomb 1 |
| D2 (A2) | Gomb 2 + Ébresztés (WAKEUP_PIN) |
| D3 (A3) | Gomb 3 |
| D4 (A4) | Gomb 4 |
| D11 | LED – Piros (Normál üzemmód) |
| D12 | LED – Kék (Verseny/Edzés üzemmód) |
| D13 | LED – Zöld (Média vezérlő üzemmód) |
| D22 (P0.13) | Töltési áram: **LOW = 100 mA**, HIGH = 50 mA – a firmware LOW-ra állítja |
| D23 (P0.17) | Töltésjelzés bemenet (LOW = tölt) – a `BAT` parancs olvassa ki |
| D14 (P0.14) | Akkumulátor-mérés engedélyezése (végig LOW) |
| D32 (P0.31) | Akkumulátor-feszültség (ADC, 1 MΩ / 510 kΩ osztón át) |

> ⚠️ **A lábszámok Arduino-lábszámok, nem a chip P0.xx sorszámai.** Az
> akkumulátor-feszültség a Seeed board csomagjában **D32** (= P0.31); több
> interneten keringő ábra tévesen 35-öt ír, ami ebben a board csomagban nem
> létező láb – az `analogRead()` némán 0-t adna rá, azaz örökre 0% töltöttséget.
> A firmware fordításkor ellenőrzi a board csomag értékeit, és eltérés esetén
> megáll.

---

## 💡 Üzemmódok és LED visszajelzés

| Üzemmód | LED szín | Leírás |
|---------|----------|--------|
| 🔴 **Normál** | Piros | Alapértelmezett Zwift vezérlés (navigáció, nézetek, akciók) |
| 🔵 **Verseny/Edzés** | Kék | Zwift versenyhez és edzéshez optimalizált gombok |
| 🟢 **Média vezérlő** | Zöld | Médialejátszó vezérlés (play/pause, hangerő, szám váltás) |

### Cél eszköz üzemmódonként

Ha az eszköz egyszerre két géphez csatlakozik, üzemmódonként megadható, hogy a
gombnyomások melyikre menjenek. A gyári beállítás:

| Üzemmód | Cél eszköz |
|---------|-----------|
| 🔴 Normál (Zwift) | Csak a Windows PC |
| 🔵 Verseny / Edzés | Csak a Windows PC |
| 🟢 Média vezérlő | Mindkét eszköz |

Így a Zwift vezérlése a PC-t érinti, a zene/hangerő viszont a telefont is.
A beállítás a [konfiguráló programban](tools/README.md) módosítható.

Ezen felül **bármelyik gombnak, bármelyik üzemmódban és bármelyik eseményhez**
(rövid / dupla / hosszú nyomás) adható saját cél, ami felülírja az üzemmódét.
Alapértelmezésben minden cella az üzemmód célpontját örökli — egyetlen kivétel a
**Média vezérlő üzemmód Gomb 1, Gomb 2 és Gomb 3 hosszú nyomása**: ezek gépfüggő
parancsok (`Win+Alt+R`, `Alt+Tab`, `Win+Alt+G`), amiknek telefonon nincs
értelmük, ezért **gyárilag csak a Windows PC-re** mennek. Az egyedi célok a
[konfiguráló programban](tools/README.md) állíthatók.

### Üzemmód váltás

A **Gomb 4 dupla kattintásával** (double-click) válthatsz az üzemmódok között:

**Normál → Verseny/Edzés → Média vezérlő → Normál → ...**

Az üzemmód automatikusan mentődik a belső fájlrendszerbe, így újraindítás után is a legutoljára használt üzemmódban indul el.

---

## 🎛️ Gombok funkciói

> Az alábbi táblázatok a **gyári alapértelmezést** mutatják. Minden cella
> szabadon átállítható a [konfiguráló programmal](tools/README.md), az eszköz
> újraprogramozása nélkül.

### 🔴 Normál üzemmód (Zwift)

| Gomb | Kattintás (Click) | Dupla kattintás (Double-click) | Hosszú nyomás (Long press) |
|------|-------------------|-------------------------------|---------------------------|
| **Gomb 1** | ← Bal nyíl (kanyarodás balra) | F9 | GUI+ALT+R |
| **Gomb 2** | Enter (kiválasztás) | Escape (vissza) | H (ugrás a segítséghez) |
| **Gomb 3** | → Jobb nyíl (kanyarodás jobbra) | F10 | GUI+ALT+G |
| **Gomb 4** | ↓ Le nyíl | ⚙️ Üzemmód váltás | ↓ Le nyíl (ismétlődő, 30 ms) |
| **Gomb 5** | ↑ Fel nyíl | 1-9 nézet váltás (ciklikus) | T |

### 🔵 Verseny/Edzés üzemmód

| Gomb | Kattintás (Click) | Dupla kattintás (Double-click) | Hosszú nyomás (Long press) |
|------|-------------------|-------------------------------|---------------------------|
| **Gomb 1** | Page Down | Numpad – (nehézség csökkentés) | ← Bal nyíl |
| **Gomb 2** | Space (erőbedobás) | Tab | Enter |
| **Gomb 3** | Page Up | Numpad + (nehézség növelés) | → Jobb nyíl |
| **Gomb 4** | G | ⚙️ Üzemmód váltás | ↓ Le nyíl (ismétlődő, 70 ms) |
| **Gomb 5** | E | 1-9 nézet váltás (ciklikus) | ↑ Fel nyíl |

### 🟢 Média vezérlő üzemmód

| Gomb | Kattintás (Click) | Dupla kattintás (Double-click) | Hosszú nyomás (Long press) |
|------|-------------------|-------------------------------|---------------------------|
| **Gomb 1** | ⏮ Előző szám | F9 | GUI+ALT+R *(csak PC)* |
| **Gomb 2** | ⏯ Play/Pause | Escape | ALT+Tab (ablakváltó, lépked amíg nyomva tartod) *(csak PC)* |
| **Gomb 3** | ⏭ Következő szám | F10 | GUI+ALT+G *(csak PC)* |
| **Gomb 4** | 🔇 Némítás | ⚙️ Üzemmód váltás | 🔉 Hangerő csökkentés (ismétlődő, 70 ms) |
| **Gomb 5** | 🎵 Médialejátszó indítása | 1-9 nézet váltás (ciklikus) | 🔊 Hangerő növelés (ismétlődő, 70 ms) |

> ⏱️ **Küldési hossz:** rövid nyomásnál és dupla kattintásnál minden cellánál
> beállítható, hogy a parancs meddig menjen ki. Amíg tart, más gomb parancsa nem
> megy ki – de **bármelyik gombnyomás megszakítja**, így az eszköz sosem marad
> hosszan süket. Hosszú nyomásnál ez nem értelmezett: ott a gomb elengedése
> zárja le a küldést, a működés változatlan.

> 🎙️ **Hangasszisztens:** a média billentyűk között választható a
> *Hangasszisztens (Siri/Google)* és a *Desktop Assistant* kód is, így
> bármelyik gombra rátehető a telefon asszisztensének indítása. **A Siri
> iPhone-on 500 ms-os küldési hosszal indul el** – egy pillanatnyi impulzusra
> jellemzően csak a képernyő ébred fel. Részletek a
> [konfiguráló program leírásában](tools/README.md#hangasszisztens-siri--google-segéd).

---

## ⚙️ Gomb-kiosztás testreszabása

A repóban található `tools/` mappa egy ablakos Python programot tartalmaz,
amivel USB-n keresztül átállítható az összes billentyű-kombináció.

**Windows alatt** elég duplán kattintani a `tools\zwift_config.bat` fájlra – ez
ellenőrzi a Python telepítést, szükség esetén feltelepíti a hiányzó `pyserial`
csomagot, majd elindítja a programot.

**Linux / macOS alatt** (vagy kézzel Windowson):

```bash
cd tools
pip install -r requirements.txt
python zwift_config_gui.py
```

1. Csatlakoztasd az eszközt USB-n, válaszd ki a soros portot, **Csatlakozás**.
2. Üzemmódonként állítsd be a **cél eszközt** (PC / telefon / mindkettő), és
   kattints a táblázat bármelyik cellájára, majd **nyomd le a kívánt
   billentyű-kombinációt** – a program felveszi (a módosítók pipákkal is
   állíthatók, illetve média billentyű, üzemmód váltás vagy nézetváltás is
   választható). A szerkesztő ablakban megadható még:
   - a cella **saját cél eszköze**, ha az adott parancsnak a többitől eltérő
     helyre kell mennie,
   - **rövid nyomásnál és dupla kattintásnál**: meddig menjen ki a parancs
     (50–5000 ms) és milyen ismétléssel,
   - **hosszú nyomásnál**: az ismétlés üteme és módja.
3. **Küldés az eszközre** – azonnal érvénybe lép.
4. **Mentés az eszköz memóriájába** – hogy újraindítás után is megmaradjon.

A kiosztás JSON fájlba is menthető és onnan visszatölthető, illetve bármikor
visszaállítható a gyári alapértelmezés.

Részletek és a soros protokoll leírása: [`tools/README.md`](tools/README.md).

---

## 🧪 Tesztek

A firmware **valódi hardveren ellenőrzött**: Windows PC és telefon egyidejű
kapcsolattal, a telefon hangasszisztensének indításával együtt.

Emellett a repóban van egy **hardver nélkül futtatható tesztkészlet**, ami a
valódi firmware-kódot és a valódi konfiguráló programot vizsgálja:

```bash
tools/tests/run_all.sh
```

Részletek: [`tools/tests/README.md`](tools/tests/README.md).

---

## 📦 Szükséges könyvtárak (Arduino)

A következő könyvtárak szükségesek a fordításhoz:

| Könyvtár | Leírás |
|----------|--------|
| `Adafruit_TinyUSB` | USB stack (nRF52 boarddal települ) |
| `Adafruit_SPIFlash` | SPI Flash kezelés |
| `Adafruit_LittleFS` / `InternalFileSystem` | Belső fájlrendszer (nRF52 boarddal települ) |
| `bluefruit` | Adafruit Bluefruit BLE könyvtár (nRF52 boarddal települ) |
| `OneButton` | Gombkezelés (click, double-click, long press) – **2.0 vagy újabb** (paraméteres callbackek) |
| `TickTwo` | Időzített feladatok (a tétlenségi számláló az alvó módhoz) |

### Board telepítése

1. Az Arduino IDE-ben add hozzá a **Seeed nRF52 Boards** board manager URL-t:
   ```
   https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
   ```
2. A **Board Manager**-ben telepítsd a **Seeed nRF52 mbed-enabled Boards** vagy **Seeed nRF52 Boards** csomagot
3. Válaszd ki a **Seeed XIAO nRF52840** boardot

### Könyvtárak telepítése

Az Arduino IDE **Library Manager**-ében telepítsd:
- `OneButton` (by Matthias Hertel) – **2.0 vagy újabb**
- `TickTwo`

> A firmware a OneButton paraméteres callbackjeit használja
> (`attachClick(fn, param)`), amit az 1.x sorozat még nem ismer. A Library
> Manager alapból a legfrissebbet telepíti, tehát ez csak akkor számít, ha
> régebbi verziót rögzítettél.

A többi könyvtár a board csomaggal együtt települ.

---

## 🚀 Feltöltés

1. Nyisd meg a `zwift_buttons_Seeed_XIAO_nRF52840.ino` fájlt az Arduino IDE-ben
   (a `zwift_config.h` fájlnak ugyanabban a mappában kell lennie – a vázlat
   fülein automatikusan megjelenik)
2. Válaszd ki a megfelelő boardot: **Seeed XIAO nRF52840**
3. Csatlakoztasd USB-n a mikrokontrollert
4. Kattints az **Upload** gombra

---

## 📱 Használat

1. **Bekapcsolás** – USB-ről vagy akkumulátorról táplálva az eszköz automatikusan elindul
2. **BLE párosítás** – Az eszköz `SEEED_ZWIFT` néven hirdeti magát. Keresd meg a Bluetooth beállításokban és párosítsd
3. **Gombok használata** – Használd a gombokat a Zwift (vagy más alkalmazás) vezérléséhez
4. **Üzemmód váltás** – Gomb 4 dupla kattintással válthatsz az üzemmódok között
5. **Második eszköz** – Párosítsd a telefont is; a konfiguráló program
   „Eszközök hozzárendelése…" ablakában add meg, melyik a PC és melyik a telefon
6. **Alvó mód** – 15 perc inaktivitás után automatikusan alvó módba lép
7. **Ébresztés** – Nyomd meg a Gomb 2-t (D2 pin) az alvó módból való felébresztéshez

---

## 🔋 Akkumulátor-szint

Az eszköz a szabványos BLE **Battery Service**-en (0x180F) jelenti a
töltöttséget, így a Windows a Bluetooth-eszközök listájában, az iPhone pedig az
Elemek („Batteries") widgetben mutatja. Percenként mérünk; értesítést csak akkor
küldünk, ha a százalék megváltozott, illetve egyszer minden új kapcsolatnak.

A mérés a P0.31-en, 1 MΩ / 510 kΩ osztón át történik. Ennek az osztónak a
forrásellenállása 338 kΩ, amihez az nRF52840 adatlapja szerint **legalább 20 µs
ADC mintavételi idő** kell – az Arduino könyvtár alapértelmezése viszont 3 µs,
ami csak 10 kΩ-ig elég. Alapértelmezetten hagyva a mintavevő kondenzátor nem
töltődik fel, és a mérés rendszeresen kevesebbet mutat a valóságosnál, ezért a
firmware 40 µs-ra állítja, és nyolc mintát átlagoltat a hardverrel (a BLE adás
áramlökései miatt).

**Töltés közben** a töltő a cellát a végfeszültségen (~4,2 V) tartja, ezért a
töltöttség ilyenkor a valóságosnál magasabbnak látszik. Ez minden
feszültség-alapú mérésre igaz; hogy éppen tölt-e, a `BAT` parancs `CHG` mezője
mondja meg.

### Ha nem látszik a töltöttség

**A leggyakoribb ok, hogy a gép a régi, elmentett szolgáltatás-listát
használja.** A párosított host (Windows, iPhone) eltárolja, milyen BLE
szolgáltatásaink vannak, és újracsatlakozáskor nem kérdezi meg újra – ezt a
Bluetooth szabvány kifejezetten megengedi neki. Ezért egy **firmware-
frissítésben újonnan felvett** szolgáltatás a **már korábban párosított** gépen
nem jelenik meg: a gombok tökéletesen működnek (a HID rész nem változott), de az
akkumulátor-szint hiányzik, mert a mentett listában még nincs benne.

A firmware ezt magától próbálja rendbe tenni: csatlakozás után kiküld egy GATT
„Service Changed" indikációt, amitől a host újra felderíti a szolgáltatásokat.
Ha ez valamiért nem jut célba, kézzel kell megújítani a párosítást:

| Eszköz | Mit kell tenni |
|--------|----------------|
| Windows | Beállítások → Bluetooth és eszközök → a `SEEED_ZWIFT`-nél „Eszköz eltávolítása", majd párosítás újra |
| iPhone | Beállítások → Bluetooth → az `i` gomb a `SEEED_ZWIFT` mellett → „Eszköz elfelejtése", majd párosítás újra |

Utána a hozzárendeléseket (PC / telefon) érdemes ellenőrizni a konfiguráló
program „Eszközök hozzárendelése…" ablakában, mert a párosítás megújításával a
BLE cím megváltozhat.

**Ha az újrapárosítás után sem látszik**, akkor a mérés vagy a szolgáltatás
maga a hibás – ezt a konfiguráló program soros portján a `BAT` paranccsal
lehet megnézni (részletek: [`tools/README.md`](tools/README.md#soros-protokoll)).

---

## ⚡ Energiagazdálkodás

- **DC-DC konverter** engedélyezve az alacsony fogyasztásért
- **Alacsony töltési áram** beállítva (pin 22 LOW)
- **Automatikus alvó mód** 900 másodperc (15 perc) inaktivitás után
- Alvó mód előtt:
  - BLE kapcsolat bontása és advertising leállítása
  - QSPI Flash alvó módba helyezve
  - LED-ek kikapcsolása
  - Belső fájlrendszer lezárása
- **Bármely gombnyomás reseteli az inaktivitás számlálót**

---

## 📄 Licenc

A projekt a repository-ban található LICENSE fájl szerint van licencelve.

---

## ✍️ Szerző

**Manszabi**
