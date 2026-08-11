#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_SPIFlash.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>
#include <nrf52840.h>
#include <OneButton.h>
#include <bluefruit.h>
#include <TickTwo.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include "zwift_config.h"

using namespace Adafruit_LittleFS_Namespace;

#define FILENAME "/jelenlegiuzemmod.txt"
#define FILENAMETMP "/jelenlegiuzemmod.tmp"
#define KEYMAPFILE "/keymap.bin"
#define KEYMAPTMPFILE "/keymap.tmp"
#define CONTENTNormal "normalUzemmod"
#define CONTENTVerseny "versenyEdzesUzemmod"
#define CONTENTMedia "mediaVezerloUzemmod"


Adafruit_LittleFS_Namespace::File file(InternalFS);

void fct_Watchdog();

BLEDis bledis;
BLEHidAdafruit blehid;

const int ledPin[] = { 11, 12, 13 };  //red, blue, green
const int pin_charging_current = 22;  //mekkora árammal töltsön
const int numOfLeds = sizeof(ledPin) / sizeof(ledPin[0]);
const int BUTTON_PIN[5] = { 0, 1, 2, 3, 4 };  // A gombokhoz csatlakoztatott tüskék
const int numOfButtons = sizeof(BUTTON_PIN) / sizeof(BUTTON_PIN[0]);
bool hasKeyPressed = false;
bool hasConsumerKeyPressed = false;
unsigned long keyPressMillis = 0;
const int keyReleaseDelay = 100; // ms delay before key release (configurable)
int offDelay = 900; //sleep delay
int nezet = 0;
bool duringLongpress = false;
String taroltUzemmod;

// Élő BLE kapcsolatok. A csatlakozás sorrendjében töltődik, a hozzárendelés a
// PC/telefon fiókokhoz a peer BLE címe alapján történik (lásd slotOfConn).
static uint16_t connHandles[ZW_MAX_CONNECTIONS];

// Melyik kapcsolatokra ment ki a legutóbbi lenyomás. A felengedést pontosan
// ezekre kell elküldeni, nem az összes élő kapcsolatra.
static uint16_t pressedTargets[ZW_MAX_CONNECTIONS];
static uint8_t pressedTargetCount = 0;

// Nyomva tartás közbeni ismétlés állapota
static int8_t repeatButton = -1;       // melyik gomb ismétel éppen (-1 = egyik sem)
static bool repeatDue = false;         // az első ismétlés azonnal menjen ki
static unsigned long lastRepeatMillis = 0;

// Soros (USB) konfigurációs protokoll
static char cmdBuf[96];
static uint8_t cmdLen = 0;
static bool cmdOverflow = false;
static bool debugSerial = true;  // a "DBG 0" paranccsal kikapcsolható

// A gomb-kiosztás a RAM-ban; a flash-ből töltődik, ott is mentődik.
KeymapConfig keymap;

OneButton button1(BUTTON_PIN[1], true);
OneButton button2(BUTTON_PIN[2], true);
OneButton button3(BUTTON_PIN[3], true);
OneButton button4(BUTTON_PIN[4], true);
OneButton button5(BUTTON_PIN[0], true);

#define WAKEUP_PIN 2

TickTwo watchDOG(fct_Watchdog, 1000, 0, MILLIS);

#define watchdogMinCounter 0  // reset value for watchdog counter
static uint32_t watchdogCounter = watchdogMinCounter;

Adafruit_FlashTransport_QSPI flashTransport;

enum uzemmod {
  normalUzemmod,
  versenyEdzesUzemmod,
  mediaVezerloUzemmod
};

uzemmod jelenlegiUzemmod = normalUzemmod;


void QSPIF_sleep(void) {
  flashTransport.begin();
  flashTransport.runCommand(0xB9);
  flashTransport.end();
}

