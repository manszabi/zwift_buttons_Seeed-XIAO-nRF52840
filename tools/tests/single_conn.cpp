// Ugyanaz a firmware ZW_MAX_CONNECTIONS=1 mellett: a korabbi, egykapcsolatos
// mukodesnek valtozatlanul jonak kell lennie.
#define ZW_MAX_CONNECTIONS 1
#include "stubs/Arduino.h"
#include "stubs/nrf52840.h"
#include "stubs/OneButton.h"
#include "stubs/bluefruit.h"
#include "stubs/Adafruit_LittleFS.h"
#include <cassert>
#include <iostream>
#include <vector>
unsigned long g_millis = 1000; uint32_t g_ADigitalPinMap[32]; SerialClass Serial;
static PowerRegs g_power; PowerRegs* NRF_POWER = &g_power;
KeyLog g_lastKey; int g_keyCount=0, g_consumerCount=0, g_keyReleaseCount=0, g_consumerReleaseCount=0;
bool g_fsWriteFail=false; bool g_fsRenameFail=false; bool g_fsRemoveFail=false;
std::vector<uint16_t> g_disconnected;
uint8_t g_lastModifier=0; uint8_t g_lastPressedCode=0;
int g_lastConnHdl=-1; int g_sentTo[8]={0}; int g_notifyFail = 0; int g_releasedTo[8]={0}; FakeConn g_conns[4]={};
BluefruitStub Bluefruit; cbfn g_pendingCb=nullptr; InternalFSClass InternalFS;
std::map<std::string,std::string> g_fs;
#include "stubs/fs_impl.h"

#include "../../zwift_config.h"
void setup(); void loop();
bool writeFileAtomic(const char*, const char*, const void*, size_t);
void connect_callback(uint16_t);
uzemmod readStoredMode();
void pruneLostTargets();
void updateRepeatTap(); void updateBurst();
void cbClick(void*); void cbDoubleClick(void*);
void cbLongStart(void*); void cbLongDuring(void*); void cbLongStop(void*);
void attachButtonCallbacks(); void updateLeds(); void releasePressedKeys(bool,bool);
void saveUzemmod(const char*); void disconnect_callback(uint16_t,uint8_t);
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

int main(){
  std::cout << "ZW_MAX_CONNECTIONS=" << ZW_MAX_CONNECTIONS
            << "  sizeof(KeymapConfig)=" << sizeof(KeymapConfig) << "\n";
  // a config formatum nem valtozhat, kulonben nem lenne oda-vissza valthato
  assert(sizeof(KeymapConfig) == 486);
  setup();
  g_conns[0].up = true; g_conns[0].bonded = true; for(int i=0;i<6;i++) g_conns[0].addr.addr[i]=0xA0+i;
  connect_callback(0);
  jelenlegiUzemmod = normalUzemmod;
  hasKeyPressed=false; hasConsumerKeyPressed=false; keyPressMillis=0;
  for(int i=0;i<8;i++) g_sentTo[i]=0;
  click1();
  assert(g_sentTo[0]==1 && g_lastKey.code==HID_KEY_ARROW_LEFT);
  std::cout << "-- egy kapcsolat: billentyu kimegy\n";
  g_millis+=200; loop(); g_millis+=200; loop();
  assert(g_keyReleaseCount>=1 && !hasKeyPressed);
  std::cout << "-- egy kapcsolat: felengedes rendben\n";
  jelenlegiUzemmod = mediaVezerloUzemmod;
  hasKeyPressed=false; hasConsumerKeyPressed=false; keyPressMillis=0;
  for(int i=0;i<8;i++) g_sentTo[i]=0;
  click2();
  assert(g_sentTo[0]==1 && g_lastKey.consumer);
  std::cout << "-- egy kapcsolat: media billentyu kimegy\n";
  Serial.tx.clear(); Serial.feed("PING\n"); handleSerial();
  assert(Serial.tx.find("CONNS=1") != std::string::npos);
  std::cout << "-- PING helyesen jelenti: CONNS=1\n";
  std::cout << "\nEGYKAPCSOLATOS MOD OK\n";
  return 0;
}
