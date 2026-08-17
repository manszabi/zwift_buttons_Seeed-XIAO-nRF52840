#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <deque>
typedef uint8_t byte;
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLUP_SENSE 3
#define LOW 0
#define HIGH 1
extern unsigned long g_millis;
inline unsigned long millis() { return g_millis; }
inline void delay(unsigned long ms) { g_millis += ms; }
// A gomb-tuskek allapota a tesztbol allithato: g_pinLow[pin] = true jelenti,
// hogy a gomb LE VAN NYOMVA (a gombok aktiv-alacsonyak, felhuzo ellenallassal).
extern bool g_pinLow[32];
inline int digitalRead(int pin) { return g_pinLow[pin] ? LOW : HIGH; }
inline void pinMode(int, int) {}
// ADC: a teszt a g_adcRaw ertekkel adja meg, mit "mer" az eszkoz.
extern int g_adcRaw;
enum { AR_DEFAULT = 0, AR_INTERNAL_3_0 = 1 };
inline void analogReference(int) {}
inline void analogReadResolution(int) {}
inline int analogRead(int) { return g_adcRaw; }
// A mintaveteli ido es a tulmintavetelezes beallitasat rogzitjuk: az akku-oszto
// forrasellenallasa (338 kOhm) miatt ezek nem elhagyhatok, a teszt ellenorzi is.
extern int g_adcSampleTime;     // us; a konyvtar alapertelmezese 3
extern int g_adcOversampling;   // 0 = nincs
inline void analogSampleTime(uint8_t us) { g_adcSampleTime = us; }
inline void analogOversampling(uint32_t n) { g_adcOversampling = (int)n; }
// A kimeneti tuskek allapota. A valodi chipen a pinMode(OUTPUT) csak az iranyt
// allitja, az OUT regisztert nem: az reset utan 0, tehat a lab LOW-ra all. Az
// aktiv-alacsony LED-eknel ez azt jelenti, hogy KIGYULLAD, amig valaki HIGH-ra
// nem irja. Ezert modellezzuk ugyanigy: pinMode(OUTPUT) LOW-ra allit.
// Az OUT reset erteke 0, ezert a tomb is csupa false-szal indul: aki nem irta
// meg HIGH-ra a labat, annal a LED vilagit.
extern bool g_pinOut[32];  // true = HIGH, false = LOW (a LED-eknel: false = vilagit)
inline void digitalWrite(int pin, int val) { if (pin >= 0 && pin < 32) g_pinOut[pin] = (val != LOW); }
extern uint32_t g_ADigitalPinMap[32];
typedef std::string String;
struct SerialClass {
  std::deque<char> rx;      // amit az eszköz "kap"
  std::string tx;           // amit az eszköz kiír
  void begin(long) {}
  int available() { return (int)rx.size(); }
  int read() { if (rx.empty()) return -1; char c = rx.front(); rx.pop_front(); return c; }
  void flush() {}
  void print(const char* s) { tx += s; }
  void print(int v) { tx += std::to_string(v); }
  void print(unsigned v) { tx += std::to_string(v); }
  void print(unsigned long v) { tx += std::to_string(v); }
  void println(const char* s) { tx += s; tx += "\n"; }
  void println(int v) { tx += std::to_string(v); tx += "\n"; }
  void println(unsigned v) { tx += std::to_string(v); tx += "\n"; }
  void println() { tx += "\n"; }
  void feed(const std::string& s) { for (char c : s) rx.push_back(c); }
};
extern SerialClass Serial;
