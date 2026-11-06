#include "qtkeymap.h"

#include "core/keycodes.h"

#include <QtCore/Qt>

namespace {

bool is_numpad(uint8_t d) {
  return (d >= DIK_NUMPAD7 && d <= DIK_NUMPADPERIOD) ||  // 0x47..0x53
         d == DIK_NUMPADSTAR || d == DIK_NUMPADMINUS || d == DIK_NUMPADPLUS ||
         d == DIK_NUMPADENTER || d == DIK_NUMPADSLASH;
}

// 0-9, 10 = '.', 11 = '*', 12 = '+', 13 = '-', 14 = '/', 15 = Enter
int numpad_index(uint8_t d) {
  switch (d) {
    case DIK_NUMPAD0: return 0;
    case DIK_NUMPAD1: return 1;
    case DIK_NUMPAD2: return 2;
    case DIK_NUMPAD3: return 3;
    case DIK_NUMPAD4: return 4;
    case DIK_NUMPAD5: return 5;
    case DIK_NUMPAD6: return 6;
    case DIK_NUMPAD7: return 7;
    case DIK_NUMPAD8: return 8;
    case DIK_NUMPAD9: return 9;
    case DIK_NUMPADPERIOD: return 10;
    case DIK_NUMPADSTAR: return 11;
    case DIK_NUMPADPLUS: return 12;
    case DIK_NUMPADMINUS: return 13;
    case DIK_NUMPADSLASH: return 14;
    case DIK_NUMPADENTER: return 15;
    default: return -1;
  }
}

// What numpad key does this Qt key correspond to?  With NumLock on Qt reports
// digits/operators; with NumLock off it reports the navigation functions.
int qt_numpad_index(int key) {
  if (key >= Qt::Key_0 && key <= Qt::Key_9) return key - Qt::Key_0;
  switch (key) {
    case Qt::Key_Period: return 10;
    case Qt::Key_Delete: return 10;
    case Qt::Key_Asterisk: return 11;
    case Qt::Key_Plus: return 12;
    case Qt::Key_Minus: return 13;
    case Qt::Key_Slash: return 14;
    case Qt::Key_Enter: return 15;
    case Qt::Key_Return: return 15;
    // NumLock off
    case Qt::Key_Insert: return 0;
    case Qt::Key_End: return 1;
    case Qt::Key_Down: return 2;
    case Qt::Key_PageDown: return 3;
    case Qt::Key_Left: return 4;
    case Qt::Key_Clear: return 5;
    case Qt::Key_Right: return 6;
    case Qt::Key_Home: return 7;
    case Qt::Key_Up: return 8;
    case Qt::Key_PageUp: return 9;
    default: return -1;
  }
}

uint8_t from_qt_key(int key) {
  if (key >= Qt::Key_A && key <= Qt::Key_Z) {
    static const uint8_t letters[26] = {
      DIK_A, DIK_B, DIK_C, DIK_D, DIK_E, DIK_F, DIK_G, DIK_H, DIK_I, DIK_J, DIK_K, DIK_L, DIK_M,
      DIK_N, DIK_O, DIK_P, DIK_Q, DIK_R, DIK_S, DIK_T, DIK_U, DIK_V, DIK_W, DIK_X, DIK_Y, DIK_Z
    };
    return letters[key - Qt::Key_A];
  }
  if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    static const uint8_t digits[10] = {DIK_0, DIK_1, DIK_2, DIK_3, DIK_4,
                                       DIK_5, DIK_6, DIK_7, DIK_8, DIK_9};
    return digits[key - Qt::Key_0];
  }
  if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
    static const uint8_t fkeys[12] = {DIK_F1,  DIK_F2,  DIK_F3,  DIK_F4,  DIK_F5,  DIK_F6,
                                      DIK_F7,  DIK_F8,  DIK_F9,  DIK_F10, DIK_F11, DIK_F12};
    return fkeys[key - Qt::Key_F1];
  }

  switch (key) {
    case Qt::Key_Escape: return DIK_ESCAPE;
    case Qt::Key_Backspace: return DIK_BACK;
    case Qt::Key_Tab: return DIK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter: return DIK_RETURN;
    case Qt::Key_Shift: return DIK_LSHIFT;
    case Qt::Key_Control: return DIK_LCONTROL;
    case Qt::Key_Alt: return DIK_LMENU;
    case Qt::Key_AltGr: return DIK_RMENU;
    case Qt::Key_Meta: return DIK_LWIN;
    case Qt::Key_Space: return DIK_SPACE;
    case Qt::Key_CapsLock: return DIK_CAPITAL;
    case Qt::Key_NumLock: return DIK_NUMLOCK;
    case Qt::Key_ScrollLock: return DIK_SCROLL;
    case Qt::Key_Print: return DIK_SYSRQ;
    case Qt::Key_Pause: return DIK_PAUSE;
    case Qt::Key_Insert: return DIK_INSERT;
    case Qt::Key_Delete: return DIK_DELETE;
    case Qt::Key_Home: return DIK_HOME;
    case Qt::Key_End: return DIK_END;
    case Qt::Key_PageUp: return DIK_PGUP;
    case Qt::Key_PageDown: return DIK_PGDN;
    case Qt::Key_Left: return DIK_LEFT;
    case Qt::Key_Right: return DIK_RIGHT;
    case Qt::Key_Up: return DIK_UP;
    case Qt::Key_Down: return DIK_DOWN;
    case Qt::Key_Menu: return DIK_APPS;
    case Qt::Key_QuoteLeft: return DIK_GRAVE;
    case Qt::Key_Minus: return DIK_MINUS;
    case Qt::Key_Equal: return DIK_EQUALS;
    case Qt::Key_BracketLeft: return DIK_LBRACKET;
    case Qt::Key_BracketRight: return DIK_RBRACKET;
    case Qt::Key_Backslash: return DIK_BACKSLASH;
    case Qt::Key_Semicolon: return DIK_SEMICOLON;
    case Qt::Key_Apostrophe: return DIK_APOSTROPHE;
    case Qt::Key_Comma: return DIK_COMMA;
    case Qt::Key_Period: return DIK_PERIOD;
    case Qt::Key_Slash: return DIK_SLASH;
    default: return 0;
  }
}

}  // namespace

