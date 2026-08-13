#pragma once
#define MILLIS 0
class TickTwo { public: TickTwo(void(*f)(), unsigned, unsigned, int) : fn(f) {}
  void start() {} void update() {} void (*fn)(); };
