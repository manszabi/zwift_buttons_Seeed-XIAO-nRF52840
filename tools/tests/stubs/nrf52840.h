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
