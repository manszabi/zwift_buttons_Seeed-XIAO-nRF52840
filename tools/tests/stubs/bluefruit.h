#pragma once
#include <cstdint>
#define HID_KEY_NONE 0x00
#define HID_KEY_A 0x04
#define HID_KEY_E 0x08
#define HID_KEY_G 0x0A
#define HID_KEY_H 0x0B
#define HID_KEY_R 0x15
#define HID_KEY_T 0x17
#define HID_KEY_1 0x1E
#define HID_KEY_2 0x1F
#define HID_KEY_3 0x20
#define HID_KEY_4 0x21
#define HID_KEY_5 0x22
#define HID_KEY_6 0x23
#define HID_KEY_7 0x24
#define HID_KEY_8 0x25
#define HID_KEY_9 0x26
#define HID_KEY_ENTER 0x28
#define HID_KEY_ESCAPE 0x29
#define HID_KEY_TAB 0x2B
#define HID_KEY_SPACE 0x2C
#define HID_KEY_F9 0x42
#define HID_KEY_F10 0x43
#define HID_KEY_PAGE_UP 0x4B
#define HID_KEY_PAGE_DOWN 0x4E
#define HID_KEY_ARROW_RIGHT 0x4F
#define HID_KEY_ARROW_LEFT 0x50
#define HID_KEY_ARROW_DOWN 0x51
#define HID_KEY_ARROW_UP 0x52
#define HID_KEY_KEYPAD_SUBTRACT 0x56
#define HID_KEY_KEYPAD_ADD 0x57
#define KEYBOARD_MODIFIER_LEFTCTRL 0x01
#define KEYBOARD_MODIFIER_LEFTSHIFT 0x02
#define KEYBOARD_MODIFIER_LEFTALT 0x04
#define KEYBOARD_MODIFIER_LEFTGUI 0x08
#define HID_USAGE_CONSUMER_SCAN_NEXT 0x00B5
#define HID_USAGE_CONSUMER_SCAN_PREVIOUS 0x00B6
#define HID_USAGE_CONSUMER_MUTE 0x00E2
#define HID_USAGE_CONSUMER_VOLUME_INCREMENT 0x00E9
#define HID_USAGE_CONSUMER_VOLUME_DECREMENT 0x00EA
#define HID_USAGE_CONSUMER_PLAY_PAUSE 0x00CD
#define HID_USAGE_CONSUMER_AL_CONSUMER_CONTROL_CONFIGURATION 0x0183
#define BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE 0
#define BLE_APPEARANCE_HID_KEYBOARD 0
#define BLE_GAP_EVENT_LENGTH_MIN 0
#define NRF_GPIO_PIN_PULLUP 0
#define NRF_GPIO_PIN_SENSE_LOW 0
inline void nrf_gpio_cfg_sense_input(uint32_t, int, int) {}
struct BLEDis { void setManufacturer(const char*) {} void setModel(const char*) {} void begin() {} };
struct KeyLog { uint8_t modifier; uint8_t code; bool consumer; uint16_t usage; };
extern KeyLog g_lastKey;
extern int g_keyCount, g_consumerCount, g_keyReleaseCount, g_consumerReleaseCount;
extern int g_lastConnHdl;      // -1 = a conn_hdl nelkuli overload
extern int g_sentTo[8];
extern int g_releasedTo[8];   // kapcsolatonkent kikuldott felengedesek
extern uint8_t g_lastModifier;
extern uint8_t g_lastPressedCode;  // csak a tenyleges leutesek
#include <vector>
extern std::vector<uint16_t> g_disconnected;
// A valodi API mind a negy fuggvenye bool-t ad vissza, es a sikertelen
// ertesites EL IS VESZIK (BLECharacteristic::notify nem sorol be es nem
// probalkozik ujra). Ezt a viselkedest modellezzuk: g_notifyFail > 0 eseten a
// kovetkezo ennyi kuldes elbukik.
extern int g_notifyFail;
inline bool notifyOk() {
  if (g_notifyFail > 0) { g_notifyFail--; return false; }
  return true;
}

