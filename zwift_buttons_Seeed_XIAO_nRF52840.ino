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
// A fireAction() hamarabb hivja, mint ahol definialva van; statikus
// fuggvenyhez az Arduino nem general automatikus prototipust.
static void startBurst(const KeyAction& a);
// Ugyanezert: a fociklus hamarabb hasznalja, mint a definiciojuk helye.
static void hidReleaseAll();
static bool hidHolding();
static void cancelBurst();

BLEDis bledis;
BLEHidAdafruit blehid;
// Akkumulátor-szint szolgáltatás: a telefon és a Windows is meg tudja mutatni.
BLEBas blebas;

// ---------------------------------------------------------------------------
// Hardveres watchdog
//
// A fct_Watchdog() név ellenére az csak tétlenségi számláló az alváshoz. Ez itt
// a chip WDT perifériája: ha a főciklus ZW_WDT_TIMEOUT_MS ideig nem eteti meg,
// a chip magától újraindul. Enélkül egy megakadt firmware (végtelen ciklus,
// holtpont) halott eszközt jelentene, amit csak kézzel lehetne újraindítani —
// kerékpáron ez menet közbeni használhatatlanságot jelent.
//
// Alvásban (System OFF) a WDT nem fut: a System OFF a GPIO / LPCOMP / NFC
// kivételével minden perifériát letilt, tehát nem ébreszti fel az eszközt.
// ---------------------------------------------------------------------------
static void hwWatchdogBegin() {
  // HALT:Pause = hibakereső alatt megáll, SLEEP:Run = alvó CPU mellett is számol
  NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos)
                    | (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
  // A számláló a 32768 Hz-es órajelről megy.
  NRF_WDT->CRV = (uint32_t)((32768ULL * ZW_WDT_TIMEOUT_MS) / 1000ULL) - 1;
  NRF_WDT->RREN = WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
}

static inline void hwWatchdogFeed() {
  NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}

// ---------------------------------------------------------------------------
// A XIAO nRF52840 akkumulátor-kezelő lábai
//
// A számok a Seeed board csomag variant.h-jából valók, és Arduino-lábszámok,
// NEM a chip P0.xx sorszámai (a variant.cpp g_ADigitalPinMap tömbje köti össze
// a kettőt): D32 -> P0.31, D14 -> P0.14, D22 -> P0.13, D23 -> P0.17.
//
// FIGYELEM: több, interneten keringő lábkiosztás-ábra a VBAT-ot 35-nek írja.
// Az itteni board csomagban ilyen láb nincs (PINS_COUNT = 33), és az
// analogRead() a tartományon kívüli lábra némán 0-t ad — vagyis örökre 0%
// töltöttséget. Ezért az alábbi ellenőrzés: ha a board csomag valaha mást
// mondana, álljon meg a fordítás, ne csendben rosszul mérjünk.
// ---------------------------------------------------------------------------
#define ZW_VBAT_PIN            32  // P0.31 (AIN7) – akkumulátor-feszültség
#define ZW_VBAT_ENABLE_PIN     14  // P0.14 – LOW engedélyezi a mérést
#define ZW_CHARGE_CURRENT_PIN  22  // P0.13 (HICHG) – LOW = 100 mA, HIGH = 50 mA
#define ZW_CHARGE_STATE_PIN    23  // P0.17 (~CHG) – LOW = tölt, HIGH = kész/nem tölt

#if defined(PIN_VBAT) && (PIN_VBAT != ZW_VBAT_PIN)
#error "A board csomag PIN_VBAT erteke mas, mint a ZW_VBAT_PIN - ellenorizd a variant.h-t!"
#endif
#if defined(VBAT_ENABLE) && (VBAT_ENABLE != ZW_VBAT_ENABLE_PIN)
#error "A board csomag VBAT_ENABLE erteke mas, mint a ZW_VBAT_ENABLE_PIN!"
#endif
#if defined(PIN_CHARGING_CURRENT) && (PIN_CHARGING_CURRENT != ZW_CHARGE_CURRENT_PIN)
#error "A board csomag PIN_CHARGING_CURRENT erteke mas, mint a ZW_CHARGE_CURRENT_PIN!"
#endif

const int ledPin[] = { 11, 12, 13 };  //red, blue, green
// P0.13 (HICHG). A Seeed dokumentációja szerint LOW = nagy (100 mA), HIGH =
// kis (50 mA) töltőáram. A lábat LOW-ra állítjuk, tehát 100 mA-rel töltünk.
const int pin_charging_current = ZW_CHARGE_CURRENT_PIN;
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

// Időzített küldés (rövid / dupla nyomás). A hosszú nyomásnál a gomb
// elengedése zárja le a küldést, itt viszont egy előre beállított idő. Amíg
// tart, a duringLongpress jelző miatt más gomb parancsa nem mehet ki.
// A repeatButton ezt kapja időzített küldés alatt: nem létező gomb sorszáma,
// így az onLongDuring()/onLongStop() egyetlen valódi gombra sem egyezik rá.
#define ZW_BURST_SLOT 100
static bool burstActive = false;
static unsigned long burstEndMillis = 0;
static KeyAction burstAction;

// Az üzemmód-LED csak felvillan, nem ég folyamatosan (lásd ZW_LED_ON_MS).
static unsigned long ledOffMillis = 0;
static bool ledsLit = false;

// Beragadt gomb felismerése. A hosszú nyomás kezdete gombonként, és egy
// bitmaszk azokról, amelyeket már beragadtnak tekintünk.
static unsigned long longPressStartMillis[ZW_NUM_BUTTONS] = { 0 };
static uint8_t stuckButtons = 0;

// Mit küld éppen az ismétlés. Nyomva tartás közben megváltozhat az üzemmód, és
// vele a művelet is; ha a kiküldött billentyű vagy a célpont más lenne, az
// ismétlést le kell zárni — különben a RÉGI billentyű felengedetlenül maradna
// (a felengedés fajtája és címzettje ugyanis az új műveletet követné).
static uint8_t repeatType = ACT_NONE;
static uint16_t repeatCode = 0;
static uint8_t repeatModifier = 0;
static uint8_t repeatMask = 0;

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

// Melyik tüskén van a Gomb 1..5. Ugyanaz, amit a fenti OneButton példányok
// használnak; azért kell külön is, hogy a bekapcsoláskor már nyomott gombokat
// fel tudjuk ismerni (a OneButton-t akkor még meg sem kérdezhetjük).
static const uint8_t BUTTON_PIN_INDEX[] = { 1, 2, 3, 4, 0 };

// A gombok sorrendje a kiosztás második indexe: Gomb 1..5.
static OneButton* const buttons[] = {
  &button1, &button2, &button3, &button4, &button5
};
// Ha a ZW_NUM_BUTTONS változik, itt is fel kell venni a gombot: e nélkül a
// tömb csendben nullákkal töltődne, és az updateButtons() null mutatót
// dereferálna az első körben.
static_assert(sizeof(buttons) / sizeof(buttons[0]) == ZW_NUM_BUTTONS,
              "a buttons[] tomb es a ZW_NUM_BUTTONS nem egyezik");
static_assert(sizeof(BUTTON_PIN_INDEX) / sizeof(BUTTON_PIN_INDEX[0]) == ZW_NUM_BUTTONS,
              "a BUTTON_PIN_INDEX es a ZW_NUM_BUTTONS nem egyezik");


#define WAKEUP_PIN 2

TickTwo watchDOG(fct_Watchdog, 1000, 0, MILLIS);

#define watchdogMinCounter 0  // reset value for watchdog counter
static uint32_t watchdogCounter = watchdogMinCounter;

Adafruit_FlashTransport_QSPI flashTransport;

uzemmod jelenlegiUzemmod = normalUzemmod;


