#include "stubs/Arduino.h"
#include "stubs/nrf52840.h"
#include "stubs/OneButton.h"
#include "stubs/bluefruit.h"
#include "stubs/Adafruit_LittleFS.h"
#include <cassert>
#include <iostream>
#include <algorithm>
#include <vector>

// --- stub globálisok ---
unsigned long g_millis = 1000;
uint32_t g_ADigitalPinMap[32];
SerialClass Serial;
static PowerRegs g_power;
PowerRegs* NRF_POWER = &g_power;
KeyLog g_lastKey; int g_keyCount = 0, g_consumerCount = 0;
int g_keyReleaseCount = 0, g_consumerReleaseCount = 0;
bool g_fsWriteFail = false; bool g_fsRenameFail = false; bool g_fsRemoveFail = false;
std::vector<uint16_t> g_disconnected;
uint8_t g_lastModifier = 0; uint8_t g_lastPressedCode = 0;
int g_lastConnHdl = -1; int g_sentTo[8] = {0}; bool g_pinLow[32] = {false}; int g_systemOffCount = 0; int g_notifyFail = 0; int g_releasedTo[8]={0};
FakeConn g_conns[4] = {};
BluefruitStub Bluefruit;
cbfn g_pendingCb = nullptr;
InternalFSClass InternalFS;
std::map<std::string, std::string> g_fs;
#include "stubs/fs_impl.h"


#include "../../zwift_config.h"
// --- amit az Arduino IDE automatikusan generál a .ino elejére ---
void setup(); void loop();
bool writeFileAtomic(const char*, const char*, const void*, size_t);
void saveUzemmod(const char* content);
void connect_callback(uint16_t);
uzemmod readStoredMode();
void pruneLostTargets();
void updateRepeatTap(); void updateBurst();
void cbClick(void*); void cbDoubleClick(void*);
void cbLongStart(void*); void cbLongDuring(void*); void cbLongStop(void*);
void attachButtonCallbacks(); void updateLeds();
void releasePressedKeys(bool, bool);
void disconnect_callback(uint16_t, uint8_t);
void startAdv(void);
void updateButtons();
void handleSerial();
bool loadKeymap();
bool saveKeymap();
void loadDefaultKeymap();

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


// Teszt-seged: egy BLE kapcsolat felepitese adott handle-lel es cimmel.
// A SoftDevice a kapcsolat letrejottekor abbahagyja a hirdetest.
static void connectPeer(uint16_t h, uint8_t addrPrefix, bool bonded = true) {
  g_conns[h].up = true;
  g_conns[h].bonded = bonded;
  g_conns[h].addr.addr_type = 1;
  for (int i = 0; i < 6; i++) g_conns[h].addr.addr[i] = (uint8_t)(addrPrefix + i);
  Bluefruit.Advertising.stop();
  connect_callback(h);
}
// A bontast a konyvtar sajat kezeloje is latja: az CSAK akkor indit ujra
// hirdetest, ha mar egyetlen kapcsolat sem maradt.
static void disconnectPeer(uint16_t h) {
  g_conns[h].up = false;
  disconnect_callback(h, 0x13);
  if (Bluefruit.connected() == 0 && Bluefruit.Advertising.restartOnDisc
      && !Bluefruit.Advertising.isRunning()) {
    Bluefruit.Advertising.start(0);
  }
}
// A leutes-impulzus lezarasa. Elesben ezt a fociklus (updateRepeatTap) vegzi
// repeatTapMs mulva; a tesztben kezzel leptetjuk az idot.
static void settleTap() { g_millis += repeatTapMs + 1; updateRepeatTap(); }

static int8_t slotOfConnTest(uint16_t h) { return slotOfConn(h); }

static void resetKeyState() {
  hasKeyPressed = false; hasConsumerKeyPressed = false;
  duringLongpress = false; keyPressMillis = 0; pressedTargetCount = 0;
  g_keyCount = g_consumerCount = g_keyReleaseCount = g_consumerReleaseCount = 0;
  for (int i = 0; i < 8; i++) g_sentTo[i] = 0;
}

static std::string send(const std::string& cmd) {
  Serial.tx.clear();
  Serial.feed(cmd + "\n");
  handleSerial();
  return Serial.tx;
}
static void expect(const std::string& got, const std::string& want, const char* what) {
  if (got.find(want) == std::string::npos) {
    std::cout << "HIBA (" << what << "): vart='" << want << "' kapott='" << got << "'\n";
    exit(1);
  }
}

