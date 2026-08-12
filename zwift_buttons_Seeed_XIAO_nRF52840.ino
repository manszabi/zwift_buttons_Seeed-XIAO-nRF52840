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
const uint16_t offDelay = 900;  // alvas elotti tetlenseg masodpercben
uint8_t nezet = 0;
bool duringLongpress = false;

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
static bool repeatSentConsumer = false;  // az ismétlés média billentyűt küldött-e
static unsigned long lastRepeatMillis = 0;

// Az ismétlés leütés-impulzusa. A felengedést nem közvetlenül a leütés után
// küldjük: egy pár ms-os impulzust a képkockánként mintavételező alkalmazások
// kihagyhatnak, egy blokkoló delay() pedig az egész főciklust megállítaná.
static const uint16_t repeatTapMs = 20;  // egy 60 Hz-es mintavétel (16,7 ms) fölött
static bool repeatTapPending = false;
static uint8_t repeatTapModifier = 0;  // 0 = teljes felengedés, egyébként nyomva marad
static unsigned long repeatTapMillis = 0;

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

// A gombok sorrendje a kiosztás második indexe: Gomb 1..5.
static OneButton* const buttons[ZW_NUM_BUTTONS] = {
  &button1, &button2, &button3, &button4, &button5
};


#define WAKEUP_PIN 2

TickTwo watchDOG(fct_Watchdog, 1000, 0, MILLIS);

#define watchdogMinCounter 0  // reset value for watchdog counter
static uint32_t watchdogCounter = watchdogMinCounter;

Adafruit_FlashTransport_QSPI flashTransport;

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

  jelenlegiUzemmod = readStoredMode();

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

  attachButtonCallbacks();

  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    connHandles[i] = BLE_CONN_HANDLE_INVALID;
  }

  Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16);
  // Két kapcsolat több SoftDevice-RAM-ot igényel; ha nem fér el, ezt tudni kell.
  if (!Bluefruit.begin(ZW_MAX_CONNECTIONS, 0)) {
    Serial.println("HIBA: a BLE stack nem indult el (kevés a RAM?)");
  }
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
  updateLeds();

  updateButtons();
  handleSerial();
  pruneLostTargets();
  updateRepeatTap();

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
  // FIGYELEM: a Bluefruit.connected() a kapcsolatok SZAMAT adja vissza, nem a
  // legnagyobb azonositot. A sajat nyilvantartasunkon kell vegigmenni,
  // kulonben egy 1-es azonositoju kapcsolat bontatlan maradna.
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (connHandles[i] != BLE_CONN_HANDLE_INVALID) {
      Bluefruit.disconnect(connHandles[i]);
    }
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

  // A callback külön (magasabb prioritású) taskról fut, ezért csak akkor
  // írunk a soros portra, ha a debug engedélyezett: egyébként beleírhatna egy
  // épp folyamatban lévő GET válasz közepébe.
  if (debugSerial) {
    Serial.print("BLE csatlakozott, conn_hdl=");
    Serial.print(conn_handle);
    Serial.print(" (");
    Serial.print(used);
    Serial.print("/");
    Serial.print(ZW_MAX_CONNECTIONS);
    Serial.println(")");
  }

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
  if (debugSerial) {
    Serial.print("BLE bontva, conn_hdl=");
    Serial.println(conn_handle);
  }
  // A lenyomás-állapotot szándékosan NEM itt takarítjuk: az a főciklus
  // adata, ez a függvény pedig másik taskról fut. A pruneLostTargets() a
  // loop()-ban veszi észre, hogy egy célpont eltűnt.
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
      // A FILE_O_WRITE nem csonkol és a fájl végére pozicionál, ezért egy
      // esetleg ottmaradt régi ideiglenes fájlhoz hozzáfűzne. A seek(0) és a
      // záró truncate() garantálja, hogy pontosan len bájt legyen benne.
      f.seek(0);
      size_t written = f.write((const uint8_t*)data, len);
      if (written == len) f.truncate(len);
      f.close();
      // A littlefs rename felülírja a célt, ha az létezik. Szándékosan NEM
      // töröljük előbb a régi fájlt: ha a törlés után az átnevezés is elbukna,
      // egyszerre veszítenénk el a régit és az újat is.
      if (written == len && InternalFS.rename(tmpPath, path)) return true;
    }
    delay(50);
  }
  InternalFS.remove(tmpPath);
  return false;
}