void setup() {
  Serial.begin(115200);

  delay(500);

  Serial.println("Reboot.");

  InternalFS.begin();

  file.open(FILENAME, FILE_O_READ);

  // file existed
  if (file) {
    Serial.println(FILENAME " file exists");

    uint32_t readlen;
    char buffer[64] = { 0 };
    readlen = file.read(buffer, sizeof(buffer) - 1);

    buffer[readlen] = 0;
    Serial.println(buffer);
    taroltUzemmod = buffer;
    file.close();
  } else {
    Serial.print("Nincs " FILENAME ", letrehozas alapertelmezessel ... ");
    Serial.println(writeFileAtomic(FILENAME, FILENAMETMP, CONTENTNormal,
                                   strlen(CONTENTNormal))
                     ? "OK"
                     : "Failed!");
  }

  if (taroltUzemmod == CONTENTNormal) {
    jelenlegiUzemmod = normalUzemmod;
  } else if (taroltUzemmod == CONTENTVerseny) {
    jelenlegiUzemmod = versenyEdzesUzemmod;
  } else if (taroltUzemmod == CONTENTMedia) {
    jelenlegiUzemmod = mediaVezerloUzemmod;
  } else {
    Serial.println("Ismeretlen uzemmod, default: normalUzemmod");
    jelenlegiUzemmod = normalUzemmod;
  }

  // Gomb-kiosztás betöltése a flash-ből (ha nincs vagy sérült: gyári alapértelmezés)
  if (!loadKeymap()) {
    Serial.println("Nincs ervenyes keymap, gyari alapertelmezes betoltve.");
    loadDefaultKeymap();
  }

  Serial.println("Done");

  watchDOG.start();
  for (int i = 0; i < numOfLeds; i++) {  //ledek
    pinMode(ledPin[i], OUTPUT);
  }
  for (int i = 0; i < numOfButtons; i++) {
    pinMode(BUTTON_PIN[i], INPUT_PULLUP);
  }
  pinMode(WAKEUP_PIN, INPUT_PULLUP_SENSE);
  pinMode(pin_charging_current, OUTPUT);  //charging current
  digitalWrite(pin_charging_current, LOW);  //toltes alacsony árammal

  NRF_POWER->DCDCEN = 1;

  // link the button 1 functions.
  button1.attachClick(click1);
  button1.attachDoubleClick(doubleclick1);
  button1.attachLongPressStart(longPressStart1);
  button1.attachLongPressStop(longPressStop1);
  button1.attachDuringLongPress(longPress1);

  // link the button 2 functions.
  button2.attachClick(click2);
  button2.attachDoubleClick(doubleclick2);
  button2.attachLongPressStart(longPressStart2);
  button2.attachLongPressStop(longPressStop2);
  button2.attachDuringLongPress(longPress2);

  // link the button 3 functions.
  button3.attachClick(click3);
  button3.attachDoubleClick(doubleclick3);
  button3.attachLongPressStart(longPressStart3);
  button3.attachLongPressStop(longPressStop3);
  button3.attachDuringLongPress(longPress3);

  // link the button 4 functions.
  button4.attachClick(click4);
  button4.attachDoubleClick(doubleclick4);
  button4.attachLongPressStart(longPressStart4);
  button4.attachLongPressStop(longPressStop4);
  button4.attachDuringLongPress(longPress4);

  // link the button 5 functions.
  button5.attachClick(click5);
  button5.attachDoubleClick(doubleclick5);
  button5.attachLongPressStart(longPressStart5);
  button5.attachLongPressStop(longPressStop5);
  button5.attachDuringLongPress(longPress5);

  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    connHandles[i] = BLE_CONN_HANDLE_INVALID;
  }

  Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16);
  Bluefruit.begin(ZW_MAX_CONNECTIONS, 0);
  Bluefruit.setTxPower(4);
  Bluefruit.autoConnLed(false);
  Bluefruit.setName("SEEED_ZWIFT");
  bledis.setManufacturer("Manszabi");
  bledis.setModel("ZWIFT_button");
  bledis.begin();
  blehid.begin();
  Bluefruit.Periph.setConnInterval(9, 12);
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
  startAdv();
}

void loop() {

  uzemmod elozoUzemmod = jelenlegiUzemmod;

  for (int i = 0; i < numOfLeds; i++) {
    digitalWrite(ledPin[i], HIGH);
  }
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      digitalWrite(ledPin[0], LOW);
      break;
    case versenyEdzesUzemmod:
      digitalWrite(ledPin[1], LOW);
      break;
    case mediaVezerloUzemmod:
      digitalWrite(ledPin[2], LOW);
      break;
    default:
      break;
  }

  updateButtons();
  handleSerial();

  if ((hasKeyPressed || hasConsumerKeyPressed) && keyPressMillis == 0) {
    keyPressMillis = millis();
  }

  if (Bluefruit.connected()) {
    unsigned long currentMillis = millis();

    if (!duringLongpress && keyPressMillis > 0 && (currentMillis - keyPressMillis >= keyReleaseDelay)) {
      releasePressedKeys(hasKeyPressed, hasConsumerKeyPressed);
      if (hasKeyPressed) hasKeyPressed = false;
      if (hasConsumerKeyPressed) hasConsumerKeyPressed = false;
      pressedTargetCount = 0;
      keyPressMillis = 0;
    }
  }

  if (elozoUzemmod != jelenlegiUzemmod) {
    Serial.println("A jelenlegiUzemmod értéke megváltozott.");
    if (jelenlegiUzemmod == normalUzemmod) saveUzemmod(CONTENTNormal);
    else if (jelenlegiUzemmod == versenyEdzesUzemmod) saveUzemmod(CONTENTVerseny);
    else if (jelenlegiUzemmod == mediaVezerloUzemmod) saveUzemmod(CONTENTMedia);
  }

  watchDOG.update();
  delay(20);
}


uint8_t checkForSoftDevice() {
  uint8_t check;
  sd_softdevice_is_enabled(&check);
  return check;
}

static void disconnectBle() {
  uint16_t connections = Bluefruit.connected();
  for (uint16_t conn = 0; conn < connections; conn++) {
    Bluefruit.disconnect(conn);
  }
}

void connect_callback(uint16_t conn_handle) {
  uint8_t used = 0;
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (connHandles[i] == BLE_CONN_HANDLE_INVALID) {
      connHandles[i] = conn_handle;
      break;
    }
  }
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (connHandles[i] != BLE_CONN_HANDLE_INVALID) used++;
  }

  Serial.print("BLE csatlakozott, conn_hdl=");
  Serial.print(conn_handle);
  Serial.print(" (");
  Serial.print(used);
  Serial.print("/");
  Serial.print(ZW_MAX_CONNECTIONS);
  Serial.println(")");

  // Amíg van szabad hely, hirdessük magunkat tovább, hogy a másik eszköz is
  // be tudjon csatlakozni. A SoftDevice a csatlakozáskor leállítja a hirdetést.
  if (used < ZW_MAX_CONNECTIONS) {
    Bluefruit.Advertising.start(0);
  }
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)reason;
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (connHandles[i] == conn_handle) connHandles[i] = BLE_CONN_HANDLE_INVALID;
  }
  Serial.print("BLE bontva, conn_hdl=");
  Serial.println(conn_handle);

  hasKeyPressed = false;
  hasConsumerKeyPressed = false;
  duringLongpress = false;
  repeatButton = -1;
  keyPressMillis = 0;
  pressedTargetCount = 0;
}

