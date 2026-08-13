#pragma once
// A teszt igy tudja megnezni, elaludt-e az eszkoz.
extern int g_systemOffCount;
inline void sd_power_system_off() { g_systemOffCount++; }
