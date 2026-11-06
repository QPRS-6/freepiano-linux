#pragma once

#include "core/config.h"

// Small bilingual helper: the application UI language follows the settings
// (defaults to Chinese on a zh_* locale, English otherwise).
inline const char* fp_tr(const char* zh, const char* en) {
  return config_get_language() == FP_LANG_SCHINESE ? zh : en;
}