// A tárolt üzemmód beolvasása. Szándékosan fix pufferrel, Arduino String
// nélkül: a String a heapet használja, ami hosszú futás mellett elaprózódhat.
uzemmod readStoredMode() {
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (!f.open(FILENAME, FILE_O_READ)) {
    Serial.print("Nincs " FILENAME ", letrehozas alapertelmezessel ... ");
    Serial.println(writeFileAtomic(FILENAME, FILENAMETMP, CONTENTNormal,
                                   strlen(CONTENTNormal))
                     ? "OK"
                     : "Failed!");
    return normalUzemmod;
  }

  char buffer[32] = { 0 };
  int readlen = f.read(buffer, sizeof(buffer) - 1);
  f.close();
  if (readlen < 0) readlen = 0;
  buffer[readlen] = 0;

  if (strcmp(buffer, CONTENTNormal) == 0) return normalUzemmod;
  if (strcmp(buffer, CONTENTVerseny) == 0) return versenyEdzesUzemmod;
  if (strcmp(buffer, CONTENTMedia) == 0) return mediaVezerloUzemmod;

  Serial.println("Ismeretlen uzemmod, default: normalUzemmod");
  return normalUzemmod;
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
  if (watchdogCounter >= (uint32_t)offDelay) {
    fct_powerdown();
  }
}

void fct_WatchdogReset() {
  watchdogCounter = watchdogMinCounter;
}

// A LED-eket csak üzemmód-váltáskor írjuk át. Ciklusonként újraírva 50-szer
// másodpercenként kapcsolgatnánk mindhárom lábat, fölöslegesen.
void updateLeds() {
  static int8_t shownMode = -1;
  if (shownMode == (int8_t)jelenlegiUzemmod) return;
  shownMode = (int8_t)jelenlegiUzemmod;

  for (int i = 0; i < numOfLeds; i++) {
    digitalWrite(ledPin[i], HIGH);
  }
  if ((uint8_t)jelenlegiUzemmod < (uint8_t)numOfLeds) {
    digitalWrite(ledPin[(uint8_t)jelenlegiUzemmod], LOW);
  }
}

