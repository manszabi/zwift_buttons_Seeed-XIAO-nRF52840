#ifndef ZWIFT_CONFIG_H
#define ZWIFT_CONFIG_H

#include <Arduino.h>

// A gomb-kiosztás (keymap) adatszerkezetei.
//
// Fontos: ezek KÜLÖN fájlban vannak, mert az Arduino IDE a .ino fájl elejére
// generálja az automatikus függvény-prototípusokat. Ha a struct-ok a .ino-ban
// lennének, a rájuk hivatkozó prototípusok a definíciójuk ELŐTT keletkeznének,
// és nem fordulna le a vázlat.

#define ZW_NUM_MODES 3    // normal, verseny/edzes, media
#define ZW_NUM_BUTTONS 5  // Gomb 1..5
#define ZW_NUM_EVENTS 3   // rovid / dupla / hosszu

// Egyszerre ennyi eszközhöz (BLE peripheral kapcsolathoz) csatlakozhat.
// 1-re állítva a korábbi, egykapcsolatos működést kapjuk vissza; a mentett
// konfiguráció formátuma ettől nem változik, így oda-vissza váltható.
#ifndef ZW_MAX_CONNECTIONS
#define ZW_MAX_CONNECTIONS 2
#endif

// Cél-eszköz "fiókok". A gombnyomás célpontját üzemmódonként bitmaszk adja meg.
#define ZW_NUM_SLOTS 2
#define ZW_SLOT_PC 0     // Windows PC
#define ZW_SLOT_PHONE 1  // telefon

#define ZW_TARGET_PC (1 << ZW_SLOT_PC)        // 0x01
#define ZW_TARGET_PHONE (1 << ZW_SLOT_PHONE)  // 0x02
#define ZW_TARGET_ALL (ZW_TARGET_PC | ZW_TARGET_PHONE)

// A KeyAction.repeat mező bitjei (hosszú nyomás).
//
// A HID billentyűzet-jelentés a billentyű ÁLLAPOTÁT írja le, nem egy leütést:
// felengedés nélkül ismételve a host végig lenyomva tartottnak látja, és a
// saját ismétlési sebességével pörgeti — a beállított ismétlési idő ilyenkor
// nem érvényesül. A ZW_REPEAT_RELEASE hatására minden ismétlés külön
// leütés+felengedés pár lesz, így az ismétlési idő tényleg azt jelenti.
// A ZW_REPEAT_HOLD_MOD csak a ZW_REPEAT_RELEASE mellett értelmes: ilyenkor az
// ismétlések között csak maga a billentyű engedődik fel, a módosító (Alt, Ctrl,
// …) végig nyomva marad, és csak a gomb elengedésekor jön fel. Ez kell pl. az
// Alt+Tab ablakváltáshoz: a Windows csak addig lépked tovább, amíg az Alt
// nyomva van — különben mindig csak a két legutóbbi ablak között vált.
#define ZW_REPEAT_ENABLED 0x01
#define ZW_REPEAT_RELEASE 0x02
#define ZW_REPEAT_HOLD_MOD 0x04
#define ZW_REPEAT_MASK (ZW_REPEAT_ENABLED | ZW_REPEAT_RELEASE | ZW_REPEAT_HOLD_MOD)

// A média (consumer) jelentés-leíró — TUD_HID_REPORT_DESC_CONSUMER — a
// 0x0000..0x03FF usage tartományt hirdeti meg. Az e fölötti kódot a fogadó
// eszköz eldobja, ezért a beállításnál visszautasítjuk.
#define ZW_CONSUMER_MAX_USAGE 0x03FF

// A rövid és a dupla nyomás küldési hosszának felső határa (ms). Amíg egy ilyen
// küldés tart, más parancs nem mehet ki, ezért nem érdemes tetszőlegesen
// hosszúra engedni.
// Hardveres watchdog: ha a főciklus ennyi ideig nem jelentkezik be, a chip
// magától újraindul. A mentett üzemmód és kiosztás miatt az újraindulás
// észrevétlen. Bőven a leghosszabb blokkoló művelet (flash-írás) fölött van.
#define ZW_WDT_TIMEOUT_MS 10000

// Meddig világítson az üzemmódot jelző LED. Folyamatosan égve ez fogyasztaná a
// legtöbbet az egész eszközön (nagyságrendekkel többet, mint a rádió), ezért
// csak felvillan: bekapcsoláskor és minden üzemmódváltáskor.
#define ZW_LED_ON_MS 2000

