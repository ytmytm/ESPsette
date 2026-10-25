#pragma once

#include <stdint.h>

inline uint8_t pgm_read_byte(const uint8_t* address) { return *address; }