// ---------------------------------------------------------------------------
// Akkumulátor-szint
//
// Az osztó 1 MΩ / 510 kΩ (a lábakat lásd fentebb), tehát a mért feszültséget
// 1510/510 arányban kell visszaszorozni.
//
// FIGYELEM: a VBAT_ENABLE-t végig LOW-on hagyjuk. Seeed figyelmeztetése szerint
// HIGH állapotban a PIN_VBAT töltés közben a megengedett 3,6 V fölé kerülhet.
// Az osztó folyamatos fogyasztása elhanyagolható (~2,8 µA): egy több éves
// alvási energiamérleg mellett nem számít, a láb védelme viszont igen.
// ---------------------------------------------------------------------------

static unsigned long lastBatteryMillis = 0;

// Tölt-e éppen az eszköz. Töltés közben a töltő a cellát a végfeszültségen
// (~4,2 V) tartja, ezért a mért érték a valódi töltöttségnél magasabbat mutat —
// ezt önmagából a feszültségből nem lehet kitalálni, ezért olvassuk ki.
static bool batteryCharging() {
  return digitalRead(ZW_CHARGE_STATE_PIN) == LOW;
}

// Elindult-e egyáltalán az akkumulátor-szolgáltatás. Ha a GATT tábla betelne,
// a blebas.begin() csendben hibát adna, és a szolgáltatás egyszerűen nem
// létezne — a hoston pontosan úgy néz ki, mintha nem is küldenénk töltöttséget.
// Ezért eltároljuk, és a `BAT` paranccsal le is lehet kérdezni.
static bool basStarted = false;

// 12 bit felbontas, 3,0 V-os belso referencia: 3000 mV / 4096 lepes, majd
// vissza az 1 MΩ / 510 kΩ osztón.
static uint16_t millivoltsFromAdc(uint32_t raw) {
  uint32_t adcMv = (raw * 3000UL) / 4096UL;
  return (uint16_t)((adcMv * 1510UL) / 510UL);
}

static uint16_t readBatteryMillivolts() {
  return millivoltsFromAdc((uint32_t)analogRead(ZW_VBAT_PIN));
}

// Feszültség -> töltöttség. A LiPo kisülési görbéje nem lineáris: a
// 3,7-3,9 V közötti szűk sávban van a kapacitás nagy része, ezért egy egyenes
// nagyot tévedne (3,7 V-ra például kétszer annyit mutatna a valóságosnál).
// Ezért töréspontos táblázatot használunk, köztük lineáris átmenettel.
static uint8_t batteryPercent(uint16_t mv) {
  static const struct { uint16_t mv; uint8_t pct; } GORBE[] = {
    { 4200, 100 }, { 4060, 90 }, { 3980, 80 }, { 3920, 70 }, { 3870, 60 },
    { 3820, 50 }, { 3790, 40 }, { 3770, 30 }, { 3740, 20 }, { 3680, 10 },
    { 3450, 5 }, { 3000, 0 }
  };
  const uint8_t n = sizeof(GORBE) / sizeof(GORBE[0]);
  if (mv >= GORBE[0].mv) return 100;
  if (mv <= GORBE[n - 1].mv) return 0;
  for (uint8_t i = 1; i < n; i++) {
    if (mv >= GORBE[i].mv) {
      // lineáris átmenet a két töréspont között
      uint16_t alsoMv = GORBE[i].mv, felsoMv = GORBE[i - 1].mv;
      uint8_t alsoP = GORBE[i].pct, felsoP = GORBE[i - 1].pct;
      return (uint8_t)(alsoP + ((uint32_t)(mv - alsoMv) * (felsoP - alsoP))
                                 / (felsoMv - alsoMv));
    }
  }
  return 0;
}

// Amit utoljára ki is értesítettünk. 0xFF = még semmit (a százalék 0…100).
static uint8_t lastBatteryPercent = 0xFF;
// Amit a GATT adatbázisban tartunk. Ezt olvassa ki a host csatlakozáskor, és
// ezt toljuk ki egy frissen csatlakozott eszköznek is.
static uint8_t currentBatteryPercent = 0;

static void updateBattery(bool force) {
  unsigned long now = millis();
  if (!force && (now - lastBatteryMillis) < ZW_BATTERY_UPDATE_MS) return;
  lastBatteryMillis = now;
  uint8_t pct = batteryPercent(readBatteryMillivolts());
  currentBatteryPercent = pct;

  // A helyi attribútum-érték frissítése. A BLEBas::write() a
  // sd_ble_gatts_value_set()-et hívja BLE_CONN_HANDLE_INVALID-dal, tehát a GATT
  // adatbázisba ír: kapcsolatfüggetlen. Ezért két csatlakozott eszköznél is
  // ugyanazt az értéket olvassa ki mindkettő, a kapcsolat megszakadása pedig
  // nem érinti — nincs mit "elveszíteni", nincs mit újrapróbálni.
  blebas.write(pct);

  if (pct == lastBatteryPercent) return;

  // Az értesítés viszont már kapcsolatonkénti. A paraméter nélküli
  // BLEBas::notify() a Bluefruit.connHandle()-t használná, ami CSAK EGY
  // kapcsolatot jelent (az utoljára felépültet), sőt annak bontásakor akkor is
  // érvénytelenre áll, ha a másik kapcsolat még él — két eszköznél tehát a
  // rossz helyre vagy sehová sem menne. Ezért kézzel megyünk végig az élő
  // kapcsolatokon, ugyanúgy, ahogy a HID jelentéseknél.
  //
  // Amíg egy parancs a levegőben van, kihagyjuk: az értesítés ugyanabból a
  // kapcsolatonkénti HVN pufferből menne, mint a billentyű-jelentés, és nem ez
  // a fontosabb. Egy perc múlva úgyis újra próbáljuk.
  if (hasKeyPressed || hasConsumerKeyPressed || burstActive || duringLongpress) return;

  lastBatteryPercent = pct;
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (connHandles[i] != BLE_CONN_HANDLE_INVALID) blebas.notify(connHandles[i], pct);
  }
}

// ---------------------------------------------------------------------------
// A host GATT gyorsítótárának érvénytelenítése
//
// A párosított host (Windows, iPhone) ELTÁROLJA a szolgáltatás-táblánkat, és
// újracsatlakozáskor nem deríti fel újra — a Bluetooth szabvány kifejezetten
// megengedi neki (Core Spec Vol 3, Part G: a bondolt kliens gyorsítótárazhatja
// a szerver attribútumait). Ennek a gyakorlati következménye az, hogy egy
// firmware-frissítésben ÚJONNAN felvett szolgáltatás — például az
// akkumulátor-szint — a már párosított gépen soha nem jelenik meg: a host a
// régi, mentett táblát használja, amiben az még nincs benne. A HID rész
// közben hibátlanul működik, mert az nem változott.
//
// Az egyetlen szabványos módja, hogy ezt megüzenjük: a GATT "Service Changed"
// indikáció. Magát a jellemzőt a SoftDevice adja hozzá (a Bluefruit alapból
// bekapcsolja), de kiküldeni nekünk kell — a könyvtár ezt nem teszi meg
// helyettünk.
//
// Miért nem rögtön a csatlakozási visszahívásból: az indikáció csak akkor megy
// ki, ha a peer engedélyezte rá az értesítést, és ezt a beállítást bondolt
// eszköznél a mentett rendszer-attribútumokból állítja vissza a könyvtár —
// ami a csatlakozás pillanatában még nem feltétlenül történt meg. Ezért
// késleltetve, a főciklusból próbálkozunk, és hiba esetén megismételjük.
// ---------------------------------------------------------------------------

// Kapcsolatonként (a connHandles-szel azonos indexeléssel): hány próbálkozás
// van még hátra, és mikor esedékes a következő.
static uint8_t gattChangedTries[ZW_MAX_CONNECTIONS];
static unsigned long gattChangedDue[ZW_MAX_CONNECTIONS];
// Ugyanígy: kell-e még kitolni az aktuális töltöttséget az új kapcsolatnak.
static bool batteryAnnounce[ZW_MAX_CONNECTIONS];
static unsigned long batteryAnnounceDue[ZW_MAX_CONNECTIONS];

