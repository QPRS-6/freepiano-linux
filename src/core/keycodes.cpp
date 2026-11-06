#include "keycodes.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

struct key_name_t {
  const char* name;
  uint8_t code;
};

// Same spellings (and lookup order) as the original FreePiano key-name table,
// so key map lines such as "keydown num1 NoteOn 1 c4" keep their meaning.
const key_name_t kKeyNames[] = {
  { "Esc",            DIK_ESCAPE },
  { "F1",             DIK_F1 },
  { "F2",             DIK_F2 },
  { "F3",             DIK_F3 },
  { "F4",             DIK_F4 },
  { "F5",             DIK_F5 },
  { "F6",             DIK_F6 },
  { "F7",             DIK_F7 },
  { "F8",             DIK_F8 },
  { "F9",             DIK_F9 },
  { "F10",            DIK_F10 },
  { "F11",            DIK_F11 },
  { "F12",            DIK_F12 },
  { "~",              DIK_GRAVE },
  { "`",              DIK_GRAVE },
  { "1",              DIK_1 },
  { "2",              DIK_2 },
  { "3",              DIK_3 },
  { "4",              DIK_4 },
  { "5",              DIK_5 },
  { "6",              DIK_6 },
  { "7",              DIK_7 },
  { "8",              DIK_8 },
  { "9",              DIK_9 },
  { "0",              DIK_0 },
  { "-",              DIK_MINUS },
  { "=",              DIK_EQUALS },
  { "Backspace",      DIK_BACK },
  { "Back",           DIK_BACK },
  { "Tab",            DIK_TAB },
  { "Q",              DIK_Q },
  { "W",              DIK_W },
  { "E",              DIK_E },
  { "R",              DIK_R },
  { "T",              DIK_T },
  { "Y",              DIK_Y },
  { "U",              DIK_U },
  { "I",              DIK_I },
  { "O",              DIK_O },
  { "P",              DIK_P },
  { "{",              DIK_LBRACKET },
  { "[",              DIK_LBRACKET },
  { "}",              DIK_RBRACKET },
  { "]",              DIK_RBRACKET },
  { "|",              DIK_BACKSLASH },
  { "\\",             DIK_BACKSLASH },
  { "CapsLock",       DIK_CAPITAL },
  { "Caps",           DIK_CAPITAL },
  { "A",              DIK_A },
  { "S",              DIK_S },
  { "D",              DIK_D },
  { "F",              DIK_F },
  { "G",              DIK_G },
  { "H",              DIK_H },
  { "J",              DIK_J },
  { "K",              DIK_K },
  { "L",              DIK_L },
  { ":",              DIK_SEMICOLON },
  { ";",              DIK_SEMICOLON },
  { "\"",             DIK_APOSTROPHE },
  { "'",              DIK_APOSTROPHE },
  { "Enter",          DIK_RETURN },
  { "Shift",          DIK_LSHIFT },
  { "Z",              DIK_Z },
  { "X",              DIK_X },
  { "C",              DIK_C },
  { "V",              DIK_V },
  { "B",              DIK_B },
  { "N",              DIK_N },
  { "M",              DIK_M },
  { "<",              DIK_COMMA },
  { ",",              DIK_COMMA },
  { ">",              DIK_PERIOD },
  { ".",              DIK_PERIOD },
  { "?",              DIK_SLASH },
  { "/",              DIK_SLASH },
  { "RShift",         DIK_RSHIFT },
  { "Ctrl",           DIK_LCONTROL },
  { "Win",            DIK_LWIN },
  { "Alt",            DIK_LMENU },
  { "Space",          DIK_SPACE },
  { "RAlt",           DIK_RMENU },
  { "Apps",           DIK_APPS },
  { "RWin",           DIK_RWIN },
  { "RCtrl",          DIK_RCONTROL },

  { "Num0",           DIK_NUMPAD0 },
  { "Num.",           DIK_NUMPADPERIOD },
  { "NumEnter",       DIK_NUMPADENTER },
  { "Num1",           DIK_NUMPAD1 },
  { "Num2",           DIK_NUMPAD2 },
  { "Num3",           DIK_NUMPAD3 },
  { "Num4",           DIK_NUMPAD4 },
  { "Num5",           DIK_NUMPAD5 },
  { "Num6",           DIK_NUMPAD6 },
  { "Num7",           DIK_NUMPAD7 },
  { "Num8",           DIK_NUMPAD8 },
  { "Num9",           DIK_NUMPAD9 },
  { "Num+",           DIK_NUMPADPLUS },
  { "NumLock",        DIK_NUMLOCK },
  { "Num/",           DIK_NUMPADSLASH },
  { "Num*",           DIK_NUMPADSTAR },
  { "Num-",           DIK_NUMPADMINUS },

  { "Insert",         DIK_INSERT },
  { "Home",           DIK_HOME },
  { "PgUp",           DIK_PGUP },
  { "PageUp",         DIK_PGUP },
  { "Delete",         DIK_DELETE },
  { "End",            DIK_END },
  { "PgDn",           DIK_PGDN },
  { "PageDown",       DIK_PGDN },

  { "Up",             DIK_UP },
  { "Left",           DIK_LEFT },
  { "Down",           DIK_DOWN },
  { "Right",          DIK_RIGHT },

  { "PrintScreen",    DIK_SYSRQ },
  { "SysRq",          DIK_SYSRQ },
  { "ScrollLock",     DIK_SCROLL },
  { "Pause",          DIK_PAUSE },
  { "Break",          DIK_PAUSE },
};

