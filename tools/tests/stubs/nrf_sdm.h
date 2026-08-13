#pragma once
#include <cstdint>
inline uint32_t sd_softdevice_is_enabled(uint8_t* p) { *p = 1; return 0; }