static void clearConnTasks(uint8_t slot) {
  gattChangedTries[slot] = 0;
  batteryAnnounce[slot] = false;
}

// Az alkalmazás saját attribútumainak első handle-je: a SoftDevice a GAP és a
// GATT szolgáltatást a tábla elején magának tartja fenn, a "megváltozott"
// tartomány e fölött kezdődik.
static uint16_t appHandleStart() {
  uint16_t start = 0;
  if (sd_ble_gatts_initial_user_handle_get(&start) != NRF_SUCCESS || start == 0) {
    start = 0x000C;   // a SoftDevice szokásos első szabad handle-je
  }
  return start;
}

static void updateGattChanged() {
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (gattChangedTries[i] == 0) continue;

    uint16_t h = connHandles[i];
    if (h == BLE_CONN_HANDLE_INVALID || !Bluefruit.connected(h)) {
      gattChangedTries[i] = 0;
      continue;
    }
    // Az előjeles különbség a millis() túlcsordulásán is helyesen dönt.
    if ((long)(millis() - gattChangedDue[i]) < 0) continue;

    gattChangedTries[i]--;

    // Párosítatlan eszköz nem tárol el semmit két kapcsolat között: az minden
    // csatlakozáskor felderíti a teljes táblát, nincs mit érvényteleníteni.
    // Ez viszont "még nem párosított"-at is jelenthet — a titkosítás felépülése
    // eltarthat egy ideig —, ezért nem adjuk fel azonnal, hanem a többi hibával
    // azonos módon újrapróbáljuk, amíg van próbálkozás.
    BLEConnection* conn = Bluefruit.Connection(h);
    if (conn == NULL || !conn->bonded()) {
      if (gattChangedTries[i] > 0) gattChangedDue[i] = millis() + ZW_GATT_CHANGED_RETRY_MS;
      continue;
    }

    uint32_t err = sd_ble_gatts_service_changed(h, appHandleStart(), 0xFFFF);
    if (err == NRF_SUCCESS) {
      gattChangedTries[i] = 0;
      if (debugSerial) {
        Serial.print("GATT-valtozas jelezve, conn_hdl=");
        Serial.println(h);
      }
    } else if (gattChangedTries[i] == 0) {
      // Elfogytak a próbálkozások. Nem végzetes: a host attól még működik,
      // csak az újonnan felvett szolgáltatást nem fogja látni, amíg a
      // párosítást kézzel meg nem újítják.
      if (debugSerial) {
        Serial.print("A GATT-valtozast nem sikerult jelezni, conn_hdl=");
        Serial.print(h);
        Serial.print(" hibakod=");
        Serial.println((unsigned)err);
      }
    } else {
      gattChangedDue[i] = millis() + ZW_GATT_CHANGED_RETRY_MS;
    }
  }
}

// Egy frissen csatlakozott eszköznek egyszer kitoljuk az aktuális töltöttséget,
// hogy ne kelljen megvárnia a következő percenkénti frissítést.
static void updateBatteryAnnounce() {
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (!batteryAnnounce[i]) continue;

    uint16_t h = connHandles[i];
    if (h == BLE_CONN_HANDLE_INVALID || !Bluefruit.connected(h)) {
      batteryAnnounce[i] = false;
      continue;
    }
    if ((long)(millis() - batteryAnnounceDue[i]) < 0) continue;

    // Ugyanaz a megfontolás, mint az updateBattery()-ben: amíg egy parancs a
    // levegőben van, ne versenyezzünk vele ugyanazért a HVN pufferért.
    if (hasKeyPressed || hasConsumerKeyPressed || burstActive || duringLongpress) continue;

    batteryAnnounce[i] = false;
    blebas.notify(h, currentBatteryPercent);
  }
}

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
  // A LED-ek aktív-alacsonyak, a pinMode(OUTPUT) viszont csak az irányt állítja:
  // az OUT regiszter reset után 0, tehát a láb LOW-ra állna, és mind a három
  // LED kigyulladna (fehéren) az indulás hátralévő részére — a BLE és a
  // fájlrendszer indítása alatt. Ezért előbb írjuk HIGH-ra: a digitalWrite az
  // OUTSET/OUTCLR regisztert állítja, ami bemenetként is érvényes, és a láb már
  // magas szinttel vált kimenetre. Az üzemmód színét az első updateLeds() adja.
  for (int i = 0; i < numOfLeds; i++) {  //ledek
    digitalWrite(ledPin[i], HIGH);
    pinMode(ledPin[i], OUTPUT);
  }
  for (int i = 0; i < numOfButtons; i++) {
    pinMode(BUTTON_PIN[i], INPUT_PULLUP);
  }
  pinMode(WAKEUP_PIN, INPUT_PULLUP_SENSE);
  // Töltőáram: a Seeed dokumentációja szerint LOW = 100 mA (nagy áram),
  // HIGH = 50 mA. Itt a 100 mA-es töltést választjuk.
  pinMode(pin_charging_current, OUTPUT);
  digitalWrite(pin_charging_current, LOW);
  // A töltésjelző láb bemenet: a BQ25101 nyitott nyelőjű kimenete húzza le,
  // felhúzásról a panelen a töltésjelző LED köre gondoskodik.
  pinMode(ZW_CHARGE_STATE_PIN, INPUT);

  // Akkumulátor-mérés előkészítése. A VBAT_ENABLE végig LOW marad (lásd a
  // readBatteryMillivolts() fölötti megjegyzést).
  pinMode(ZW_VBAT_ENABLE_PIN, OUTPUT);
  digitalWrite(ZW_VBAT_ENABLE_PIN, LOW);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  // A mintavételi idő NEM hagyható alapértelmezetten. Az osztó forrás-
  // ellenállása 1 MΩ ∥ 510 kΩ = 338 kΩ; az nRF52840 adatlapja szerint 400 kΩ-ig
  // 20 µs mintavételi idő kell. A könyvtár alapértelmezése viszont 3 µs, ami
  // csak 10 kΩ-ig elég: ennyi idő alatt a mintavevő kondenzátor nem töltődik
  // fel a bemeneti feszültségre, ezért a mérés rendszeresen ALACSONYABBAT ad a
  // valóságosnál — és vele a jelentett töltöttség is kevesebb. A 40 µs a
  // tartomány teteje, tartalékkal; percenként egyszer mérünk, az ára semmi.
  analogSampleTime(40);
  // Nyolc minta hardveres átlagolása egyetlen analogRead()-en belül. A BLE adás
  // áramlökései megrántják a tápot és vele az osztó kimenetét is; átlagolás
  // nélkül emiatt ugrálna a jelentett százalék.
  analogOversampling(8);

  NRF_POWER->DCDCEN = 1;

  attachButtonCallbacks();

  // Bekapcsoláskor (és ébredéskor, ami szintén újraindulás) már nyomott gomb
  // szinte biztosan beragadt: a bekapcsolás pillanatában nem tartja senki
  // nyomva. Ilyenkor eleve figyelmen kívül hagyjuk, amíg fel nem engedik —
  // így nem kell megvárni a 30 másodperces felismerést, és a gomb parancsa
  // egyszer sem megy ki tévedésből.
  for (uint8_t i = 0; i < ZW_NUM_BUTTONS; i++) {
    if (digitalRead(BUTTON_PIN[BUTTON_PIN_INDEX[i]]) == LOW) {
      stuckButtons |= (uint8_t)(1 << i);
      Serial.print("Gomb ");
      Serial.print(i + 1);
      Serial.println(" mar indulaskor nyomva - figyelmen kivul hagyom, amig fel nem engedik");
    }
  }

  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    connHandles[i] = BLE_CONN_HANDLE_INVALID;
    clearConnTasks(i);
  }

  Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16);
  // Két kapcsolat több SoftDevice-RAM-ot igényel; ha nem fér el, ezt tudni kell.
  if (!Bluefruit.begin(ZW_MAX_CONNECTIONS, 0)) {
    Serial.println("HIBA: a BLE stack nem indult el (keves a RAM?)");
  }
  Bluefruit.setTxPower(4);
  Bluefruit.autoConnLed(false);
  Bluefruit.setName("SEEED_ZWIFT");
  bledis.setManufacturer("Manszabi");
  bledis.setModel("ZWIFT_button");
  bledis.begin();
  blehid.begin();
  // A visszatérési értéket meg KELL nézni: ha a GATT attribútum-tábla betelne,
  // a szolgáltatás csendben létre sem jönne, és a hoston ez pontosan úgy néz
  // ki, mintha nem küldenénk töltöttséget.
  uint32_t basErr = blebas.begin();
  basStarted = (basErr == 0);
  if (!basStarted) {
    Serial.print("HIBA: az akkumulator-szolgaltatas nem indult el, hibakod=");
    Serial.println((unsigned)basErr);
  }
  updateBattery(true);   // legyen mit mutatnia a csatlakozás pillanatában
  Bluefruit.Periph.setConnInterval(9, 12);
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
  startAdv();

  // A hardveres watchdogot utoljára indítjuk: az indulás hosszabb műveletei
  // (fájlrendszer, BLE) így nem futhatnak bele.
  hwWatchdogBegin();
}

