#include <Arduino.h>
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

using namespace Adafruit_LittleFS_Namespace;

#define FILENAME "/jelenlegiuzemmod.txt"
#define CONTENTNormal "normalUzemmod"
#define CONTENTVerseny "versenyEdzesUzemmod"
#define CONTENTMedia "mediaVezerloUzemmod"


Adafruit_LittleFS_Namespace::File file(InternalFS);

void fct_Watchdog();

BLEDis bledis;
BLEHidAdafruit blehid;
bool BTConnected = false;

const int ledPin[] = { 11, 12, 13 };  //red, blue, green
const int pin_charging_current = 22;  //mekkora árammal töltsön
const int numOfLeds = sizeof(ledPin) / sizeof(ledPin[0]);
const int BUTTON_PIN[5] = { 0, 1, 2, 3, 4 };  // A gombokhoz csatlakoztatott tüskék
const int numOfButtons = sizeof(BUTTON_PIN) / sizeof(BUTTON_PIN[0]);
bool hasKeyPressed = false;
bool hasCosumerKeyPressed = false;
unsigned long previousMillis = 0;
const long interval = 1000;
unsigned long currentMillis;
int offDelay = 900; //sleep delay
int nezet = 0;
bool duringLongpress = false;
String taroltUzemmod;

OneButton button1(BUTTON_PIN[1], true);
OneButton button2(BUTTON_PIN[2], true);
OneButton button3(BUTTON_PIN[3], true);
OneButton button4(BUTTON_PIN[4], true);
OneButton button5(BUTTON_PIN[0], true);

#define WAKEUP_PIN 2

TickTwo watchDOG(fct_Watchdog, 1000, 0, MILLIS);

#define watchdogMinCounter 0  // 10minuten
static uint32_t watchdogCounter = watchdogMinCounter;

Adafruit_FlashTransport_QSPI flashTransport;

enum uzemmod {
  normalUzemmod,
  versenyEdzesUzemmod,
  mediaVezerloUzemmod
};

uzemmod jelenlegiUzemmod = normalUzemmod;
uzemmod elozoUzemmod = jelenlegiUzemmod;


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
    readlen = file.read(buffer, sizeof(buffer));

    buffer[readlen] = 0;
    Serial.println(buffer);
    taroltUzemmod = buffer;
    file.close();
  } else {
    Serial.print("Open " FILENAME " file to write ... ");
    if (file.open(FILENAME, FILE_O_WRITE)) {
      Serial.println("OK");
      file.write(CONTENTNormal, strlen(CONTENTNormal));
      file.close();
    } else {
      Serial.println("Failed!");
    }
  }

  if (taroltUzemmod == "normaluzemmod") {
    jelenlegiUzemmod = normalUzemmod;
  } else if (taroltUzemmod == "versengyEdzesUzemmod") {
    jelenlegiUzemmod = versenyEdzesUzemmod;
  } else if (taroltUzemmod == "mediaVezerloUzemmod") {
    jelenlegiUzemmod = mediaVezerloUzemmod;
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

  Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16);
  Bluefruit.begin(1, 0);
  Bluefruit.setTxPower(4);
  Bluefruit.autoConnLed(false);
  Bluefruit.setName("SEEED_ZWIFT");
  bledis.setManufacturer("Manszabi");
  bledis.setModel("ZWIFT_button");
  bledis.begin();
  blehid.begin();
  Bluefruit.Periph.setConnInterval(9, 12);
  startAdv();
}

