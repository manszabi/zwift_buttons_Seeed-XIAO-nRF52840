# 🎮 Zwift Buttons – Seeed XIAO nRF52840

Bluetooth Low Energy (BLE) HID billentyűzet a **Zwift** kerékpáros edzőprogram vezérléséhez, **Seeed XIAO nRF52840** mikrokontrollerre építve.

Az eszköz 5 fizikai gombbal rendelkezik, és 3 különböző üzemmódot támogat. BLE-n keresztül billentyűzet-eszközként jelentkezik be, így vezeték nélkül küldhetsz billentyűparancsokat a Zwiftnek (vagy bármely más alkalmazásnak).

---

## 📋 Jellemzők

- **BLE HID billentyűzet** – Az eszköz Bluetooth billentyűzetként párosítható bármely számítógéphez, telefonhoz vagy tablethez
- **5 fizikai gomb** – Mindegyik gombhoz 3 művelet tartozik: rövid nyomás (click), dupla kattintás (double-click), hosszú nyomás (long press)
- **3 üzemmód** – Normál (Zwift), Verseny/Edzés, Média vezérlő
- **Üzemmód-mentés** – Az aktuális üzemmód a belső flash-memóriába mentődik, újraindítás után is megmarad
- **LED visszajelzés** – 3 szín (piros, kék, zöld) jelzi az aktuális üzemmódot
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
| D22 | Töltési áram szabályozás |

---

## 💡 Üzemmódok és LED visszajelzés

| Üzemmód | LED szín | Leírás |
|---------|----------|--------|
| 🔴 **Normál** | Piros | Alapértelmezett Zwift vezérlés (navigáció, nézetek, akciók) |
| 🔵 **Verseny/Edzés** | Kék | Zwift versenyhez és edzéshez optimalizált gombok |
| 🟢 **Média vezérlő** | Zöld | Médialejátszó vezérlés (play/pause, hangerő, szám váltás) |

### Üzemmód váltás

A **Gomb 4 dupla kattintásával** (double-click) válthatsz az üzemmódok között:

**Normál → Verseny/Edzés → Média vezérlő → Normál → ...**

Az üzemmód automatikusan mentődik a belső fájlrendszerbe, így újraindítás után is a legutoljára használt üzemmódban indul el.

---

## 🎛️ Gombok funkciói

### 🔴 Normál üzemmód (Zwift)

| Gomb | Kattintás (Click) | Dupla kattintás (Double-click) | Hosszú nyomás (Long press) |
|------|-------------------|-------------------------------|---------------------------|
| **Gomb 1** | ← Bal nyíl (kanyarodás balra) | F9 | GUI+ALT+R |
| **Gomb 2** | Enter (kiválasztás) | Escape (vissza) | H (ugrás a segítséghez) |
| **Gomb 3** | → Jobb nyíl (kanyarodás jobbra) | F10 | GUI+ALT+G |
| **Gomb 4** | ↓ Le nyíl | ⚙️ Üzemmód váltás | ↓ Le nyíl (ismétlődő) |
| **Gomb 5** | ↑ Fel nyíl | 1-9 nézet váltás (ciklikus) | T |

### 🔵 Verseny/Edzés üzemmód

| Gomb | Kattintás (Click) | Dupla kattintás (Double-click) | Hosszú nyomás (Long press) |
|------|-------------------|-------------------------------|---------------------------|
| **Gomb 1** | Page Down | Numpad – (nehézség csökkentés) | ← Bal nyíl |
| **Gomb 2** | Space (erőbedobás) | Tab | Enter |
| **Gomb 3** | Page Up | Numpad + (nehézség növelés) | → Jobb nyíl |
| **Gomb 4** | G | ⚙️ Üzemmód váltás | ↓ Le nyíl (ismétlődő) |
| **Gomb 5** | E | 1-9 nézet váltás (ciklikus) | ↑ Fel nyíl |

### 🟢 Média vezérlő üzemmód

| Gomb | Kattintás (Click) | Dupla kattintás (Double-click) | Hosszú nyomás (Long press) |
|------|-------------------|-------------------------------|---------------------------|
| **Gomb 1** | ⏮ Előző szám | F9 | GUI+ALT+R |
| **Gomb 2** | ⏯ Play/Pause | Escape | ALT+Tab (ablakváltás) |
| **Gomb 3** | ⏭ Következő szám | F10 | GUI+ALT+G |
| **Gomb 4** | 🔇 Némítás | ⚙️ Üzemmód váltás | 🔉 Hangerő csökkentés (ismétlődő) |
| **Gomb 5** | ⚙️ Beállítások megnyitás | 1-9 nézet váltás (ciklikus) | 🔊 Hangerő növelés (ismétlődő) |

---

## 📦 Szükséges könyvtárak (Arduino)

A következő könyvtárak szükségesek a fordításhoz:

| Könyvtár | Leírás |
|----------|--------|
| `Adafruit_TinyUSB` | USB stack (nRF52 boarddal települ) |
| `Adafruit_SPIFlash` | SPI Flash kezelés |
| `Adafruit_LittleFS` / `InternalFileSystem` | Belső fájlrendszer (nRF52 boarddal települ) |
| `bluefruit` | Adafruit Bluefruit BLE könyvtár (nRF52 boarddal települ) |
| `OneButton` | Gombkezelés (click, double-click, long press) |
| `TickTwo` | Időzített feladatok (watchdog timer) |

### Board telepítése

1. Az Arduino IDE-ben add hozzá a **Seeed nRF52 Boards** board manager URL-t:
   ```
   https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
   ```
2. A **Board Manager**-ben telepítsd a **Seeed nRF52 mbed-enabled Boards** vagy **Seeed nRF52 Boards** csomagot
3. Válaszd ki a **Seeed XIAO nRF52840** boardot

### Könyvtárak telepítése

Az Arduino IDE **Library Manager**-ében telepítsd:
- `OneButton` (by Matthias Hertel)
- `TickTwo`

A többi könyvtár a board csomaggal együtt települ.

---

## 🚀 Feltöltés

1. Nyisd meg a `zwift_buttons_Seeed_XIAO_nRF52840.ino` fájlt az Arduino IDE-ben
2. Válaszd ki a megfelelő boardot: **Seeed XIAO nRF52840**
3. Csatlakoztasd USB-n a mikrokontrollert
4. Kattints az **Upload** gombra

---

## 📱 Használat

1. **Bekapcsolás** – USB-ről vagy akkumulátorról táplálva az eszköz automatikusan elindul
2. **BLE párosítás** – Az eszköz `SEEED_ZWIFT` néven hirdeti magát. Keresd meg a Bluetooth beállításokban és párosítsd
3. **Gombok használata** – Használd a gombokat a Zwift (vagy más alkalmazás) vezérléséhez
4. **Üzemmód váltás** – Gomb 4 dupla kattintással válthatsz az üzemmódok között
5. **Alvó mód** – 15 perc inaktivitás után automatikusan alvó módba lép
6. **Ébresztés** – Nyomd meg a Gomb 2-t (D2 pin) az alvó módból való felébresztéshez

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