// Biztonságos fájlírás: előbb ideiglenes fájlba írunk, és csak hibátlan kiírás
// után cseréljük le a meglévőt egy atomikus átnevezéssel. Így egy félbemaradt
// mentés nem teszi tönkre a korábbi tartalmat.
bool writeFileAtomic(const char* path, const char* tmpPath,
                     const void* data, size_t len) {
  const int maxRetries = 3;
  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    InternalFS.remove(tmpPath);
    Adafruit_LittleFS_Namespace::File f(InternalFS);
    if (f.open(tmpPath, FILE_O_WRITE)) {
      size_t written = f.write((const uint8_t*)data, len);
      f.close();
      if (written == len) {
        if (InternalFS.rename(tmpPath, path)) return true;
        // Ha a felülírásos átnevezés nem megy, előbb töröljük a régit.
        InternalFS.remove(path);
        if (InternalFS.rename(tmpPath, path)) return true;
      }
    }
    delay(50);
  }
  InternalFS.remove(tmpPath);
  return false;
}

void saveUzemmod(const char* content) {
  if (writeFileAtomic(FILENAME, FILENAMETMP, content, strlen(content))) {
    Serial.print("Uzemmod elmentve: ");
    Serial.println(content);
  } else {
    Serial.println("saveUzemmod: a mentes nem sikerult, a korabbi ertek megmaradt!");
  }
}

void ble_sleep(void) {
  Bluefruit.Advertising.restartOnDisconnect(false);
  disconnectBle();
  Bluefruit.Advertising.stop();
}

void fct_powerdown() {
  ble_sleep();
  InternalFS.end();
  for (int i = 0; i < numOfLeds; i++) {
    digitalWrite(ledPin[i], HIGH);
  }
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[WAKEUP_PIN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  QSPIF_sleep();
  if (checkForSoftDevice() == 1) {
    // SoftDevice enabled
    sd_power_system_off();
  } else {
    // No SoftDevice
    NRF_POWER->SYSTEMOFF = 1;
  }
}

void fct_Watchdog() {
  watchdogCounter++;
  if (watchdogCounter == (uint32_t)offDelay) {
    fct_powerdown();
  }
}

void fct_WatchdogReset() {
  watchdogCounter = watchdogMinCounter;
}

void updateButtons() {
  button1.tick();
  button2.tick();
  button3.tick();
  button4.tick();
  button5.tick();
}

void startAdv(void) {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);
  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 480);  // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);     // number of seconds in fast mode
  Bluefruit.Advertising.start(0);               // 0 = Don't stop advertising after n seconds
}


// ---------------------------------------------------------------------------
// Gomb-kiosztás (keymap) kezelése
// ---------------------------------------------------------------------------

static void setAction(uint8_t mode, uint8_t btn, uint8_t evt,
                      uint8_t type, uint8_t modifier, uint16_t code,
                      uint8_t repeat, uint16_t repeatMs) {
  KeyAction& a = keymap.map[mode][btn][evt];
  a.type = type;
  a.modifier = modifier;
  a.code = code;
  a.repeat = repeat;
  a.reserved = 0;
  a.repeatMs = repeatMs;
}