uint8_t dik_from_qt(int qt_key, uint32_t native_scan_code, uint32_t native_vk) {
  (void)native_vk;

  const uint8_t fallback = from_qt_key(qt_key);
  // Linux deliverables: X11 key codes are evdev + 8; some toolkits report the
  // evdev code directly.  Never blindly trust the +8 form: it aliases many
  // keys onto numpad/extended scan codes (C -> RShift, F5 -> Numpad7, ...).
  const uint8_t candidate_x11 = native_scan_code >= 9 ? key_code_from_evdev(native_scan_code - 8) : 0;
  const uint8_t candidate_raw = key_code_from_evdev(native_scan_code);

  // 1) An exact match with the logical Qt key is unambiguous.
  if (fallback && candidate_x11 == fallback) return candidate_x11;
  if (fallback && candidate_raw == fallback) return candidate_raw;

  // 2) Right-side modifiers cannot be distinguished by Qt's logical key.
  auto right_variant = [&](uint8_t left, uint8_t right) {
    return fallback == left && (candidate_x11 == right || candidate_raw == right);
  };
  if (right_variant(DIK_LSHIFT, DIK_RSHIFT)) return DIK_RSHIFT;
  if (right_variant(DIK_LCONTROL, DIK_RCONTROL)) return DIK_RCONTROL;
  if (right_variant(DIK_LMENU, DIK_RMENU)) return DIK_RMENU;
  if (right_variant(DIK_LWIN, DIK_RWIN)) return DIK_RWIN;

  // 3) Numpad keys: use the Qt key to pick between the +8/raw candidates.
  const int want = qt_numpad_index(qt_key);
  if (want >= 0) {
    if (numpad_index(candidate_x11) == want) return candidate_x11;
    if (numpad_index(candidate_raw) == want) return candidate_raw;
  }
  if (is_numpad(candidate_x11)) return candidate_x11;
  if (is_numpad(candidate_raw)) return candidate_raw;

  // 4) Fall back to the logical key, then to whatever the scan code says.
  if (fallback) return fallback;
  if (candidate_x11) return candidate_x11;
  return candidate_raw;
}