void updateButtons() {
  for (uint8_t i = 0; i < ZW_NUM_BUTTONS; i++) buttons[i]->tick();
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

// FIGYELEM: itt szándékosan túlterhelés van, nem alapértelmezett paraméter. Az
// Arduino a vázlat elejére generálja a prototípusokat, és egy alapértelmezett
// érték a prototípusban ÉS a definícióban is szerepelve fordítási hibát ad.
static void setAction(uint8_t mode, uint8_t btn, uint8_t evt,
                      uint8_t type, uint8_t modifier, uint16_t code,
                      uint8_t repeat, uint16_t repeatMs, uint8_t target) {
  KeyAction& a = keymap.map[mode][btn][evt];
  a.type = type;
  a.modifier = modifier;
  a.code = code;
  a.repeat = repeat;
  a.target = target;
  a.repeatMs = repeatMs;
}

static void setAction(uint8_t mode, uint8_t btn, uint8_t evt,
                      uint8_t type, uint8_t modifier, uint16_t code,
                      uint8_t repeat, uint16_t repeatMs) {
  setAction(mode, btn, evt, type, modifier, code, repeat, repeatMs, 0);
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
  //
  // Bármelyik művelet kaphat saját célt (KeyAction.target), ez felülírja az
  // üzemmódét; gyárilag egy kivétellel mindegyik 0, azaz örököl. A kivétel a
  // média üzemmód 1-3. gombjának hosszú nyomása: gépfüggő parancsok
  // (Win+Alt+R, Alt+Tab, Win+Alt+G), amiknek telefonon nincs értelmük, ezért
  // csak a PC-re mennek.
  keymap.modeTarget[0] = ZW_TARGET_PC;   // Normál (Zwift)
  keymap.modeTarget[1] = ZW_TARGET_PC;   // Verseny / edzés
  keymap.modeTarget[2] = ZW_TARGET_ALL;  // Média vezérlő

  // Az ismétlődő műveletek gyárilag külön leütéseket küldenek, hogy a
  // beállított ismétlési idő tényleg érvényesüljön.
  const uint8_t REPEAT_TAPS = ZW_REPEAT_ENABLED | ZW_REPEAT_RELEASE;

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
  setAction(0, 3, EV_LONG, ACT_KEY, 0, HID_KEY_ARROW_DOWN, REPEAT_TAPS, 30);

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
  setAction(1, 3, EV_LONG, ACT_KEY, 0, HID_KEY_ARROW_DOWN, REPEAT_TAPS, 70);

  setAction(1, 4, EV_CLICK, ACT_KEY, 0, HID_KEY_E, 0, 0);
  setAction(1, 4, EV_DOUBLE, ACT_VIEW_CYCLE, 0, 0, 0, 0);
  setAction(1, 4, EV_LONG, ACT_KEY, 0, HID_KEY_ARROW_UP, 0, 0);

  // --- Média vezérlő üzemmód ---
  setAction(2, 0, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_SCAN_PREVIOUS, 0, 0);
  setAction(2, 0, EV_DOUBLE, ACT_KEY, 0, HID_KEY_F9, 0, 0);
  setAction(2, 0, EV_LONG, ACT_KEY, MOD_GUI_ALT, HID_KEY_R, 0, 0, ZW_TARGET_PC);

  setAction(2, 1, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_PLAY_PAUSE, 0, 0);
  setAction(2, 1, EV_DOUBLE, ACT_KEY, 0, HID_KEY_ESCAPE, 0, 0);
  // Alt+Tab: az Alt végig nyomva marad, a Tab ismétlődik — így a Windows
  // ablakváltója nyitva marad és tovább lépked, nem csak két ablak közt vált.
  setAction(2, 1, EV_LONG, ACT_KEY, KEYBOARD_MODIFIER_LEFTALT, HID_KEY_TAB,
            REPEAT_TAPS | ZW_REPEAT_HOLD_MOD, 500, ZW_TARGET_PC);

  setAction(2, 2, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_SCAN_NEXT, 0, 0);
  setAction(2, 2, EV_DOUBLE, ACT_KEY, 0, HID_KEY_F10, 0, 0);
  setAction(2, 2, EV_LONG, ACT_KEY, MOD_GUI_ALT, HID_KEY_G, 0, 0, ZW_TARGET_PC);

  setAction(2, 3, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_MUTE, 0, 0);
  setAction(2, 3, EV_DOUBLE, ACT_MODE_NEXT, 0, 0, 0, 0);
  setAction(2, 3, EV_LONG, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_VOLUME_DECREMENT, REPEAT_TAPS, 70);

  setAction(2, 4, EV_CLICK, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_AL_CONSUMER_CONTROL_CONFIGURATION, 0, 0);
  setAction(2, 4, EV_DOUBLE, ACT_VIEW_CYCLE, 0, 0, 0, 0);
  setAction(2, 4, EV_LONG, ACT_CONSUMER, 0, HID_USAGE_CONSUMER_VOLUME_INCREMENT, REPEAT_TAPS, 70);
}

// A 2-es formátumban a repeat mező csak 0/1 lehetett, és az 1 azt jelentette,
// hogy az ismétlés végig lenyomva tartja a billentyűt — ez a hostnál beragadt
// billentyűnek látszik, és a beállított ismétlési idő sem érvényesül. A 3-as
// formátumban ugyanez külön leütéseket jelent, ezért a régi mentéseket
// betöltéskor átalakítjuk, hogy a javítás oda is eljusson.
static void migrateKeymapV2toV3() {
  for (uint8_t m = 0; m < ZW_NUM_MODES; m++) {
    for (uint8_t b = 0; b < ZW_NUM_BUTTONS; b++) {
      for (uint8_t e = 0; e < ZW_NUM_EVENTS; e++) {
        KeyAction& a = keymap.map[m][b][e];
        if (a.repeat == ZW_REPEAT_ENABLED) a.repeat |= ZW_REPEAT_RELEASE;
      }
    }
  }
  keymap.version = ZW_KEYMAP_VERSION;
  Serial.println("keymap: regi (2-es) mentes atalakitva 3-as formatumra");
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
  if (tmp.magic != ZW_KEYMAP_MAGIC
      || tmp.version < ZW_KEYMAP_MIN_VERSION || tmp.version > ZW_KEYMAP_VERSION
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
  if (tmp.version < 3) migrateKeymapV2toV3();
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
//
// A getPeerAddr() párosított kapcsolatnál a bond identity címet adja vissza,
// ami újracsatlakozás után is ugyanaz. Párosítás ELŐTT viszont a nyers
// kapcsolódási címet kapjuk, ami a telefonoknál rendszeresen változó
// (resolvable private) cím — azzal nem lehet eszközt azonosítani.
static int8_t slotOfConn(uint16_t conn_hdl) {
  BLEConnection* conn = Bluefruit.Connection(conn_hdl);
  if (conn == NULL || !conn->bonded()) return -1;
  ble_gap_addr_t peer = conn->getPeerAddr();
  for (uint8_t s = 0; s < ZW_NUM_SLOTS; s++) {
    if (!keymap.peers[s].valid) continue;
    if (keymap.peers[s].addrType != peer.addr_type) continue;
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

// Egy művelet cél-maszkja: ha a művelethez tartozik felülbírálás, az számít,
// egyébként az üzemmódhoz beállított célpont.
static uint8_t targetMaskOf(const KeyAction& a) {
  return a.target ? a.target : keymap.modeTarget[(uint8_t)jelenlegiUzemmod];
}

static bool sendKeyboard(uint8_t modifier, uint8_t keycode, uint8_t mask) {
  // A duringLongpress is kizáró feltétel: ismétlés közben a főciklus nem
  // engedi fel a billentyűt, így egy közben indított másik művelet
  // felengedése is elmaradna (beragadt billentyű a másik eszközön).
  if (duringLongpress || hasKeyPressed || hasConsumerKeyPressed) return false;
  uint16_t targets[ZW_MAX_CONNECTIONS];
  uint8_t n = collectTargets(mask, targets);
  if (n == 0) return false;

  uint8_t keycodes[6] = { keycode, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
  for (uint8_t i = 0; i < n; i++) {
    blehid.keyboardReport(targets[i], modifier, keycodes);
    pressedTargets[i] = targets[i];
  }
  pressedTargetCount = n;
  hasKeyPressed = true;
  delay(5);
  return true;
}

static bool sendConsumer(uint16_t usage, uint8_t mask) {
  if (duringLongpress || hasKeyPressed || hasConsumerKeyPressed) return false;
  uint16_t targets[ZW_MAX_CONNECTIONS];
  uint8_t n = collectTargets(mask, targets);
  if (n == 0) return false;

  for (uint8_t i = 0; i < n; i++) {
    blehid.consumerKeyPress(targets[i], usage);
    pressedTargets[i] = targets[i];
  }
  pressedTargetCount = n;
  hasConsumerKeyPressed = true;
  delay(5);
  return true;
}

// A lenyomás pontosan azokra a kapcsolatokra volt kiküldve, amiket a
// pressedTargets tárol — a felengedést is ezekre kell elküldeni.
// A bontott kapcsolatok kivétele a lenyomott célpontok közül. A főciklusból
// hívjuk, hogy a megosztott állapotot csak egy task módosítsa. Ha egyetlen
// célpont sem maradt, a lenyomás-állapotot is nullázzuk.
void pruneLostTargets() {
  uint8_t kept = 0;
  for (uint8_t i = 0; i < pressedTargetCount; i++) {
    if (Bluefruit.connected(pressedTargets[i])) pressedTargets[kept++] = pressedTargets[i];
  }
  if (kept == pressedTargetCount) return;

  pressedTargetCount = kept;
  if (kept == 0) {
    repeatTapPending = false;
    hasKeyPressed = false;
    hasConsumerKeyPressed = false;
    duringLongpress = false;
    repeatButton = -1;
    keyPressMillis = 0;
  }
}

void releasePressedKeys(bool keyboard, bool consumer) {
  for (uint8_t i = 0; i < pressedTargetCount; i++) {
    uint16_t h = pressedTargets[i];
    if (!Bluefruit.connected(h)) continue;
    if (keyboard) blehid.keyRelease(h);
    if (consumer) blehid.consumerKeyRelease(h);
  }
}

// Egyszeri művelet (rövid / dupla / nem ismétlődő hosszú nyomás).
// Az ismétlés leütés-impulzusának lezárása. Ha van nyomva tartandó módosító,
// csak magát a billentyűt engedjük fel (Alt+Tab), egyébként teljeset küldünk.
static void endRepeatTap() {
  repeatTapPending = false;
  if (repeatTapModifier) {
    uint8_t none[6] = { HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE,
                        HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
    for (uint8_t i = 0; i < pressedTargetCount; i++) {
      if (Bluefruit.connected(pressedTargets[i])) {
        blehid.keyboardReport(pressedTargets[i], repeatTapModifier, none);
      }
    }
  } else {
    releasePressedKeys(!repeatSentConsumer, repeatSentConsumer);
  }
}

// A főciklusból hívjuk, hogy az impulzus a gomb-tickektől függetlenül záruljon.
void updateRepeatTap() {
  if (repeatTapPending && (millis() - repeatTapMillis) >= repeatTapMs) {
    endRepeatTap();
  }
}

static void fireAction(uint8_t btn, uint8_t evt) {
  const KeyAction& a = currentAction(btn, evt);
  switch (a.type) {
    case ACT_KEY:
      sendKeyboard(a.modifier, (uint8_t)a.code, targetMaskOf(a));
      break;

    case ACT_CONSUMER:
      sendConsumer(a.code, targetMaskOf(a));
      break;

    case ACT_MODE_NEXT:
      jelenlegiUzemmod = (uzemmod)(((uint8_t)jelenlegiUzemmod + 1) % ZW_NUM_MODES);
      break;

    case ACT_VIEW_CYCLE: {
      // A számlálót csak akkor léptetjük, ha a billentyű tényleg kiment,
      // különben elcsúszna a Zwift-ben ténylegesen beállított nézettől. A
      // küldés nem csak kapcsolat hiányában maradhat el, hanem akkor is, ha az
      // üzemmód célpontja épp nincs csatlakozva — ezért a sendKeyboard()
      // visszatérési értékére támaszkodunk.
      static const uint8_t HID_KEYS[9] = { HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4, HID_KEY_5,
                                           HID_KEY_6, HID_KEY_7, HID_KEY_8, HID_KEY_9 };
      int next = (nezet >= 9) ? 1 : nezet + 1;
      if (sendKeyboard(0, HID_KEYS[next - 1], targetMaskOf(a))) nezet = next;
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
  uint8_t n = collectTargets(targetMaskOf(a), targets);
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
  repeatSentConsumer = (a.type == ACT_CONSUMER);

  if (a.repeat & ZW_REPEAT_RELEASE) {
    // Külön leütésekként küldjük: a HID jelentés a billentyű ÁLLAPOTÁT írja le,
    // ezért felengedés nélkül a host végig lenyomva tartottnak látja, és a saját
    // ismétlési sebességével pörgeti — az ismétlési idő így nem érvényesülne.
    // A felengedés repeatTapMs múlva megy ki (updateRepeatTap), nem azonnal.
    // A pressedTargets szándékosan megmarad: a gomb elengedésekor küldött záró
    // felengedés így akkor is helyreállítja az állapotot, ha egy közbenső
    // felengedés-értesítés elveszne.
    repeatTapPending = true;
    repeatTapMillis = millis();
    repeatTapModifier = ((a.repeat & ZW_REPEAT_HOLD_MOD) && a.type == ACT_KEY)
                          ? a.modifier : 0;
  }

  hasKeyPressed = false;
  hasConsumerKeyPressed = false;
  duringLongpress = true;
}

static bool isRepeating(const KeyAction& a) {
  return (a.repeat & ZW_REPEAT_ENABLED) && (a.type == ACT_KEY || a.type == ACT_CONSUMER);
}

// Folyamatban lévő ismétlés lezárása. Mindkét jelzőt beállítjuk, hogy a
// főciklus a billentyűzet- és a média-billentyűt is felengedje: nyomva tartás
// közben a kettő közül bármelyik lehetett az utolsó kiküldött esemény.
// Ismétlés lezárása a gomb elengedésekor. A felengedést a főciklus küldi ki a
// keyReleaseDelay letelte után; csak azt a fajtát jelöljük, amit az ismétlés
// ténylegesen küldött, hogy ne menjen fölösleges felengedés a hostnak (és ne
// tiltsuk le indokolatlanul a többi gombot a késleltetés idejére).
static void finishRepeat() {
  if (repeatTapPending) endRepeatTap();   // a függő impulzust azonnal lezárjuk
  repeatButton = -1;
  repeatDue = false;
  duringLongpress = false;
  // Záró felengedés akkor is, ha az ismétlések között már engedtünk fel: ez a
  // biztonsági háló arra az esetre, ha egy közbenső felengedés elveszne.
  if (pressedTargetCount == 0) return;
  if (repeatSentConsumer) {
    hasConsumerKeyPressed = true;
  } else {
    hasKeyPressed = true;
  }
}

// Ismétlés azonnali lezárása, ha egy másik gomb veszi át. Itt nem bízhatjuk a
// felengedést a főciklusra: az új ismétlés még előtte felülírná a célpont-
// listát, és a régi célponton beragadna a billentyű.
static void abortRepeat() {
  repeatTapPending = false;
  releasePressedKeys(!repeatSentConsumer, repeatSentConsumer);
  pressedTargetCount = 0;
  repeatButton = -1;
  repeatDue = false;
  duringLongpress = false;
  hasKeyPressed = false;
  hasConsumerKeyPressed = false;
  keyPressMillis = 0;
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
    // nyomva), azt előbb lezárjuk, hogy ne ragadjon be a billentyű.
    if (repeatButton >= 0 && repeatButton != (int8_t)btn) {
      abortRepeat();
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

  // Az üzemmód menet közben is megváltozhat (másik gomb dupla kattintása vagy
  // a MODE parancs). Ilyenkor a currentAction() már MÁS műveletet ad vissza;
  // ha az nem ismétlődő, az ismétlést le kell zárni — különben a nulla
  // ismétlési idő miatt fékezetlenül szórnánk ki a billentyűt.
  const KeyAction& a = currentAction(btn, EV_LONG);
  if (!isRepeating(a)) {
    abortRepeat();
    return;
  }

  // Külön leütéseknél a leütés-impulzusnak be kell férnie két ismétlés közé.
  uint16_t interval = a.repeatMs;
  if (a.repeat & ZW_REPEAT_RELEASE) {
    if (interval < repeatTapMs + 10) interval = repeatTapMs + 10;
  } else if (interval == 0) {
    interval = 60;
  }

  unsigned long now = millis();
  if (!repeatDue && (now - lastRepeatMillis) < interval) return;
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

// A OneButton 2.x paraméteres callbackeket is fogad, így gombonként nem kell
// külön becsomagoló függvény: a gomb sorszámát adjuk át a közös kezelőknek.
// (Ehhez legalább OneButton 2.0 kell — lásd a README könyvtár-táblázatát.)
void cbClick(void* p) { onClick((uint8_t)(uintptr_t)p); }
void cbDoubleClick(void* p) { onDoubleClick((uint8_t)(uintptr_t)p); }
void cbLongStart(void* p) { onLongStart((uint8_t)(uintptr_t)p); }
void cbLongDuring(void* p) { onLongDuring((uint8_t)(uintptr_t)p); }
void cbLongStop(void* p) { onLongStop((uint8_t)(uintptr_t)p); }

void attachButtonCallbacks() {
  for (uint8_t i = 0; i < ZW_NUM_BUTTONS; i++) {
    void* idx = (void*)(uintptr_t)i;
    buttons[i]->attachClick(cbClick, idx);
    buttons[i]->attachDoubleClick(cbDoubleClick, idx);
    buttons[i]->attachLongPressStart(cbLongStart, idx);
    buttons[i]->attachDuringLongPress(cbLongDuring, idx);
    buttons[i]->attachLongPressStop(cbLongStop, idx);
  }
}

// ---------------------------------------------------------------------------
// Soros (USB) konfigurációs protokoll
//
// Sor alapú, ASCII. Minden parancs '\n'-nel zárul, minden válasz egy sor.
//
//   PING                                        -> OK ZWIFT_BUTTONS PROTO=5 ...
//   GET                                         -> MAP ... (45 sor) + END
//   SET <m> <b> <e> <t> <mod> <code> <rep> <ms> [<tgt>] -> OK
//
//   rep: ismétlés bitmaszk (csak hosszú nyomásnál) — 1 = ismétlés be,
//        2 = felengedés az ismétlések között, 4 = a módosító nyomva marad
//        (csak a 2 mellett). Használható értékek: 0, 1, 3, 7.
//   SAVE                                        -> OK SAVED | ERR SAVE
//   LOAD                                        -> OK LOADED | ERR LOAD
//   DEFAULTS                                    -> OK DEFAULTS
//   MODE [n]                                    -> OK MODE <n>
//   DBG <0|1>                                   -> OK DBG <n>
//
//   GET valasza a MAP sorok utan uzemmodonkent egy TARGET <m> <maszk> sort is
//   tartalmaz (maszk: 1 = PC, 2 = telefon, 3 = mindketto).
//
//   A MAP/SET utolso mezoje (tgt) muveletenkenti cel-felulbiralas: 0 eseten az
//   uzemmod celpontja ervenyes, egyebkent ez a maszk. A SET-nel elhagyhato.
//
//   SETTARGET <m> <maszk>                       -> OK
//   PEERS                                       -> SLOT/CONN sorok + END
//   ASSIGN <slot> <conn_hdl>                    -> OK | ERR NOTBONDED
//   CLEARSLOT <slot>                            -> OK
// ---------------------------------------------------------------------------

static void printMapLine(uint8_t m, uint8_t b, uint8_t e) {
  const KeyAction& a = keymap.map[m][b][e];
  char line[64];
  snprintf(line, sizeof(line), "MAP %u %u %u %u %u %u %u %u %u",
           (unsigned)m, (unsigned)b, (unsigned)e,
           (unsigned)a.type, (unsigned)a.modifier, (unsigned)a.code,
           (unsigned)a.repeat, (unsigned)a.repeatMs, (unsigned)a.target);
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
    char line[72];
    snprintf(line, sizeof(line), "CONN %u %s %d %u",
             (unsigned)h, addrStr, (int)slotOfConn(h),
             (unsigned)(conn->bonded() ? 1 : 0));
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
  // Párosítás nélkül a cím változó (resolvable private address), tehát
  // eltárolva soha többé nem egyezne. Ilyenkor inkább hibát adunk.
  if (!conn->bonded()) {
    Serial.println("ERR NOTBONDED");
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
  unsigned tgt = 0;  // elhagyható: 0 = az üzemmód célpontja érvényes
  int got = sscanf(args, "%u %u %u %u %u %u %u %u %u",
                   &m, &b, &e, &t, &mod, &code, &rep, &ms, &tgt);
  if (got != 8 && got != 9) {
    Serial.println("ERR ARGS");
    return;
  }
  if (m >= ZW_NUM_MODES || b >= ZW_NUM_BUTTONS || e >= ZW_NUM_EVENTS) {
    Serial.println("ERR RANGE");
    return;
  }
  if (t >= ACT_TYPE_COUNT || mod > 0xFF || code > 0xFFFF
      || rep > ZW_REPEAT_MASK || ms > 0xFFFF) {
    Serial.println("ERR VALUE");
    return;
  }
  // A billentyűkód egy bájt; a nagyobb érték csendben csonkolódna a küldésnél.
  if (t == ACT_KEY && code > 0xFF) {
    Serial.println("ERR VALUE");
    return;
  }
  // Értelmetlen ismétlés-kombinációk: a RELEASE/HOLD_MOD bit önmagában
  // (ismétlés nélkül), illetve a HOLD_MOD a RELEASE nélkül csendben elveszne.
  if (rep && !(rep & ZW_REPEAT_ENABLED)) {
    Serial.println("ERR VALUE");
    return;
  }
  if ((rep & ZW_REPEAT_HOLD_MOD) && !(rep & ZW_REPEAT_RELEASE)) {
    Serial.println("ERR VALUE");
    return;
  }
  if (tgt > ZW_TARGET_ALL) {
    Serial.println("ERR VALUE");
    return;
  }
  setAction((uint8_t)m, (uint8_t)b, (uint8_t)e, (uint8_t)t, (uint8_t)mod,
            (uint16_t)code, (uint8_t)rep, (uint16_t)(ms == 0 ? 60 : ms),
            (uint8_t)tgt);
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

  // Vezető szóközök átugrása: terminálból könnyű elgépelni, és enélkül a
  // parancs üresnek látszana, amire némán nem válaszolnánk semmit.
  while (*cmd == ' ' || *cmd == '\t') cmd++;

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