// A gyári kiosztás: ugyanaz, ami korábban be volt drótozva a kódba.
void loadDefaultKeymap() {
  // A cél-eszköz hozzárendelés (melyik a PC, melyik a telefon) túléli a gyári
  // visszaállítást: az a fizikai eszközökről szól, nem a gomb-kiosztásról.
  PeerSlot savedPeers[ZW_NUM_SLOTS];
  memcpy(savedPeers, keymap.peers, sizeof(savedPeers));

  memset(&keymap, 0, sizeof(keymap));
  memcpy(keymap.peers, savedPeers, sizeof(savedPeers));
  keymap.magic = ZW_KEYMAP_MAGIC;
  keymap.version = ZW_KEYMAP_VERSION;
  keymap.entrySize = sizeof(KeyAction);
  keymap.modes = ZW_NUM_MODES;
  keymap.buttons = ZW_NUM_BUTTONS;
  keymap.events = ZW_NUM_EVENTS;
  keymap.slots = ZW_NUM_SLOTS;

  // Cél-eszközök üzemmódonként: a Zwift vezérlés a PC-re megy, a média
  // vezérlés mindkét eszközre. A fiók-hozzárendelés (melyik a PC, melyik a
  // telefon) párosítás után a konfiguráló programban állítható be.
  keymap.modeTarget[0] = ZW_TARGET_PC;   // Normál (Zwift)
  keymap.modeTarget[1] = ZW_TARGET_PC;   // Verseny / edzés
  keymap.modeTarget[2] = ZW_TARGET_ALL;  // Média vezérlő

  const uint8_t MOD_GUI_ALT = KEYBOARD_MODIFIER_LEFTGUI | KEYBOARD_MODIFIER_LEFTALT;

  // --- Normál üzemmód (Zwift) ---
  setAction(0, 0, EV_CLICK, ACT_KEY, 0, HID_KEY_ARROW_LEFT, 0, 0);
  setAction(0, 0, EV_DOUBLE, ACT_KEY, 0, HID_KEY_F9, 0, 0);
  setAction(0, 0, EV_LONG, ACT_KEY, MOD_GUI_ALT, HID_KEY_R, 0, 0);

  setAction(0, 1, EV_CLICK, ACT_KEY, 0, HID_KEY_ENTER, 0, 0);
  setAction(0, 1, EV_DOUBLE, ACT_KEY, 0, HID_KEY_ESCAPE, 0, 0);
  setAction(0, 1, EV_LONG, ACT_KEY, 0, HID_KEY_H, 0, 0);

  setAction(0, 2, EV_CLICK, ACT_KEY, 0, HID_KEY_ARROW_RIGHT, 0, 0);
  setAction(0, 2, EV_DOUBLE, ACT_KEY, 0, HID_KEY_F10, 0, 0);
  setAction(0, 2, EV_LONG, ACT_KEY, MOD_GUI_ALT, HID_KEY_G, 0, 0);

  setAction(0, 3, EV_CLICK, ACT_KEY, 0, HID_KEY_ARROW_DOWN, 0, 0);
  setAction(0, 3, EV_DOUBLE, ACT_MODE_NEXT, 0, 0, 0, 0);
  setAction(0, 3, EV_LONG, ACT_KEY, 0, HID_KEY_ARROW_DOWN, 1, 30);

  setAction(0, 4, EV_CLICK, ACT_KEY, 0, HID_KEY_ARROW_UP, 0, 0);
  setAction(0, 4, EV_DOUBLE, ACT_VIEW_CYCLE, 0, 0, 0, 0);
  setAction(0, 4, EV_LONG, ACT_KEY, 0, HID_KEY_T, 0, 0);

  // --- Verseny / edzés üzemmód ---
  setAction(1, 0, EV_CLICK, ACT_KEY, 0, HID_KEY_PAGE_DOWN, 0, 0);
  setAction(1, 0, EV_DOUBLE, ACT_KEY, 0, HID_KEY_KEYPAD_SUBTRACT, 0, 0);
  setAction(1, 0, EV_LONG, ACT_KEY, 0, HID_KEY_ARROW_LEFT, 0, 0);

  setAction(1, 1, EV_CLICK, ACT_KEY, 0, HID_KEY_SPACE, 0, 0);
  setAction(1, 1, EV_DOUBLE, ACT_KEY, 0, HID_KEY_TAB, 0, 0);
  setAction(1, 1, EV_LONG, ACT_KEY, 0, HID_KEY_ENTER, 0, 0);

  setAction(1, 2, EV_CLICK, ACT_KEY, 0, HID_KEY_PAGE_UP, 0, 0);
  setAction(1, 2, EV_DOUBLE, ACT_KEY, 0, HID_KEY_KEYPAD_ADD, 0, 0);
  setAction(1, 2, EV_LONG, ACT_KEY, 0, HID_KEY_ARROW_RIGHT, 0, 0);

  setAction(1, 3, EV_CLICK, ACT_KEY, 0, HID_KEY_G, 0, 0);
  setAction(1, 3, EV_DOUBLE, ACT_MODE_NEXT, 0, 0, 0, 0);
  setAction(1, 3, EV_LONG, ACT_KEY, 0, HID_KEY_ARROW_DOWN, 1, 70);

  setAction(1, 4, EV_CLICK, ACT_KEY, 0, HID_KEY_E, 0, 0);
  setAction(1, 4, EV_DOUBLE, ACT_VIEW_CYCLE, 0, 0, 0, 0);
  setAction(1, 4, EV_LONG, ACT_KEY, 0, HID_KEY_ARROW_UP, 0, 0);

  // --- Média vezérlő üzemmód ---
  setAction(2, 0, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_SCAN_PREVIOUS, 0, 0);
  setAction(2, 0, EV_DOUBLE, ACT_KEY, 0, HID_KEY_F9, 0, 0);
  setAction(2, 0, EV_LONG, ACT_KEY, MOD_GUI_ALT, HID_KEY_R, 0, 0);

  setAction(2, 1, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_PLAY_PAUSE, 0, 0);
  setAction(2, 1, EV_DOUBLE, ACT_KEY, 0, HID_KEY_ESCAPE, 0, 0);
  setAction(2, 1, EV_LONG, ACT_KEY, KEYBOARD_MODIFIER_LEFTALT, HID_KEY_TAB, 0, 0);

  setAction(2, 2, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_SCAN_NEXT, 0, 0);
  setAction(2, 2, EV_DOUBLE, ACT_KEY, 0, HID_KEY_F10, 0, 0);
  setAction(2, 2, EV_LONG, ACT_KEY, MOD_GUI_ALT, HID_KEY_G, 0, 0);

  setAction(2, 3, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_MUTE, 0, 0);
  setAction(2, 3, EV_DOUBLE, ACT_MODE_NEXT, 0, 0, 0, 0);
  setAction(2, 3, EV_LONG, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_VOLUME_DECREMENT, 1, 70);

  setAction(2, 4, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_AL_CONSUMER_CONTROL_CONFIGURATION, 0, 0);
  setAction(2, 4, EV_DOUBLE, ACT_VIEW_CYCLE, 0, 0, 0, 0);
  setAction(2, 4, EV_LONG, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_VOLUME_INCREMENT, 1, 70);
}

static uint32_t zwCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}

static uint32_t keymapCrc(const KeymapConfig& cfg) {
  return zwCrc32((const uint8_t*)&cfg, sizeof(KeymapConfig) - sizeof(uint32_t));
}