// Milyen gyakran mérjük és jelentsük az akkumulátor töltöttségét.
#define ZW_BATTERY_UPDATE_MS 60000

// Meddig várjuk meg az ADC eltolás-kalibrálását. A hardver ~120 µs alatt végez;
// ez a korlát csak azért van, hogy egy beragadt periféria ne fagyassza le az
// eszközt (lásd az adcCalibrateOffset() fölötti magyarázatot).
#define ZW_ADC_CALIB_TIMEOUT_MS 5

// Csatlakozás után mennyivel jelezzük a hostnak, hogy változhatott a GATT
// szolgáltatás-tábla (lásd az updateGattChanged() fölötti magyarázatot a
// .ino-ban), és hányszor próbáljuk meg. A késleltetés azért kell, mert a
// titkosítás felépülése és a mentett CCCD-k visszaállítása még folyamatban
// lehet közvetlenül a csatlakozás után — addig az indikáció nem menne ki.
#define ZW_GATT_CHANGED_DELAY_MS 3000
#define ZW_GATT_CHANGED_RETRY_MS 2000
#define ZW_GATT_CHANGED_TRIES 5

// Csatlakozás után ennyivel toljuk ki az új eszköznek az aktuális töltöttséget.
// A GATT-változás jelzése után legyen, hogy a host már az újrafelderített
// szolgáltatásra kapja meg. (A host magától is kiolvassa az értéket, ez csak
// azért van, hogy ne kelljen megvárnia a következő percenkénti frissítést.)
#define ZW_BATTERY_ANNOUNCE_MS 6000

// Ha egy gombot ennél tovább tartanak nyomva, az szinte biztosan fizikai
// beragadás: a leghosszabb értelmes tartás (hangerő, Alt+Tab ablakváltás)
// nagyságrendekkel rövidebb. A firmware ilyenkor figyelmen kívül hagyja a
// gombot, amíg fel nem engedik.
#define ZW_STUCK_BUTTON_MS 30000

#define ZW_MAX_HOLD_MS 5000
// Ennél rövidebb küldésnek nincs értelme (a rendes rövid impulzus 100 ms), és a
// konfiguráló program sem enged kisebbet – ha a firmware mégis elfogadná, a
// program utána nem tudná visszaküldeni a kiosztást.
#define ZW_MIN_HOLD_MS 50

#define ZW_KEYMAP_MAGIC 0x4B42575AUL  // "ZWBK"
// 3: a repeat mező bitmaszkká vált.
// 4: a KeyAction kiegészült a holdMs mezővel (rövid/dupla nyomás küldési
//    hossza), ezért a bejegyzés 8 helyett 10 bájt. A 2-es és 3-as mentések
//    továbbra is betölthetők: a firmware betöltéskor átalakítja őket
//    (lásd migrateKeymapV3, migrateKeymapV2toV3).
#define ZW_KEYMAP_VERSION 4
#define ZW_KEYMAP_MIN_VERSION 2

// Az eszköz és a Python konfiguráló program közti protokoll verziója.
// 7: új `BAT` parancs (akkumulátor-mérés diagnosztika).
#define ZW_PROTO_VERSION 7

// Üzemmódok. A sorszám egyben a keymap első indexe is.
// (Azért itt, és nem a .ino-ban: az Arduino a vázlat elejére generálja a
// prototípusokat, így az ezt visszaadó függvények különben nem fordulnának.)
enum uzemmod : uint8_t {
  normalUzemmod = 0,
  versenyEdzesUzemmod = 1,
  mediaVezerloUzemmod = 2
};

// Egy gombhoz tartozó esemény típusa.
enum ZwEvent : uint8_t {
  EV_CLICK = 0,   // rövid nyomás
  EV_DOUBLE = 1,  // dupla kattintás
  EV_LONG = 2     // hosszú nyomás
};

// Egy esemény hatására végrehajtott művelet típusa.
enum ZwActionType : uint8_t {
  ACT_NONE = 0,        // nincs művelet
  ACT_KEY = 1,         // normál billentyű (HID keycode + módosítók)
  ACT_CONSUMER = 2,    // média (consumer) billentyű, pl. play/pause
  ACT_MODE_NEXT = 3,   // váltás a következő üzemmódra
  ACT_VIEW_CYCLE = 4,  // Zwift nézetváltás: 1..9 körbe
  ACT_TYPE_COUNT       // csak érvényesség-ellenőrzéshez
};

