#pragma once
#include <cstdint>
struct PowerRegs { uint32_t DCDCEN; uint32_t SYSTEMOFF; };
extern PowerRegs* NRF_POWER;

// A hardveres watchdog regiszterei. A teszt g_wdtFeeds/g_wdtStarted alapjan
// tudja ellenorizni, hogy a firmware inditja-e es eteti-e.
struct WdtRegs { uint32_t CONFIG; uint32_t CRV; uint32_t RREN; uint32_t TASKS_START; uint32_t RR[8]; };
extern WdtRegs* NRF_WDT;
#define WDT_CONFIG_HALT_Pause 1
#define WDT_CONFIG_HALT_Pos 3
#define WDT_CONFIG_SLEEP_Run 1
#define WDT_CONFIG_SLEEP_Pos 0
#define WDT_RREN_RR0_Msk 1
#define WDT_RR_RR_Reload 0x6E524635UL

// A SAADC-bol csak az eltolas-kalibralashoz kello regiszterek. A valodi chipen a
// TASKS_CALIBRATEOFFSET irasa inditja a kalibralast, es a HARDVER allitja be
// kesobb az EVENTS_CALIBRATEDONE-t; a stub ezt azonnal megteszi.
//
// g_saadcCalibStuck = true eseten a kesz-jelzes sosem jon meg: igy ellenorizheto,
// hogy a firmware idokorlatja tenyleg kivezet a varakozasbol. A konyvtar sajat
// analogCalibrateOffset()-je ilyenkor orokre bennragadna.
extern bool g_saadcCalibStuck;
extern int g_saadcCalibCount;   // hanyszor inditottunk kalibralast
struct SaadcRegs {
  uint32_t ENABLE = 0;
  uint32_t EVENTS_CALIBRATEDONE = 0;
  // Tasknak latszo mezo: az ertekadas inditja a muveletet, ahogy a chipen is.
  struct Task {
    SaadcRegs* owner = nullptr;
    void operator=(uint32_t) {
      g_saadcCalibCount++;
      if (!g_saadcCalibStuck) owner->EVENTS_CALIBRATEDONE = 1;
    }
  } TASKS_CALIBRATEOFFSET;
  SaadcRegs() { TASKS_CALIBRATEOFFSET.owner = this; }
};
extern SaadcRegs* NRF_SAADC;