struct BLEHidAdafruit {
  void begin() {}
  // A valodi API szerinti overload-keszlet: van conn_hdl nelkuli ("aktualis
  // kapcsolat") es conn_hdl-es ("tobb kapcsolat") valtozat is.
  bool keyboardReport(uint8_t mod, uint8_t* keys) {
    if (!notifyOk()) return false;
    g_lastKey = {mod, keys[0], false, 0}; g_keyCount++; g_lastConnHdl = -1;
    return true;
  }
  bool keyboardReport(uint16_t conn_hdl, uint8_t mod, uint8_t* keys) {
    if (!notifyOk()) return false;
    g_lastKey = {mod, keys[0], false, 0}; g_lastModifier = mod;
    g_lastConnHdl = (int)conn_hdl;
    // Az ures billentyu (mod nelkul vagy modositoval) nem uj leutes, hanem
    // felengedes: kulon szamoljuk, ahogy a valodi host is ertelmezi.
    if (keys[0] == HID_KEY_NONE) { g_keyReleaseCount++; g_releasedTo[conn_hdl]++; }
    else { g_keyCount++; g_sentTo[conn_hdl]++; g_lastPressedCode = keys[0]; }
    return true;
  }
  bool keyRelease() { if (!notifyOk()) return false;
    g_keyReleaseCount++; g_lastConnHdl = -1; g_lastModifier = 0; return true; }
  bool keyRelease(uint16_t conn_hdl) { if (!notifyOk()) return false;
    g_keyReleaseCount++; g_releasedTo[conn_hdl]++; g_lastConnHdl = (int)conn_hdl;
    g_lastModifier = 0; return true; }
  bool consumerKeyPress(uint16_t usage) {
    if (!notifyOk()) return false;
    g_lastKey = {0, 0, true, usage}; g_consumerCount++; g_lastConnHdl = -1;
    return true;
  }
  bool consumerKeyPress(uint16_t conn_hdl, uint16_t usage) {
    if (!notifyOk()) return false;
    g_lastKey = {0, 0, true, usage}; g_consumerCount++; g_lastConnHdl = (int)conn_hdl;
    g_sentTo[conn_hdl]++;
    return true;
  }
  bool consumerKeyRelease() { if (!notifyOk()) return false;
    g_consumerReleaseCount++; g_lastConnHdl = -1; return true; }
  bool consumerKeyRelease(uint16_t conn_hdl) { if (!notifyOk()) return false;
    g_consumerReleaseCount++; g_releasedTo[conn_hdl]++; g_lastConnHdl = (int)conn_hdl;
    return true; }
};

#define BLE_CONN_HANDLE_INVALID 0xFFFF
struct ble_gap_addr_t { uint8_t addr_type; uint8_t addr[6]; };
// Teszt-modell: legfeljebb 4 elo kapcsolat, mindegyiknek sajat BLE cimmel.
struct FakeConn { bool up; bool bonded; ble_gap_addr_t addr; };
extern FakeConn g_conns[4];
class BLEConnection {
 public:
  BLEConnection(uint16_t h) : _h(h) {}
  uint16_t handle() { return _h; }
  bool bonded() { return g_conns[_h].bonded; }
  ble_gap_addr_t getPeerAddr() { return g_conns[_h].addr; }
 private:
  uint16_t _h;
};
// A valodi BLEAdvertising viselkedeset koveti: a hirdetes befejezodik, amikor
// letrejon egy kapcsolat, es a konyvtar CSAK akkor inditja ujra magatol, ha
// mar egyetlen kapcsolat sem maradt (BLEAdvertising.cpp: BLE_GAP_EVT_DISCONNECTED
// -> "0 == Bluefruit.Periph.connected()"). Ket kapcsolatnal ez lenyeges.
struct AdvStub {
  int startCount = 0;
  bool running = false;
  bool restartOnDisc = false;
  void addFlags(int){} void addTxPower(){} void addAppearance(int){}
  void addService(BLEHidAdafruit&){} void addName(){}
  void restartOnDisconnect(bool e){ restartOnDisc = e; }
  void setInterval(int,int){} void setFastTimeout(int){}
  void start(int){ startCount++; running = true; }
  void stop(){ running = false; }
  bool isRunning(){ return running; }
};
struct PeriphStub {
  void setConnInterval(int,int){}
  void setConnectCallback(void(*f)(uint16_t)) { connectCb = f; }
  void setDisconnectCallback(void(*f)(uint16_t,uint8_t)) { disconnectCb = f; }
  void (*connectCb)(uint16_t) = nullptr;
  void (*disconnectCb)(uint16_t,uint8_t) = nullptr;
};
struct BluefruitStub {
  bool isConnected = true;   // visszafele kompatibilis kapcsolo a regi tesztekhez
  AdvStub Advertising; PeriphStub Periph;
  void configPrphConn(int,int,int,int){} bool begin(int,int){ return true; } void setTxPower(int){}
  void autoConnLed(bool){} void setName(const char*){}
  uint16_t connected() {
    if (!isConnected) return 0;
    uint16_t n = 0;
    for (int i = 0; i < 4; i++) if (g_conns[i].up) n++;
    return n ? n : 1;   // ha a teszt nem allitott be kapcsolatot, 1-nek szamit
  }
  bool connected(uint16_t h) { return isConnected && h < 4 && g_conns[h].up; }
  BLEConnection* Connection(uint16_t h) {
    if (h >= 4 || !g_conns[h].up) return NULL;
    static BLEConnection c(0); c = BLEConnection(h); return &c;
  }
  void disconnect(uint16_t h){ g_disconnected.push_back(h); }
};
extern BluefruitStub Bluefruit;