// Egy (üzemmód, gomb, esemény) hármashoz tartozó művelet.
//
// Az ismétlés-beállítások mindhárom eseménynél ugyanazt jelentik, csak a
// küldés HOSSZÁT más adja meg:
//   EV_LONG            – amíg a gombot nyomva tartod (a holdMs nem játszik),
//   EV_CLICK/EV_DOUBLE – pontosan holdMs ideig (0 = a régi rövid impulzus).
struct __attribute__((packed)) KeyAction {
  uint8_t type;      // ZwActionType
  uint8_t modifier;  // KEYBOARD_MODIFIER_* bitmaszk (csak ACT_KEY esetén)
  uint16_t code;     // ACT_KEY: HID keycode, ACT_CONSUMER: consumer usage
  // ZW_REPEAT_* bitmaszk. A 0/1 érték a korábbi jelentésével egyezik, ezért a
  // régi mentések változatlanul betölthetők.
  uint8_t repeat;
  // Cél-eszköz felülbírálás: 0 = az üzemmód célpontja érvényes, egyébként
  // ZW_TARGET_* bitmaszk. (Korábban ez a bájt kihasználatlan volt és mindig
  // 0-ra íródott, ezért a régi mentések változtatás nélkül betölthetők.)
  uint8_t target;
  uint16_t repeatMs; // ismétlés két küldése közti idő (ms)
  // Csak EV_CLICK / EV_DOUBLE: meddig tartson a küldés (ms). 0 = a korábbi
  // viselkedés, azaz egyetlen rövid impulzus. Amíg tart, más gomb parancsa
  // nem mehet ki.
  uint16_t holdMs;
};

// Egy cél-eszköz azonosítása a BLE címe alapján. A kapcsolat-azonosítók
// (conn handle) csatlakozási sorrend szerint kapják az értéküket, ezért nem
// alkalmasak arra, hogy megjegyezzük, melyik a PC és melyik a telefon.
struct __attribute__((packed)) PeerSlot {
  uint8_t valid;     // 0 = üres fiók
  uint8_t addrType;  // ble_gap_addr_t.addr_type
  uint8_t addr[6];   // ble_gap_addr_t.addr (little endian, ahogy a SoftDevice adja)
};

// A 3-as (és korábbi) formátum bejegyzése: holdMs nélkül, 8 bájt. Csak a régi
// mentések betöltéséhez kell, lásd loadKeymap().
struct __attribute__((packed)) KeyActionV3 {
  uint8_t type;
  uint8_t modifier;
  uint16_t code;
  uint8_t repeat;
  uint8_t target;
  uint16_t repeatMs;
};

// A teljes, flash-be mentett konfiguráció.
struct __attribute__((packed)) KeymapConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t entrySize;  // sizeof(KeyAction) - formátum-ellenőrzéshez
  uint8_t modes;
  uint8_t buttons;
  uint8_t events;
  uint8_t slots;
  KeyAction map[ZW_NUM_MODES][ZW_NUM_BUTTONS][ZW_NUM_EVENTS];
  uint8_t modeTarget[ZW_NUM_MODES];  // üzemmódonként ZW_TARGET_* bitmaszk
  uint8_t reserved;
  PeerSlot peers[ZW_NUM_SLOTS];
  uint32_t crc;  // CRC32 a struktúra elejétől a crc mezőig
};

// A 3-as formátum teljes szerkezete — a mezők sorrendje azonos, csak a
// bejegyzések kisebbek. A régi mentés így beolvasható és átalakítható.
struct __attribute__((packed)) KeymapConfigV3 {
  uint32_t magic;
  uint16_t version;
  uint16_t entrySize;
  uint8_t modes;
  uint8_t buttons;
  uint8_t events;
  uint8_t slots;
  KeyActionV3 map[ZW_NUM_MODES][ZW_NUM_BUTTONS][ZW_NUM_EVENTS];
  uint8_t modeTarget[ZW_NUM_MODES];
  uint8_t reserved;
  PeerSlot peers[ZW_NUM_SLOTS];
  uint32_t crc;
};

#endif  // ZWIFT_CONFIG_H
