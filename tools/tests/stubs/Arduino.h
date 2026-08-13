#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <deque>
typedef uint8_t byte;
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLUP_SENSE 3
#define LOW 0
#define HIGH 1
extern unsigned long g_millis;
inline unsigned long millis() { return g_millis; }
inline void delay(unsigned long ms) { g_millis += ms; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
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