void loop() {

  digitalWrite(pin_charging_current, LOW);
  //toltes alacsony árammal

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
  }

  if (Bluefruit.connected()) {

    updateButtons();

    if (hasKeyPressed) {
      blehid.keyRelease();
    }

    if (hasCosumerKeyPressed) {
      blehid.consumerKeyRelease();
    }

    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      if (!duringLongpress) {
        hasKeyPressed = false;
        hasCosumerKeyPressed = false;
      }
    }
  }

  if (elozoUzemmod != jelenlegiUzemmod) {
    Serial.println("A jelenlegiUzemmod változó értéke megváltozott.");
    Serial.print("Open " FILENAME " file to write ... ");
    InternalFS.remove(FILENAME);

    if (jelenlegiUzemmod == normalUzemmod) {
      if (file.open(FILENAME, FILE_O_WRITE)) {
        Serial.println("OK");
        file.write(CONTENTNormal, strlen(CONTENTNormal));
        file.close();
      } else {
        Serial.println("Failed! CONTENTNormal");
      }
    } else if (jelenlegiUzemmod == versenyEdzesUzemmod) {
      if (file.open(FILENAME, FILE_O_WRITE)) {
        Serial.println("OK");
        file.write(CONTENTVerseny, strlen(CONTENTVerseny));
        file.close();
      } else {
        Serial.println("Failed! CONTENTVerseny");
      }
    } else if (jelenlegiUzemmod == mediaVezerloUzemmod) {
      if (file.open(FILENAME, FILE_O_WRITE)) {
        Serial.println("OK");
        file.write(CONTENTMedia, strlen(CONTENTMedia));
        file.close();
      } else {
        Serial.println("Failed! CONTENTMedia");
      }
    }
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

void ble_sleep(void) {
  Bluefruit.Advertising.restartOnDisconnect(false);
  disconnectBle();
  Bluefruit.Advertising.stop();
}

void fct_powerdown() {
  InternalFS.end();
  for (int i = 0; i < numOfLeds; i++) {
    digitalWrite(ledPin[i], HIGH);
  }
  ble_sleep();
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
  if (watchdogCounter == offDelay) {
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

void click1() {
  Serial.println("Button 1 click.");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ARROW_LEFT, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_PAGE_DOWN, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          blehid.consumerKeyPress(0, HID_USAGE_CONSUMER_SCAN_PREVIOUS);
          hasCosumerKeyPressed = true;
          delay(5);
        }
        break;
      }
  }
}


void doubleclick1() {
  Serial.println("Button 1 doubleclick.");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_F9, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_KEYPAD_SUBTRACT, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_F9, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
  }
}

void longPressStart1() {
  Serial.println("Button 1 longPress start");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_GUI_LEFT, HID_KEY_ALT_LEFT, HID_KEY_R, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ARROW_LEFT, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_GUI_LEFT, HID_KEY_ALT_LEFT, HID_KEY_R, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
  }
}

void longPress1() {
  Serial.println("Button 1 longPress...");
  fct_WatchdogReset();
}

void longPressStop1() {
  Serial.println("Button 1 longPress stop");
  fct_WatchdogReset();
}

void click2() {
  Serial.println("Button 2 click.");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ENTER, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_SPACE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          blehid.consumerKeyPress(0, HID_USAGE_CONSUMER_PLAY_PAUSE);
          hasCosumerKeyPressed = true;
          delay(5);
        }
        break;
      }
  }
}

void doubleclick2() {
  Serial.println("Button 2 doubleclick.");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ESCAPE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_TAB, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ESCAPE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
  }
}

void longPressStart2() {
  Serial.println("Button 2 longPress start");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_H, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ENTER, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ALT_LEFT, HID_KEY_TAB, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(25);
        }
        break;
      }
  }
}

void longPress2() {
  Serial.println("Button 2 longPress...");
  fct_WatchdogReset();
}

void longPressStop2() {
  Serial.println("Button 2 longPress stop");
  fct_WatchdogReset();
}

void click3() {
  Serial.println("Button 3 click.");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ARROW_RIGHT, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_PAGE_UP, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          blehid.consumerKeyPress(0, HID_USAGE_CONSUMER_SCAN_NEXT);
          hasCosumerKeyPressed = true;
          delay(5);
        }
        break;
      }
  }
}

void doubleclick3() {
  Serial.println("Button 3 doubleclick.");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_F10, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_KEYPAD_ADD, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_F10, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
  }
}

void longPressStart3() {
  Serial.println("Button 3 longPress start");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_GUI_LEFT, HID_KEY_ALT_LEFT, HID_KEY_G, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ARROW_RIGHT, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_GUI_LEFT, HID_KEY_ALT_LEFT, HID_KEY_G, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
  }
}

void longPress3() {
  Serial.println("Button 3 longPress...");
  fct_WatchdogReset();
}

void longPressStop3() {
  Serial.println("Button 3 longPress stop");
  fct_WatchdogReset();
}

void click4() {
  Serial.println("Button 4 click.");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ARROW_DOWN, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_G, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          blehid.consumerKeyPress(0, HID_USAGE_CONSUMER_MUTE);
          hasCosumerKeyPressed = true;
          delay(5);
        }
        break;
      }
  }
}


