// A valodi firmware-kod soros hidja: stdin -> handleSerial() -> stdout.
#include "stubs/Arduino.h"
#include "stubs/nrf52840.h"
#include "stubs/OneButton.h"
#include "stubs/bluefruit.h"
#include "stubs/Adafruit_LittleFS.h"
#include <iostream>
unsigned long g_millis = 1000;
uint32_t g_ADigitalPinMap[32];
SerialClass Serial;
static PowerRegs g_power; PowerRegs* NRF_POWER = &g_power;
KeyLog g_lastKey; int g_keyCount = 0, g_consumerCount = 0;
int g_keyReleaseCount = 0, g_consumerReleaseCount = 0;
bool g_fsWriteFail = false; bool g_fsRenameFail = false; bool g_fsRemoveFail = false;
std::vector<uint16_t> g_disconnected;
uint8_t g_lastModifier = 0; uint8_t g_lastPressedCode = 0;
int g_lastConnHdl = -1; int g_sentTo[8] = {0}; bool g_pinLow[32] = {false}; bool g_pinOut[32] = {false}; int g_systemOffCount = 0; int g_adcRaw = 0; int g_batteryPercent = -1; int g_batteryNotified[8] = {-1,-1,-1,-1,-1,-1,-1,-1}; int g_batteryNotifyCount = 0;
static WdtRegs g_wdtRegs; WdtRegs* NRF_WDT = &g_wdtRegs; int g_notifyFail = 0; int g_releasedTo[8]={0};
uint32_t g_basBeginErr = 0;
int g_svcChangedFail = 0; int g_svcChangedCount = 0; int g_svcChangedTo[8] = {0};
uint16_t g_svcChangedStart = 0; uint16_t g_svcChangedEnd = 0;
FakeConn g_conns[4] = {};
BluefruitStub Bluefruit; cbfn g_pendingCb = nullptr;
InternalFSClass InternalFS; std::map<std::string, std::string> g_fs;
#include "stubs/fs_impl.h"

#include "../../zwift_config.h"
void setup(); void loop(); bool writeFileAtomic(const char*, const char*, const void*, size_t);
void saveUzemmod(const char*); void connect_callback(uint16_t);
uzemmod readStoredMode();
void pruneLostTargets();
void updateRepeatTap(); void updateBurst();
void cbClick(void*); void cbDoubleClick(void*);
void cbLongStart(void*); void cbLongDuring(void*); void cbLongStop(void*);
void attachButtonCallbacks(); void updateLeds();
void releasePressedKeys(bool, bool);
void disconnect_callback(uint16_t, uint8_t);
void startAdv(void); void updateButtons(); void handleSerial();
bool loadKeymap(); bool saveKeymap(); void loadDefaultKeymap();
#include "../../zwift_buttons_Seeed_XIAO_nRF52840.ino"

// A teszt a gomb-esemenyeket nevvel hivja; a firmware mar parameteres
// callbackeket hasznal, ezert itt kotjuk ossze a kettot.
static void click1(){cbClick((void*)0);} static void doubleclick1(){cbDoubleClick((void*)0);}
static void longPressStart1(){cbLongStart((void*)0);} static void longPress1(){cbLongDuring((void*)0);}
static void longPressStop1(){cbLongStop((void*)0);}
static void click2(){cbClick((void*)1);} static void doubleclick2(){cbDoubleClick((void*)1);}
static void longPressStart2(){cbLongStart((void*)1);} static void longPress2(){cbLongDuring((void*)1);}
static void longPressStop2(){cbLongStop((void*)1);}
static void click3(){cbClick((void*)2);} static void doubleclick3(){cbDoubleClick((void*)2);}
static void longPressStart3(){cbLongStart((void*)2);} static void longPress3(){cbLongDuring((void*)2);}
static void longPressStop3(){cbLongStop((void*)2);}
static void click4(){cbClick((void*)3);} static void doubleclick4(){cbDoubleClick((void*)3);}
static void longPressStart4(){cbLongStart((void*)3);} static void longPress4(){cbLongDuring((void*)3);}
static void longPressStop4(){cbLongStop((void*)3);}
static void click5(){cbClick((void*)4);} static void doubleclick5(){cbDoubleClick((void*)4);}
static void longPressStart5(){cbLongStart((void*)4);} static void longPress5(){cbLongDuring((void*)4);}
static void longPressStop5(){cbLongStop((void*)4);}

int main() {
  setup();
  // Ket szimulalt BLE kapcsolat, hogy a PEERS/ASSIGN ut is jarhato legyen.
  for (uint16_t h = 0; h < 2; h++) {
    g_conns[h].up = true;
    g_conns[h].bonded = true;
    g_conns[h].addr.addr_type = 1;
    for (int i = 0; i < 6; i++) g_conns[h].addr.addr[i] = (uint8_t)(0xA0 + 0x10 * h + i);
    connect_callback(h);
  }
  Serial.tx.clear();
  std::string line;
  while (std::getline(std::cin, line)) {
    Serial.feed(line + "\n");
    handleSerial();
    std::cout << Serial.tx << std::flush;
    Serial.tx.clear();
  }
  return 0;
}
