#pragma once

// Basic types shared by the portable FreePiano core.
// The original FreePiano sources target Win32/DirectInput; this port keeps the
// same event model (4-byte "key_bind_t" tuples), key numbering (IBM set-1 scan
// codes as used by DirectInput) and text key-map format so that existing
// .map / .fpm files keep working.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

typedef uint8_t byte;
typedef uint32_t uint;

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#endif

// A single FreePiano event/script command: (action, arg1, arg2, arg3).
struct key_bind_t {
  byte a;
  byte b;
  byte c;
  byte d;

  key_bind_t() : a(0), b(0), c(0), d(0) {}
  key_bind_t(byte a, byte b, byte c, byte d) : a(a), b(b), c(c), d(d) {}

  bool operator==(const key_bind_t& o) const {
    return a == o.a && b == o.b && c == o.c && d == o.d;
  }
  bool operator!=(const key_bind_t& o) const { return !(*this == o); }
};

template <class T>
inline T clamp_value(T value, T min_value, T max_value) {
  if (value < min_value) value = min_value;
  if (value > max_value) value = max_value;
  return value;
}

template <class T>
inline T wrap_value(T value, T min_value, T max_value) {
  if (value < min_value) value = max_value;
  if (value > max_value) value = min_value;
  return value;
}