int main() {
  // A bekapcsolaskor mar nyomott gomb felismeresehez a tuske-allapotot a
  // setup() ELOTT kell beallitani; a legtobb teszt szamara minden gomb szabad.
  for (int i = 0; i < 32; i++) g_pinLow[i] = false;
  std::cout << "sizeof(KeyAction)=" << sizeof(KeyAction)
            << "  sizeof(KeymapConfig)=" << sizeof(KeymapConfig) << "\n";
  assert(sizeof(KeyAction) == 10);      // + holdMs (4-es formatum)
  assert(sizeof(KeyActionV3) == 8);     // a regi bejegyzes valtozatlan
  assert(sizeof(PeerSlot) == 8);
  // fejlec 12 + terkep 450 + modeTarget 3 + reserved 1 + peers 16 + crc 4
  assert(sizeof(KeymapConfig) == 12 + 45 * 10 + 3 + 1 + 2 * 8 + 4);
  assert(sizeof(KeymapConfigV3) == 12 + 45 * 8 + 3 + 1 + 2 * 8 + 4);

  setup();
  std::cout << "-- setup lefutott, gyari kiosztas betoltve\n";

  // A tobbi teszt egy elo kapcsolatot felteteléz (mint a valosagban, amikor
  // egy host csatlakozik). A 0-s handle-t hozzuk fel.
  connectPeer(0, 0xA0);
  std::cout << "-- 1 BLE kapcsolat felepult (conn_hdl=0)\n";

  // 1) PING
  expect(send("PING"), "OK ZWIFT_BUTTONS PROTO=6 MODES=3 BUTTONS=5 EVENTS=3 SLOTS=2 CONNS=2", "PING");
  send("DBG 0");

  // 2) GET: 45 MAP sor + END
  std::string get = send("GET");
  size_t maps = 0, pos = 0;
  while ((pos = get.find("MAP ", pos)) != std::string::npos) { maps++; pos += 4; }
  assert(maps == 45);
  expect(get, "END", "GET/END");
  expect(get, "MAP 0 0 0 1 0 80 0 60", "gyari: normal/G1/rovid = balra nyil (0x50=80)");
  expect(get, "MAP 2 3 2 2 0 234 3 70 0", "gyari: media/G4/hosszu = hangero- ismetlo, kulon leutesek");
  expect(get, "MAP 0 3 1 3 0 0 0 60", "gyari: normal/G4/dupla = uzemmod valtas");
  std::cout << "-- GET ok (" << maps << " sor)\n";

  // 3) SET + visszaolvasas
  expect(send("SET 0 0 0 1 12 21 0 60"), "OK", "SET");
  expect(send("GET"), "MAP 0 0 0 1 12 21 0 60 0", "SET utani GET");
  expect(send("SET 9 0 0 1 0 4 0 60"), "ERR RANGE", "ervenytelen uzemmod");
  expect(send("SET 0 0 0 9 0 4 0 60"), "ERR VALUE", "ervenytelen tipus");
  expect(send("SET 0 0"), "ERR ARGS", "hianyzo argumentum");
  expect(send("HULYESEG"), "ERR UNKNOWN", "ismeretlen parancs");
  std::cout << "-- SET / hibakezeles ok\n";

  // 4) SAVE -> flash, majd ujraindulas szimulacio
  expect(send("SAVE"), "OK SAVED", "SAVE");
  assert(g_fs.count("/keymap.bin") == 1 && g_fs["/keymap.bin"].size() == sizeof(KeymapConfig));
  memset(&keymap, 0, sizeof(keymap));
  assert(loadKeymap());
  assert(keymap.map[0][0][0].modifier == 12 && keymap.map[0][0][0].code == 21);
  std::cout << "-- SAVE/LOAD korbeer, CRC ok\n";

  // 5) serult fajl -> elutasitas
  g_fs["/keymap.bin"][20] ^= 0xFF;
  assert(!loadKeymap());
  std::cout << "-- serult keymap elutasitva (CRC)\n";
  loadDefaultKeymap();

  // 6) Gombesemenyek: Alt+Win+R hosszu nyomasra (normal/G1)
  g_keyCount = g_consumerCount = g_keyReleaseCount = g_consumerReleaseCount = 0;
  longPressStart1();
  assert(g_keyCount == 1 && g_lastKey.code == HID_KEY_R
         && g_lastKey.modifier == (KEYBOARD_MODIFIER_LEFTGUI | KEYBOARD_MODIFIER_LEFTALT));
  longPressStop1();
  std::cout << "-- egyszeri hosszu nyomas ok (mod=0x" << std::hex
            << (int)g_lastKey.modifier << std::dec << ")\n";

  // 7) Ismetlodo hosszu nyomas (normal/G4 = le nyil, 30 ms).
  //    Gyarilag KULON LEUTESEK: minden ismetles utan felengedes is megy ki.
  hasKeyPressed = false; hasConsumerKeyPressed = false; keyPressMillis = 0;
  g_keyCount = 0; g_keyReleaseCount = 0;
  longPressStart4();
  longPress4();                       // elso ismetles azonnal
  assert(g_keyCount == 1 && g_lastKey.code == HID_KEY_ARROW_DOWN);
  assert(g_keyReleaseCount == 0);     // a felengedes az impulzus vegen jon
  settleTap();
  assert(g_keyReleaseCount == 1);     // most mar felengedve
  longPress4();                       // meg nem telt le az ismetlesi ido
  assert(g_keyCount == 1);
  g_millis += 40;
  longPress4();                       // most igen
  settleTap();
  assert(g_keyCount == 2 && g_keyReleaseCount == 2);
  assert(duringLongpress && !hasKeyPressed);
  longPressStop4();
  // a celpontok megmaradnak: a zaro felengedes a biztonsagi halo
  assert(!duringLongpress && hasKeyPressed);
  loop(); g_millis += 200; loop();
  assert(g_keyReleaseCount == 3);     // egy zaro biztonsagi felengedes
  std::cout << "-- ismetlodo hosszu nyomas kulon leutesekkel ok\n";

  // 7b) Ugyanez NYOMVA TARTVA (rep = csak ZW_REPEAT_ENABLED): a billentyu
  //     lenyomva marad, a felengedest a fociklus kuldi a gomb elengedesekor.
  expect(send("SET 0 3 2 1 0 81 1 30"), "OK", "normal/G4/hosszu -> nyomva tartva");
  hasKeyPressed = false; hasConsumerKeyPressed = false; keyPressMillis = 0;
  g_keyCount = 0; g_keyReleaseCount = 0;
  longPressStart4();
  longPress4();
  assert(g_keyCount == 1 && g_keyReleaseCount == 0);   // NEM enged fel
  g_millis += 40; longPress4();
  assert(g_keyCount == 2 && g_keyReleaseCount == 0);
  // Nyomva tartott modban a billentyu tenyleg lenyomva van: a tulajdonos ezt
  // jelzi is. A fociklus automatikus felengedeset a duringLongpress tiltja.
  assert(duringLongpress && hasKeyPressed);
  longPressStop4();
  assert(!duringLongpress && hasKeyPressed);
  loop(); g_millis += 200; loop();     // a fociklus engedi fel
  assert(g_keyReleaseCount == 1 && !hasKeyPressed);
  std::cout << "-- ismetlodo hosszu nyomas nyomva tartva ok\n";
  loadDefaultKeymap();

  // 8) Media consumer gomb (media/G2/rovid = play/pause)
  jelenlegiUzemmod = mediaVezerloUzemmod;
  hasKeyPressed = false; hasConsumerKeyPressed = false; keyPressMillis = 0;
  g_consumerCount = 0;
  click2();
  assert(g_consumerCount == 1 && g_lastKey.consumer
         && g_lastKey.usage == HID_USAGE_CONSUMER_PLAY_PAUSE);
  std::cout << "-- consumer billentyu ok\n";

  // 9) Uzemmod valtas dupla kattintasra + mentes (a valos uton: loop()-on belul)
  jelenlegiUzemmod = normalUzemmod;
  hasKeyPressed = false; hasConsumerKeyPressed = false; keyPressMillis = 0;
  g_fs.erase("/jelenlegiuzemmod.txt");
  g_pendingCb = doubleclick4;      // a kovetkezo tick()-en sul ki, mint igazibol
  loop();
  assert(jelenlegiUzemmod == versenyEdzesUzemmod);
  assert(g_fs["/jelenlegiuzemmod.txt"] == CONTENTVerseny);
  // MODE parancs szinten a loop()-on belul (handleSerial) fut le
  g_fs.erase("/jelenlegiuzemmod.txt");
  Serial.feed("MODE 2\n");
  loop();
  assert(jelenlegiUzemmod == mediaVezerloUzemmod);
  assert(g_fs["/jelenlegiuzemmod.txt"] == CONTENTMedia);
  std::cout << "-- uzemmod valtas (gomb + MODE parancs) es mentese ok\n";

  // 10) Nezetvaltas 1-9 korbe
  jelenlegiUzemmod = normalUzemmod; nezet = 0;
  for (int i = 1; i <= 10; i++) {
    hasKeyPressed = false; hasConsumerKeyPressed = false;
    doubleclick5();
    uint8_t want = (uint8_t)(HID_KEY_1 + ((i - 1) % 9));
    if (g_lastKey.code != want) { std::cout << "HIBA nezet " << i << "\n"; return 1; }
  }
  std::cout << "-- nezetvaltas (1-9 korbe) ok\n";

  // 11) BLE nelkul ne ragadjon be az allapot
  Bluefruit.isConnected = false;
  hasKeyPressed = false; hasConsumerKeyPressed = false; keyPressMillis = 0;
  click1(); click1(); click1();
  assert(!hasKeyPressed && !hasConsumerKeyPressed);
  Bluefruit.isConnected = true;
  click1();
  assert(hasKeyPressed);
  std::cout << "-- BLE kapcsolat nelkuli allapotkezeles ok\n";

  // 12) Tul hosszu parancs
  expect(send(std::string(200, 'X')), "ERR TOOLONG", "tul hosszu parancs");
  expect(send("PING"), "OK ZWIFT_BUTTONS", "tullepes utan helyreall");
  std::cout << "-- parancs-puffer tullepes kezelve\n";

  // ==================== REGRESSZIOS TESZTEK (code review) ====================
  std::cout << "\n--- regresszios tesztek ---\n";

  // R1) A nezet-szamlalo ne csusszon el, ha a billentyu nem ment ki
  loadDefaultKeymap();
  jelenlegiUzemmod = normalUzemmod; nezet = 0;
  Bluefruit.isConnected = false;
  hasKeyPressed = false; hasConsumerKeyPressed = false; keyPressMillis = 0;
  doubleclick5(); doubleclick5(); doubleclick5();   // BLE nincs -> nem megy ki semmi
  Bluefruit.isConnected = true;
  hasKeyPressed = false; hasConsumerKeyPressed = false;
  doubleclick5();
  if (g_lastKey.code != HID_KEY_1) {
    std::cout << "HIBA R1: nezet elcsuszott, kuldott kod=0x" << std::hex
              << (int)g_lastKey.code << std::dec << " (vart: HID_KEY_1)\n";
    return 1;
  }
  // ugyanez akkor is, ha egy korabbi billentyu meg nincs felengedve
  nezet = 0; hasKeyPressed = true;
  doubleclick5(); doubleclick5();
  hasKeyPressed = false;
  doubleclick5();
  assert(g_lastKey.code == HID_KEY_1);
  std::cout << "-- R1 nezet-szamlalo nem csuszik el\n";

  // R2) Ket gomb egyszerre nyomva: ne ragadjon be a billentyuzet-kod.
  //     Normal uzemmodban G4 hosszu = ismetlodo billentyu, G5 hosszu-t
  //     atallitjuk ismetlodo media billentyure.
  expect(send("SET 0 4 2 2 0 234 1 70"), "OK", "G5 hosszu = ismetlodo media (nyomva tartva)");
  jelenlegiUzemmod = normalUzemmod;
  hasKeyPressed = false; hasConsumerKeyPressed = false; keyPressMillis = 0;
  duringLongpress = false;
  g_keyCount = g_consumerCount = g_keyReleaseCount = g_consumerReleaseCount = 0;
  longPressStart4(); longPress4();          // G4 ismetel (billentyu)
  assert(g_keyCount == 1);
  longPressStart5(); longPress5();          // G5 atveszi (media)
  assert(g_consumerCount == 1);
  longPressStop4();                          // G4 elengedve (mar nem o ismetel)
  longPressStop5();                          // G5 elengedve
  loop(); g_millis += 200; loop();
  if (g_keyReleaseCount == 0) {
    std::cout << "HIBA R2: a billentyuzet-kod beragadt (nem volt keyRelease)\n";
    return 1;
  }
  assert(g_consumerReleaseCount >= 1);
  assert(!hasKeyPressed && !hasConsumerKeyPressed && !duringLongpress);
  std::cout << "-- R2 ket parhuzamos hosszu nyomas utan minden felengedve\n";
  loadDefaultKeymap();

  // R3) ACT_KEY eseten a 8 bitnel nagyobb kod hibat adjon (ne csonkoljon)
  expect(send("SET 0 0 0 1 0 336 0 60"), "ERR VALUE", "ACT_KEY kod > 0xFF");
  expect(send("SET 0 0 0 2 0 336 0 60"), "OK", "ACT_CONSUMER kod > 0xFF megengedett");
  expect(send("GET"), "MAP 0 0 0 2 0 336 0 60 0", "consumer kod valtozatlan");
  loadDefaultKeymap();
  std::cout << "-- R3 tul nagy billentyukod elutasitva, media kod atmegy\n";

  // R4) Sikertelen mentes ne semmisitse meg a korabbi kiosztast
  expect(send("SET 1 1 1 1 0 4 0 60"), "OK", "elso kiosztas");
  assert(saveKeymap());
  std::string savedGood = g_fs["/keymap.bin"];
  expect(send("SET 1 1 1 1 0 5 0 60"), "OK", "masodik kiosztas");
  g_fsWriteFail = true;                      // a flash iras most elromlik
  bool ok = saveKeymap();
  g_fsWriteFail = false;
  assert(!ok);
  if (g_fs.count("/keymap.bin") == 0) {
    std::cout << "HIBA R4: a sikertelen mentes torolte a regi kiosztast\n";
    return 1;
  }
  assert(g_fs["/keymap.bin"] == savedGood);
  assert(g_fs.count("/keymap.tmp") == 0);    // ne maradjon szemet
  memset(&keymap, 0, sizeof(keymap));
  assert(loadKeymap() && keymap.map[1][1][1].code == 4);
  std::cout << "-- R4 sikertelen mentes utan a regi kiosztas megmaradt\n";

  // R5) Sikeres mentes utan sincs ideiglenes fajl
  assert(saveKeymap());
  assert(g_fs.count("/keymap.tmp") == 0);
  std::cout << "-- R5 sikeres mentes nem hagy ideiglenes fajlt\n";

  // R6) Sikertelen uzemmod-mentes ne semmisitse meg a korabbi erteket
  //     (a hosszabb -> rovidebb irany a kritikus: "versenyEdzesUzemmod" -> "normalUzemmod")
  g_fs.erase("/jelenlegiuzemmod.txt");
  saveUzemmod(CONTENTVerseny);
  assert(g_fs["/jelenlegiuzemmod.txt"] == CONTENTVerseny);
  g_fsWriteFail = true;
  saveUzemmod(CONTENTNormal);              // a flash iras elromlik
  g_fsWriteFail = false;
  if (g_fs.count("/jelenlegiuzemmod.txt") == 0) {
    std::cout << "HIBA R6: a sikertelen mentes torolte a korabbi uzemmodot\n";
    return 1;
  }
  if (g_fs["/jelenlegiuzemmod.txt"] != CONTENTVerseny) {
    std::cout << "HIBA R6: serult uzemmod-fajl: '"
              << g_fs["/jelenlegiuzemmod.txt"] << "'\n";
    return 1;
  }
  assert(g_fs.count("/jelenlegiuzemmod.tmp") == 0);
  std::cout << "-- R6 sikertelen uzemmod-mentes utan a regi ertek megmaradt\n";

  // R7) Hosszabb -> rovidebb ertek irasakor ne maradjon szemet a vegen
  saveUzemmod(CONTENTNormal);
  if (g_fs["/jelenlegiuzemmod.txt"] != CONTENTNormal) {
    std::cout << "HIBA R7: '" << g_fs["/jelenlegiuzemmod.txt"]
              << "' (vart: " << CONTENTNormal << ")\n";
    return 1;
  }
  assert(g_fs.count("/jelenlegiuzemmod.tmp") == 0);
  // es a boot-logika is helyesen olvassa vissza
  assert(readStoredMode() == normalUzemmod);
  std::cout << "-- R7 rovidebb ertek irasakor nem marad szemet a fajl vegen\n";

  // R8) A media billentyuk a VALODI kapcsolat-azonositora menjenek, ne a
  //     hardkodolt 0-ra. Szandekosan olyan handle-t hasznalunk, ami nem 0.
  disconnectPeer(0);
  connectPeer(2, 0xC0);
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  jelenlegiUzemmod = mediaVezerloUzemmod;
  resetKeyState();
  g_lastConnHdl = 99;
  click2();                                  // media/G2/rovid = play/pause
  assert(g_consumerCount == 1);
  if (g_lastConnHdl != 2) {
    std::cout << "HIBA R8: a media billentyu conn_hdl=" << g_lastConnHdl
              << " -re ment, pedig az elo kapcsolat a 2-es\n";
    return 1;
  }
  // ugyanez az ismetlodo hosszu nyomasnal (media/G4/hosszu = hangero -)
  resetKeyState();
  g_lastConnHdl = 99;
  longPressStart4(); longPress4();
  assert(g_lastKey.consumer);
  if (g_lastConnHdl != 2) {
    std::cout << "HIBA R8: az ismetlodo media kuldes conn_hdl=" << g_lastConnHdl << "\n";
    return 1;
  }
  longPressStop4();
  std::cout << "-- R8 media billentyuk a valodi kapcsolat-azonositora mennek\n";
  disconnectPeer(2);
  connectPeer(0, 0xA0);                      // alaphelyzet a kovetkezo tesztekhez

  // ==================== TOBB EGYIDEJU KAPCSOLAT ====================
  std::cout << "\n--- ket eszkoz egyszerre ---\n";

  // R9) Gyari cel-beallitasok
  loadDefaultKeymap();
  assert(keymap.modeTarget[0] == ZW_TARGET_PC);
  assert(keymap.modeTarget[1] == ZW_TARGET_PC);
  assert(keymap.modeTarget[2] == ZW_TARGET_ALL);
  expect(send("GET"), "TARGET 0 1", "Normal -> csak PC");
  expect(send("GET"), "TARGET 1 1", "Verseny -> csak PC");
  expect(send("GET"), "TARGET 2 3", "Media -> mindketto");
  std::cout << "-- R9 gyari celpontok: Normal/Verseny=PC, Media=mindketto\n";

  // R10) Masodik eszkoz csatlakozasa + hirdetes ujrainditasa
  int advBefore = Bluefruit.Advertising.startCount;
  connectPeer(1, 0xB0);
  assert(connHandles[0] == 0 && connHandles[1] == 1);
  // ket kapcsolat van, tobb hely nincs -> ne hirdessen tovabb
  assert(Bluefruit.Advertising.startCount == advBefore);
  std::cout << "-- R10 masodik eszkoz csatlakozott, a hirdetes leallt\n";

  // R11) Hozzarendeles nelkul minden kapcsolatra megy (kicsomagolas utani allapot)
  expect(send("CLEARSLOT 0"), "OK", "0-s fiok torlese");
  expect(send("CLEARSLOT 1"), "OK", "1-es fiok torlese");
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  click1();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 1);
  std::cout << "-- R11 hozzarendeles nelkul mindket eszkoz megkapja\n";

  // R12) Fiokok hozzarendelese: conn 0 = PC, conn 1 = telefon
  expect(send("ASSIGN 0 0"), "OK", "PC = conn 0");
  expect(send("ASSIGN 1 1"), "OK", "telefon = conn 1");
  expect(send("PEERS"), "CONN 0 A5:A4:A3:A2:A1:A0 0", "conn 0 a 0-s fiokban");
  expect(send("PEERS"), "CONN 1 B5:B4:B3:B2:B1:B0 1", "conn 1 az 1-es fiokban");
  expect(send("PEERS"), "SLOT 0 1 A5:A4:A3:A2:A1:A0", "0-s fiok cime");
  std::cout << "-- R12 fiok-hozzarendeles BLE cim alapjan\n";

  // R13) Normal uzemmod: CSAK a PC kapja meg
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  click1();
  if (!(g_sentTo[0] == 1 && g_sentTo[1] == 0)) {
    std::cout << "HIBA R13: Normal uzemmodban PC=" << g_sentTo[0]
              << " telefon=" << g_sentTo[1] << " (vart: 1 / 0)\n";
    return 1;
  }
  // a felengedes is csak a PC-re megy
  g_millis += 200; loop(); g_millis += 200; loop();
  assert(g_keyReleaseCount == 1);
  std::cout << "-- R13 Normal uzemmod: csak a PC kap gombnyomast es felengedest\n";

  // R14) Media uzemmod: MINDKET eszkoz megkapja
  jelenlegiUzemmod = mediaVezerloUzemmod;
  resetKeyState();
  click2();                       // media/G2/rovid = play/pause
  if (!(g_sentTo[0] == 1 && g_sentTo[1] == 1)) {
    std::cout << "HIBA R14: Media uzemmodban PC=" << g_sentTo[0]
              << " telefon=" << g_sentTo[1] << " (vart: 1 / 1)\n";
    return 1;
  }
  assert(pressedTargetCount == 2);
  g_millis += 200; loop(); g_millis += 200; loop();
  assert(g_consumerReleaseCount == 2);   // mindket kapcsolatra megy a felengedes
  std::cout << "-- R14 Media uzemmod: mindket eszkoz kap gombnyomast es felengedest\n";

  // R15) Csak telefon
  expect(send("SETTARGET 0 2"), "OK", "Normal -> csak telefon");
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  click1();
  assert(g_sentTo[0] == 0 && g_sentTo[1] == 1);
  expect(send("SETTARGET 0 1"), "OK", "vissza PC-re");
  expect(send("SETTARGET 0 0"), "ERR VALUE", "ures maszk elutasitva");
  expect(send("SETTARGET 0 4"), "ERR VALUE", "ismeretlen bit elutasitva");
  expect(send("SETTARGET 9 1"), "ERR RANGE", "ervenytelen uzemmod");
  std::cout << "-- R15 celpont atallithato, ervenytelen maszk elutasitva\n";

  // R16) Ha a celeszkoz nincs csatlakozva, ne menjen ki semmi (es ne ragadjon be)
  jelenlegiUzemmod = normalUzemmod;      // celpont: PC (conn 0)
  disconnectPeer(0);
  resetKeyState();
  click1();
  assert(g_keyCount == 0 && !hasKeyPressed);
  // a telefon celpontu uzemmod viszont mukodik
  jelenlegiUzemmod = mediaVezerloUzemmod;
  resetKeyState();
  click2();
  assert(g_sentTo[1] == 1 && g_consumerCount == 1);
  std::cout << "-- R16 nem csatlakozott celeszkoznel nem ragad be az allapot\n";

  // R17) A PC visszacsatlakozasakor a hirdetes ujraindul, es a fiok megmarad
  advBefore = Bluefruit.Advertising.startCount;
  connectPeer(0, 0xA0);
  assert(slotOfConn(0) == ZW_SLOT_PC);   // BLE cim alapjan visszatalal
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  click1();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 0);
  std::cout << "-- R17 visszacsatlakozas utan a fiok-hozzarendeles megmarad\n";

  // R18) A konfiguracio (celpontok + fiokok) tulel egy mentes/betoltes kort
  expect(send("SETTARGET 1 3"), "OK", "Verseny -> mindketto");
  assert(saveKeymap());
  memset(&keymap, 0, sizeof(keymap));
  assert(loadKeymap());
  assert(keymap.modeTarget[1] == ZW_TARGET_ALL);
  assert(keymap.peers[ZW_SLOT_PC].valid && keymap.peers[ZW_SLOT_PHONE].valid);
  assert(keymap.peers[ZW_SLOT_PC].addr[0] == 0xA0);
  std::cout << "-- R18 celpontok es fiokok tuleltek a flash korbeerest\n";

  // R19) A gyari visszaallitas megtartja a fiok-hozzarendelest
  expect(send("DEFAULTS"), "OK DEFAULTS", "gyari visszaallitas");
  assert(keymap.modeTarget[1] == ZW_TARGET_PC);         // celpont visszaall
  assert(keymap.peers[ZW_SLOT_PC].valid);               // de a fiok megmarad
  assert(keymap.peers[ZW_SLOT_PC].addr[0] == 0xA0);
  std::cout << "-- R19 gyari visszaallitas nem torli a PC/telefon hozzarendelest\n";

  // R20) Ugyanaz az eszkoz ne kerulhessen ket fiokba
  expect(send("ASSIGN 1 0"), "OK", "conn 0 athelyezese az 1-es fiokba");
  assert(!keymap.peers[0].valid);        // a regi hozzarendeles megszunt
  assert(keymap.peers[1].valid && keymap.peers[1].addr[0] == 0xA0);
  expect(send("ASSIGN 0 7"), "ERR NOTCONNECTED", "nem letezo kapcsolat");
  expect(send("ASSIGN 9 0"), "ERR RANGE", "ervenytelen fiok");
  std::cout << "-- R20 egy eszkoz csak egy fiokban lehet\n";

  // ==================== MASODIK KOROS REVIEW JAVITASAI ====================
  std::cout << "\n--- masodik koros regresszio ---\n";
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0); connectPeer(1, 0xB0);
  send("ASSIGN 0 0"); send("ASSIGN 1 1");

  // R21) Egy kapcsolat bontasa ne hagyja beragadva a masikra kiment billentyut
  jelenlegiUzemmod = mediaVezerloUzemmod;      // celpont: mindketto
  resetKeyState();
  click2();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 1 && pressedTargetCount == 2);
  disconnectPeer(0);                            // a PC lecsatlakozik
  g_millis += 200; loop(); g_millis += 200; loop();
  if (g_consumerReleaseCount == 0) {
    std::cout << "HIBA R21: a telefonon beragadt a billentyu (nem volt felengedes)\n";
    return 1;
  }
  assert(!hasConsumerKeyPressed && pressedTargetCount == 0);
  std::cout << "-- R21 egy kapcsolat bontasa nem ragasztja be a masikat\n";
  connectPeer(0, 0xA0);

  // R22) A nezet-szamlalo ne csusszon el, ha a celeszkoz nincs csatlakozva
  expect(send("SETTARGET 0 1"), "OK", "Normal -> csak PC");
  jelenlegiUzemmod = normalUzemmod;
  nezet = 0;
  disconnectPeer(0);                            // a PC (a celpont) nincs fent
  resetKeyState();
  doubleclick5(); doubleclick5(); doubleclick5();
  if (nezet != 0) {
    std::cout << "HIBA R22: a nezet-szamlalo " << nezet
              << "-re lepett, pedig egy billentyu sem ment ki\n";
    return 1;
  }
  connectPeer(0, 0xA0);
  resetKeyState();
  doubleclick5();
  assert(g_lastKey.code == HID_KEY_1 && nezet == 1);
  std::cout << "-- R22 nezet-szamlalo nem lep, ha a celeszkoz nincs fent\n";

  // R23) disconnectBle() a valodi azonositokon menjen vegig (ne 0..count-1)
  disconnectPeer(0);                            // csak az 1-es handle marad elo
  assert(connHandles[0] == BLE_CONN_HANDLE_INVALID);
  g_disconnected.clear();
  ble_sleep();
  if (std::find(g_disconnected.begin(), g_disconnected.end(), (uint16_t)1)
      == g_disconnected.end()) {
    std::cout << "HIBA R23: az 1-es azonositoju kapcsolat bontatlan maradt "
                 "(bontva: " << g_disconnected.size() << " db)\n";
    return 1;
  }
  std::cout << "-- R23 alvas elott minden elo kapcsolat bontasra kerul\n";
  connectPeer(0, 0xA0);

  // R24) Parositatlan kapcsolat: ne lehessen fiokhoz rendelni, es ne is
  //      azonositsuk (a cime valtozo, tehat ertelmetlen elmenteni)
  disconnectPeer(1);
  connectPeer(1, 0xE0, /*bonded=*/false);
  expect(send("ASSIGN 1 1"), "ERR NOTBONDED", "parositatlan kapcsolat elutasitva");
  assert(slotOfConn(1) == -1);
  expect(send("PEERS"), "CONN 1 E5:E4:E3:E2:E1:E0 -1 0", "PEERS jelzi: nincs parositva");
  g_conns[1].bonded = true;                     // parositas megtortent
  expect(send("ASSIGN 1 1"), "OK", "parositas utan mar hozzarendelheto");
  expect(send("PEERS"), "CONN 1 E5:E4:E3:E2:E1:E0 1 1", "PEERS jelzi: parositva");
  std::cout << "-- R24 csak parositott kapcsolat rendelheto fiokhoz\n";

  // R25) Sikertelen atnevezes ne semmisitse meg a regi konfiguraciot
  assert(saveKeymap());
  std::string good = g_fs["/keymap.bin"];
  g_fsRenameFail = true;
  bool ok2 = saveKeymap();
  g_fsRenameFail = false;
  assert(!ok2);
  if (g_fs.count("/keymap.bin") == 0 || g_fs["/keymap.bin"] != good) {
    std::cout << "HIBA R25: a sikertelen atnevezes tonkretette a mentett konfiguraciot\n";
    return 1;
  }
  assert(g_fs.count("/keymap.tmp") == 0);
  std::cout << "-- R25 sikertelen atnevezes utan a regi konfiguracio ep\n";

  // ==================== MUVELETENKENTI CEL-FELULBIRALAS ====================
  std::cout << "\n--- muveletenkenti cel-felulbiralas ---\n";
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0); connectPeer(1, 0xB0);
  send("ASSIGN 0 0"); send("ASSIGN 1 1");

  // R26) Gyari felulbiralas: a gepfuggo parancsok csak a PC-re mennek
  expect(send("GET"), "MAP 2 0 2 1 12 21 0 60 1", "Media/G1/hosszu gyarilag csak PC");
  jelenlegiUzemmod = mediaVezerloUzemmod;       // az uzemmod celpontja: mindketto
  // a harom gepfuggo hosszu nyomas gyarilag CSAK a PC-re megy
  for (uint8_t b = 0; b < 3; b++) {
    assert(keymap.map[2][b][EV_LONG].target == ZW_TARGET_PC);
  }
  resetKeyState(); longPressStart1();
  if (!(g_sentTo[0] == 1 && g_sentTo[1] == 0)) {
    std::cout << "HIBA R26: G1 hosszu PC=" << g_sentTo[0] << " telefon=" << g_sentTo[1] << "\n";
    return 1;
  }
  resetKeyState(); longPressStart3();
  if (!(g_sentTo[0] == 1 && g_sentTo[1] == 0)) {
    std::cout << "HIBA R26: G3 hosszu PC=" << g_sentTo[0] << " telefon=" << g_sentTo[1] << "\n";
    return 1;
  }
  // a media gombok viszont tovabbra is mindkettore
  resetKeyState(); click1();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 1);
  resetKeyState(); longPressStart4(); longPress4();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 1);
  longPressStop4();
  std::cout << "-- R26 gyarilag a 3 gepfuggo hosszu nyomas csak a PC-re megy\n";

  // R27) Media/G1 hosszu -> csak a PC, kozben a tobbi cella valtozatlan
  expect(send("SET 2 0 2 1 12 21 0 60 1"), "OK", "Media/G1/hosszu -> csak PC");
  resetKeyState();
  longPressStart1();
  if (!(g_sentTo[0] == 1 && g_sentTo[1] == 0)) {
    std::cout << "HIBA R27: PC=" << g_sentTo[0] << " telefon=" << g_sentTo[1]
              << " (vart: 1 / 0)\n";
    return 1;
  }
  // ugyanennek a gombnak a ROVID nyomasa tovabbra is mindkettore megy
  resetKeyState();
  click1();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 1);
  std::cout << "-- R27 csak a hosszu nyomas celja valtozott, a rovide nem\n";

  // R28) Media/G2 hosszu -> csak a telefon
  expect(send("SET 2 1 2 1 4 43 0 60 2"), "OK", "Media/G2/hosszu -> csak telefon");
  resetKeyState();
  longPressStart2();
  assert(g_sentTo[0] == 0 && g_sentTo[1] == 1);
  // a G5 hosszu nyomasa (nincs felulbiralva) tovabbra is mindkettore megy
  resetKeyState();
  longPressStart5(); longPress5();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 1);
  longPressStop5();
  std::cout << "-- R28 gombonkent kulon allithato, a tobbit nem erinti\n";

  // R29) A felulbiralas az ismetlodo hosszu nyomasra is vonatkozik
  expect(send("SET 2 3 2 2 0 234 1 70 1"), "OK", "Media/G4/hosszu ismetlo -> csak PC");
  resetKeyState();
  longPressStart4(); longPress4();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 0);
  g_millis += 100; longPress4();
  assert(g_sentTo[0] == 2 && g_sentTo[1] == 0);
  longPressStop4();
  std::cout << "-- R29 az ismetles is a felulbiralt celpontra megy\n";

  // R30) Ervenytelen maszk elutasitva, a 9. mezo elhagyhato marad
  expect(send("SET 2 0 2 1 12 21 0 60 4"), "ERR VALUE", "ismeretlen cel-bit");
  expect(send("SET 2 0 2 1 12 21 0 60"), "OK", "a 9. mezo elhagyhato");
  expect(send("GET"), "MAP 2 0 2 1 12 21 0 60 0", "elhagyva 0 lesz (oroklodik)");
  std::cout << "-- R30 ervenytelen maszk elutasitva, a mezo elhagyhato\n";

  // R31) A felulbiralas tulel egy flash korbeerest
  expect(send("SET 2 0 2 1 12 21 0 60 1"), "OK", "felulbiralas visszaallitva");
  assert(saveKeymap());
  memset(&keymap, 0, sizeof(keymap));
  assert(loadKeymap());
  assert(keymap.map[2][0][2].target == ZW_TARGET_PC);
  assert(keymap.map[2][2][2].target == ZW_TARGET_PC);
  std::cout << "-- R31 a felulbiralas tulelte a mentest\n";

  // ==================== HARMADIK KOROS REVIEW ====================
  std::cout << "\n--- harmadik koros regresszio ---\n";
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0); connectPeer(1, 0xB0);
  send("ASSIGN 0 0"); send("ASSIGN 1 1");

  // R32) Ismetles kozben inditott MASIK muvelet ne ragadjon be a sajat celpontjan
  expect(send("SETTARGET 2 2"), "OK", "Media uzemmod -> csak telefon");
  expect(send("SET 2 0 2 1 12 21 0 60 1"), "OK", "Media/G1/hosszu -> csak PC");
  jelenlegiUzemmod = mediaVezerloUzemmod;
  resetKeyState();
  longPressStart4(); longPress4();           // G4 ismetel a telefonra
  assert(g_sentTo[1] == 1 && duringLongpress);
  longPressStart1();                          // kozben G1 hosszu nyomas (PC)
  if (g_sentTo[0] != 0) {
    std::cout << "HIBA R32: ismetles kozben masik muvelet indult (PC=" << g_sentTo[0]
              << "), aminek a felengedese elmaradna\n";
    return 1;
  }
  longPressStop1();
  longPressStop4();
  g_millis += 200; loop(); g_millis += 200; loop();
  assert(!hasKeyPressed && !hasConsumerKeyPressed && pressedTargetCount == 0);
  std::cout << "-- R32 ismetles alatt nem indul masik kuldes (nincs beragadas)\n";
  send("SETTARGET 2 3"); loadDefaultKeymap();

  // R33) Ismetles vege utan csak a tenylegesen kuldott fajta engedodjon fel
  jelenlegiUzemmod = mediaVezerloUzemmod;     // G4 hosszu = media, ismetlo
  resetKeyState();
  longPressStart4(); longPress4();
  longPressStop4();
  g_millis += 200; loop(); g_millis += 200; loop();
  if (g_keyReleaseCount != 0) {
    std::cout << "HIBA R33: media ismetles utan folosleges billentyu-felengedes\n";
    return 1;
  }
  assert(g_consumerReleaseCount >= 1);
  // billentyus ismetlesnel forditva
  jelenlegiUzemmod = normalUzemmod;           // G4 hosszu = le nyil, ismetlo
  resetKeyState();
  longPressStart4(); longPress4();
  longPressStop4();
  g_millis += 200; loop(); g_millis += 200; loop();
  assert(g_keyReleaseCount >= 1 && g_consumerReleaseCount == 0);
  std::cout << "-- R33 csak a ténylegesen kuldott fajta engedodik fel\n";

  // R34) Ket gomb: az atvett ismetles azonnal felengedodik a regi celponton
  // Mindketto NYOMVA TARTVA modban, kulonben nincs mit felengedni atvetelkor.
  expect(send("SET 0 3 2 1 0 81 1 30"), "OK", "Normal/G4/hosszu = billentyu, nyomva tartva");
  expect(send("SET 0 4 2 2 0 234 1 70"), "OK", "Normal/G5/hosszu = media, nyomva tartva");
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  longPressStart4(); longPress4();            // billentyus ismetles (nyomva tartva)
  assert(g_keyCount == 1 && g_keyReleaseCount == 0);
  longPressStart5();                           // G5 atveszi
  if (g_keyReleaseCount == 0) {
    std::cout << "HIBA R34: az atvett ismetles billentyuje nem lett felengedve\n";
    return 1;
  }
  longPress5();
  longPressStop5();
  g_millis += 200; loop(); g_millis += 200; loop();
  assert(!hasKeyPressed && !hasConsumerKeyPressed);
  std::cout << "-- R34 atvetelkor a regi ismetles azonnal felengedodik\n";
  loadDefaultKeymap();

  // R35) A bontast a fociklus veszi eszre (a callback nem turkalja az allapotot)
  jelenlegiUzemmod = mediaVezerloUzemmod;
  resetKeyState();
  click2();                                    // mindket eszkozre megy
  assert(pressedTargetCount == 2);
  g_conns[0].up = false;                       // a PC eltunik, callback nelkul is
  disconnect_callback(0, 0x13);
  assert(pressedTargetCount == 2);             // a callback nem nyul hozza
  loop();
  assert(pressedTargetCount == 1 && pressedTargets[0] == 1);
  g_millis += 200; loop(); g_millis += 200; loop();
  assert(g_consumerReleaseCount >= 1 && !hasConsumerKeyPressed);
  std::cout << "-- R35 a bontast a fociklus takaritja, a felengedes kimegy\n";
  connectPeer(0, 0xA0);

  // R36) Regi, hosszabb ideiglenes fajl ne fuzodjon hozza az uj tartalomhoz
  g_fs["/jelenlegiuzemmod.tmp"] = "versenyEdzesUzemmod";   // ottmaradt szemet
  saveUzemmod(CONTENTNormal);
  if (g_fs["/jelenlegiuzemmod.txt"] != CONTENTNormal) {
    std::cout << "HIBA R36: '" << g_fs["/jelenlegiuzemmod.txt"] << "'\n";
    return 1;
  }
  assert(readStoredMode() == normalUzemmod);
  std::cout << "-- R36 ottmaradt ideiglenes fajl nem rontja el a mentest\n";

  // R37) Az uzemmod beolvasasa Arduino String nelkul, ervenytelen tartalomra is
  g_fs["/jelenlegiuzemmod.txt"] = "valami egeszen mas";
  assert(readStoredMode() == normalUzemmod);
  g_fs["/jelenlegiuzemmod.txt"] = CONTENTMedia;
  assert(readStoredMode() == mediaVezerloUzemmod);
  g_fs.erase("/jelenlegiuzemmod.txt");
  assert(readStoredMode() == normalUzemmod);
  assert(g_fs.count("/jelenlegiuzemmod.txt") == 1);   // letre is hozta
  std::cout << "-- R37 uzemmod-beolvasas fix pufferrel, hibas tartalomra is\n";

  // ==================== ISMETLES: KULON LEUTESEK ====================
  std::cout << "\n--- ismetles: felengedes az ismetlesek kozott ---\n";
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0);

  // R38) Nagy ismetlesi idonel a billentyu ne maradjon lenyomva
  //      (ez volt a "beragadtnak tunik Windows alatt" hiba)
  expect(send("SET 0 3 2 1 0 81 3 2000"), "OK", "normal/G4/hosszu: 2000 ms, kulon leutesek");
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  longPressStart4(); longPress4(); settleTap();
  assert(g_keyCount == 1);
  if (g_keyReleaseCount != 1) {
    std::cout << "HIBA R38: az ismetles utan nem ment ki felengedes -> a host "
                 "vegig lenyomva tartottnak latja\n";
    return 1;
  }
  // 2 masodpercig nem tortenik semmi tovabbi
  for (int i = 0; i < 50; i++) { g_millis += 20; longPress4(); updateRepeatTap(); }
  assert(g_keyCount == 1 && g_keyReleaseCount == 1);
  g_millis += 1100;
  longPress4(); settleTap();
  assert(g_keyCount == 2 && g_keyReleaseCount == 2);
  longPressStop4();
  loop(); g_millis += 200; loop();
  assert(g_keyCount == 2 && g_keyReleaseCount == 3);   // + zaro biztonsagi felengedes
  std::cout << "-- R38 2000 ms-os ismetles: 2 leutes, 2 felengedes, semmi tobb\n";

  // R39) Nyomva tartva modban viszont NINCS kozbenso felengedes
  expect(send("SET 0 3 2 1 0 81 1 2000"), "OK", "ugyanaz nyomva tartva");
  resetKeyState();
  longPressStart4(); longPress4();
  assert(g_keyCount == 1 && g_keyReleaseCount == 0);
  g_millis += 2100; longPress4();
  assert(g_keyCount == 2 && g_keyReleaseCount == 0);
  longPressStop4();
  loop(); g_millis += 200; loop();
  assert(g_keyReleaseCount == 1);      // egyetlen felengedes a vegen
  std::cout << "-- R39 nyomva tartva modban csak a vegen van felengedes\n";

  // R40) Media (consumer) billentyunel is mukodik a kulon leutes
  expect(send("SET 2 3 2 2 0 234 3 100"), "OK", "media/G4/hosszu: kulon leutesek");
  jelenlegiUzemmod = mediaVezerloUzemmod;
  resetKeyState();
  longPressStart4(); longPress4(); settleTap();
  assert(g_consumerCount == 1 && g_consumerReleaseCount == 1);
  assert(g_keyReleaseCount == 0);      // billentyuzet-felengedes nem megy ki
  g_millis += 150; longPress4(); settleTap();
  assert(g_consumerCount == 2 && g_consumerReleaseCount == 2);
  longPressStop4();
  loop(); g_millis += 200; loop();
  assert(g_consumerReleaseCount == 3); // + zaro biztonsagi felengedes
  std::cout << "-- R40 media billentyu kulon leutesekkel, felesleges esemeny nelkul\n";

  // R41) Ket eszkoznel mindkettore megy a felengedes is
  connectPeer(1, 0xB0);
  expect(send("SETTARGET 2 3"), "OK", "media -> mindketto");
  resetKeyState();
  longPressStart4(); longPress4(); settleTap();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 1);
  assert(g_consumerReleaseCount == 2);   // kapcsolatonkent egy felengedes
  longPressStop4();
  std::cout << "-- R41 ket eszkoznel a felengedes is mindkettore megy\n";

  // R42) Ervenyesseg: a rep mezo bitmaszk
  expect(send("SET 0 3 2 1 0 81 3 100"), "OK", "rep=3 elfogadva");
  expect(send("SET 0 3 2 1 0 81 2 100"), "ERR VALUE", "rep=2 (RELEASE ismetles nelkul) elutasitva");
  expect(send("SET 0 3 2 1 0 81 5 100"), "ERR VALUE", "rep=5 (HOLD_MOD RELEASE nelkul) elutasitva");
  expect(send("SET 0 3 2 1 0 81 4 100"), "ERR VALUE", "rep=4 (csak HOLD_MOD) elutasitva");
  expect(send("SET 0 3 2 1 0 81 6 100"), "ERR VALUE", "rep=6 elutasitva");
  expect(send("SET 0 3 2 1 0 81 0 100"), "OK", "rep=0 (nincs ismetles) rendben");
  expect(send("SET 0 3 2 1 0 81 1 100"), "OK", "rep=1 (nyomva tartva) rendben");
  expect(send("SET 0 3 2 1 0 81 8 100"), "ERR VALUE", "rep=8 elutasitva");
  // rep=0 utan a hosszu nyomas egyszeri kuldes, nem ismetles
  expect(send("SET 0 3 2 1 0 81 0 100"), "OK", "nincs ismetles");
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  longPressStart4();
  int afterFirst = g_keyCount;
  assert(afterFirst > 0);                // egyszeri kuldes tortent
  longPress4(); g_millis += 200; longPress4();
  if (g_keyCount != afterFirst) {
    std::cout << "HIBA R42: nem ismetlodo muvelet ismetelt\n";
    return 1;
  }
  longPressStop4();
  std::cout << "-- R42 rep mezo ervenyessege es a csak-RELEASE bit kezelese\n";
  loadDefaultKeymap();

  // ==================== ALT+TAB: MODOSITO NYOMVA TARTASA ====================
  std::cout << "\n--- modosito nyomva tartasa ismetles kozben ---\n";
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1); connectPeer(0, 0xA0);

  // R43) Gyari Alt+Tab: ismetlodo, kulon leutesek, a modosito nyomva marad
  const KeyAction& altTab = keymap.map[2][1][EV_LONG];
  assert(altTab.type == ACT_KEY && altTab.code == HID_KEY_TAB);
  assert(altTab.modifier == KEYBOARD_MODIFIER_LEFTALT);
  assert(altTab.repeat == (ZW_REPEAT_ENABLED | ZW_REPEAT_RELEASE | ZW_REPEAT_HOLD_MOD));
  jelenlegiUzemmod = mediaVezerloUzemmod;
  resetKeyState();
  longPressStart2(); longPress2(); settleTap();
  assert(g_keyCount == 1 && g_lastPressedCode == HID_KEY_TAB);
  // az ismetles utani "felengedes" jelentesben az ALT-nak benne kell maradnia
  if (g_lastModifier != KEYBOARD_MODIFIER_LEFTALT) {
    std::cout << "HIBA R43: az ismetles utan a modosito is felengedodott "
                 "(mod=0x" << std::hex << (int)g_lastModifier << std::dec
              << ") -> a Windows csak ket ablak kozott valtana\n";
    return 1;
  }
  assert(g_keyReleaseCount == 1);      // a Tab felengedve, az Alt nem
  // masodik lepes: ujabb Tab, az Alt vegig nyomva
  g_millis += 600; longPress2(); settleTap();
  assert(g_keyCount == 2 && g_lastPressedCode == HID_KEY_TAB
         && g_lastModifier == KEYBOARD_MODIFIER_LEFTALT);
  // a gomb elengedesekor viszont az ALT is felengedodik
  longPressStop2();
  assert(hasKeyPressed && pressedTargetCount == 1);   // van meg mit felengedni
  loop(); g_millis += 200; loop();
  if (g_lastModifier != 0) {
    std::cout << "HIBA R43: a gomb elengedese utan az Alt nyomva maradt\n";
    return 1;
  }
  assert(!hasKeyPressed);
  std::cout << "-- R43 Alt+Tab: az Alt vegig nyomva, a vegen felengedve\n";

  // R44) HOLD_MOD nelkul a modosito is felengedodik minden ismetlesnel
  expect(send("SET 2 1 2 1 4 43 3 500 1"), "OK", "Alt+Tab HOLD_MOD nelkul");
  resetKeyState();
  longPressStart2(); longPress2(); settleTap();
  assert(g_keyCount == 1);
  if (g_lastModifier != 0) {
    std::cout << "HIBA R44: HOLD_MOD nelkul is nyomva maradt a modosito\n";
    return 1;
  }
  longPressStop2();
  loop(); g_millis += 200; loop();
  assert(!hasKeyPressed);
  std::cout << "-- R44 HOLD_MOD nelkul a modosito is felengedodik\n";

  // R45) HOLD_MOD modosito nelkuli billentyunel nem valtoztat semmin
  expect(send("SET 0 3 2 1 0 81 7 100"), "OK", "le nyil, HOLD_MOD-dal (nincs modosito)");
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  longPressStart4(); longPress4(); settleTap();
  assert(g_keyCount == 1 && g_keyReleaseCount == 1);
  assert(g_lastModifier == 0);          // teljes felengedes, mint HOLD_MOD nelkul
  longPressStop4();
  std::cout << "-- R45 modosito nelkul a HOLD_MOD nem valtoztat semmin\n";

  // R46) Ervenyesseg: a rep mezo mar 0..7
  expect(send("SET 0 3 2 1 0 81 7 100"), "OK", "rep=7 elfogadva");
  expect(send("SET 0 3 2 1 0 81 8 100"), "ERR VALUE", "rep=8 elutasitva");
  std::cout << "-- R46 rep mezo ervenyessege 0..7\n";
  loadDefaultKeymap();

  // ==================== FUNKCIONALIS ATFESULES ====================
  std::cout << "\n--- funkcionalis atfesules ---\n";
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1); connectPeer(0, 0xA0);

  // F1) LOAD parancs: a flash-ben levo allapotot tolti vissza
  expect(send("SET 0 0 0 1 0 4 0 60"), "OK", "modositas (A billentyu)");
  expect(send("SAVE"), "OK SAVED", "mentes");
  expect(send("SET 0 0 0 1 0 5 0 60"), "OK", "ujabb modositas (B billentyu)");
  expect(send("GET"), "MAP 0 0 0 1 0 5 0 60 0", "a RAM-ban a B van");
  expect(send("LOAD"), "OK LOADED", "visszatoltes");
  expect(send("GET"), "MAP 0 0 0 1 0 4 0 60 0", "a LOAD utan ujra az A");
  std::cout << "-- F1 LOAD parancs visszatolt a flash-bol\n";

  // F2) LOAD ervenytelen fajlnal hibat ad, es nem rontja el a RAM-ot
  g_fs["/keymap.bin"][30] ^= 0xFF;                  // CRC serules
  expect(send("SET 0 0 0 1 0 6 0 60"), "OK", "RAM-ban C billentyu");
  expect(send("LOAD"), "ERR LOAD", "serult fajl elutasitva");
  expect(send("GET"), "MAP 0 0 0 1 0 6 0 60 0", "a RAM valtozatlan maradt");
  g_fs.erase("/keymap.bin");
  expect(send("LOAD"), "ERR LOAD", "hianyzo fajl elutasitva");
  std::cout << "-- F2 hibas/hianyzo mentes nem rontja el a memoriat\n";

  // F3) Parancs-ertelmezes: kis/nagybetu, ismeretlen, ures, hianyos
  expect(send("ping"), "OK ZWIFT_BUTTONS", "kisbetus parancs");
  expect(send("PiNg"), "OK ZWIFT_BUTTONS", "vegyes betus parancs");
  expect(send("NINCSILYEN"), "ERR UNKNOWN", "ismeretlen parancs");
  expect(send("SET"), "ERR ARGS", "parameter nelkuli SET");
  expect(send("SET 0 0 0"), "ERR ARGS", "hianyos SET");
  expect(send("SETTARGET"), "ERR ARGS", "parameter nelkuli SETTARGET");
  expect(send("ASSIGN"), "ERR ARGS", "parameter nelkuli ASSIGN");
  expect(send("CLEARSLOT"), "ERR ARGS", "parameter nelkuli CLEARSLOT");
  std::cout << "-- F3 parancs-ertelmezes es hianyos parameterek\n";

  // F4) Szamertek-hatarok: a negativ ertek elojeltelenul olvasva tullep
  expect(send("SET 0 0 0 1 0 -1 0 60"), "ERR VALUE", "negativ kod elutasitva");
  expect(send("SET 0 0 0 1 -1 4 0 60"), "ERR VALUE", "negativ modosito elutasitva");
  expect(send("SET -1 0 0 1 0 4 0 60"), "ERR RANGE", "negativ uzemmod elutasitva");
  expect(send("SET 0 9 0 1 0 4 0 60"), "ERR RANGE", "tul nagy gombszam");
  expect(send("SET 0 0 9 1 0 4 0 60"), "ERR RANGE", "tul nagy esemeny");
  expect(send("MODE 9"), "ERR RANGE", "tul nagy uzemmod");
  expect(send("CLEARSLOT 9"), "ERR RANGE", "tul nagy fiok");
  std::cout << "-- F4 szamertek-hatarok mindenhol ellenorizve\n";

  // F5) MODE parameter nelkul az aktualisat jelenti
  jelenlegiUzemmod = versenyEdzesUzemmod;
  expect(send("MODE"), "OK MODE 1", "MODE parameter nelkul");
  expect(send("MODE 0"), "OK MODE 0", "MODE 0");
  assert(jelenlegiUzemmod == normalUzemmod);
  std::cout << "-- F5 MODE lekerdezes es beallitas\n";

  // F6) DBG oda-vissza kapcsolhato
  expect(send("DBG 1"), "OK DBG 1", "debug be");
  assert(debugSerial);
  expect(send("DBG 0"), "OK DBG 0", "debug ki");
  assert(!debugSerial);
  std::cout << "-- F6 DBG kapcsolo\n";

  // F7) Uzemmod-valtas BLE kapcsolat nelkul is mukodik
  loadDefaultKeymap();
  Bluefruit.isConnected = false;
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  doubleclick4();
  if (jelenlegiUzemmod != versenyEdzesUzemmod) {
    std::cout << "HIBA F7: BLE nelkul nem valt uzemmodot\n";
    return 1;
  }
  Bluefruit.isConnected = true;
  jelenlegiUzemmod = normalUzemmod;
  std::cout << "-- F7 uzemmod-valtas BLE nelkul is megy\n";

  // F8) Nezetvaltas korbeer 9 utan
  nezet = 0;
  for (int i = 1; i <= 11; i++) {
    resetKeyState();
    doubleclick5();
    uint8_t want = (uint8_t)(HID_KEY_1 + ((i - 1) % 9));
    if (g_lastPressedCode != want) {
      std::cout << "HIBA F8: " << i << ". nezet 0x" << std::hex << (int)g_lastPressedCode
                << " (vart 0x" << (int)want << ")" << std::dec << "\n";
      return 1;
    }
  }
  std::cout << "-- F8 nezetvaltas korbeer (11 lepes at a 9-en)\n";

  // F9) Minden gomb minden esemenye kikuld valamit a gyari kiosztassal
  loadDefaultKeymap();
  for (uint8_t m = 0; m < ZW_NUM_MODES; m++) {
    jelenlegiUzemmod = (uzemmod)m;
    for (uint8_t b = 0; b < ZW_NUM_BUTTONS; b++) {
      for (uint8_t e = 0; e < ZW_NUM_EVENTS; e++) {
        const KeyAction& a = keymap.map[m][b][e];
        if (a.type != ACT_KEY && a.type != ACT_CONSUMER) continue;
        resetKeyState();
        // az adott gomb adott esemenye
        void (*fns[5][3])() = {
          {click1, doubleclick1, longPressStart1}, {click2, doubleclick2, longPressStart2},
          {click3, doubleclick3, longPressStart3}, {click4, doubleclick4, longPressStart4},
          {click5, doubleclick5, longPressStart5}};
        fns[b][e]();
        if (e == EV_LONG && isRepeating(a)) {
          void (*during[5])() = {longPress1, longPress2, longPress3, longPress4, longPress5};
          during[b]();
        }
        if (g_keyCount + g_consumerCount == 0) {
          std::cout << "HIBA F9: uzemmod " << (int)m << " gomb " << (b + 1)
                    << " esemeny " << (int)e << " nem kuldott semmit\n";
          return 1;
        }
        void (*stops[5])() = {longPressStop1, longPressStop2, longPressStop3,
                              longPressStop4, longPressStop5};
        if (e == EV_LONG) stops[b]();
      }
    }
  }
  std::cout << "-- F9 mind a 45 gyari bejegyzes kikuld valamit\n";
  jelenlegiUzemmod = normalUzemmod;

  // ==================== NEGYEDIK KOROS REVIEW ====================
  std::cout << "\n--- negyedik koros regresszio ---\n";
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1); connectPeer(0, 0xA0);

  // R47) Uzemmod-valtas ismetles kozben ne szorja fekezetlenul a billentyut
  jelenlegiUzemmod = mediaVezerloUzemmod;      // G5 hosszu = hangero+, ismetlodo
  resetKeyState();
  longPressStart5(); longPress5(); settleTap();
  assert(g_consumerCount == 1);
  jelenlegiUzemmod = normalUzemmod;            // masik ujjal uzemmodot valtunk
  int before47 = g_keyCount;
  for (int i = 0; i < 20; i++) { g_millis += 25; longPress5(); updateRepeatTap(); }
  if (g_keyCount - before47 > 1) {
    std::cout << "HIBA R47: uzemmod-valtas utan " << (g_keyCount - before47)
              << " billentyu ment ki fekezetlenul\n";
    return 1;
  }
  assert(repeatButton == -1);                  // az ismetles lezarult
  assert(g_consumerReleaseCount >= 1);         // a media billentyu felengedve
  longPressStop5();
  std::cout << "-- R47 uzemmod-valtas ismetles kozben rendesen lezarja azt\n";

  // R48) Valodi 2-es/3-as formatumu (8 bajtos bejegyzesu) mentes betoltese.
  // A fajl tenyleg a regi szerkezet, nem csak atirt verziomezo.
  for (uint16_t ver = 2; ver <= 3; ver++) {
    loadDefaultKeymap();
    KeymapConfigV3 old3;
    memset(&old3, 0, sizeof(old3));
    old3.magic = ZW_KEYMAP_MAGIC;
    old3.version = ver;
    old3.entrySize = sizeof(KeyActionV3);
    old3.modes = ZW_NUM_MODES; old3.buttons = ZW_NUM_BUTTONS;
    old3.events = ZW_NUM_EVENTS; old3.slots = ZW_NUM_SLOTS;
    for (uint8_t m = 0; m < 3; m++)
      for (uint8_t b = 0; b < 5; b++)
        for (uint8_t e = 0; e < 3; e++) {
          const KeyAction& src = keymap.map[m][b][e];
          KeyActionV3& dst = old3.map[m][b][e];
          dst.type = src.type; dst.modifier = src.modifier; dst.code = src.code;
          // a regi formatumban az ismetles mindig a "nyomva tartva" mod volt
          dst.repeat = src.repeat ? (ver == 2 ? ZW_REPEAT_ENABLED : src.repeat) : 0;
          dst.target = src.target; dst.repeatMs = src.repeatMs;
        }
    memcpy(old3.modeTarget, keymap.modeTarget, sizeof(old3.modeTarget));
    memcpy(old3.peers, keymap.peers, sizeof(old3.peers));
    old3.crc = zwCrc32((const uint8_t*)&old3, sizeof(old3) - sizeof(uint32_t));
    assert(writeFileAtomic("/keymap.bin", "/keymap.tmp", &old3, sizeof(old3)));

    memset(&keymap, 0, sizeof(keymap));
    if (!loadKeymap()) {
      std::cout << "HIBA R48: a " << ver << "-es formatumu mentes nem toltodott be\n";
      return 1;
    }
    if (keymap.version != ZW_KEYMAP_VERSION || keymap.entrySize != sizeof(KeyAction)) {
      std::cout << "HIBA R48: a betoltott kiosztas nem az uj formatum lett\n";
      return 1;
    }
    // a kiosztas tartalma atjott
    assert(keymap.map[0][0][0].type == ACT_KEY && keymap.map[0][0][0].code == HID_KEY_ARROW_LEFT);
    assert(keymap.modeTarget[2] == ZW_TARGET_ALL);
    assert(keymap.map[2][0][EV_LONG].target == ZW_TARGET_PC);
    // az uj mezo a korabbi viselkedest jelento 0
    assert(keymap.map[0][0][0].holdMs == 0);
    // 2-es mentesnel a beragadt-billentyu javitas is eljut ide
    if (ver == 2 && keymap.map[0][3][2].repeat != (ZW_REPEAT_ENABLED | ZW_REPEAT_RELEASE)) {
      std::cout << "HIBA R48: a regi repeat=1 nem alakult at (kapott "
                << (int)keymap.map[0][3][2].repeat << ")\n";
      return 1;
    }
    assert(keymap.map[0][0][0].repeat == 0);
  }
  std::cout << "-- R48 valodi 2-es es 3-as mentes is atjon az uj formatumba\n";

  // R49) Ismeretlen (tul uj) verzio elutasitva
  loadDefaultKeymap();
  keymap.version = 99; keymap.crc = keymapCrc(keymap);
  assert(writeFileAtomic("/keymap.bin", "/keymap.tmp", &keymap, sizeof(keymap)));
  memset(&keymap, 0, sizeof(keymap));
  assert(!loadKeymap());
  std::cout << "-- R49 ismeretlen formatum-verzio elutasitva\n";
  loadDefaultKeymap(); g_fs.erase("/keymap.bin");

  // R50) Leutes-impulzus hossza: legalabb egy 60 Hz-es mintavetel (16,7 ms)
  assert(repeatTapMs >= 17);
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  longPressStart4(); longPress4();
  assert(g_keyCount == 1 && g_keyReleaseCount == 0);   // meg tart az impulzus
  g_millis += 10; updateRepeatTap();
  assert(g_keyReleaseCount == 0);                       // 10 ms utan meg nem
  g_millis += 15; updateRepeatTap();
  assert(g_keyReleaseCount == 1);                       // 25 ms utan igen
  longPressStop4();
  std::cout << "-- R50 a leutes-impulzus " << repeatTapMs << " ms, nem blokkolo\n";

  // R51) Vezeto szokozos parancs is ertelmezheto
  expect(send("  PING"), "OK ZWIFT_BUTTONS", "vezeto szokozok atugorva");
  expect(send("\tGET"), "END", "vezeto tabulator atugorva");
  std::cout << "-- R51 vezeto szokozok nem nemitjak el a valaszt\n";

  // R52) Zaro biztonsagi felengedes elveszett notifikacio ellen
  loadDefaultKeymap();
  resetKeyState();
  longPressStart4(); longPress4(); settleTap();
  assert(pressedTargetCount > 0);      // a celpontok megmaradnak
  longPressStop4();
  int rel52 = g_keyReleaseCount;
  loop(); g_millis += 200; loop();
  if (g_keyReleaseCount <= rel52) {
    std::cout << "HIBA R52: nincs zaro biztonsagi felengedes\n";
    return 1;
  }
  std::cout << "-- R52 a gomb elengedesekor zaro felengedes is kimegy\n";

  // R53) Cel-felulbiralas BARMELYIK cellan (nem csak a Media/hosszu harmason)
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0); connectPeer(1, 0xB0);
  send("ASSIGN 0 0"); send("ASSIGN 1 1");
  {
    // Minden uzemmod / gomb / esemeny elfogadja a 9. mezot.
    int refused = 0;
    for (int m = 0; m < ZW_NUM_MODES; m++)
      for (int b = 0; b < ZW_NUM_BUTTONS; b++)
        for (int e = 0; e < ZW_NUM_EVENTS; e++) {
          char c[64];
          snprintf(c, sizeof(c), "SET %d %d %d 1 0 81 0 60 2", m, b, e);
          if (send(c).find("OK") == std::string::npos) refused++;
        }
    if (refused) {
      std::cout << "HIBA R53: " << refused << " cella nem fogadta el a cel-felulbiralast\n";
      return 1;
    }
    // …es tenyleg ervenyesul ott is, ahol eddig nem lehetett beallitani.
    loadDefaultKeymap();
    expect(send("SETTARGET 0 1"), "OK", "Normal uzemmod -> csak PC");
    expect(send("SET 0 0 0 1 0 80 0 60 2"), "OK", "Normal/G1/rovid -> csak telefon");
    expect(send("SET 0 1 1 1 0 79 0 60 2"), "OK", "Normal/G2/dupla -> csak telefon");
    jelenlegiUzemmod = normalUzemmod;

    resetKeyState();
    click1(); g_millis += 200; loop();
    if (!(g_sentTo[0] == 0 && g_sentTo[1] == 1)) {
      std::cout << "HIBA R53: rovid nyomas felulbiralasa nem ervenyesult (PC="
                << g_sentTo[0] << " telefon=" << g_sentTo[1] << ")\n";
      return 1;
    }
    resetKeyState();
    doubleclick2(); g_millis += 200; loop();
    if (!(g_sentTo[0] == 0 && g_sentTo[1] == 1)) {
      std::cout << "HIBA R53: dupla kattintas felulbiralasa nem ervenyesult (PC="
                << g_sentTo[0] << " telefon=" << g_sentTo[1] << ")\n";
      return 1;
    }
    // A felulbiralas nelkuli cella tovabbra is az uzemmod celpontjat hasznalja.
    resetKeyState();
    click3(); g_millis += 200; loop();
    assert(g_sentTo[0] == 1 && g_sentTo[1] == 0);
  }
  std::cout << "-- R53 cel-felulbiralas minden cellan mukodik, a tobbi orokol\n";

  // R54) A gyari kiosztasban tovabbra is CSAK a Media/G1-G3 hosszu van felulbiralva
  loadDefaultKeymap();
  {
    int overridden = 0;
    for (int m = 0; m < ZW_NUM_MODES; m++)
      for (int b = 0; b < ZW_NUM_BUTTONS; b++)
        for (int e = 0; e < ZW_NUM_EVENTS; e++)
          if (keymap.map[m][b][e].target) {
            overridden++;
            bool expected = (m == 2 && e == EV_LONG && b <= 2
                             && keymap.map[m][b][e].target == ZW_TARGET_PC);
            if (!expected) {
              std::cout << "HIBA R54: varatlan gyari felulbiralas " << m << "/" << b
                        << "/" << e << " = " << (int)keymap.map[m][b][e].target << "\n";
              return 1;
            }
          }
    if (overridden != 3) {
      std::cout << "HIBA R54: " << overridden << " gyari felulbiralas (vart: 3)\n";
      return 1;
    }
  }
  std::cout << "-- R54 gyarilag csak a Media/G1-G3 hosszu ter el, a tobbi orokol\n";

  // R55) Media kod: a HID leiro 0x03FF-ig hirdet, a folotte levot ne fogadjuk el
  loadDefaultKeymap();
  expect(send("SET 2 0 0 2 0 207 0 60"), "OK", "Siri (0x00CF) elfogadva");
  expect(send("GET"), "MAP 2 0 0 2 0 207 0 60", "a 0x00CF vissza is olvashato");
  expect(send("SET 2 0 0 2 0 1023 0 60"), "OK", "a tartomany teteje (0x03FF) meg jo");
  expect(send("SET 2 0 0 2 0 1024 0 60"), "ERR VALUE", "0x0400 mar elutasitva");
  expect(send("SET 2 0 0 2 0 65535 0 60"), "ERR VALUE", "0xFFFF elutasitva");
  // Billentyu-muveletnel valtozatlanul a 0xFF a hatar
  expect(send("SET 2 0 0 1 0 256 0 60"), "ERR VALUE", "billentyunel tovabbra is 0xFF");
  std::cout << "-- R55 media kod a HID tartomanyon kivul elutasitva\n";

  // R56) Fuggo lenyomas + kozben indulo ismetles: a regi celponton se ragadjon be
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0); connectPeer(1, 0xB0);
  send("ASSIGN 0 0"); send("ASSIGN 1 1");
  jelenlegiUzemmod = mediaVezerloUzemmod;
  resetKeyState();
  for (int i = 0; i < 8; i++) g_releasedTo[i] = 0;
  // Media/G1/rovid = media billentyu, orokli az uzemmod celpontjat -> MINDKETTO
  click1();
  loop();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 1);
  assert(hasConsumerKeyPressed);
  // Meg a 100 ms-os felengedes elott indul a G2 hosszu nyomas: Alt+Tab, CSAK a PC-re
  g_millis += 30;
  longPressStart2(); longPress2();
  for (int i = 0; i < 40; i++) { g_millis += 25; loop(); }
  longPressStop2();
  for (int i = 0; i < 40; i++) { g_millis += 25; loop(); }
  if (g_releasedTo[1] == 0) {
    std::cout << "HIBA R56: a telefon nem kapott felengedest, a media billentyu"
              << " beragadt (PC-re " << g_releasedTo[0] << ", telefonra "
              << g_releasedTo[1] << ")\n";
    return 1;
  }
  assert(!hasKeyPressed && !hasConsumerKeyPressed && pressedTargetCount == 0);
  std::cout << "-- R56 az ismetles atvetele elott a fuggo lenyomas felengedodik\n";

  // R57) Uzemmod-valtas ismetles kozben, ha az UJ muvelet IS ismetlodo, de mast kuld
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(1);                  // egy kapcsolat eleg, konnyebb szamolni
  jelenlegiUzemmod = normalUzemmod;   // G4 hosszu = Le nyil (BILLENTYU), ismetlodo
  resetKeyState();
  longPressStart4(); longPress4();
  assert(g_keyCount == 1 && g_consumerCount == 0);
  {
    int relBefore = g_keyReleaseCount;
    send("MODE 2");                   // Media: G4 hosszu = Hangero- (MEDIA), szinten ismetlodo
    for (int i = 0; i < 10; i++) { g_millis += 100; longPress4(); }
    if (g_keyReleaseCount == relBefore) {
      std::cout << "HIBA R57: a billentyus ismetles felengedes nelkul maradt, "
                << "miutan az uj uzemmod media billentyut ismetel\n";
      return 1;
    }
    if (g_consumerCount != 0) {
      std::cout << "HIBA R57: a valtas utan az ismetles nem allt le\n";
      return 1;
    }
  }
  longPressStop4();
  for (int i = 0; i < 10; i++) { g_millis += 50; loop(); }
  assert(!hasKeyPressed && !hasConsumerKeyPressed && pressedTargetCount == 0);
  jelenlegiUzemmod = normalUzemmod;
  std::cout << "-- R57 mas muveletre valto ismetles rendesen lezarul\n";

  // R58) Lassu fociklus: a leutesek kozti felengedes akkor sem maradhat ki
  loadDefaultKeymap();
  jelenlegiUzemmod = normalUzemmod;   // 30 ms-os ismetles, kulon leutesekkel
  resetKeyState();
  longPressStart4();
  for (int i = 0; i < 12; i++) {
    g_millis += 35;                   // hosszabb kor, mint az ismetlesi ido
    updateRepeatTap();
    longPress4();
  }
  if (g_keyReleaseCount < g_keyCount - 1) {
    std::cout << "HIBA R58: lassu koroknel elmaradnak a felengedesek ("
              << g_keyCount << " leutes, " << g_keyReleaseCount << " felengedes)\n";
    return 1;
  }
  longPressStop4();
  for (int i = 0; i < 5; i++) { g_millis += 50; loop(); }
  std::cout << "-- R58 lassu koroknel is minden leutest felengedes kovet\n";

  // R59) Idozitett kuldes rovid nyomasnal: tartas, felengedes nelkul
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(1);
  jelenlegiUzemmod = normalUzemmod;
  expect(send("SET 0 0 0 1 0 80 0 60 0 1000"), "OK", "G1 rovid = 1000 ms-ig tartva");
  expect(send("GET"), "MAP 0 0 0 1 0 80 0 60 0 1000", "a 10. mezo visszaolvashato");
  resetKeyState();
  click1();
  assert(g_keyCount == 1);            // egyetlen lenyomas
  {
    int rel = g_keyReleaseCount;
    // A kuldes alatt a fociklus nem enged fel, es nem is kuld semmi mast.
    for (int i = 0; i < 15; i++) { g_millis += 10; loop(); }
    if (g_keyCount != 1) {
      std::cout << "HIBA R59: idozitett kuldes kozben masik parancs is kiment ("
                << g_keyCount << " leutes)\n";
      return 1;
    }
    if (g_keyReleaseCount != rel) {
      std::cout << "HIBA R59: a billentyu korabban felengedodott\n";
      return 1;
    }
    assert(burstActive);
  }
  // az ido letelte utan felengedodik es ujra lehet parancsot kuldeni
  // (a kor most 5 ms-ot var, mert idozitett kuldes van folyamatban)
  for (int i = 0; i < 40; i++) { g_millis += 30; loop(); }
  if (g_keyReleaseCount == 0) {
    std::cout << "HIBA R59: a beallitott ido utan sem lett felengedve\n";
    return 1;
  }
  assert(!burstActive && !duringLongpress && !hasKeyPressed);
  resetKeyState();
  click2(); loop();
  assert(g_keyCount == 1);            // a kuldes vege utan mar mehet mas
  std::cout << "-- R59 rovid nyomas idozitett kuldese vegigtart\n";

  // R59b) Futo kuldest barmelyik gombnyomas megszakit, de a sajat parancsa
  //       mar nem megy ki - igy sosem torlodik ket parancs egymasra.
  loadDefaultKeymap();
  jelenlegiUzemmod = normalUzemmod;
  expect(send("SET 0 0 0 1 0 80 0 60 0 5000"), "OK", "G1 rovid = 5 masodperc");
  resetKeyState();
  click1();
  assert(burstActive && g_keyCount == 1);
  {
    int before = g_keyCount;
    click2();                          // masik gomb: megszakit
    if (burstActive) {
      std::cout << "HIBA R59b: a gombnyomas nem szakitotta meg a kuldest\n";
      return 1;
    }
    if (g_keyCount != before) {
      std::cout << "HIBA R59b: a megszakito gombnyomas parancsa is kiment\n";
      return 1;
    }
  }
  // a megszakitas utan felengedodik, es a kovetkezo gombnyomas mar mukodik
  for (int i = 0; i < 10; i++) { g_millis += 50; loop(); }
  assert(g_keyReleaseCount >= 1 && !hasKeyPressed);
  resetKeyState();
  click2(); loop();
  assert(g_keyCount == 1);
  std::cout << "-- R59b a futo kuldes megszakithato, a megszakito nyomas nem kuld\n";

  // R60) Idozitett kuldes kulon leutesekkel (dupla kattintas)
  loadDefaultKeymap();
  jelenlegiUzemmod = normalUzemmod;
  expect(send("SET 0 0 1 1 0 80 3 100 0 1000"), "OK", "G1 dupla = 1000 ms, 100 ms-onkent");
  resetKeyState();
  doubleclick1();
  for (int i = 0; i < 60; i++) { g_millis += 20; loop(); }
  // 1000 ms alatt 100 ms-onkent: nagysagrendileg 10 leutes, mindegyik felengedve
  if (g_keyCount < 8 || g_keyCount > 12) {
    std::cout << "HIBA R60: " << g_keyCount << " leutes ment ki (vart: ~10)\n";
    return 1;
  }
  if (g_keyReleaseCount < g_keyCount - 1) {
    std::cout << "HIBA R60: " << g_keyReleaseCount << " felengedes " << g_keyCount
              << " leutesre\n";
    return 1;
  }
  assert(!burstActive && !hasKeyPressed && !hasConsumerKeyPressed);
  std::cout << "-- R60 idozitett kuldes kulon leutesekkel a beallitott utemben\n";

  // R61) A hossz csak rovid/dupla nyomasnal ervenyes, es kell hozza ismetles-ido
  expect(send("SET 0 0 2 1 0 80 0 60 0 1000"), "ERR VALUE", "hosszu nyomasnal nincs hossz");
  expect(send("SET 0 0 0 1 0 80 3 100 0 6000"), "ERR VALUE", "5000 ms folott elutasitva");
  expect(send("SET 0 0 0 1 0 80 3 100 0 0"), "ERR VALUE", "ismetles hossz nelkul ertelmetlen");
  expect(send("SET 0 0 0 1 0 80 0 60 0 0"), "OK", "hossz nelkul a regi viselkedes");
  expect(send("SET 0 0 0 4 0 0 0 60 0 1000"), "ERR VALUE", "nezetvaltas nem tarthato nyomva");
  expect(send("SET 0 0 0 3 0 0 0 60 0 1000"), "ERR VALUE", "uzemmodvaltas sem");
  std::cout << "-- R61 a kuldesi hossz ervenyessege ellenorzott\n";

  // R62) Ismetles kozben ne induljon idozitett kuldes (masik celponton beragadna)
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0); connectPeer(1, 0xB0);
  send("ASSIGN 0 0"); send("ASSIGN 1 1");
  jelenlegiUzemmod = mediaVezerloUzemmod;
  // G4 hosszu: media, NYOMVA TARTVA (rep=1) -> nincs kozbenso felengedes
  expect(send("SET 2 3 2 2 0 234 1 200"), "OK", "G4 hosszu = nyomva tartva");
  // G1 rovid: 1000 ms-os idozitett kuldes, CSAK a PC-re
  expect(send("SET 2 0 0 1 0 80 0 60 1 1000"), "OK", "G1 rovid = 1000 ms, csak PC");
  resetKeyState();
  for (int i = 0; i < 8; i++) g_releasedTo[i] = 0;
  longPressStart4(); longPress4();
  assert(g_sentTo[0] == 1 && g_sentTo[1] == 1 && duringLongpress);
  click1();                     // ismetles kozben erkezo rovid nyomas
  if (burstActive) {
    std::cout << "HIBA R62: ismetles kozben elindult egy idozitett kuldes, "
              << "a masik celponton beragadna a billentyu\n";
    return 1;
  }
  longPressStop4();
  for (int i = 0; i < 40; i++) { g_millis += 40; loop(); }
  if (g_releasedTo[1] == 0) {
    std::cout << "HIBA R62: a telefon nem kapott felengedest\n";
    return 1;
  }
  assert(!hasKeyPressed && !hasConsumerKeyPressed && pressedTargetCount == 0);
  // az ismetles vege utan viszont mar indulhat
  resetKeyState();
  click1();
  assert(burstActive);
  for (int i = 0; i < 50; i++) { g_millis += 40; loop(); }
  assert(!burstActive);
  std::cout << "-- R62 ismetles kozben nem indul idozitett kuldes\n";

  // R63) A review altal jelzett firmware-hibak
  // a) kapcsolat-veszte utan ne ragadjon az ismetles-lenyomat
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(1);
  jelenlegiUzemmod = normalUzemmod;      // G4 hosszu = billentyu
  resetKeyState();
  longPressStart4(); longPress4();
  assert(g_keyCount == 1);
  disconnectPeer(0);                      // megszakad a kapcsolat
  loop();                                 // pruneLostTargets
  longPressStop4();
  connectPeer(0, 0xA0);
  jelenlegiUzemmod = mediaVezerloUzemmod; // G5 hosszu = MEDIA tipus
  resetKeyState();
  longPressStart5();
  for (int i = 0; i < 5; i++) { g_millis += 100; longPress5(); }
  if (g_consumerCount == 0) {
    std::cout << "HIBA R63a: a korabbi ismetles lenyomata miatt az uj ismetles"
              << " azonnal megszakadt\n";
    return 1;
  }
  longPressStop5();
  for (int i = 0; i < 5; i++) { g_millis += 50; loop(); }

  // b) ha a celpont nincs csatlakozva, az idozitett kuldes ne blokkoljon
  loadDefaultKeymap();
  send("ASSIGN 0 0");                     // csak a PC van hozzarendelve
  jelenlegiUzemmod = mediaVezerloUzemmod;
  expect(send("SET 2 4 0 1 0 80 0 60 2 5000"), "OK", "G5 rovid: 5 s, csak telefonra");
  resetKeyState();
  click5();
  if (burstActive) {
    std::cout << "HIBA R63b: elindult a kuldes, pedig egyetlen celpont sem volt\n";
    return 1;
  }
  {
    int before = g_keyCount + g_consumerCount;
    for (int i = 0; i < 5; i++) { g_millis += 50; loop(); click1(); }
    if (g_keyCount + g_consumerCount == before) {
      std::cout << "HIBA R63b: a tobbi gomb is nemava valt\n";
      return 1;
    }
  }

  // c) a kuldesi hossz also hatara
  loadDefaultKeymap();
  expect(send("SET 0 0 0 1 0 4 0 60 0 20"), "ERR VALUE", "20 ms tul rovid");
  expect(send("SET 0 0 0 1 0 4 0 60 0 50"), "OK", "50 ms elfogadva");
  expect(send("SET 0 0 0 1 0 4 0 60 0 0"), "OK", "0 = rovid impulzus");
  std::cout << "-- R63 kapcsolat-veszte, ures celpont es hossz-hatar rendben\n";

  // R64) Ket eszkoz: ha az EGYIK kiesik, tudjon-e visszacsatlakozni
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0); connectPeer(1, 0xB0);
  assert(!Bluefruit.Advertising.isRunning());   // ket kapcsolat: nincs mit hirdetni
  disconnectPeer(1);                            // a telefon kiesik
  if (!Bluefruit.Advertising.isRunning()) {
    std::cout << "HIBA R64: egy kapcsolat kiesese utan nem hirdet az eszkoz, "
              << "a kiesett eszkoz nem tud visszacsatlakozni\n";
    return 1;
  }
  connectPeer(1, 0xB0);                         // vissza is tud jonni
  assert(Bluefruit.connected() == 2);
  std::cout << "-- R64 egy kapcsolat kiesese utan ujra hirdet az eszkoz\n";

  // R65) Ismetles kozben az EGYIK kapcsolat kiesik: a masik mukodjon tovabb
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0); connectPeer(1, 0xB0);
  send("ASSIGN 0 0"); send("ASSIGN 1 1");
  jelenlegiUzemmod = mediaVezerloUzemmod;    // G4 hosszu: mindket eszkozre
  resetKeyState();
  longPressStart4(); longPress4();
  assert(g_sentTo[0] >= 1 && g_sentTo[1] >= 1);
  disconnectPeer(1);                          // a telefon kiesik
  loop();
  assert(pressedTargetCount == 1);            // a PC megmarad celpontnak
  {
    int pcBefore = g_sentTo[0];
    for (int i = 0; i < 6; i++) { g_millis += 100; longPress4(); }
    if (g_sentTo[0] == pcBefore) {
      std::cout << "HIBA R65: az egyik kapcsolat kiesese utan a masik sem kap\n";
      return 1;
    }
  }
  longPressStop4();
  for (int i = 0; i < 10; i++) { g_millis += 50; loop(); }
  assert(!hasKeyPressed && !hasConsumerKeyPressed && pressedTargetCount == 0);
  std::cout << "-- R65 egy kapcsolat kiesese nem allitja le a masikat\n";

  // R66) Visszacsatlakozas MAS kapcsolat-azonositoval: a fiok a BLE cim alapjan
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xB0);                       // a TELEFON jon vissza, de 0-s azonositoval
  if (slotOfConnTest(0) != 1) {
    std::cout << "HIBA R66: a telefon nem a sajat fiokjaba kerult (kapott: "
              << (int)slotOfConnTest(0) << ")\n";
    return 1;
  }
  jelenlegiUzemmod = normalUzemmod;           // Normal: csak a PC-re megy
  resetKeyState();
  click1();
  if (g_sentTo[0] != 0) {
    std::cout << "HIBA R66: a PC-nek szolo parancs a telefonra ment\n";
    return 1;
  }
  std::cout << "-- R66 a fiok a BLE cimet koveti, nem a kapcsolat-azonositot\n";

  // R67) Elveszett HID-ertesites: a felengedes ujrakuldese
  // A valodi BLECharacteristic::notify() nem sorol be es nem probalkozik ujra,
  // ezert egy eldobott FELENGEDES a hostnal beragadt billentyu lenne.
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0);
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  click1();
  assert(g_keyCount == 1);
  g_notifyFail = 1;                     // a kovetkezo kuldes (a felengedes) elveszik
  for (int i = 0; i < 20; i++) { g_millis += 30; loop(); }
  if (g_keyReleaseCount == 0) {
    std::cout << "HIBA R67: az elveszett felengedest nem kuldi ujra, "
              << "a billentyu beragad a hoston\n";
    return 1;
  }
  assert(!hasKeyPressed && pressedTargetCount == 0);

  // b) Ha a LENYOMAS veszik el, ne jegyezzuk fel celpontnak
  resetKeyState();
  g_notifyFail = 1;
  click2();
  if (hasKeyPressed || pressedTargetCount != 0) {
    std::cout << "HIBA R67: el nem kuldott lenyomast is nyilvantart\n";
    return 1;
  }
  for (int i = 0; i < 10; i++) { g_millis += 30; loop(); }
  if (g_keyReleaseCount != 0) {
    std::cout << "HIBA R67: felengedest kuld olyan billentyure, ami sosem ment ki\n";
    return 1;
  }

  // c) Tartos hiba eseten se ragadjon be az eszkoz orokre
  resetKeyState();
  click1();
  assert(hasKeyPressed);
  g_notifyFail = 1000;                  // minden tovabbi kuldes elbukik
  for (int i = 0; i < 60; i++) { g_millis += 30; loop(); }
  if (hasKeyPressed || pressedTargetCount != 0) {
    std::cout << "HIBA R67: tartos kuldesi hiba eseten vegleg beragad az allapot\n";
    return 1;
  }
  g_notifyFail = 0;
  resetKeyState();
  click2(); 
  assert(g_keyCount == 1);              // utana ujra mukodik
  std::cout << "-- R67 elveszett ertesites: felengedes ujrakuldve, lenyomas nem hazudik\n";

  // R68) Egyszerre (ugyanabban a korben) megnyomott gombok
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0);
  jelenlegiUzemmod = normalUzemmod;

  // a) Ket rovid nyomas ugyanabban a korben: csak az elso megy ki (a masodik
  //    nem torlodhat ra), de a masodik semmit nem ront el.
  resetKeyState();
  click1();                       // Balra nyil
  click3();                       // Jobbra nyil - a 100 ms-os ablakon belul
  if (g_keyCount != 1 || g_lastPressedCode != HID_KEY_ARROW_LEFT) {
    std::cout << "HIBA R68a: " << g_keyCount << " billentyu ment ki, utolso 0x"
              << std::hex << (int)g_lastPressedCode << std::dec << "\n";
    return 1;
  }
  for (int i = 0; i < 10; i++) { g_millis += 30; loop(); }
  assert(!hasKeyPressed && pressedTargetCount == 0);
  // b) A felengedes utan viszont a masodik gomb is mukodik
  resetKeyState();
  click3();
  assert(g_keyCount == 1 && g_lastPressedCode == HID_KEY_ARROW_RIGHT);
  for (int i = 0; i < 10; i++) { g_millis += 30; loop(); }

  // c) Ket hosszu nyomas egyszerre: a kesobb indulo veszi at, a masik nem
  //    ragad be, es a vegen tiszta az allapot.
  expect(send("SET 0 0 2 1 0 80 3 100"), "OK", "G1 hosszu = ismetlodo balra");
  expect(send("SET 0 2 2 1 0 79 3 100"), "OK", "G3 hosszu = ismetlodo jobbra");
  resetKeyState();
  longPressStart1(); longPressStart3();
  assert(repeatButton == 2);      // a G3 vette at
  for (int i = 0; i < 4; i++) { g_millis += 120; updateRepeatTap(); longPress1(); longPress3(); }
  if (g_lastPressedCode != HID_KEY_ARROW_RIGHT) {
    std::cout << "HIBA R68c: nem az atvevo gomb billentyuje megy ki\n";
    return 1;
  }
  longPressStop1(); longPressStop3();
  for (int i = 0; i < 10; i++) { g_millis += 50; loop(); }
  assert(!hasKeyPressed && !hasConsumerKeyPressed && pressedTargetCount == 0);

  // d) Az uzemmodvaltas akkor is lefut, ha epp mas parancs van folyamatban:
  //    nem kuld BLE-re, ezert nem torlodhat semmivel.
  loadDefaultKeymap();
  jelenlegiUzemmod = normalUzemmod;
  resetKeyState();
  click1();
  doubleclick4();
  if (jelenlegiUzemmod != versenyEdzesUzemmod) {
    std::cout << "HIBA R68d: az uzemmodvaltas elmaradt\n";
    return 1;
  }
  jelenlegiUzemmod = normalUzemmod;
  for (int i = 0; i < 10; i++) { g_millis += 50; loop(); }
  std::cout << "-- R68 egyszerre nyomott gombok: nincs torlodas es nincs beragadas\n";

  // R69) Fizikailag beragadt gomb: a longPressStop soha nem erkezik meg
  loadDefaultKeymap();
  send("CLEARSLOT 0"); send("CLEARSLOT 1");
  disconnectPeer(0); disconnectPeer(1);
  connectPeer(0, 0xA0);
  jelenlegiUzemmod = normalUzemmod;      // G4 hosszu = ismetlodo le nyil
  resetKeyState();
  longPressStart4();
  // 25 masodpercig meg rendes nyomva tartas: ismetel
  for (int i = 0; i < 800; i++) { g_millis += 30; updateRepeatTap(); longPress4(); }
  assert(g_keyCount > 0);
  // 35 masodperc utan mar beragadtnak kell tekintenie
  for (int i = 0; i < 400; i++) { g_millis += 30; updateRepeatTap(); longPress4(); loop(); }
  int afterStuck = g_keyCount;
  for (int i = 0; i < 200; i++) { g_millis += 30; updateRepeatTap(); longPress4(); loop(); }
  if (g_keyCount != afterStuck) {
    std::cout << "HIBA R69: a beragadt gomb tovabb szorja a billentyut ("
              << (g_keyCount - afterStuck) << " tovabbi leutes)\n";
    return 1;
  }
  // a) a tobbi gomb ujra hasznalhato
  {
    int before = g_keyCount;
    resetKeyState();
    click1();
    if (g_keyCount == 0) {
      std::cout << "HIBA R69: a beragadt gomb miatt a tobbi gomb is nema marad\n";
      return 1;
    }
    (void)before;
  }
  // b) az eszkoz el tud aludni: a tetlensegi szamlalot mar nem nullazza
  watchdogCounter = 0;
  for (int i = 0; i < 50; i++) { g_millis += 30; longPress4(); watchdogCounter++; }
  if (watchdogCounter == 0) {
    std::cout << "HIBA R69: a beragadt gomb ebren tartja az eszkozt\n";
    return 1;
  }
  // c) felengedes utan a gomb ujra mukodik
  longPressStop4();
  for (int i = 0; i < 10; i++) { g_millis += 50; loop(); }
  resetKeyState();
  longPressStart4(); longPress4();
  if (g_keyCount == 0) {
    std::cout << "HIBA R69: felengedes utan sem eled fel a gomb\n";
    return 1;
  }
  longPressStop4();
  for (int i = 0; i < 10; i++) { g_millis += 50; loop(); }
  assert(!hasKeyPressed && pressedTargetCount == 0);
  std::cout << "-- R69 beragadt gomb: leall, nem tiltja a tobbit, engedi az alvast\n";

  // R70) Bekapcsolaskor / ebredeskor mar nyomott gomb
  // Az ebredes a System OFF-bol ujraindulas, ezert ez a ketto ugyanaz az eset.
  {
    // A Gomb 4 a 4-es tuskén van (BUTTON_PIN_INDEX). Beragadva indulunk.
    g_pinLow[4] = true;
    setup();                         // ujrainditas szimulalasa
    send("DBG 0");
    loadDefaultKeymap();
    send("CLEARSLOT 0"); send("CLEARSLOT 1");
    disconnectPeer(0); disconnectPeer(1);
    connectPeer(0, 0xA0);
    jelenlegiUzemmod = normalUzemmod;

    resetKeyState();
    longPressStart4();               // a OneButton 800 ms utan ezt kuldene
    for (int i = 0; i < 100; i++) { g_millis += 30; updateRepeatTap(); longPress4(); loop(); }
    if (g_keyCount != 0) {
      std::cout << "HIBA R70: az induláskor nyomott gomb parancsa kiment ("
                << g_keyCount << " leutes)\n";
      return 1;
    }
    // a tobbi gomb kozben hasznalhato
    resetKeyState();
    click1();
    assert(g_keyCount == 1);
    for (int i = 0; i < 10; i++) { g_millis += 30; loop(); }
    // es az eszkoz el tud aludni (a szamlalo no)
    watchdogCounter = 0;
    for (int i = 0; i < 50; i++) { g_millis += 30; longPress4(); watchdogCounter++; }
    assert(watchdogCounter > 0);
    // felengedes utan a gomb ujra mukodik
    g_pinLow[4] = false;
    longPressStop4();
    for (int i = 0; i < 10; i++) { g_millis += 50; loop(); }
    resetKeyState();
    longPressStart4(); longPress4();
    if (g_keyCount == 0) {
      std::cout << "HIBA R70: felengedes utan sem eled fel a gomb\n";
      return 1;
    }
    longPressStop4();
    for (int i = 0; i < 10; i++) { g_millis += 50; loop(); }
  }

  // b) Alvas, amikor az EBRESZTO gomb van nyomva: a System OFF-ot a DETECT jel
  //    azonnal megszakitana, ujrainditasi hurkot okozva.
  g_systemOffCount = 0;
  g_pinLow[WAKEUP_PIN] = true;
  watchdogCounter = 0;
  for (int i = 0; i < 1000; i++) fct_Watchdog();
  if (g_systemOffCount != 0) {
    std::cout << "HIBA R70: elaludt, pedig az ebreszto gomb nyomva van - "
              << "azonnal ujraindulna\n";
    return 1;
  }
  g_pinLow[WAKEUP_PIN] = false;
  g_systemOffCount = 0;
  fct_Watchdog();
  if (g_systemOffCount != 1) {
    std::cout << "HIBA R70: felengedes utan sem alszik el\n";
    return 1;
  }
  fct_WatchdogReset();
  std::cout << "-- R70 induláskor nyomott gomb es beragadt ebreszto gomb kezelve\n";

  // R71) Megszakadt konfig-mentes (aramszunet / ujrainditas mentes kozben)
  // A mentes atmeneti fajlba ir, es csak hibatlan kiiras utan nevezi at. A
  // littlefs atnevezese aramszunet-biztos (DESIGN.md: globalis allapot +
  // helyreallitas mount-kor), tehat vagy a REGI, vagy az UJ tartalom marad.
  {
    loadDefaultKeymap();
    expect(send("SET 0 0 0 1 0 4 0 60"), "OK", "kiindulasi ertek");
    assert(saveKeymap());
    assert(keymap.map[0][0][0].code == 0x04);

    // a) Megszakadas az atmeneti fajl irasa kozben
    expect(send("SET 0 0 0 1 0 5 0 60"), "OK", "uj, el nem mentett ertek");
    g_fsWriteFail = true;
    bool saved = saveKeymap();
    g_fsWriteFail = false;
    assert(!saved);                       // a mentes hibat jelez
    memset(&keymap, 0, sizeof(keymap));   // "ujrainditas"
    if (!loadKeymap() || keymap.map[0][0][0].code != 0x04) {
      std::cout << "HIBA R71a: megszakadt iras utan elveszett a korabbi mentes\n";
      return 1;
    }

    // b) Megszakadas az atnevezes elott
    expect(send("SET 0 0 0 1 0 6 0 60"), "OK", "ujabb ertek");
    g_fsRenameFail = true;
    saved = saveKeymap();
    g_fsRenameFail = false;
    assert(!saved);
    memset(&keymap, 0, sizeof(keymap));
    if (!loadKeymap() || keymap.map[0][0][0].code != 0x04) {
      std::cout << "HIBA R71b: megszakadt atnevezes utan elveszett a mentes\n";
      return 1;
    }

    // c) Ottmaradt (szemet) atmeneti fajl nem zavarja a kovetkezo mentest
    g_fs["/keymap.tmp"] = std::string(200, 'X');
    expect(send("SET 0 0 0 1 0 7 0 60"), "OK", "harmadik ertek");
    if (!saveKeymap()) {
      std::cout << "HIBA R71c: ottmaradt atmeneti fajl utan nem sikerul a mentes\n";
      return 1;
    }
    memset(&keymap, 0, sizeof(keymap));
    if (!loadKeymap() || keymap.map[0][0][0].code != 0x07) {
      std::cout << "HIBA R71c: a mentes nem az uj tartalmat orizte meg\n";
      return 1;
    }

    // d) Csonka fajl (fel-irodott) - nem szabad betolteni
    g_fs["/keymap.bin"].resize(200);
    memset(&keymap, 0, sizeof(keymap));
    if (loadKeymap()) {
      std::cout << "HIBA R71d: csonka fajlt is betolt\n";
      return 1;
    }

    // e) Ep meretu, de serult tartalom: a CRC fogja meg
    loadDefaultKeymap();
    expect(send("SET 0 0 0 1 0 8 0 60"), "OK", "negyedik ertek");
    assert(saveKeymap());
    g_fs["/keymap.bin"][100] = (char)(g_fs["/keymap.bin"][100] ^ 0x01);
    memset(&keymap, 0, sizeof(keymap));
    if (loadKeymap()) {
      std::cout << "HIBA R71e: egy bitbillenest nem vesz eszre a CRC\n";
      return 1;
    }

    // f) Ugyanez az uzemmod-fajlnal: a megszakadt mentes nem torli a regit
    g_fs.clear();
    saveUzemmod(CONTENTMedia);
    assert(readStoredMode() == mediaVezerloUzemmod);
    g_fsWriteFail = true;
    saveUzemmod(CONTENTNormal);
    g_fsWriteFail = false;
    if (readStoredMode() != mediaVezerloUzemmod) {
      std::cout << "HIBA R71f: megszakadt uzemmod-mentes elvesztette a regi erteket\n";
      return 1;
    }
    g_fs.clear();
    loadDefaultKeymap();
  }
  std::cout << "-- R71 megszakadt mentes: vagy a regi, vagy az uj marad - soha nem szemet\n";

  std::cout << "\nMINDEN TESZT SIKERES\n";
  return 0;
}
