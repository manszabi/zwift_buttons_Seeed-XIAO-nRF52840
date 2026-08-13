#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <algorithm>
#define FILE_O_READ 0
#define FILE_O_WRITE 1
extern std::map<std::string, std::string> g_fs;
extern bool g_fsWriteFail;    // teszt: minden iras hibara fusson
extern bool g_fsRenameFail;  // teszt: minden atnevezes hibara fusson
extern bool g_fsRemoveFail;  // teszt: a torles hibara fusson
class InternalFSClass {
 public:
  void begin() {} void end() {}
  bool remove(const char* n) { if (g_fsRemoveFail) return false; return g_fs.erase(n) > 0; }
  bool rename(const char* o, const char* n) {
    if (g_fsRenameFail) return false;
    if (!g_fs.count(o)) return false;
    g_fs[n] = g_fs[o]; g_fs.erase(o); return true;
  }
};
extern InternalFSClass InternalFS;
namespace Adafruit_LittleFS_Namespace {
class File {
 public:
  File(InternalFSClass&) {}
  bool open(const char* name, int mode);
  operator bool() const { return open_; }
  int read(void* buf, uint16_t len);
  bool seek(uint32_t pos) { pos_ = pos; return true; }
  bool truncate(uint32_t pos);
  bool truncate();
  size_t write(const uint8_t* buf, size_t len);
  size_t write(const char* buf, size_t len) { return write((const uint8_t*)buf, len); }
  void close() { open_ = false; }
 private:
  bool open_ = false; std::string name_; int mode_ = 0; size_t pos_ = 0;
};
}
