#pragma once
// A LittleFS stub megvalositasa, kozosen mindharom teszt-programnak.
// A valodi Adafruit LittleFS szemantikaja: a FILE_O_WRITE nem csonkol, es a
// fajl VEGERE pozicional (ezert kell a firmware-ben a seek(0) + truncate()).
#include "Adafruit_LittleFS.h"
#include <algorithm>
#include <cstring>
namespace Adafruit_LittleFS_Namespace {
inline bool File::open(const char* n, int m) {
  name_ = n; mode_ = m; pos_ = 0;
  if (m == FILE_O_READ) { open_ = g_fs.count(n) > 0; }
  else { open_ = true; pos_ = g_fs[n].size(); }
  return open_;
}
inline int File::read(void* buf, uint16_t len) {
  std::string& s = g_fs[name_];
  size_t n = std::min((size_t)len, s.size() - pos_);
  memcpy(buf, s.data() + pos_, n); pos_ += n; return (int)n;
}
inline size_t File::write(const uint8_t* b, size_t l) {
  if (g_fsWriteFail) return 0;
  std::string& s = g_fs[name_];
  if (s.size() < pos_ + l) s.resize(pos_ + l);
  memcpy(&s[pos_], b, l); pos_ += l; return l;
}
inline bool File::truncate(uint32_t pos) { g_fs[name_].resize(pos); return true; }
inline bool File::truncate() { g_fs[name_].resize(pos_); return true; }
}