// Betöltés a belső fájlrendszerből. false = nincs vagy érvénytelen.
bool loadKeymap() {
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (!f.open(KEYMAPFILE, FILE_O_READ)) {
    return false;
  }
  KeymapConfig tmp;
  uint32_t readlen = f.read((uint8_t*)&tmp, sizeof(tmp));
  f.close();

  if (readlen != sizeof(tmp)) {
    Serial.println("keymap: hibas fajlmeret");
    return false;
  }
  if (tmp.magic != ZW_KEYMAP_MAGIC || tmp.version != ZW_KEYMAP_VERSION
      || tmp.entrySize != sizeof(KeyAction)
      || tmp.modes != ZW_NUM_MODES || tmp.buttons != ZW_NUM_BUTTONS
      || tmp.events != ZW_NUM_EVENTS || tmp.slots != ZW_NUM_SLOTS) {
    Serial.println("keymap: ismeretlen formatum");
    return false;
  }
  if (keymapCrc(tmp) != tmp.crc) {
    Serial.println("keymap: CRC hiba");
    return false;
  }
  memcpy(&keymap, &tmp, sizeof(keymap));
  Serial.println("keymap: betoltve a flash-bol");
  return true;
}

// Mentés a belső fájlrendszerbe.
bool saveKeymap() {
  keymap.magic = ZW_KEYMAP_MAGIC;
  keymap.version = ZW_KEYMAP_VERSION;
  keymap.entrySize = sizeof(KeyAction);
  keymap.modes = ZW_NUM_MODES;
  keymap.buttons = ZW_NUM_BUTTONS;
  keymap.events = ZW_NUM_EVENTS;
  keymap.slots = ZW_NUM_SLOTS;
  keymap.reserved = 0;
  keymap.crc = keymapCrc(keymap);

  return writeFileAtomic(KEYMAPFILE, KEYMAPTMPFILE, &keymap, sizeof(keymap));
}


// ---------------------------------------------------------------------------
// Műveletek végrehajtása
// ---------------------------------------------------------------------------

static const KeyAction& currentAction(uint8_t btn, uint8_t evt) {
  return keymap.map[(uint8_t)jelenlegiUzemmod][btn][evt];
}

// Egy élő kapcsolat melyik cél-fiókba tartozik? -1 = nincs hozzárendelve.
static int8_t slotOfConn(uint16_t conn_hdl) {
  BLEConnection* conn = Bluefruit.Connection(conn_hdl);
  if (conn == NULL) return -1;
  ble_gap_addr_t peer = conn->getPeerAddr();
  for (uint8_t s = 0; s < ZW_NUM_SLOTS; s++) {
    if (!keymap.peers[s].valid) continue;
    if (memcmp(keymap.peers[s].addr, peer.addr, 6) == 0) return (int8_t)s;
  }
  return -1;
}

static bool anySlotAssigned() {
  for (uint8_t s = 0; s < ZW_NUM_SLOTS; s++) {
    if (keymap.peers[s].valid) return true;
  }
  return false;
}

// Összegyűjti azokat az élő kapcsolatokat, amelyekre a maszk szerint küldeni
// kell. Ha még egyetlen fiók sincs hozzárendelve, minden kapcsolatra küld —
// így az eszköz párosítás után azonnal használható, hozzárendelés nélkül is.
static uint8_t collectTargets(uint8_t mask, uint16_t* out) {
  uint8_t n = 0;
  bool assigned = anySlotAssigned();
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    uint16_t h = connHandles[i];
    if (h == BLE_CONN_HANDLE_INVALID) continue;
    if (!Bluefruit.connected(h)) continue;
    if (assigned) {
      int8_t slot = slotOfConn(h);
      if (slot < 0) continue;                    // ismeretlen eszköz
      if (!(mask & (1 << (uint8_t)slot))) continue;  // nem ez a célpont
    }
    out[n++] = h;
  }
  return n;
}

static uint8_t currentTargetMask() {
  return keymap.modeTarget[(uint8_t)jelenlegiUzemmod];
}