bool name_equals(const char* a, const char* b) {
  while (*a && *b) {
    if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
    ++a;
    ++b;
  }
  return *a == *b;
}

}  // namespace

const char* key_code_name(uint8_t code) {
  static char numeric[16];
  for (const auto& n : kKeyNames) {
    if (n.code == code) return n.name;
  }
  std::snprintf(numeric, sizeof(numeric), "%u", (unsigned)code);
  return numeric;
}

uint8_t key_code_from_name(const char* name) {
  if (!name) return 0;
  for (const auto& n : kKeyNames) {
    if (name_equals(name, n.name)) return n.code;
  }
  return 0;
}

const char* key_code_display_name(uint8_t code) {
  switch (code) {
    case DIK_RETURN: return "Enter";
    case DIK_BACK: return "Backspace";
    case DIK_CAPITAL: return "Caps";
    case DIK_LSHIFT: return "Shift";
    case DIK_LCONTROL: return "Ctrl";
    case DIK_LMENU: return "Alt";
    case DIK_SPACE: return "Space";
    default: return key_code_name(code);
  }
}

// evdev (Linux input) -> DirectInput scan code.
uint8_t key_code_from_evdev(uint32_t c) {
  switch (c) {
    case 1: return DIK_ESCAPE;
    case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11:
      return (uint8_t)c;  // 1..0 match scan codes 0x02..0x0B
    case 12: return DIK_MINUS;
    case 13: return DIK_EQUALS;
    case 14: return DIK_BACK;
    case 15: return DIK_TAB;
    case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25:
      return (uint8_t)c;  // Q..P
    case 26: return DIK_LBRACKET;
    case 27: return DIK_RBRACKET;
    case 28: return DIK_RETURN;
    case 29: return DIK_LCONTROL;
    case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37: case 38:
      return (uint8_t)c;  // A..L
    case 39: return DIK_SEMICOLON;
    case 40: return DIK_APOSTROPHE;
    case 41: return DIK_GRAVE;
    case 42: return DIK_LSHIFT;
    case 43: return DIK_BACKSLASH;
    case 44: case 45: case 46: case 47: case 48: case 49: case 50:
      return (uint8_t)c;  // Z..M
    case 51: return DIK_COMMA;
    case 52: return DIK_PERIOD;
    case 53: return DIK_SLASH;
    case 54: return DIK_RSHIFT;
    case 55: return DIK_NUMPADSTAR;
    case 56: return DIK_LMENU;
    case 57: return DIK_SPACE;
    case 58: return DIK_CAPITAL;
    case 59: return DIK_F1;
    case 60: return DIK_F2;
    case 61: return DIK_F3;
    case 62: return DIK_F4;
    case 63: return DIK_F5;
    case 64: return DIK_F6;
    case 65: return DIK_F7;
    case 66: return DIK_F8;
    case 67: return DIK_F9;
    case 68: return DIK_F10;
    case 69: return DIK_NUMLOCK;
    case 70: return DIK_SCROLL;
    case 71: return DIK_NUMPAD7;
    case 72: return DIK_NUMPAD8;
    case 73: return DIK_NUMPAD9;
    case 74: return DIK_NUMPADMINUS;
    case 75: return DIK_NUMPAD4;
    case 76: return DIK_NUMPAD5;
    case 77: return DIK_NUMPAD6;
    case 78: return DIK_NUMPADPLUS;
    case 79: return DIK_NUMPAD1;
    case 80: return DIK_NUMPAD2;
    case 81: return DIK_NUMPAD3;
    case 82: return DIK_NUMPAD0;
    case 83: return DIK_NUMPADPERIOD;
    case 87: return DIK_F11;
    case 88: return DIK_F12;
    case 96: return DIK_NUMPADENTER;
    case 97: return DIK_RCONTROL;
    case 98: return DIK_NUMPADSLASH;
    case 99: return DIK_SYSRQ;
    case 100: return DIK_RMENU;
    case 102: return DIK_HOME;
    case 103: return DIK_UP;
    case 104: return DIK_PGUP;
    case 105: return DIK_LEFT;
    case 106: return DIK_RIGHT;
    case 107: return DIK_END;
    case 108: return DIK_DOWN;
    case 109: return DIK_PGDN;
    case 110: return DIK_INSERT;
    case 111: return DIK_DELETE;
    case 116: return DIK_POWER;
    case 142: return DIK_SLEEP;
    case 143: return DIK_WAKE;
    case 125: return DIK_LWIN;
    case 126: return DIK_RWIN;
    case 127: return DIK_APPS;
    default: return 0;
  }
}
