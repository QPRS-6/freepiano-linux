#pragma once

// DirectInput scan codes (IBM PC set 1).  FreePiano key maps use these numbers
// (and the names below); keeping them makes the original .map files load
// unchanged and lets recorded .fpm songs remain compatible.

#include "fp_types.h"

enum : uint8_t {
  DIK_ESCAPE = 0x01,
  DIK_1 = 0x02, DIK_2 = 0x03, DIK_3 = 0x04, DIK_4 = 0x05, DIK_5 = 0x06,
  DIK_6 = 0x07, DIK_7 = 0x08, DIK_8 = 0x09, DIK_9 = 0x0A, DIK_0 = 0x0B,
  DIK_MINUS = 0x0C, DIK_EQUALS = 0x0D, DIK_BACK = 0x0E, DIK_TAB = 0x0F,
  DIK_Q = 0x10, DIK_W = 0x11, DIK_E = 0x12, DIK_R = 0x13, DIK_T = 0x14,
  DIK_Y = 0x15, DIK_U = 0x16, DIK_I = 0x17, DIK_O = 0x18, DIK_P = 0x19,
  DIK_LBRACKET = 0x1A, DIK_RBRACKET = 0x1B, DIK_RETURN = 0x1C,
  DIK_LCONTROL = 0x1D,
  DIK_A = 0x1E, DIK_S = 0x1F, DIK_D = 0x20, DIK_F = 0x21, DIK_G = 0x22,
  DIK_H = 0x23, DIK_J = 0x24, DIK_K = 0x25, DIK_L = 0x26,
  DIK_SEMICOLON = 0x27, DIK_APOSTROPHE = 0x28, DIK_GRAVE = 0x29,
  DIK_LSHIFT = 0x2A, DIK_BACKSLASH = 0x2B,
  DIK_Z = 0x2C, DIK_X = 0x2D, DIK_C = 0x2E, DIK_V = 0x2F, DIK_B = 0x30,
  DIK_N = 0x31, DIK_M = 0x32,
  DIK_COMMA = 0x33, DIK_PERIOD = 0x34, DIK_SLASH = 0x35, DIK_RSHIFT = 0x36,
  DIK_NUMPADSTAR = 0x37, DIK_LMENU = 0x38, DIK_SPACE = 0x39,
  DIK_CAPITAL = 0x3A,
  DIK_F1 = 0x3B, DIK_F2 = 0x3C, DIK_F3 = 0x3D, DIK_F4 = 0x3E, DIK_F5 = 0x3F,
  DIK_F6 = 0x40, DIK_F7 = 0x41, DIK_F8 = 0x42, DIK_F9 = 0x43, DIK_F10 = 0x44,
  DIK_NUMLOCK = 0x45, DIK_SCROLL = 0x46,
  DIK_NUMPAD7 = 0x47, DIK_NUMPAD8 = 0x48, DIK_NUMPAD9 = 0x49,
  DIK_NUMPADMINUS = 0x4A, DIK_NUMPAD4 = 0x4B, DIK_NUMPAD5 = 0x4C,
  DIK_NUMPAD6 = 0x4D, DIK_NUMPADPLUS = 0x4E, DIK_NUMPAD1 = 0x4F,
  DIK_NUMPAD2 = 0x50, DIK_NUMPAD3 = 0x51, DIK_NUMPAD0 = 0x52,
  DIK_NUMPADPERIOD = 0x53,
  DIK_F11 = 0x57, DIK_F12 = 0x58,

  // Extended keys: DirectInput reports an 0x80 flag for the extended block.
  DIK_NUMPADENTER = 0x9C,
  DIK_RCONTROL = 0x9D,
  DIK_NUMPADSLASH = 0xB5,
  DIK_SYSRQ = 0xB7,
  DIK_PAUSE = 0xC5,
  DIK_RMENU = 0xB8,
  DIK_HOME = 0xC7,
  DIK_UP = 0xC8,
  DIK_PGUP = 0xC9,
  DIK_LEFT = 0xCB,
  DIK_RIGHT = 0xCD,
  DIK_END = 0xCF,
  DIK_DOWN = 0xD0,
  DIK_PGDN = 0xD1,
  DIK_INSERT = 0xD2,
  DIK_DELETE = 0xD3,
  DIK_LWIN = 0xDB,
  DIK_RWIN = 0xDC,
  DIK_APPS = 0xDD,
  DIK_POWER = 0xDE,
  DIK_SLEEP = 0xDF,
  DIK_WAKE = 0xE3,
};

// The canonical English name of a scan code (first name in the original table).
const char* key_code_name(uint8_t code);

// Convert a case-insensitive key name from a .map file to a scan code.
// Returns 0 when the name is unknown.
uint8_t key_code_from_name(const char* name);

// Convert a Linux evdev key code (KEY_A == 30, etc.) to a DirectInput scan code.
// Returns 0 when the key is not mapped.
uint8_t key_code_from_evdev(uint32_t evdev);

// Human-readable shortcuts for the UI.
const char* key_code_display_name(uint8_t code);
