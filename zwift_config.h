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

#define ZW_KEYMAP_MAGIC 0x4B42575AUL  // "ZWBK"
#define ZW_KEYMAP_VERSION 1

// Az eszköz és a Python konfiguráló program közti protokoll verziója.
#define ZW_PROTO_VERSION 1

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
  uint8_t reserved;  // igazítás / későbbi bővítés
  uint16_t repeatMs; // ismétlés két küldése közti idő (ms)
};

// A teljes, flash-be mentett konfiguráció.
struct __attribute__((packed)) KeymapConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t entrySize;  // sizeof(KeyAction) - formátum-ellenőrzéshez
  uint8_t modes;
  uint8_t buttons;
  uint8_t events;
  uint8_t reserved;
  KeyAction map[ZW_NUM_MODES][ZW_NUM_BUTTONS][ZW_NUM_EVENTS];
  uint32_t crc;  // CRC32 a struktúra elejétől a crc mezőig
};

#endif  // ZWIFT_CONFIG_H
