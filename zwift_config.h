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

#define ZW_KEYMAP_MAGIC 0x4B42575AUL  // "ZWBK"
#define ZW_KEYMAP_VERSION 2

// Az eszköz és a Python konfiguráló program közti protokoll verziója.
#define ZW_PROTO_VERSION 3

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
struct __attribute__((packed)) KeyAction {
  uint8_t type;      // ZwActionType
  uint8_t modifier;  // KEYBOARD_MODIFIER_* bitmaszk (csak ACT_KEY esetén)
  uint16_t code;     // ACT_KEY: HID keycode, ACT_CONSUMER: consumer usage
  uint8_t repeat;    // csak EV_LONG: 1 = ismétlés nyomva tartás közben
  // Cél-eszköz felülbírálás: 0 = az üzemmód célpontja érvényes, egyébként
  // ZW_TARGET_* bitmaszk. (Korábban ez a bájt kihasználatlan volt és mindig
  // 0-ra íródott, ezért a régi mentések változtatás nélkül betölthetők.)
  uint8_t target;
  uint16_t repeatMs; // ismétlés két küldése közti idő (ms)
};

// Egy cél-eszköz azonosítása a BLE címe alapján. A kapcsolat-azonosítók
// (conn handle) csatlakozási sorrend szerint kapják az értéküket, ezért nem
// alkalmasak arra, hogy megjegyezzük, melyik a PC és melyik a telefon.
struct __attribute__((packed)) PeerSlot {
  uint8_t valid;     // 0 = üres fiók
  uint8_t addrType;  // ble_gap_addr_t.addr_type
  uint8_t addr[6];   // ble_gap_addr_t.addr (little endian, ahogy a SoftDevice adja)
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

#endif  // ZWIFT_CONFIG_H