void doubleclick4() {
  Serial.println("Button 4 doubleclick.");
  fct_WatchdogReset();
  if (!hasCosumerKeyPressed && !hasKeyPressed) {
    switch (jelenlegiUzemmod) {
      case normalUzemmod:
        jelenlegiUzemmod = versenyEdzesUzemmod;
        break;
      case versenyEdzesUzemmod:
        jelenlegiUzemmod = mediaVezerloUzemmod;
        break;
      case mediaVezerloUzemmod:
        jelenlegiUzemmod = normalUzemmod;
        break;
    }
    hasKeyPressed = true;
    delay(5);
  }
}

void longPressStart4() {
  Serial.println("Button 4 longPress start");
  fct_WatchdogReset();
}

void longPress4() {
  Serial.println("Button 4 longPress...");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ARROW_DOWN, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = false;
          duringLongpress = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ARROW_DOWN, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = false;
          duringLongpress = true;
          delay(50);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          blehid.consumerKeyPress(0, HID_USAGE_CONSUMER_VOLUME_DECREMENT);
          hasCosumerKeyPressed = false;
          hasKeyPressed = false;
          duringLongpress = true;
          delay(50);
        }
        break;
      }
  }
}

void longPressStop4() {
  Serial.println("Button 4 longPress stop");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        hasKeyPressed = true;
        duringLongpress = false;
        break;
      }
    case versenyEdzesUzemmod:
      {
        hasKeyPressed = true;
        duringLongpress = false;
        break;
      }
    case mediaVezerloUzemmod:
      {
        hasCosumerKeyPressed = true;
        hasKeyPressed = true;
        duringLongpress = false;
        break;
      }
  }
}

void click5() {
  Serial.println("Button 5 click.");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ARROW_UP, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_E, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          blehid.consumerKeyPress(0, HID_USAGE_CONSUMER_AL_CONSUMER_CONTROL_CONFIGURATION);
          hasCosumerKeyPressed = true;
          delay(35);
        }
        break;
      }
  }
}

void doubleclick5() {
  Serial.println("Button 5 doubleclick.");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          nezet++;
          uint8_t HID_KEYS[9] = { HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4, HID_KEY_5, HID_KEY_6, HID_KEY_7, HID_KEY_8, HID_KEY_9 };
          uint8_t keycodeView[6] = { HID_KEYS[nezet - 1], HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodeView);
          if (nezet == 9) {
            nezet = 0;
          }
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          nezet++;
          uint8_t HID_KEYS[9] = { HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4, HID_KEY_5, HID_KEY_6, HID_KEY_7, HID_KEY_8, HID_KEY_9 };
          uint8_t keycodeView[6] = { HID_KEYS[nezet - 1], HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodeView);
          if (nezet == 9) {
            nezet = 0;
          }
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        nezet++;
        uint8_t HID_KEYS[9] = { HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4, HID_KEY_5, HID_KEY_6, HID_KEY_7, HID_KEY_8, HID_KEY_9 };
        uint8_t keycodeView[6] = { HID_KEYS[nezet - 1], HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
        blehid.keyboardReport(0, keycodeView);
        if (nezet == 9) {
          nezet = 0;
        }
        hasKeyPressed = true;
        break;
      }
  }
}

void longPressStart5() {
  Serial.println("Button 5 longPress start");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_T, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case versenyEdzesUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          uint8_t keycodes[6] = { HID_KEY_ARROW_UP, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
          blehid.keyboardReport(0, keycodes);
          hasKeyPressed = true;
          delay(5);
        }
        break;
      }
    case mediaVezerloUzemmod:
      {
        break;
      }
  }
}

void longPress5() {
  Serial.println("Button 5 longPress...");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        break;
      }
    case versenyEdzesUzemmod:
      {
        break;
      }
    case mediaVezerloUzemmod:
      {
        if (!hasCosumerKeyPressed && !hasKeyPressed) {
          blehid.consumerKeyPress(0, HID_USAGE_CONSUMER_VOLUME_INCREMENT);
          hasCosumerKeyPressed = false;
          hasKeyPressed = false;
          duringLongpress = true;
          delay(50);
        }
        break;
      }
  }
}

void longPressStop5() {
  Serial.println("Button 5 longPress stop");
  fct_WatchdogReset();
  switch (jelenlegiUzemmod) {
    case normalUzemmod:
      {
        break;
      }
    case versenyEdzesUzemmod:
      {
        break;
      }
    case mediaVezerloUzemmod:
      {
        hasCosumerKeyPressed = true;
        hasKeyPressed = true;
        duringLongpress = false;
        break;
      }
  }
}
