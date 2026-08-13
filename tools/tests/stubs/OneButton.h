#pragma once
#include <stdint.h>
typedef void (*cbfn)();
typedef void (*pcbfn)(void*);
// A teszt ezen keresztul tud a loop()-on BELUL gomb-callbacket kivaltani,
// ugyanugy, ahogy a valodi OneButton::tick() teszi.
extern cbfn g_pendingCb;
class OneButton {
 public:
  OneButton(int, bool) {}
  pcbfn click = nullptr, dbl = nullptr, lstart = nullptr, lstop = nullptr, lduring = nullptr;
  void* clickP = nullptr; void* dblP = nullptr; void* lstartP = nullptr;
  void* lstopP = nullptr; void* lduringP = nullptr;
  void attachClick(pcbfn f, void* p) { click = f; clickP = p; }
  void attachDoubleClick(pcbfn f, void* p) { dbl = f; dblP = p; }
  void attachLongPressStart(pcbfn f, void* p) { lstart = f; lstartP = p; }
  void attachLongPressStop(pcbfn f, void* p) { lstop = f; lstopP = p; }
  void attachDuringLongPress(pcbfn f, void* p) { lduring = f; lduringP = p; }
  void tick() { if (g_pendingCb) { cbfn f = g_pendingCb; g_pendingCb = nullptr; f(); } }
};
