#pragma once
#include <cstdint>
struct PowerRegs { uint32_t DCDCEN; uint32_t SYSTEMOFF; };
extern PowerRegs* NRF_POWER;