void loop() {

  uzemmod elozoUzemmod = jelenlegiUzemmod;
  updateLeds();

  // A leütés-impulzus lezárása a gomb-tickek ELŐTT fut: így egy elhúzódó kör
  // után is kimegy a felengedés, mielőtt a következő ismétlés elindulna.
  updateRepeatTap();
  updateBurst();
  updateButtons();
  handleSerial();
  pruneLostTargets();

  if ((hasKeyPressed || hasConsumerKeyPressed) && keyPressMillis == 0) {
    keyPressMillis = millis();
  }

  if (Bluefruit.connected()) {
    unsigned long currentMillis = millis();

    if (!duringLongpress && keyPressMillis > 0 && (currentMillis - keyPressMillis >= keyReleaseDelay)) {
      hidReleaseAll();
    }
  }

  if (elozoUzemmod != jelenlegiUzemmod) {
    Serial.println("A jelenlegiUzemmod erteke megvaltozott.");
    if (jelenlegiUzemmod == normalUzemmod) saveUzemmod(CONTENTNormal);
    else if (jelenlegiUzemmod == versenyEdzesUzemmod) saveUzemmod(CONTENTVerseny);
    else if (jelenlegiUzemmod == mediaVezerloUzemmod) saveUzemmod(CONTENTMedia);
  }

  // A hardveres watchdog etetése: ha a főciklus megakad, a chip újraindul.
  hwWatchdogFeed();
  updateBattery(false);
  // Csatlakozás utáni teendők: a host GATT gyorsítótárának érvénytelenítése,
  // majd az aktuális töltöttség kitolása az új eszköznek.
  updateGattChanged();
  updateBatteryAnnounce();

  watchDOG.update();
  // A delay() ezen a magon vTaskDelay: átadja a vezérlést az ütemezőnek (és
  // üríti az USB CDC puffert), tehát nem foglalja a processzort. Amíg időzített
  // dolgunk van — leütés-impulzus, ismétlés, időzített küldés —, sűrűbben
  // ébredünk, hogy a felengedés pontos maradjon; egyébként ritkábban, ami
  // energiatakarékosabb.
  delay((repeatTapPending || duringLongpress || burstActive) ? 5 : 20);
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
      // A tényleges munkát a főciklus végzi el: ez a visszahívás másik
      // (magasabb prioritású) taskról fut, itt csak feljegyezzük a teendőt.
      gattChangedTries[i] = ZW_GATT_CHANGED_TRIES;
      gattChangedDue[i] = millis() + ZW_GATT_CHANGED_DELAY_MS;
      batteryAnnounce[i] = true;
      batteryAnnounceDue[i] = millis() + ZW_BATTERY_ANNOUNCE_MS;
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
  uint8_t used = 0;
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (connHandles[i] == conn_handle) {
      connHandles[i] = BLE_CONN_HANDLE_INVALID;
      clearConnTasks(i);
    }
  }
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (connHandles[i] != BLE_CONN_HANDLE_INVALID) used++;
  }

  if (debugSerial) {
    Serial.print("BLE bontva, conn_hdl=");
    Serial.print(conn_handle);
    Serial.print(" (");
    Serial.print(used);
    Serial.print("/");
    Serial.print(ZW_MAX_CONNECTIONS);
    Serial.println(")");
  }

  // A hirdetést magunknak kell újraindítani, ha maradt még kapcsolat: a
  // Bluefruit csak akkor teszi meg helyettünk, ha MINDEN kapcsolat megszűnt
  // (BLEAdvertising.cpp, BLE_GAP_EVT_DISCONNECTED: "0 == Bluefruit.Periph.connected()").
  // Enélkül két eszköz esetén a kieső eszköz soha nem tudna visszacsatlakozni,
  // amíg a másik kapcsolat él.
  if (used > 0 && used < ZW_MAX_CONNECTIONS && !Bluefruit.Advertising.isRunning()) {
    Bluefruit.Advertising.start(0);
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
    // Ha az ébresztő gomb épp nyomva van (tipikusan mert beragadt), az alvás
    // azonnali ébredéssel és újraindulással járna: a System OFF-ot a DETECT jel
    // rögtön megszakítja. Ilyenkor inkább ébren maradunk, és másodpercenként
    // újrapróbáljuk — amint a gomb felenged, elalszunk.
    if (digitalRead(WAKEUP_PIN) == LOW) {
      watchdogCounter = (uint32_t)offDelay;
      return;
    }
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

  // Üzemmódváltáskor (és bekapcsoláskor, mert a shownMode -1-ről indul)
  // felvillan az adott szín.
  if (shownMode != (int8_t)jelenlegiUzemmod) {
    shownMode = (int8_t)jelenlegiUzemmod;
    for (int i = 0; i < numOfLeds; i++) {
      digitalWrite(ledPin[i], HIGH);
    }
    if ((uint8_t)jelenlegiUzemmod < (uint8_t)numOfLeds) {
      digitalWrite(ledPin[(uint8_t)jelenlegiUzemmod], LOW);
    }
    ledsLit = true;
    ledOffMillis = millis() + ZW_LED_ON_MS;
    return;
  }

  // A beállított idő letelte után elalszik: folyamatosan égve a LED fogyasztana
  // a legtöbbet az eszközön.
  if (ledsLit && (long)(millis() - ledOffMillis) >= 0) {
    ledsLit = false;
    for (int i = 0; i < numOfLeds; i++) {
      digitalWrite(ledPin[i], HIGH);
    }
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
                      uint8_t repeat, uint16_t repeatMs, uint8_t target,
                      uint16_t holdMs) {
  KeyAction& a = keymap.map[mode][btn][evt];
  a.type = type;
  a.modifier = modifier;
  a.code = code;
  a.repeat = repeat;
  a.target = target;
  a.repeatMs = repeatMs;
  a.holdMs = holdMs;
}

static void setAction(uint8_t mode, uint8_t btn, uint8_t evt,
                      uint8_t type, uint8_t modifier, uint16_t code,
                      uint8_t repeat, uint16_t repeatMs, uint8_t target) {
  setAction(mode, btn, evt, type, modifier, code, repeat, repeatMs, target, 0);
}

static void setAction(uint8_t mode, uint8_t btn, uint8_t evt,
                      uint8_t type, uint8_t modifier, uint16_t code,
                      uint8_t repeat, uint16_t repeatMs) {
  setAction(mode, btn, evt, type, modifier, code, repeat, repeatMs, 0, 0);
}

// A mentés fejléce. Három helyen kell ugyanez (gyári kiosztás, mentés, régi
// formátum átvétele), ezért egy helyen tartjuk: ha a formátum változik, itt kell
// hozzányúlni, nem három párhuzamos másolatban.
static void initKeymapHeader() {
  keymap.magic = ZW_KEYMAP_MAGIC;
  keymap.version = ZW_KEYMAP_VERSION;
  keymap.entrySize = sizeof(KeyAction);
  keymap.modes = ZW_NUM_MODES;
  keymap.buttons = ZW_NUM_BUTTONS;
  keymap.events = ZW_NUM_EVENTS;
  keymap.slots = ZW_NUM_SLOTS;
}

// A gyári kiosztás: ugyanaz, ami korábban be volt drótozva a kódba.
void loadDefaultKeymap() {
  // A cél-eszköz hozzárendelés (melyik a PC, melyik a telefon) túléli a gyári
  // visszaállítást: az a fizikai eszközökről szól, nem a gomb-kiosztásról.
  PeerSlot savedPeers[ZW_NUM_SLOTS];
  memcpy(savedPeers, keymap.peers, sizeof(savedPeers));

  memset(&keymap, 0, sizeof(keymap));
  memcpy(keymap.peers, savedPeers, sizeof(savedPeers));
  initKeymapHeader();

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

  // Ahol nincs ismétlés, ott az ismétlési idő önmagában nem jelent semmit — de
  // a SET parancs a 0-t úgyis 60-ra alakítja. Ha itt 0 maradna, a beolvasás →
  // visszaküldés kör csendben megváltoztatná az értéket, és a konfiguráló
  // program adatfájlja sem egyezne az eszköz kiosztásával. Ezért itt is a
  // ugyanaz az alapérték.
  for (uint8_t m = 0; m < ZW_NUM_MODES; m++) {
    for (uint8_t b = 0; b < ZW_NUM_BUTTONS; b++) {
      for (uint8_t e = 0; e < ZW_NUM_EVENTS; e++) {
        if (keymap.map[m][b][e].repeatMs == 0) keymap.map[m][b][e].repeatMs = 60;
      }
    }
  }
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

// A 3-as (és korábbi) formátumú mentés átvétele. A mezők sorrendje azonos, csak
// a bejegyzések rövidebbek és nincs bennük küldési hossz — az 0 lesz, ami épp a
// korábbi viselkedést jelenti.
static void migrateKeymapV3(const KeymapConfigV3& old) {
  memset(&keymap, 0, sizeof(keymap));
  initKeymapHeader();
  for (uint8_t m = 0; m < ZW_NUM_MODES; m++) {
    for (uint8_t b = 0; b < ZW_NUM_BUTTONS; b++) {
      for (uint8_t e = 0; e < ZW_NUM_EVENTS; e++) {
        const KeyActionV3& src = old.map[m][b][e];
        KeyAction& dst = keymap.map[m][b][e];
        dst.type = src.type;
        dst.modifier = src.modifier;
        dst.code = src.code;
        dst.repeat = src.repeat;
        dst.target = src.target;
        dst.repeatMs = src.repeatMs;
        dst.holdMs = 0;
      }
    }
  }
  memcpy(keymap.modeTarget, old.modeTarget, sizeof(keymap.modeTarget));
  memcpy(keymap.peers, old.peers, sizeof(keymap.peers));
  Serial.println("keymap: regi (3-as vagy korabbi) mentes atalakitva");
}

// Betöltés a belső fájlrendszerből. false = nincs vagy érvénytelen.
bool loadKeymap() {
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (!f.open(KEYMAPFILE, FILE_O_READ)) {
    return false;
  }
  // A nagyobbik szerkezettel olvasunk: a régi mentés ennek az elejére kerül,
  // így nem kell két külön puffert a veremben tartani.
  KeymapConfig tmp;
  uint32_t readlen = f.read((uint8_t*)&tmp, sizeof(tmp));
  f.close();

  // Régi, 3-as (vagy korábbi) formátumú mentés: kisebb fájl, kisebb bejegyzések.
  if (readlen == sizeof(KeymapConfigV3)) {
    const KeymapConfigV3& old = *(const KeymapConfigV3*)&tmp;
    if (old.magic != ZW_KEYMAP_MAGIC
        || old.version < ZW_KEYMAP_MIN_VERSION || old.version > 3
        || old.entrySize != sizeof(KeyActionV3)
        || old.modes != ZW_NUM_MODES || old.buttons != ZW_NUM_BUTTONS
        || old.events != ZW_NUM_EVENTS || old.slots != ZW_NUM_SLOTS) {
      Serial.println("keymap: ismeretlen formatum");
      return false;
    }
    if (zwCrc32((const uint8_t*)&old, sizeof(KeymapConfigV3) - sizeof(uint32_t))
        != old.crc) {
      Serial.println("keymap: CRC hiba");
      return false;
    }
    uint16_t oldVersion = old.version;
    migrateKeymapV3(old);
    if (oldVersion < 3) migrateKeymapV2toV3();
    return true;
  }

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
  Serial.println("keymap: betoltve a flash-bol");
  return true;
}

// Mentés a belső fájlrendszerbe.
bool saveKeymap() {
  initKeymapHeader();
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

// ---------------------------------------------------------------------------
// A HID-kimenet egyetlen tulajdonosa
//
// Korábban öt különböző helyről íródott, hogy „mi van most lenyomva és kinél"
// (pressedTargets, hasKeyPressed, hasConsumerKeyPressed). Aki később írt bele,
// elvesztette a másik felengedését — a régi célponton (pl. a telefonon)
// felengedetlenül maradt a billentyű. Mostantól minden küldés ezen a néhány
// függvényen megy át, és a lenyomás MINDIG felengedi az előzőt a saját
// célpontjain, mielőtt átvenné a listát.
//
// Az alábbi három változót csak ezek a függvények (és a pruneLostTargets)
// írhatják: pressedTargets, pressedTargetCount, hasKeyPressed,
// hasConsumerKeyPressed.
// ---------------------------------------------------------------------------

// Mit tart éppen lenyomva a tulajdonos (a felengedéshez és annak eldöntéséhez,
// hogy egy újabb küldés ugyanaz-e, vagy átvétel).
static uint16_t heldCode = 0;
static uint8_t heldModifier = 0;

static bool hidHolding() {
  return hasKeyPressed || hasConsumerKeyPressed;
}

// Ugyanazt a lenyomást ismételjük-e ugyanazokra a kapcsolatokra? Nyomva tartott
// módban az ismétlés ezt teszi: ilyenkor NEM szabad közben felengedni, mert épp
// az a lényeg, hogy a billentyű végig lent maradjon.
static bool hidSameAsHeld(bool consumer, uint8_t modifier, uint16_t code,
                          const uint16_t* targets, uint8_t n) {
  if (!hidHolding()) return false;
  if (consumer != hasConsumerKeyPressed) return false;
  if (code != heldCode || modifier != heldModifier) return false;
  if (n != pressedTargetCount) return false;
  for (uint8_t i = 0; i < n; i++) {
    if (targets[i] != pressedTargets[i]) return false;
  }
  return true;
}

// Egy elveszett felengedés a hostnál BERAGADT billentyű, ezért újrapróbáljuk.
// A SoftDevice-nak elfogyhatnak a szabad küldési pufferei, és a Bluefruit
// ilyenkor egyszerűen eldobja a jelentést: a BLECharacteristic::notify() nem
// sorol be és nem próbálkozik újra, csak false-szal tér vissza. Korlátos
// számú próbálkozás után feladjuk, hogy egy néma kapcsolat ne bénítsa meg az
// eszközt (a kapcsolat bontását a pruneLostTargets amúgy is észreveszi).
static uint8_t releaseRetries = 0;
static const uint8_t releaseMaxRetries = 20;

// A felengedés kiküldése pontosan azokra a kapcsolatokra, amikre a lenyomás
// ment. keepModifier != 0 esetén csak maga a billentyű engedődik fel, a
// módosító nyomva marad (ez kell az Alt+Tab ablakváltáshoz).
// false = legalább egy célpont nem kapta meg.
static bool hidSendRelease(uint8_t keepModifier) {
  bool allOk = true;
  for (uint8_t i = 0; i < pressedTargetCount; i++) {
    uint16_t h = pressedTargets[i];
    // A bontott kapcsolatnak nincs mit felengedni: az a billentyű a hostnál is
    // felszabadul, amikor az eszköz eltűnik.
    if (!Bluefruit.connected(h)) continue;
    if (keepModifier) {
      uint8_t none[6] = { HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE,
                          HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
      if (!blehid.keyboardReport(h, keepModifier, none)) allOk = false;
      continue;
    }
    if (hasKeyPressed && !blehid.keyRelease(h)) allOk = false;
    if (hasConsumerKeyPressed && !blehid.consumerKeyRelease(h)) allOk = false;
  }
  return allOk;
}

// Az ismétlések közti felengedés: a billentyű felmegy, de a célpontok
// nyilvántartása megmarad, mert a gomb elengedésekor még jön egy záró
// felengedés (biztonsági háló elveszett értesítésre). Hold-mód esetén csak
// maga a billentyű megy fel, a módosító nyomva marad.
static void hidTapRelease(uint8_t keepModifier) {
  hidSendRelease(keepModifier);
  hasKeyPressed = false;
  hasConsumerKeyPressed = false;
}

// A záró felengedés előjegyzése: a főciklus a keyReleaseDelay letelte után
// küldi ki. Akkor is kell, ha közben már volt felengedés.
static void hidMarkHeld(bool consumer) {
  if (pressedTargetCount == 0) return;
  hasKeyPressed = !consumer;
  hasConsumerKeyPressed = consumer;
}

// Felengedés és felejtés: a nyilvántartás csak akkor ürül, ha a felengedés
// tényleg kiment. Különben megmarad, és a főciklus a következő körben
// újrapróbálja — így egy pillanatnyi torlódás nem hagy beragadt billentyűt.
static void hidReleaseAll() {
  if (!hidSendRelease(0) && ++releaseRetries < releaseMaxRetries) return;
  pressedTargetCount = 0;
  hasKeyPressed = false;
  hasConsumerKeyPressed = false;
  keyPressMillis = 0;
  releaseRetries = 0;
}

// Lenyomás küldése. false = nem ment ki semmi (nincs csatlakozott célpont).
static bool hidPress(bool consumer, uint8_t modifier, uint16_t code, uint8_t mask) {
  uint16_t targets[ZW_MAX_CONNECTIONS];
  uint8_t n = collectTargets(mask, targets);
  // Ha nincs kinek küldeni, a korábbi lenyomáshoz sem nyúlunk: azt a saját
  // időzítése fogja rendben felengedni.
  if (n == 0) return false;

  // Itt a kulcs: ha MÁS van lenyomva, azt előbb felengedjük a SAJÁT
  // célpontjain — különben a lista felülíródna, és a régi célponton
  // felengedetlen maradna a billentyű. Ugyanannak az ismétlése viszont nem
  // felengedés, hanem folytatás.
  if (!hidSameAsHeld(consumer, modifier, code, targets, n) && hidHolding()) {
    hidReleaseAll();
    // Ha a felengedés nem ment ki, most nem küldünk újat: előbb a régit kell
    // rendbe tenni, különben pont az a beragadt billentyű keletkezne.
    if (hidHolding()) return false;
  }

  // Csak azt jegyezzük fel célpontként, akihez a lenyomás tényleg kiment: egy
  // el sem küldött billentyűre később fölösleges felengedést küldenénk.
  uint8_t sent = 0;
  for (uint8_t i = 0; i < n; i++) {
    bool ok;
    if (consumer) {
      ok = blehid.consumerKeyPress(targets[i], code);
    } else {
      uint8_t keycodes[6] = { (uint8_t)code, HID_KEY_NONE, HID_KEY_NONE,
                              HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
      ok = blehid.keyboardReport(targets[i], modifier, keycodes);
    }
    if (ok) pressedTargets[sent++] = targets[i];
  }
  if (sent == 0) return false;

  pressedTargetCount = sent;
  hasKeyPressed = !consumer;
  hasConsumerKeyPressed = consumer;
  heldCode = code;
  heldModifier = consumer ? 0 : modifier;
  releaseRetries = 0;
  return true;
}

static bool sendKeyboard(uint8_t modifier, uint8_t keycode, uint8_t mask) {
  // Ismétlés vagy időzített küldés közben a főciklus nem engedi fel a
  // billentyűt, ezért egy közben indított másik művelet felengedése is
  // elmaradna — ilyenkor inkább nem küldünk semmit.
  if (duringLongpress || hidHolding()) return false;
  if (!hidPress(false, modifier, keycode, mask)) return false;
  // A delay() ezen a magon vTaskDelay: átadja a vezérlést az ütemezőnek (és
  // üríti az USB CDC puffert), tehát nem foglalja a processzort.
  delay(5);
  return true;
}

static bool sendConsumer(uint16_t usage, uint8_t mask) {
  if (duringLongpress || hidHolding()) return false;
  if (!hidPress(true, 0, usage, mask)) return false;
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
    burstActive = false;
    repeatTapPending = false;
    hasKeyPressed = false;
    hasConsumerKeyPressed = false;
    duringLongpress = false;
    repeatButton = -1;
    repeatDue = false;
    // A lenyomat is nullázandó: különben a következő ismétlés az ITT megszakadt
    // művelettel hasonlítaná össze magát, és rögtön le is állna.
    repeatType = ACT_NONE;
    keyPressMillis = 0;
  }
}

// Az ismétlés leütés-impulzusának lezárása. Ha van nyomva tartandó módosító,
// csak magát a billentyűt engedjük fel (Alt+Tab), egyébként teljeset küldünk.
static void endRepeatTap() {
  repeatTapPending = false;
  hidTapRelease(repeatTapModifier);
}

// A főciklusból hívjuk, hogy az impulzus a gomb-tickektől függetlenül záruljon.
void updateRepeatTap() {
  if (repeatTapPending && (millis() - repeatTapMillis) >= repeatTapMs) {
    endRepeatTap();
  }
}

// Egyszeri művelet végrehajtása (rövid / dupla / nem ismétlődő hosszú nyomás).
static void fireAction(uint8_t btn, uint8_t evt) {
  const KeyAction& a = currentAction(btn, evt);
  // Rövid és dupla nyomásnál beállítható, hogy a parancs meddig menjen ki; ezt
  // külön állapotgép hajtja, mert a főciklust nem szabad blokkolni.
  if (a.holdMs && (evt == EV_CLICK || evt == EV_DOUBLE)
      && (a.type == ACT_KEY || a.type == ACT_CONSUMER)) {
    startBurst(a);
    return;
  }
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

  // Egy még függő leütés-impulzust itt zárunk le, nem bízzuk a főciklusra: ha
  // egy kör hosszabbra nyúlik az ismétlési időnél, a következő ismétlés
  // előbb érkezne, mint a felengedés — és az impulzus felülíródva elveszne.
  // A host ilyenkor végig lenyomva látná a billentyűt (beragadt billentyű).
  if (repeatTapPending) endRepeatTap();

  // Egyszer számoljuk ki: menet közben változhat az üzemmód, és a két
  // felhasználás akkor sem térhet el egymástól.
  const uint8_t mask = targetMaskOf(a);
  // A hidPress() felengedi az esetleg még függő korábbi lenyomást a saját
  // célpontjain, mielőtt átvenné a listát.
  if (!hidPress(a.type == ACT_CONSUMER, a.modifier, a.code, mask)) return;

  repeatSentConsumer = (a.type == ACT_CONSUMER);
  repeatType = a.type;
  repeatCode = a.code;
  repeatModifier = a.modifier;
  repeatMask = mask;

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

  // A jelzőket NEM töröljük: a tulajdonos tartja nyilván, mi van lenyomva. A
  // főciklus automatikus felengedését a duringLongpress tiltja addig, amíg az
  // ismétlés tart.
  duringLongpress = true;
}

// Két küldés közti idő. Külön leütéseknél a leütés-impulzusnak is be kell
// férnie, ezért van alsó határ.
static uint16_t repeatInterval(const KeyAction& a) {
  uint16_t interval = a.repeatMs;
  if (a.repeat & ZW_REPEAT_RELEASE) {
    if (interval < repeatTapMs + 10) interval = repeatTapMs + 10;
  } else if (interval == 0) {
    interval = 60;
  }
  return interval;
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
  repeatType = ACT_NONE;
  hidMarkHeld(repeatSentConsumer);
}

// Ismétlés azonnali lezárása, ha egy másik gomb veszi át. Itt nem bízhatjuk a
// felengedést a főciklusra: az új ismétlés még előtte felülírná a célpont-
// listát, és a régi célponton beragadna a billentyű.
static void abortRepeat() {
  repeatTapPending = false;
  repeatType = ACT_NONE;
  repeatButton = -1;
  repeatDue = false;
  duringLongpress = false;
  hidReleaseAll();
}

// Időzített küldés indítása (rövid / dupla nyomás). A hosszú nyomás ismétlés-
// gépezetét használjuk, csak a leállás feltétele más: nem a gomb elengedése,
// hanem a beállított idő letelte.
static void startBurst(const KeyAction& a) {
  // Amíg egy hosszú nyomás ismétlése vagy egy másik időzített küldés tart, nem
  // indítunk újat: a célpont-listát felülírnánk, és a korábbi billentyű a saját
  // célpontján felengedetlen maradna. Ugyanez a kizárás él a sendKeyboard()-ben
  // is, csak az ismétlés-gépezet kerülné meg.
  if (duringLongpress || burstActive) return;

  uint16_t hold = a.holdMs;
  if (hold > ZW_MAX_HOLD_MS) hold = ZW_MAX_HOLD_MS;

  burstAction = a;
  // A repeatButton egy nem létező gomb sorszámát kapja: így egyetlen gomb
  // hosszú nyomása sem tudja véletlenül átvenni vagy lezárni ezt a küldést.
  repeatButton = ZW_BURST_SLOT;
  lastRepeatMillis = millis();
  sendRepeat(burstAction);   // az első küldés azonnal menjen ki

  // Ha egyetlen célpont sem volt csatlakozva, ki sem ment semmi — ilyenkor nem
  // indítjuk el a küldést, különben a beállított ideig fölöslegesen blokkolná
  // az összes többi gombot (a sendKeyboard() ugyanígy csak visszatér).
  if (pressedTargetCount == 0) {
    repeatButton = -1;
    duringLongpress = false;
    return;
  }
  burstActive = true;
  burstEndMillis = millis() + hold;
}

// Futó időzített küldés megszakítása. Enélkül egy hosszúra (akár 5 s-ra)
// állított küldés alatt az eszköz süketnek tűnne: az üzemmódváltás sem menne.
// A megszakítást kiváltó gombnyomás maga NEM hajtódik végre — így továbbra sem
// mehet ki két parancs egymásra torlódva.
static void cancelBurst() {
  burstActive = false;
  finishRepeat();
}

// A főciklusból hívjuk: fenntartja, majd a beállított idő végén lezárja a
// rövid/dupla nyomáshoz tartozó küldést.
void updateBurst() {
  if (!burstActive) return;
  unsigned long now = millis();

  if ((long)(now - burstEndMillis) >= 0) {
    burstActive = false;
    finishRepeat();
    return;
  }

  // Ismétlés nélkül egyetlen lenyomás tartja magát a küldés végéig, nem kell
  // újraküldeni; külön leütéseknél viszont ütemezetten jönnek az újabbak.
  if (!(burstAction.repeat & ZW_REPEAT_ENABLED)) return;

  uint16_t interval = repeatInterval(burstAction);
  if ((now - lastRepeatMillis) < interval) return;
  lastRepeatMillis = now;
  sendRepeat(burstAction);
}

static void onClick(uint8_t btn) {
  if (debugSerial) { Serial.print("Button "); Serial.print(btn + 1); Serial.println(" click."); }
  fct_WatchdogReset();
  // Az induláskor nyomott gomb első eseménye még nem parancs: csak azt jelzi,
  // hogy a gomb felszabadult.
  if (stuckButtons & (1 << btn)) { stuckButtons &= (uint8_t)~(1 << btn); return; }
  // Futó időzített küldést ez a gombnyomás megszakítja, de a saját parancsa
  // már nem megy ki (lásd cancelBurst).
  if (burstActive) { cancelBurst(); return; }
  fireAction(btn, EV_CLICK);
}

static void onDoubleClick(uint8_t btn) {
  if (debugSerial) { Serial.print("Button "); Serial.print(btn + 1); Serial.println(" doubleclick."); }
  fct_WatchdogReset();
  // Az induláskor nyomott gomb első eseménye még nem parancs: csak azt jelzi,
  // hogy a gomb felszabadult.
  if (stuckButtons & (1 << btn)) { stuckButtons &= (uint8_t)~(1 << btn); return; }
  // Futó időzített küldést ez a gombnyomás megszakítja, de a saját parancsa
  // már nem megy ki (lásd cancelBurst).
  if (burstActive) { cancelBurst(); return; }
  fireAction(btn, EV_DOUBLE);
}

static void onLongStart(uint8_t btn) {
  if (debugSerial) { Serial.print("Button "); Serial.print(btn + 1); Serial.println(" longPress start"); }
  fct_WatchdogReset();
  // Bekapcsoláskor már nyomva volt: nem indítunk rá műveletet, és a jelölést is
  // csak a tényleges felengedés (onLongStop) törli.
  if (stuckButtons & (1 << btn)) return;
  longPressStartMillis[btn] = millis();
  if (burstActive) { cancelBurst(); return; }  // a futó küldést megszakítja
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
  // Beragadt gombot nem szolgálunk ki: nem szórjuk a billentyűt a hostnak, nem
  // tiltjuk le tőle a többi gombot, és — mivel a watchdogot sem nullázzuk — az
  // eszköz el tud aludni, nem meríti le az akkumulátort.
  if (stuckButtons & (1 << btn)) return;
  if ((millis() - longPressStartMillis[btn]) >= ZW_STUCK_BUTTON_MS) {
    stuckButtons |= (uint8_t)(1 << btn);
    if (repeatButton == (int8_t)btn) abortRepeat();
    if (debugSerial) {
      Serial.print("Button ");
      Serial.print(btn + 1);
      Serial.println(" tul regota nyomva - beragadtnak tekintem, figyelmen kivul hagyom");
    }
    return;
  }

  fct_WatchdogReset();
  if (repeatButton != (int8_t)btn) return;

  // Az üzemmód menet közben is megváltozhat (másik gomb dupla kattintása vagy
  // a MODE parancs). Ilyenkor a currentAction() már MÁS műveletet ad vissza.
  // Le kell zárni az ismétlést, ha
  //   - az új művelet nem ismétlődő (különben a nulla ismétlési idő miatt
  //     fékezetlenül szórnánk ki a billentyűt), vagy
  //   - más billentyűt/média kódot vagy más célpontot küldene: a felengedés
  //     mindig az ÉPPEN aktuális műveletet követi, így a régi billentyű
  //     felengedés nélkül maradna a régi célponton.
  const KeyAction& a = currentAction(btn, EV_LONG);
  if (!isRepeating(a)
      || (repeatType != ACT_NONE
          && (a.type != repeatType || a.code != repeatCode
              || a.modifier != repeatModifier || targetMaskOf(a) != repeatMask))) {
    abortRepeat();
    return;
  }

  uint16_t interval = repeatInterval(a);

  unsigned long now = millis();
  if (!repeatDue && (now - lastRepeatMillis) < interval) return;
  repeatDue = false;
  lastRepeatMillis = now;
  sendRepeat(a);
}

static void onLongStop(uint8_t btn) {
  if (debugSerial) { Serial.print("Button "); Serial.print(btn + 1); Serial.println(" longPress stop"); }
  fct_WatchdogReset();
  // A gomb felengedett: ha beragadtnak jelöltük, mostantól újra használható.
  stuckButtons &= (uint8_t)~(1 << btn);
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
//   PING                                        -> OK ZWIFT_BUTTONS PROTO=7 ...
//   GET                                         -> MAP ... (45 sor) + END
//   SET <m> <b> <e> <t> <mod> <code> <rep> <ms> [<tgt> [<hold>]] -> OK
//
//   rep: ismétlés bitmaszk — 1 = ismétlés be, 2 = felengedés az ismétlések
//        között, 4 = a módosító nyomva marad (csak a 2 mellett). Használható
//        értékek: 0, 1, 3, 7. Rövid/dupla nyomásnál csak hold mellett érvényes.
//   hold: csak rövid és dupla nyomásnál (billentyű vagy média műveletnél):
//        meddig menjen ki a parancs, ms-ban. 0 = a szokásos rövid impulzus,
//        egyébként 50..5000. Hosszú nyomásnál ERR VALUE, mert ott a gomb
//        elengedése zárja le a küldést. Amíg egy ilyen küldés tart, más
//        parancs nem megy ki.
//   SAVE                                        -> OK SAVED | ERR SAVE
//   LOAD                                        -> OK LOADED | ERR LOAD
//   DEFAULTS                                    -> OK DEFAULTS
//   MODE [n]                                    -> OK MODE <n>
//   DBG <0|1>                                   -> OK DBG <n>
//   BAT                                         -> OK BAT RAW=.. MV=.. PCT=.. ...
//
//   GET valasza a MAP sorok utan uzemmodonkent egy TARGET <m> <maszk> sort is
//   tartalmaz (maszk: 1 = PC, 2 = telefon, 3 = mindketto).
//
//   A tgt mezo muveletenkenti cel-felulbiralas: 0 eseten az uzemmod celpontja
//   ervenyes, egyebkent ez a maszk. A tgt es a hold a SET-nel elhagyhato; a
//   MAP mindig mind a 10 mezot kiirja.
//
//   SETTARGET <m> <maszk>                       -> OK
//   PEERS                                       -> SLOT/CONN sorok + END
//   ASSIGN <slot> <conn_hdl>                    -> OK | ERR NOTBONDED
//   CLEARSLOT <slot>                            -> OK
// ---------------------------------------------------------------------------

static void printMapLine(uint8_t m, uint8_t b, uint8_t e) {
  const KeyAction& a = keymap.map[m][b][e];
  char line[72];
  snprintf(line, sizeof(line), "MAP %u %u %u %u %u %u %u %u %u %u",
           (unsigned)m, (unsigned)b, (unsigned)e,
           (unsigned)a.type, (unsigned)a.modifier, (unsigned)a.code,
           (unsigned)a.repeat, (unsigned)a.repeatMs, (unsigned)a.target,
           (unsigned)a.holdMs);
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

// BAT: az akkumulátor-mérés és -jelentés pillanatnyi állapota. Azért van, mert
// a hoston egyetlen szám látszik (vagy semmi), és abból nem derül ki, hogy a
// mérés rossz, a szolgáltatás nem indult el, vagy a host nem is kérdezi.
//   RAW  – a nyers ADC érték (0…4095); 0 vagy 4095 mérési hibára utal
//   MV   – ebből számolt akkumulátor-feszültség
//   PCT  – a töltöttségi görbe szerinti százalék
//   BAS  – elindult-e a BLE akkumulátor-szolgáltatás (1 = igen)
//   SENT – amit utoljára ki is értesítettünk (-1 = még semmit)
//   CONN – hány élő kapcsolat kapja
//   CHG  – tölt-e éppen (1 = igen; ilyenkor a mért feszültség a valódi
//          töltöttségnél magasabb, mert a töltő a végfeszültségen tartja)
static void cmdBat() {
  uint32_t raw = (uint32_t)analogRead(ZW_VBAT_PIN);
  uint16_t mv = millivoltsFromAdc(raw);
  uint8_t conns = 0;
  for (uint8_t i = 0; i < ZW_MAX_CONNECTIONS; i++) {
    if (connHandles[i] != BLE_CONN_HANDLE_INVALID) conns++;
  }
  char line[112];
  snprintf(line, sizeof(line), "OK BAT RAW=%lu MV=%u PCT=%u BAS=%u SENT=%d CONN=%u CHG=%u",
           (unsigned long)raw, (unsigned)mv, (unsigned)batteryPercent(mv),
           (unsigned)(basStarted ? 1 : 0),
           (lastBatteryPercent == 0xFF) ? -1 : (int)lastBatteryPercent,
           (unsigned)conns, (unsigned)(batteryCharging() ? 1 : 0));
  Serial.println(line);
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
  unsigned tgt = 0;   // elhagyható: 0 = az üzemmód célpontja érvényes
  unsigned hold = 0;  // elhagyható: 0 = rövid impulzus (a korábbi viselkedés)
  int got = sscanf(args, "%u %u %u %u %u %u %u %u %u %u",
                   &m, &b, &e, &t, &mod, &code, &rep, &ms, &tgt, &hold);
  if (got < 8 || got > 10) {
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
  // A HID jelentés-leíró a 0x0000..0x03FF média (consumer) tartományt hirdeti
  // meg, az e fölötti kódot a fogadó eszköz eldobná — némán nem működő
  // beállítás helyett inkább itt utasítjuk vissza.
  if (t == ACT_CONSUMER && code > ZW_CONSUMER_MAX_USAGE) {
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
  // A küldési hossz csak a rövid és a dupla nyomásnál értelmes: hosszú
  // nyomásnál a gomb elengedése zárja le a küldést. Némán ható nélküli
  // beállítás helyett inkább visszautasítjuk.
  if (hold && e == EV_LONG) {
    Serial.println("ERR VALUE");
    return;
  }
  if (hold && (hold < ZW_MIN_HOLD_MS || hold > ZW_MAX_HOLD_MS)) {
    Serial.println("ERR VALUE");
    return;
  }
  // Küldési hossza csak annak a műveletnek van, ami billentyűt vagy média
  // kódot küld; a nézetváltás és az üzemmódváltás nem tartható nyomva.
  if (hold && t != ACT_KEY && t != ACT_CONSUMER) {
    Serial.println("ERR VALUE");
    return;
  }
  // Ugyanígy: ismétlés-beállítás küldési hossz nélkül sem érvényesülne.
  if (rep && !hold && e != EV_LONG) {
    Serial.println("ERR VALUE");
    return;
  }
  setAction((uint8_t)m, (uint8_t)b, (uint8_t)e, (uint8_t)t, (uint8_t)mod,
            (uint16_t)code, (uint8_t)rep, (uint16_t)(ms == 0 ? 60 : ms),
            (uint8_t)tgt, (uint16_t)hold);
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
  } else if (strcmp(cmd, "BAT") == 0) {
    cmdBat();
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
