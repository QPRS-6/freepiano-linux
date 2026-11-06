#include "qtkeymap.h"

#include "core/keycodes.h"

#include <QtCore/Qt>

namespace {

bool is_numpad(uint8_t d) {
  return (d >= DIK_NUMPAD7 && d <= DIK_NUMPADPERIOD) ||   // 0x47..0x53
         d == DIK_NUMPADSTAR || d == DIK_NUMPADMINUS || d == DIK_NUMPADPLUS ||
         d == DIK_NUMPADENTER || d == DIK_NUMPADSLASH;
}

bool is_extended(uint8_t d) {
  switch (d) {
    case DIK_RCONTROL:
    case DIK_RMENU:
    case DIK_RSHIFT:
    case DIK_INSERT:
    case DIK_DELETE:
    case DIK_HOME:
    case DIK_END:
    case DIK_PGUP:
    case DIK_PGDN:
    case DIK_LEFT:
    case DIK_RIGHT:
    case DIK_UP:
    case DIK_DOWN:
    case DIK_SYSRQ:
    case DIK_PAUSE:
    case DIK_SCROLL:
    case DIK_LWIN:
    case DIK_RWIN:
    case DIK_APPS:
      return true;
    default:
      return false;
  }
}

uint8_t from_qt_key(int key) {
  if (key >= Qt::Key_A && key <= Qt::Key_Z) {
    // Qt letters are ASCII; map to the PC keyboard matrix.
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
  uint8_t fallback = from_qt_key(qt_key);

  // Linux delivers X11 key codes (evdev + 8) on both X11 and Wayland.  Be
  // tolerant about the offset and prefer scan codes for numpad/extended keys,
  // where Qt's logical key cannot distinguish left/right or keypad variants.
  uint8_t candidates[2] = {0, 0};
  if (native_scan_code >= 9) candidates[0] = key_code_from_evdev(native_scan_code - 8);
  candidates[1] = key_code_from_evdev(native_scan_code);

  for (uint8_t c : candidates) {
    if (c && (is_numpad(c) || is_extended(c))) return c;
  }
  if (fallback) return fallback;
  for (uint8_t c : candidates) {
    if (c) return c;
  }
  return 0;
}