static void sendKeyboard(uint8_t modifier, uint8_t keycode) {
  if (hasKeyPressed || hasConsumerKeyPressed) return;
  uint16_t targets[ZW_MAX_CONNECTIONS];
  uint8_t n = collectTargets(currentTargetMask(), targets);
  if (n == 0) return;

  uint8_t keycodes[6] = { keycode, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
  for (uint8_t i = 0; i < n; i++) {
    blehid.keyboardReport(targets[i], modifier, keycodes);
    pressedTargets[i] = targets[i];
  }
  pressedTargetCount = n;
  hasKeyPressed = true;
  delay(5);
}

static void sendConsumer(uint16_t usage) {
  if (hasKeyPressed || hasConsumerKeyPressed) return;
  uint16_t targets[ZW_MAX_CONNECTIONS];
  uint8_t n = collectTargets(currentTargetMask(), targets);
  if (n == 0) return;

  for (uint8_t i = 0; i < n; i++) {
    blehid.consumerKeyPress(targets[i], usage);
    pressedTargets[i] = targets[i];
  }
  pressedTargetCount = n;
  hasConsumerKeyPressed = true;
  delay(5);
}

// A lenyomás pontosan azokra a kapcsolatokra volt kiküldve, amiket a
// pressedTargets tárol — a felengedést is ezekre kell elküldeni.
void releasePressedKeys(bool keyboard, bool consumer) {
  for (uint8_t i = 0; i < pressedTargetCount; i++) {
    uint16_t h = pressedTargets[i];
    if (!Bluefruit.connected(h)) continue;
    if (keyboard) blehid.keyRelease(h);
    if (consumer) blehid.consumerKeyRelease(h);
  }
}

// Egyszeri művelet (rövid / dupla / nem ismétlődő hosszú nyomás).
static void fireAction(uint8_t btn, uint8_t evt) {
  const KeyAction& a = currentAction(btn, evt);
  switch (a.type) {
    case ACT_KEY:
      sendKeyboard(a.modifier, (uint8_t)a.code);
      break;

    case ACT_CONSUMER:
      sendConsumer(a.code);
      break;

    case ACT_MODE_NEXT:
      jelenlegiUzemmod = (uzemmod)(((uint8_t)jelenlegiUzemmod + 1) % ZW_NUM_MODES);
      break;

    case ACT_VIEW_CYCLE: {
      // A számlálót csak akkor léptetjük, ha a billentyű tényleg kimegy,
      // különben elcsúszna a Zwift-ben ténylegesen beállított nézettől.
      if (!Bluefruit.connected() || hasKeyPressed || hasConsumerKeyPressed) break;
      if (nezet >= 9) nezet = 0;
      nezet++;
      static const uint8_t HID_KEYS[9] = { HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4, HID_KEY_5,
                                           HID_KEY_6, HID_KEY_7, HID_KEY_8, HID_KEY_9 };
      sendKeyboard(0, HID_KEYS[nezet - 1]);
      break;
    }

    case ACT_NONE:
    default:
      break;
  }
}

// Nyomva tartás közbeni ismétlés: a billentyű nyomva marad, a felengedést
// a longPressStop bízza a főciklusra.
static void sendRepeat(const KeyAction& a) {
  if (a.type != ACT_KEY && a.type != ACT_CONSUMER) return;

  uint16_t targets[ZW_MAX_CONNECTIONS];
  uint8_t n = collectTargets(currentTargetMask(), targets);
  if (n == 0) return;

  for (uint8_t i = 0; i < n; i++) {
    if (a.type == ACT_KEY) {
      uint8_t keycodes[6] = { (uint8_t)a.code, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
      blehid.keyboardReport(targets[i], a.modifier, keycodes);
    } else {
      blehid.consumerKeyPress(targets[i], a.code);
    }
    pressedTargets[i] = targets[i];
  }
  pressedTargetCount = n;
  hasKeyPressed = false;
  hasConsumerKeyPressed = false;
  duringLongpress = true;
}

static bool isRepeating(const KeyAction& a) {
  return a.repeat && (a.type == ACT_KEY || a.type == ACT_CONSUMER);
}

// Folyamatban lévő ismétlés lezárása. Mindkét jelzőt beállítjuk, hogy a
// főciklus a billentyűzet- és a média-billentyűt is felengedje: nyomva tartás
// közben a kettő közül bármelyik lehetett az utolsó kiküldött esemény.
static void finishRepeat() {
  repeatButton = -1;
  repeatDue = false;
  duringLongpress = false;
  hasKeyPressed = true;
  hasConsumerKeyPressed = true;
}

static void onClick(uint8_t btn) {
  if (debugSerial) { Serial.print("Button "); Serial.print(btn + 1); Serial.println(" click."); }
  fct_WatchdogReset();
  fireAction(btn, EV_CLICK);
}

static void onDoubleClick(uint8_t btn) {
  if (debugSerial) { Serial.print("Button "); Serial.print(btn + 1); Serial.println(" doubleclick."); }
  fct_WatchdogReset();
  fireAction(btn, EV_DOUBLE);
}

static void onLongStart(uint8_t btn) {
  if (debugSerial) { Serial.print("Button "); Serial.print(btn + 1); Serial.println(" longPress start"); }
  fct_WatchdogReset();
  const KeyAction& a = currentAction(btn, EV_LONG);
  if (isRepeating(a)) {
    // Ha egy másik gomb ismétlése volt folyamatban (két gomb egyszerre
    // nyomva), azt előbb rendesen lezárjuk, hogy ne ragadjon be a billentyű.
    if (repeatButton >= 0 && repeatButton != (int8_t)btn) {
      finishRepeat();
    }
    repeatButton = (int8_t)btn;
    repeatDue = true;  // az első ismétlés azonnal menjen ki
    lastRepeatMillis = millis();
  } else {
    fireAction(btn, EV_LONG);
  }
}

static void onLongDuring(uint8_t btn) {
  fct_WatchdogReset();
  if (repeatButton != (int8_t)btn) return;
  const KeyAction& a = currentAction(btn, EV_LONG);
  unsigned long now = millis();
  if (!repeatDue && (now - lastRepeatMillis) < a.repeatMs) return;
  repeatDue = false;
  lastRepeatMillis = now;
  sendRepeat(a);
}

static void onLongStop(uint8_t btn) {
  if (debugSerial) { Serial.print("Button "); Serial.print(btn + 1); Serial.println(" longPress stop"); }
  fct_WatchdogReset();
  if (repeatButton != (int8_t)btn) return;
  // A főciklus a keyReleaseDelay letelte után engedi fel a billentyűt.
  finishRepeat();
}

// A OneButton csak paraméter nélküli függvényeket fogad, ezért gombonként
// külön kis becsomagoló függvények hívják a közös kezelőket.
void click1() { onClick(0); }
void doubleclick1() { onDoubleClick(0); }
void longPressStart1() { onLongStart(0); }
void longPress1() { onLongDuring(0); }
void longPressStop1() { onLongStop(0); }

void click2() { onClick(1); }
void doubleclick2() { onDoubleClick(1); }
void longPressStart2() { onLongStart(1); }
void longPress2() { onLongDuring(1); }
void longPressStop2() { onLongStop(1); }

void click3() { onClick(2); }
void doubleclick3() { onDoubleClick(2); }
void longPressStart3() { onLongStart(2); }
void longPress3() { onLongDuring(2); }
void longPressStop3() { onLongStop(2); }

void click4() { onClick(3); }
void doubleclick4() { onDoubleClick(3); }
void longPressStart4() { onLongStart(3); }
void longPress4() { onLongDuring(3); }
void longPressStop4() { onLongStop(3); }

void click5() { onClick(4); }
void doubleclick5() { onDoubleClick(4); }
void longPressStart5() { onLongStart(4); }
void longPress5() { onLongDuring(4); }
void longPressStop5() { onLongStop(4); }


// ---------------------------------------------------------------------------
// Soros (USB) konfigurációs protokoll
//
// Sor alapú, ASCII. Minden parancs '\n'-nel zárul, minden válasz egy sor.
//
//   PING                                        -> OK ZWIFT_BUTTONS PROTO=1 ...
//   GET                                         -> MAP ... (45 sor) + END
//   SET <m> <b> <e> <t> <mod> <code> <rep> <ms> -> OK
//   SAVE                                        -> OK SAVED | ERR SAVE
//   LOAD                                        -> OK LOADED | ERR LOAD
//   DEFAULTS                                    -> OK DEFAULTS
//   MODE [n]                                    -> OK MODE <n>
//   DBG <0|1>                                   -> OK DBG <n>
//
//   GET valasza a MAP sorok utan uzemmodonkent egy TARGET <m> <maszk> sort is
//   tartalmaz (maszk: 1 = PC, 2 = telefon, 3 = mindketto).
//
//   SETTARGET <m> <maszk>                       -> OK
//   PEERS                                       -> SLOT/CONN sorok + END
//   ASSIGN <slot> <conn_hdl>                    -> OK
//   CLEARSLOT <slot>                            -> OK
// ---------------------------------------------------------------------------

static void printMapLine(uint8_t m, uint8_t b, uint8_t e) {
  const KeyAction& a = keymap.map[m][b][e];
  char line[64];
  snprintf(line, sizeof(line), "MAP %u %u %u %u %u %u %u %u",
           (unsigned)m, (unsigned)b, (unsigned)e,
           (unsigned)a.type, (unsigned)a.modifier, (unsigned)a.code,
           (unsigned)a.repeat, (unsigned)a.repeatMs);
  Serial.println(line);
}

static void cmdGet() {
  for (uint8_t m = 0; m < ZW_NUM_MODES; m++) {
    for (uint8_t b = 0; b < ZW_NUM_BUTTONS; b++) {
      for (uint8_t e = 0; e < ZW_NUM_EVENTS; e++) {
        printMapLine(m, b, e);
      }
    }
  }
  for (uint8_t m = 0; m < ZW_NUM_MODES; m++) {
    char line[32];
    snprintf(line, sizeof(line), "TARGET %u %u", (unsigned)m, (unsigned)keymap.modeTarget[m]);
    Serial.println(line);
  }
  Serial.println("END");
}

// A BLE címet a szokásos, megjelenítési sorrendben írjuk ki (MSB elöl), mert a
// telefon és a Windows is így mutatja.
static void formatAddr(const uint8_t* addr, char* out, size_t outLen) {
  snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X",
           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static void cmdPeers() {
  char addrStr[20];
  for (uint8_t s = 0; s < ZW_NUM_SLOTS; s++) {
    char line[64];
    if (keymap.peers[s].valid) {
      formatAddr(keymap.peers[s].addr, addrStr, sizeof(addrStr));
    } else {
      strcpy(addrStr, "-");
    }
    snprintf(line, sizeof(line), "SLOT %u %u %s",
             (unsigned)s, (unsigned)keymap.peers[s].valid, addrStr);
    Serial.println(line);
  }
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    uint16_t h = connHandles[i];
    if (h == BLE_CONN_HANDLE_INVALID || !Bluefruit.connected(h)) continue;
    BLEConnection* conn = Bluefruit.Connection(h);
    if (conn == NULL) continue;
    ble_gap_addr_t peer = conn->getPeerAddr();
    formatAddr(peer.addr, addrStr, sizeof(addrStr));
    char line[64];
    snprintf(line, sizeof(line), "CONN %u %s %d",
             (unsigned)h, addrStr, (int)slotOfConn(h));
    Serial.println(line);
  }
  Serial.println("END");
}

static void cmdSetTarget(const char* args) {
  unsigned m, mask;
  if (sscanf(args, "%u %u", &m, &mask) != 2) {
    Serial.println("ERR ARGS");
    return;
  }
  if (m >= ZW_NUM_MODES) {
    Serial.println("ERR RANGE");
    return;
  }
  if (mask == 0 || mask > ZW_TARGET_ALL) {
    Serial.println("ERR VALUE");
    return;
  }
  keymap.modeTarget[m] = (uint8_t)mask;
  Serial.println("OK");
}

// ASSIGN <slot> <conn_hdl>: az adott élő kapcsolat BLE címét a fiókhoz köti.
static void cmdAssign(const char* args) {
  unsigned s, h;
  if (sscanf(args, "%u %u", &s, &h) != 2) {
    Serial.println("ERR ARGS");
    return;
  }
  if (s >= ZW_NUM_SLOTS) {
    Serial.println("ERR RANGE");
    return;
  }
  if (!Bluefruit.connected((uint16_t)h)) {
    Serial.println("ERR NOTCONNECTED");
    return;
  }
  BLEConnection* conn = Bluefruit.Connection((uint16_t)h);
  if (conn == NULL) {
    Serial.println("ERR NOTCONNECTED");
    return;
  }
  ble_gap_addr_t peer = conn->getPeerAddr();

  // Ugyanaz az eszköz ne kerüljön két fiókba egyszerre.
  for (uint8_t i = 0; i < ZW_NUM_SLOTS; i++) {
    if (i != s && keymap.peers[i].valid
        && memcmp(keymap.peers[i].addr, peer.addr, 6) == 0) {
      keymap.peers[i].valid = 0;
    }
  }
  keymap.peers[s].valid = 1;
  keymap.peers[s].addrType = peer.addr_type;
  memcpy(keymap.peers[s].addr, peer.addr, 6);
  Serial.println("OK");
}

static void cmdClearSlot(const char* args) {
  unsigned s;
  if (sscanf(args, "%u", &s) != 1) {
    Serial.println("ERR ARGS");
    return;
  }
  if (s >= ZW_NUM_SLOTS) {
    Serial.println("ERR RANGE");
    return;
  }
  memset(&keymap.peers[s], 0, sizeof(PeerSlot));
  Serial.println("OK");
}

static void cmdSet(const char* args) {
  unsigned m, b, e, t, mod, code, rep, ms;
  if (sscanf(args, "%u %u %u %u %u %u %u %u", &m, &b, &e, &t, &mod, &code, &rep, &ms) != 8) {
    Serial.println("ERR ARGS");
    return;
  }
  if (m >= ZW_NUM_MODES || b >= ZW_NUM_BUTTONS || e >= ZW_NUM_EVENTS) {
    Serial.println("ERR RANGE");
    return;
  }
  if (t >= ACT_TYPE_COUNT || mod > 0xFF || code > 0xFFFF || rep > 1 || ms > 0xFFFF) {
    Serial.println("ERR VALUE");
    return;
  }
  // A billentyűkód egy bájt; a nagyobb érték csendben csonkolódna a küldésnél.
  if (t == ACT_KEY && code > 0xFF) {
    Serial.println("ERR VALUE");
    return;
  }
  setAction((uint8_t)m, (uint8_t)b, (uint8_t)e, (uint8_t)t, (uint8_t)mod,
            (uint16_t)code, (uint8_t)rep, (uint16_t)(ms == 0 ? 60 : ms));
  Serial.println("OK");
}

static void cmdMode(const char* args) {
  unsigned n;
  if (sscanf(args, "%u", &n) == 1) {
    if (n >= ZW_NUM_MODES) {
      Serial.println("ERR RANGE");
      return;
    }
    jelenlegiUzemmod = (uzemmod)n;
  }
  char line[24];
  snprintf(line, sizeof(line), "OK MODE %u", (unsigned)jelenlegiUzemmod);
  Serial.println(line);
}

static void processCommand(char* cmd) {
  fct_WatchdogReset();

  // parancs és argumentumok szétválasztása
  char* args = cmd;
  while (*args && *args != ' ') args++;
  if (*args == ' ') {
    *args = 0;
    args++;
  }

  for (char* p = cmd; *p; p++) *p = toupper((unsigned char)*p);

  if (cmd[0] == 0) {
    return;
  } else if (strcmp(cmd, "PING") == 0) {
    char line[112];
    snprintf(line, sizeof(line),
             "OK ZWIFT_BUTTONS PROTO=%u MODES=%u BUTTONS=%u EVENTS=%u SLOTS=%u CONNS=%u",
             (unsigned)ZW_PROTO_VERSION, (unsigned)ZW_NUM_MODES,
             (unsigned)ZW_NUM_BUTTONS, (unsigned)ZW_NUM_EVENTS,
             (unsigned)ZW_NUM_SLOTS, (unsigned)ZW_MAX_CONNECTIONS);
    Serial.println(line);
  } else if (strcmp(cmd, "GET") == 0) {
    cmdGet();
  } else if (strcmp(cmd, "SET") == 0) {
    cmdSet(args);
  } else if (strcmp(cmd, "SETTARGET") == 0) {
    cmdSetTarget(args);
  } else if (strcmp(cmd, "PEERS") == 0) {
    cmdPeers();
  } else if (strcmp(cmd, "ASSIGN") == 0) {
    cmdAssign(args);
  } else if (strcmp(cmd, "CLEARSLOT") == 0) {
    cmdClearSlot(args);
  } else if (strcmp(cmd, "SAVE") == 0) {
    Serial.println(saveKeymap() ? "OK SAVED" : "ERR SAVE");
  } else if (strcmp(cmd, "LOAD") == 0) {
    Serial.println(loadKeymap() ? "OK LOADED" : "ERR LOAD");
  } else if (strcmp(cmd, "DEFAULTS") == 0) {
    loadDefaultKeymap();
    Serial.println("OK DEFAULTS");
  } else if (strcmp(cmd, "MODE") == 0) {
    cmdMode(args);
  } else if (strcmp(cmd, "DBG") == 0) {
    unsigned n;
    if (sscanf(args, "%u", &n) == 1) debugSerial = (n != 0);
    char line[20];
    snprintf(line, sizeof(line), "OK DBG %u", (unsigned)(debugSerial ? 1 : 0));
    Serial.println(line);
  } else {
    Serial.println("ERR UNKNOWN");
  }
}

void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (cmdOverflow) {
        Serial.println("ERR TOOLONG");
      } else {
        cmdBuf[cmdLen] = 0;
        processCommand(cmdBuf);
      }
      cmdLen = 0;
      cmdOverflow = false;
      continue;
    }
    if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    } else {
      cmdOverflow = true;
    }
  }
}
