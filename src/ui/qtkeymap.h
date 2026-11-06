#pragma once

#include <cstdint>

// Translate a Qt key event into a FreePiano/DirectInput scan code (0 = none).
uint8_t dik_from_qt(int qt_key, uint32_t native_scan_code, uint32_t native_vk);
