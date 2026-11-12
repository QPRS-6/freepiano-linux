// Portable re-implementation of FreePiano's configuration / key-map engine.
//
// Based on the original FreePiano sources (BSD-3-Clause),
// https://github.com/Energy0124/freepiano , src/config.cpp.  The text key-map
// format, event encoding and semantics are kept compatible; Win32 specific
// parts (registry, VST, DirectSound) are replaced by portable equivalents.

#include "config.h"

#include "engine.h"
#include "keycodes.h"
#include "song.h"
#include "sync.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iconv.h>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// name tables (same spellings as the original)
// ---------------------------------------------------------------------------
struct name_t {
  const char* name;
  uint32_t value;
};

static const name_t bind_names[] = {
  { "Keydown", 1 }, { "Keyup", 2 }, { "Label", 3 }, { "Color", 4 },
  { "键盘按下", 1 }, { "键盘松开", 2 }, { "标签", 3 }, { "颜色", 4 },
  { "Key", 1 },
};

static const name_t action_names[] = {
  { "KeySignature", SM_KEY_SIGNATURE },
  { "Octave", SM_OCTAVE },
  { "Velocity", SM_VELOCITY },
  { "Channel", SM_CHANNEL },
  { "Transpose", SM_TRANSPOSE },
  { "FollowKey", SM_FOLLOW_KEY },
  { "Volume", SM_VOLUME },
  { "Play", SM_PLAY },
  { "Record", SM_RECORD },
  { "Stop", SM_STOP },
  { "Group", SM_SETTING_GROUP },
  { "GroupCount", SM_SETTING_GROUP_COUNT },
  { "Note", SM_NOTE_ON },
  { "NoteOn", SM_NOTE_ON },
  { "NoteOff", SM_NOTE_OFF },
  { "NotePressure", SM_NOTE_PRESSURE },
  { "Pressure", SM_PRESSURE },
  { "Pitch", SM_PITCH },
  { "Program", SM_PROGRAM },
  { "BankMSB", SM_BANK_MSB },
  { "BankLSB", SM_BANK_LSB },
  { "Sustain", SM_SUSTAIN },
  { "Modulation", SM_MODULATION },

  { "曲调", SM_KEY_SIGNATURE },
  { "八度", SM_OCTAVE },
  { "力度", SM_VELOCITY },
  { "通道", SM_CHANNEL },
  { "移位", SM_TRANSPOSE },
  { "跟随曲调", SM_FOLLOW_KEY },
  { "总音量", SM_VOLUME },
  { "播放", SM_PLAY },
  { "录制", SM_RECORD },
  { "停止", SM_STOP },
  { "分组", SM_SETTING_GROUP },
  { "分组总数", SM_SETTING_GROUP_COUNT },
  { "音符", SM_NOTE_ON },
  { "止音", SM_NOTE_OFF },
  { "音符力度", SM_NOTE_PRESSURE },
  { "通道力度", SM_PRESSURE },
  { "弯音", SM_PITCH },
  { "乐器", SM_PROGRAM },
  { "乐器组1", SM_BANK_MSB },
  { "乐器组2", SM_BANK_LSB },
  { "延音", SM_SUSTAIN },
  { "颤音", SM_MODULATION },

  { "Octshift", SM_OCTAVE },
  { "KeyboardOctshift", SM_OCTAVE },
  { "KeyboardVeolcity", SM_VELOCITY },
  { "KeyboardChannel", SM_CHANNEL },
  { "Controller", SM_CONTROLLER_DEPRECATED },
  { "MIDI", 0xff },
};

static const name_t value_action_names[] = {
  { "Set", SM_VALUE_SET }, { "Inc", SM_VALUE_INC }, { "Dec", SM_VALUE_DEC },
  { "Flip", SM_VALUE_FLIP }, { "Press", SM_VALUE_PRESS },
  { "Set10", SM_VALUE_SET10 }, { "Set1", SM_VALUE_SET1 },
  { "SyncSet", SM_VALUE_SET | SM_VALUE_SYNC },
  { "SyncInc", SM_VALUE_INC | SM_VALUE_SYNC },
  { "SyncDec", SM_VALUE_DEC | SM_VALUE_SYNC },
  { "SyncFlip", SM_VALUE_FLIP | SM_VALUE_SYNC },
  { "SyncPress", SM_VALUE_PRESS | SM_VALUE_SYNC },

  { "设置为", SM_VALUE_SET }, { "增加", SM_VALUE_INC }, { "减少", SM_VALUE_DEC },
  { "反转", SM_VALUE_FLIP }, { "脉冲", SM_VALUE_PRESS },
  { "设置十位", SM_VALUE_SET10 }, { "设置个位", SM_VALUE_SET1 },
  { "同步设置", SM_VALUE_SET | SM_VALUE_SYNC },
  { "同步增加", SM_VALUE_INC | SM_VALUE_SYNC },
  { "同步减少", SM_VALUE_DEC | SM_VALUE_SYNC },
  { "同步反转", SM_VALUE_FLIP | SM_VALUE_SYNC },
  { "同步脉冲", SM_VALUE_PRESS | SM_VALUE_SYNC },
};

static const name_t channel_names[] = {
  { "In_0", 0 }, { "In_1", 1 }, { "In_2", 2 }, { "In_3", 3 },
  { "In_4", 4 }, { "In_5", 5 }, { "In_6", 6 }, { "In_7", 7 },
  { "In_8", 8 }, { "In_9", 9 }, { "In_10", 10 }, { "In_11", 11 },
  { "In_12", 12 }, { "In_13", 13 }, { "In_14", 14 }, { "In_15", 15 },

  { "Out_0", 16 }, { "Out_1", 17 }, { "Out_2", 18 }, { "Out_3", 19 },
  { "Out_4", 20 }, { "Out_5", 21 }, { "Out_6", 22 }, { "Out_7", 23 },
  { "Out_8", 24 }, { "Out_9", 25 }, { "Out_10", 26 }, { "Out_11", 27 },
  { "Out_12", 28 }, { "Out_13", 29 }, { "Out_14", 30 }, { "Out_15", 31 },

  { "Ch_L", 0 }, { "Ch_R", 1 },
  { "Ch_0", 0 }, { "Ch_1", 1 }, { "Ch_2", 2 }, { "Ch_3", 3 },
  { "Ch_4", 4 }, { "Ch_5", 5 }, { "Ch_6", 6 }, { "Ch_7", 7 },
  { "Ch_8", 8 }, { "Ch_9", 9 }, { "Ch_10", 10 }, { "Ch_11", 11 },
  { "Ch_12", 12 }, { "Ch_13", 13 }, { "Ch_14", 14 }, { "Ch_15", 15 },
};

static const name_t controller_names[] = {
  { "BankSelect", 0 }, { "Modulation", 1 }, { "BreathControl", 2 },
  { "FootPedal", 4 }, { "Portamento", 5 }, { "DataEntry", 6 },
  { "MainVolume", 7 }, { "Balance", 8 }, { "Pan", 10 }, { "Expression", 11 },
  { "EffectSelector1", 12 }, { "EffectSelector2", 13 },
  { "GeneralPurpose1", 16 }, { "GeneralPurpose2", 17 },
  { "GeneralPurpose3", 18 }, { "GeneralPurpose4", 19 },
  { "SustainPedal", 64 }, { "PortamentoPedal", 65 }, { "SostenutoPedal", 66 },
  { "SoftPedal", 67 }, { "LegatoPedal", 68 }, { "Hold2", 69 },
  { "SoundController1", 70 }, { "SoundController6", 75 },
  { "DataIncrement", 96 }, { "DataDecrement", 97 },
  { "NRPNLSB", 98 }, { "NRPNMSB", 99 }, { "RPNLSB", 100 }, { "RPNMSB", 101 },
  { "AllSoundsOff", 120 }, { "ResetAllControllers", 121 },
  { "LocalControlOnOff", 122 }, { "AllNotesOff", 123 },
  { "OmniModeOff", 124 }, { "OmniModeOn", 125 }, { "MonoModeOn", 126 },
  { "PolyModeOn", 127 },
};

static const name_t instrument_type_names[] = {
  { "None", INSTRUMENT_TYPE_NONE },
  { "Builtin", INSTRUMENT_TYPE_BUILTIN },
  { "MdaPiano", INSTRUMENT_TYPE_MDA_PIANO },
  { "Piano", INSTRUMENT_TYPE_MDA_PIANO },
  { "SoundFont", INSTRUMENT_TYPE_SOUNDFONT },
  { "SF2", INSTRUMENT_TYPE_SOUNDFONT },
  { "VSTI", INSTRUMENT_TYPE_MDA_PIANO },  // legacy Windows configs (mdaPiano)
  { "MIDI", INSTRUMENT_TYPE_NONE },
};

static const name_t output_type_names[] = {
  { "Auto", OUTPUT_TYPE_AUTO },
  { "PortAudio", OUTPUT_TYPE_PORTAUDIO },
  { "None", OUTPUT_TYPE_NULL },
};

static const name_t boolean_names[] = {
  { "disable", 0 }, { "no", 0 }, { "false", 0 },
  { "enable", 1 }, { "yes", 1 }, { "true", 1 },
};

// ---------------------------------------------------------------------------
// data
// ---------------------------------------------------------------------------
struct setting_t {
  std::multimap<uint8_t, key_bind_t> keydown_map;
  std::multimap<uint8_t, key_bind_t> keyup_map;
  key_label_t key_label[256];
  int8_t key_octshift[16];
  int8_t key_transpose[16];
  uint8_t key_velocity[16];
  uint8_t key_channel[16];
  int8_t key_signature;
  uint8_t follow_key[16];
  int16_t midi_program[16];
  int16_t midi_controller[16][128];

  setting_t() { clear(); }

  void clear() {
    key_signature = 0;
    keydown_map.clear();
    keyup_map.clear();
    for (auto& l : key_label) {
      l.text[0] = 0;
      l.color = 0;
    }
    for (int i = 0; i < 16; ++i) {
      key_octshift[i] = 0;
      key_transpose[i] = 0;
      key_velocity[i] = 127;
      key_channel[i] = 0;
      follow_key[i] = 1;
      midi_program[i] = -1;
      for (int j = 0; j < 128; ++j) midi_controller[i][j] = -1;
    }
  }
};

static std::vector<setting_t> settings(MAX_SETTING_GROUP);
static uint32_t current_setting = 0;
static uint32_t setting_count = 1;
static setting_t clipboard_setting;

struct global_setting_t {
  int instrument_type = INSTRUMENT_TYPE_MDA_PIANO;
  std::string instrument_path;
  std::string output_device;
  int output_type = OUTPUT_TYPE_AUTO;
  int output_delay = 10;
  int output_volume = 100;
  int sample_rate = 48000;
  int buffer_size = 256;
  std::string keymap;
  std::string midi_output_port;
  std::map<std::string, midi_input_config_t> midi_inputs;

  bool enable_hotkey = true;
  bool enable_resize = true;
  bool midi_transpose = false;
  int key_fade = 0;
  uint8_t auto_color = 0;
  uint8_t preview_color = 0;
  uint8_t note_display = 0;
  int language = -1;
  std::string last_error;
};

static global_setting_t g;

static uint32_t map_version = 0;
static const uint32_t map_current_version = 0x01080000;  // FreePiano 1.8

// ---------------------------------------------------------------------------
// paths
// ---------------------------------------------------------------------------
static std::string exe_dir() {
  std::error_code ec;
  fs::path p = fs::read_symlink("/proc/self/exe", ec);
  if (ec) return {};
  return p.parent_path().string();
}

static bool has_data(const fs::path& p) {
  std::error_code ec;
  return fs::exists(p / "freepiano.map", ec) || fs::exists(p / "default_setting.txt", ec);
}

static std::string& data_dir() {
  static std::string dir = [] {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("FREEPIANO_DATA_DIR")) candidates.push_back(env);
    std::string exe = exe_dir();
    if (!exe.empty()) {
      fs::path base(exe);
      candidates.push_back((base / "assets").string());
      candidates.push_back((base / ".." / "share" / "freepiano").string());
      candidates.push_back(base.string());
    }
#ifdef FP_SOURCE_DATA_DIR
    candidates.push_back(FP_SOURCE_DATA_DIR);
#endif
#ifdef FP_INSTALL_DATA_DIR
    candidates.push_back(FP_INSTALL_DATA_DIR);
#endif
    candidates.push_back("/usr/share/freepiano");
    candidates.push_back("/usr/local/share/freepiano");

    for (const auto& c : candidates) {
      if (has_data(c)) return fs::weakly_canonical(c).string();
    }
    return candidates.empty() ? std::string(".") : candidates.front();
  }();
  return dir;
}

static bool dir_writable(const fs::path& path) {
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec) return false;
  fs::path probe = path / ".freepiano-write-test";
  {
    std::ofstream test(probe, std::ios::binary | std::ios::trunc);
    if (!test) return false;
  }
  fs::remove(probe, ec);
  return true;
}

static std::string& config_dir() {
  static std::string dir = [] {
    if (const char* env = std::getenv("FREEPIANO_CONFIG_DIR")) return std::string(env);

    std::vector<fs::path> candidates;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
      candidates.push_back(fs::path(xdg) / "freepiano");
    }
    if (const char* home = std::getenv("HOME")) {
      candidates.push_back(fs::path(home) / ".config" / "freepiano");
    }
    // Read-only home directories (immutable systems, sandboxes, live images):
    // keep working from a writable location next to the data or the executable.
    std::string exe = exe_dir();
    if (!exe.empty()) candidates.push_back(fs::path(exe) / ".config");
    candidates.push_back(fs::path(data_dir()) / ".config");
    std::error_code ec;
    candidates.push_back(fs::temp_directory_path(ec) / "freepiano");

    for (const auto& candidate : candidates) {
      if (dir_writable(candidate)) return candidate.string();
    }
    return candidates.empty() ? std::string(".") : candidates.front().string();
  }();
  return dir;
}

std::string config_user_map_dir() {
  static std::string dir = [] {
    std::string base;
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) base = xdg;
    else if (const char* home = std::getenv("HOME")) base = std::string(home) + "/.local/share";
    else base = ".";
    std::string path = base + "/freepiano/maps";
    std::error_code ec;
    fs::create_directories(path, ec);
    return path;
  }();
  return dir;
}

const std::string& config_data_dir() { return data_dir(); }
const std::string& config_config_dir() { return config_dir(); }

std::string config_media_path(const std::string& path) {
  if (path.empty()) return data_dir();
  fs::path p(path);
  if (p.is_absolute()) return p.string();
  std::error_code ec;
  fs::path a = fs::path(data_dir()) / p;
  if (fs::exists(a, ec)) return a.string();
  fs::path b = fs::path(config_user_map_dir()) / p;
  if (fs::exists(b, ec)) return b.string();
  fs::path c = fs::current_path(ec) / p;
  if (!ec && fs::exists(c, ec)) return c.string();
  return a.string();
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static bool is_valid_utf8(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i];
    int n = 0;
    if (c < 0x80) { ++i; continue; }
    else if ((c & 0xE0) == 0xC0) n = 1;
    else if ((c & 0xF0) == 0xE0) n = 2;
    else if ((c & 0xF8) == 0xF0) n = 3;
    else return false;
    if (i + n >= s.size()) return false;
    for (int k = 1; k <= n; ++k) {
      if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
    }
    i += n + 1;
  }
  return true;
}

// FreePiano key maps have historically been saved as GBK on Chinese Windows.
static std::string decode_text(const std::string& text) {
  if (text.empty() || is_valid_utf8(text)) return text;
  iconv_t cd = iconv_open("UTF-8", "GB18030");
  if (cd == (iconv_t)-1) return text;
  std::string out;
  out.resize(text.size() * 3 + 16);
  char* inbuf = const_cast<char*>(text.data());
  size_t inleft = text.size();
  char* outbuf = out.data();
  size_t outleft = out.size();
  iconv(cd, &inbuf, &inleft, &outbuf, &outleft);
  out.resize(out.size() - outleft);
  iconv_close(cd);
  return out;
}

static std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
  return s.substr(b, e - b);
}

static std::string base_name(const std::string& path) {
  fs::path p(path);
  return p.filename().string();
}

// ---------------------------------------------------------------------------
// tokenizer
// ---------------------------------------------------------------------------
namespace {

class Parser {
 public:
  explicit Parser(const char* s) : s_(s) {}

  bool at_end() const { return *s_ == 0 || *s_ == '\r' || *s_ == '\n'; }
  const char* rest() const { return s_; }

  bool space() {
    const char* p = s_;
    while (*p == ' ' || *p == '\t') ++p;
    if (p == s_) return false;
    s_ = p;
    return true;
  }

  bool end() {
    const char* p = s_;
    while (*p == ' ' || *p == '\t') ++p;
    bool ok = (*p == 0 || *p == '\r' || *p == '\n');
    while (*p == '\r' || *p == '\n') ++p;
    if (ok) s_ = p;
    return ok;
  }

  bool word(const char* w) {
    const char* p = s_;
    while (*w) {
      if (std::tolower((unsigned char)*p) != std::tolower((unsigned char)*w)) return false;
      ++p; ++w;
    }
    const char* save = s_;
    s_ = p;
    if (space() || end()) return true;
    s_ = save;
    return false;
  }

  bool hex(uint32_t* v) {
    const char* p = s_;
    uint32_t value = 0;
    bool any = false;
    for (;;) {
      char c = *p;
      int d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else break;
      value = value * 16 + (uint32_t)d;
      ++p;
      any = true;
    }
    if (!any) return false;
    const char* save = s_;
    s_ = p;
    if (space() || end()) {
      *v = value;
      return true;
    }
    s_ = save;
    return false;
  }

  bool number(int* v) {
    const char* p = s_;
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    else if (*p == '+') { ++p; }
    if (*p < '0' || *p > '9') return false;
    long long value = 0;
    while (*p >= '0' && *p <= '9') {
      value = value * 10 + (*p - '0');
      if (value > 0x7fffffff) return false;
      ++p;
    }
    const char* save = s_;
    s_ = p;
    if (space() || end()) {
      *v = (int)(neg ? -value : value);
      return true;
    }
    s_ = save;
    return false;
  }

  bool number(uint32_t* v) {
    if (*s_ == '$') {
      Parser tmp(s_ + 1);
      uint32_t vv = 0;
      if (!tmp.hex(&vv)) return false;
      s_ = tmp.rest();
      *v = vv;
      return true;
    }
    int iv = 0;
    const char* save = s_;
    if (!number(&iv)) {
      s_ = save;
      return false;
    }
    *v = (uint32_t)iv;
    return true;
  }

  bool name_value(const name_t* names, size_t count, uint32_t* value) {
    for (size_t i = 0; i < count; ++i) {
      if (word(names[i].name)) {
        *value = names[i].value;
        return true;
      }
    }
    return false;
  }

  bool value(const name_t* names, size_t count, uint32_t* value) {
    if (names && name_value(names, count, value)) return true;
    return number(value);
  }

  // Either "<op> <value>" or "<value> [<op>]" (default Set).
  bool change_value(const name_t* names, size_t count, uint32_t* op, int32_t* value) {
    const char* save = s_;
    uint32_t action = 0;
    if (name_value(value_action_names, ARRAY_COUNT(value_action_names), &action)) {
      int32_t v = 0;
      if (value_as_int(names, count, &v)) {
        *op = action;
        *value = v;
        return true;
      }
    }
    s_ = save;
    int32_t v = 0;
    if (value_as_int(names, count, &v)) {
      uint32_t a = SM_VALUE_SET;
      name_value(value_action_names, ARRAY_COUNT(value_action_names), &a);
      *op = a;
      *value = v;
      return true;
    }
    s_ = save;
    return false;
  }

  bool value_as_int(const name_t* names, size_t count, int32_t* value) {
    uint32_t n = 0;
    if (names && name_value(names, count, &n)) {
      *value = (int32_t)n;
      return true;
    }
    int iv = 0;
    if (number(&iv)) {
      *value = iv;
      return true;
    }
    return false;
  }

 private:
  const char* s_;
};

int note_name_to_value(const char* word, int* value) {
  // C-1 .. G10, sharps only (as in the original note name table).
  if (!word || !*word) return 0;
  char letter = (char)std::toupper((unsigned char)word[0]);
  int base;
  switch (letter) {
    case 'C': base = 0; break;
    case 'D': base = 2; break;
    case 'E': base = 4; break;
    case 'F': base = 5; break;
    case 'G': base = 7; break;
    case 'A': base = 9; break;
    case 'B': base = 11; break;
    default: return 0;
  }
  const char* p = word + 1;
  if (*p == '#') { base += 1; ++p; }
  if (*p < '0' || *p > '9') return 0;
  int octave = 0;
  while (*p >= '0' && *p <= '9') {
    octave = octave * 10 + (*p - '0');
    ++p;
  }
  if (*p != 0) return 0;
  *value = 12 * (octave + 1) + base;
  return 1;
}

bool parse_note_token(Parser& p, uint32_t* value) {
  const char* s = p.rest();
  char token[16];
  size_t n = 0;
  while (s[n] && s[n] != ' ' && s[n] != '\t' && s[n] != '\r' && s[n] != '\n' &&
         n < sizeof(token) - 1) {
    token[n] = s[n];
    ++n;
  }
  token[n] = 0;
  if (n == 0) return false;
  int v = 0;
  if (!note_name_to_value(token, &v)) return false;
  if (!p.word(token)) return false;
  *value = (uint32_t)(v < 0 ? 0 : v);
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// public accessors
// ---------------------------------------------------------------------------
const char* config_get_last_error() { return g.last_error.c_str(); }
void config_set_last_error(const char* text) { g.last_error = text ? text : ""; }

int config_get_output_volume() {
  fp_lock lock;
  return g.output_volume;
}
void config_set_output_volume(int volume) {
  fp_lock lock;
  g.output_volume = clamp_value(volume, 0, 200);
}

int config_get_output_type() { fp_lock lock; return g.output_type; }
void config_set_output_type(int type) { fp_lock lock; g.output_type = type; }
const std::string& config_get_output_device() { return g.output_device; }
void config_set_output_device(const std::string& device) { fp_lock lock; g.output_device = device; }
int config_get_sample_rate() { fp_lock lock; return g.sample_rate; }
void config_set_sample_rate(int rate) { fp_lock lock; g.sample_rate = clamp_value(rate, 8000, 192000); }
int config_get_buffer_size() { fp_lock lock; return g.buffer_size; }
void config_set_buffer_size(int frames) { fp_lock lock; g.buffer_size = clamp_value(frames, 32, 4096); }
int config_get_output_delay() { fp_lock lock; return g.output_delay; }
void config_set_output_delay(int value) { fp_lock lock; g.output_delay = clamp_value(value, 0, 1000); }

int config_select_instrument(int type, const std::string& name) {
  fp_lock lock;
  if (type < INSTRUMENT_TYPE_NONE || type > INSTRUMENT_TYPE_MDA_PIANO) type = INSTRUMENT_TYPE_MDA_PIANO;
  g.instrument_type = type;
  g.instrument_path = name;
  return 0;
}
int config_get_instrument_type() { fp_lock lock; return g.instrument_type; }
const std::string& config_get_instrument_path() { return g.instrument_path; }
void config_set_soundfont(const std::string& path) {
  fp_lock lock;
  g.instrument_path = path;
  if (!path.empty()) g.instrument_type = INSTRUMENT_TYPE_SOUNDFONT;
}

int config_select_output(int type, const std::string& device) {
  fp_lock lock;
  g.output_type = type;
  g.output_device = device;
  return 0;
}

bool config_get_enable_resize_window() { fp_lock lock; return g.enable_resize; }
void config_set_enable_resize_window(bool enable) { fp_lock lock; g.enable_resize = enable; }
bool config_get_enable_hotkey() { fp_lock lock; return g.enable_hotkey; }
void config_set_enable_hotkey(bool enable) { fp_lock lock; g.enable_hotkey = enable; }
bool config_get_midi_transpose() { fp_lock lock; return g.midi_transpose; }
void config_set_midi_transpose(bool value) { fp_lock lock; g.midi_transpose = value; }
int config_get_key_fade() { fp_lock lock; return g.key_fade; }
void config_set_key_fade(int value) { fp_lock lock; g.key_fade = clamp_value<int>(value, 0, 100); }
uint8_t config_get_auto_color() { fp_lock lock; return g.auto_color; }
void config_set_auto_color(uint8_t value) { fp_lock lock; g.auto_color = value; }
uint8_t config_get_preview_color() { fp_lock lock; return g.preview_color; }
void config_set_preview_color(uint8_t value) { fp_lock lock; g.preview_color = clamp_value<int>(value, 0, 100); }
uint8_t config_get_note_display() { fp_lock lock; return g.note_display; }
void config_set_note_display(uint8_t value) { fp_lock lock; g.note_display = value > 2 ? 0 : value; }

int config_get_language() {
  fp_lock lock;
  if (g.language >= 0) return g.language;
  const char* lang = std::getenv("LANG");
  bool zh = lang && (std::strncmp(lang, "zh", 2) == 0);
  return zh ? FP_LANG_SCHINESE : FP_LANG_ENGLISH;
}
void config_set_language(int lang) { fp_lock lock; g.language = lang; }

void config_set_midi_input_config(const std::string& device, const midi_input_config_t& cfg) {
  fp_lock lock;
  g.midi_inputs[device] = cfg;
}
bool config_get_midi_input_config(const std::string& device, midi_input_config_t* cfg) {
  fp_lock lock;
  auto it = g.midi_inputs.find(device);
  if (it == g.midi_inputs.end()) return false;
  if (cfg) *cfg = it->second;
  return true;
}
const std::map<std::string, midi_input_config_t>& config_get_midi_inputs() { return g.midi_inputs; }

const std::string& config_get_midi_output_port() { return g.midi_output_port; }
void config_set_midi_output_port(const std::string& port) { fp_lock lock; g.midi_output_port = port; }

const std::string& config_get_keymap() { return g.keymap; }

// ---------------------------------------------------------------------------
// bindings
// ---------------------------------------------------------------------------
int config_bind_get_keydown(uint8_t code, key_bind_t* buff, int size) {
  fp_lock lock;
  int n = 0;
  auto range = settings[current_setting].keydown_map.equal_range(code);
  for (auto it = range.first; it != range.second && n < size; ++it) buff[n++] = it->second;
  return n;
}
int config_bind_get_keyup(uint8_t code, key_bind_t* buff, int size) {
  fp_lock lock;
  int n = 0;
  auto range = settings[current_setting].keyup_map.equal_range(code);
  for (auto it = range.first; it != range.second && n < size; ++it) buff[n++] = it->second;
  return n;
}
void config_bind_clear_keydown(uint8_t code) {
  fp_lock lock;
  settings[current_setting].keydown_map.erase(code);
}
void config_bind_clear_keyup(uint8_t code) {
  fp_lock lock;
  settings[current_setting].keyup_map.erase(code);
}
void config_bind_add_keydown(uint8_t code, key_bind_t bind) {
  fp_lock lock;
  settings[current_setting].keydown_map.insert({code, bind});
}
void config_bind_add_keyup(uint8_t code, key_bind_t bind) {
  fp_lock lock;
  settings[current_setting].keyup_map.insert({code, bind});
}
void config_bind_set_label(uint8_t code, const char* label) {
  fp_lock lock;
  std::snprintf(settings[current_setting].key_label[code].text,
                sizeof(settings[current_setting].key_label[code].text), "%s", label ? label : "");
}
const char* config_bind_get_label(uint8_t code) {
  return settings[current_setting].key_label[code].text;
}
void config_bind_set_color(uint8_t code, uint32_t color) {
  fp_lock lock;
  settings[current_setting].key_label[code].color = color;
}
uint32_t config_bind_get_color(uint8_t code) {
  return settings[current_setting].key_label[code].color;
}

// ---------------------------------------------------------------------------
// per-group properties
// ---------------------------------------------------------------------------
void config_set_key_signature(char key) { fp_lock lock; settings[current_setting].key_signature = key; }
char config_get_key_signature() { return settings[current_setting].key_signature; }

void config_set_key_transpose(uint8_t channel, char transpose) {
  fp_lock lock;
  if (channel < 16) settings[current_setting].key_transpose[channel] = transpose;
}
char config_get_key_transpose(uint8_t channel) {
  return channel < 16 ? settings[current_setting].key_transpose[channel] : 0;
}
void config_set_key_octshift(uint8_t channel, char shift) {
  fp_lock lock;
  if (channel < 16) settings[current_setting].key_octshift[channel] = shift;
}
char config_get_key_octshift(uint8_t channel) {
  return channel < 16 ? settings[current_setting].key_octshift[channel] : 0;
}
void config_set_key_velocity(uint8_t channel, uint8_t velocity) {
  fp_lock lock;
  if (channel < 16) settings[current_setting].key_velocity[channel] = velocity;
}
uint8_t config_get_key_velocity(uint8_t channel) {
  return channel < 16 ? settings[current_setting].key_velocity[channel] : 0;
}
void config_set_follow_key(uint8_t channel, uint8_t value) {
  fp_lock lock;
  if (channel < 16) settings[current_setting].follow_key[channel] = value;
}
uint8_t config_get_follow_key(uint8_t channel) {
  return channel < 16 ? settings[current_setting].follow_key[channel] : 0;
}
int config_get_output_channel(uint8_t channel) {
  if (channel <= SM_INPUT_MAX) return settings[current_setting].key_channel[channel] & 0x0f;
  return channel & 0x0f;
}
void config_set_output_channel(uint8_t channel, uint8_t value) {
  fp_lock lock;
  if (channel < 16) settings[current_setting].key_channel[channel] = value & 0x0f;
}
int config_get_program(uint8_t channel) {
  int ch = config_get_output_channel(channel);
  return settings[current_setting].midi_program[ch];
}
void config_set_program(uint8_t channel, uint8_t value) {
  fp_lock lock;
  int ch = config_get_output_channel(channel);
  if (ch < 16) settings[current_setting].midi_program[ch] = value;
}
int config_get_controller(uint8_t channel, uint8_t id) {
  int ch = config_get_output_channel(channel);
  if (ch >= 16) return -1;
  return settings[current_setting].midi_controller[ch][id];
}
void config_set_controller(uint8_t channel, uint8_t id, uint8_t value) {
  fp_lock lock;
  int ch = config_get_output_channel(channel);
  if (ch < 16) settings[current_setting].midi_controller[ch][id] = value;
}

uint32_t config_get_setting_group() { fp_lock lock; return current_setting; }

void config_set_setting_group(uint32_t id) {
  fp_lock lock;
  if (id < setting_count) current_setting = id;

  // update controller/program state so the next group sounds right immediately
  for (int ch = 0; ch < 16; ++ch) {
    for (int i = 0; i < 128; ++i) {
      int v = config_get_controller((uint8_t)(SM_OUTPUT_0 + ch), (uint8_t)i);
      if (v >= 0 && v < 128) song_output_event((uint8_t)(SM_MIDI_CONTROLLER | ch), (uint8_t)i, (uint8_t)v, 0);
    }
    int prog = config_get_program((uint8_t)(SM_OUTPUT_0 + ch));
    if (prog >= 0 && prog < 128) song_output_event((uint8_t)(SM_MIDI_PROGRAM | ch), (uint8_t)prog, 0, 0);
  }
}

uint32_t config_get_setting_group_count() { fp_lock lock; return setting_count; }

void config_set_setting_group_count(uint32_t count) {
  fp_lock lock;
  if (count < 1) count = 1;
  if (count > settings.size()) count = (uint32_t)settings.size();
  uint32_t target = current_setting < count ? current_setting : count - 1;
  for (uint32_t i = setting_count; i < count; ++i) settings[i].clear();
  current_setting = target;
  setting_count = count;
}

void config_insert_setting_group(uint32_t pos) {
  fp_lock lock;
  if (pos > setting_count) pos = setting_count;
  config_set_setting_group_count(setting_count + 1);
  for (uint32_t i = setting_count - 1; i > pos; --i) settings[i] = settings[i - 1];
  current_setting = pos;
  settings[pos].clear();
}

int config_add_setting_group_copy(uint32_t source) {
  fp_lock lock;
  if (source >= setting_count || setting_count >= settings.size()) return -1;
  uint32_t id = setting_count;
  settings[id] = settings[source];
  setting_count = id + 1;
  current_setting = id;
  return (int)id;
}

void config_delete_setting_group(uint32_t id) {
  fp_lock lock;
  if (id >= setting_count) return;
  for (uint32_t i = id + 1; i < setting_count; ++i) settings[i - 1] = settings[i];
  config_set_setting_group_count(setting_count - 1);
  config_set_setting_group(current_setting);
}

void config_clear_key_setting() {
  fp_lock lock;
  settings[current_setting].clear();
}

void config_copy_key_setting() {
  fp_lock lock;
  clipboard_setting = settings[current_setting];
}
void config_paste_key_setting() {
  fp_lock lock;
  settings[current_setting] = clipboard_setting;
}

// ---------------------------------------------------------------------------
// names / labels
// ---------------------------------------------------------------------------
const char* config_get_key_name(uint8_t code) { return key_code_name(code); }

const char* config_get_note_name(uint8_t note) {
  static thread_local char buff[16];
  if (note > 127) return "";
  static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  std::snprintf(buff, sizeof(buff), "%s%d", names[note % 12], (int)(note / 12) - 1);
  return buff;
}

const char* config_get_channel_name(uint8_t ch) {
  for (const auto& n : channel_names) {
    if ((uint8_t)n.value == ch) return n.name;
  }
  static char buff[16];
  std::snprintf(buff, sizeof(buff), "%u", (unsigned)ch);
  return buff;
}

static std::string op_suffix(uint32_t action, int32_t change) {
  char buff[32];
  switch (action & 0x0f) {
    case SM_VALUE_SET:
    case SM_VALUE_SET10:
    case SM_VALUE_SET1:
      std::snprintf(buff, sizeof(buff), "%d", change);
      break;
    case SM_VALUE_INC:
      std::snprintf(buff, sizeof(buff), "+%d", change);
      break;
    case SM_VALUE_DEC:
      std::snprintf(buff, sizeof(buff), "-%d", change);
      break;
    case SM_VALUE_FLIP:
      std::snprintf(buff, sizeof(buff), "~");
      break;
    case SM_VALUE_PRESS:
      std::snprintf(buff, sizeof(buff), "*%d", change);
      break;
    default:
      buff[0] = 0;
      break;
  }
  return buff;
}

std::string config_default_keylabel(key_bind_t bind) {
  if (bind.a >= SM_MIDI_MESSAGE_START) {
    if ((bind.a & 0xf0) == SM_MIDI_NOTEON || (bind.a & 0xf0) == SM_MIDI_NOTEOFF) {
      return bind.b < 128 ? config_get_note_name(bind.b) : "";
    }
    return "";
  }

  switch (bind.a) {
    case SM_NOTE_ON:
    case SM_NOTE_OFF:
      return config_get_note_name(bind.c);
    case SM_NOTE_PRESSURE: return "Touch";
    case SM_PRESSURE: return "Press";
    case SM_PITCH: return "Bend";
    case SM_PROGRAM: return std::string("Prog") + op_suffix(bind.c, (int8_t)bind.d);
    case SM_BANK_MSB:
    case SM_BANK_LSB: return std::string("Bank") + op_suffix(bind.c, (int8_t)bind.d);
    case SM_SUSTAIN: return std::string("Sust") + op_suffix(bind.c, (int8_t)bind.d);
    case SM_MODULATION: return std::string("Mod") + op_suffix(bind.c, (int8_t)bind.d);
    case SM_KEY_SIGNATURE: return std::string("Key") + op_suffix(bind.b, (int8_t)bind.c);
    case SM_TRANSPOSE: return std::string("Trsp") + op_suffix(bind.c, (int8_t)bind.d);
    case SM_FOLLOW_KEY: return "Follow";
    case SM_OCTAVE: return std::string("Oct") + op_suffix(bind.c, (int8_t)bind.d);
    case SM_VELOCITY: return std::string("Vel") + op_suffix(bind.c, (int8_t)bind.d);
    case SM_CHANNEL: return std::string("Chan") + op_suffix(bind.c, (int8_t)bind.d);
    case SM_VOLUME: return std::string("Vol") + op_suffix(bind.b, (int8_t)bind.c);
    case SM_PLAY: return "Play";
    case SM_RECORD: return "Rec";
    case SM_STOP: return "Stop";
    case SM_SETTING_GROUP: return std::string("Grp") + op_suffix(bind.b, (int8_t)bind.c);
    default: return "";
  }
}

int config_default_keylabel(char* buff, size_t size, key_bind_t bind) {
  std::string s = config_default_keylabel(bind);
  std::snprintf(buff, size, "%s", s.c_str());
  return (int)std::strlen(buff);
}

// ---------------------------------------------------------------------------
// key map parsing
// ---------------------------------------------------------------------------
static bool parse_event(Parser& p, key_bind_t* e) {
  uint32_t action = 0;
  if (!p.value(action_names, ARRAY_COUNT(action_names), &action)) return false;
  if (action == 0) return false;

  uint32_t arg1 = 0, arg2 = 0, arg3 = 0;

  switch (action) {
    case SM_SETTING_GROUP_COUNT:
      if (!p.value(nullptr, 0, &arg1)) return false;
      break;

    case SM_KEY_SIGNATURE:
    case SM_VOLUME:
    case SM_SETTING_GROUP: {
      uint32_t op = SM_VALUE_SET;
      int32_t v = 0;
      if (!p.change_value(nullptr, 0, &op, &v)) return false;
      arg1 = op;
      arg2 = (uint32_t)v;
      break;
    }

    case SM_CONTROLLER_DEPRECATED: {
      if (map_version >= 0x01080000) return false;
      uint32_t ch = 0, id = 0, op = 0;
      int32_t v = 0;
      if (!p.value(channel_names, ARRAY_COUNT(channel_names), &ch)) return false;
      if (!p.value(controller_names, ARRAY_COUNT(controller_names), &id)) return false;
      if (!p.change_value(nullptr, 0, &op, &v)) return false;
      if (id == 64) {
        if (op == SM_VALUE_FLIP) v = 127;
        action = SM_SUSTAIN;
        arg1 = ch;
        arg2 = op;
        arg3 = (uint32_t)v;
      } else if (op == SM_VALUE_SET) {
        action = SM_MIDI_CONTROLLER | (ch & 0x0f);
        arg1 = id;
        arg2 = (uint32_t)v;
        arg3 = 0;
      } else {
        return false;
      }
      break;
    }

    case SM_PROGRAM:
    case SM_TRANSPOSE:
    case SM_FOLLOW_KEY:
    case SM_OCTAVE:
    case SM_VELOCITY:
    case SM_BANK_MSB:
    case SM_BANK_LSB:
    case SM_SUSTAIN:
    case SM_PRESSURE:
    case SM_PITCH:
    case SM_MODULATION: {
      uint32_t ch = 0, op = 0;
      int32_t v = 0;
      if (!p.value(channel_names, ARRAY_COUNT(channel_names), &ch)) return false;
      if (!p.change_value(nullptr, 0, &op, &v)) return false;
      arg1 = ch;
      arg2 = op;
      arg3 = (uint32_t)v;
      break;
    }

    case SM_CHANNEL: {
      uint32_t ch = 0, op = 0;
      int32_t v = 0;
      if (!p.value(channel_names, ARRAY_COUNT(channel_names), &ch)) return false;
      if (!p.change_value(channel_names, ARRAY_COUNT(channel_names), &op, &v)) return false;
      arg1 = ch;
      arg2 = op;
      arg3 = (uint32_t)v;
      break;
    }

    case SM_NOTE_ON:
    case SM_NOTE_OFF:
    case SM_NOTE_PRESSURE: {
      uint32_t ch = 0;
      if (!p.value(channel_names, ARRAY_COUNT(channel_names), &ch)) return false;
      uint32_t note = 0;
      if (!parse_note_token(p, &note)) return false;
      if (map_version < 0x01070000) {
        note = (uint32_t)((int)note - 12);
      }
      int vel = 127;
      p.number(&vel);
      arg1 = ch;
      arg2 = note & 0xff;
      arg3 = (uint32_t)clamp_value(vel, 0, 127);
      break;
    }

    default: {
      if (*p.rest() == '$') {
        uint32_t msg = 0;
        if (!p.number(&msg)) return false;
        action = (msg >> 24) & 0xff;
        arg1 = (msg >> 16) & 0xff;
        arg2 = (msg >> 8) & 0xff;
        arg3 = msg & 0xff;
      } else {
        if (!p.hex(&action)) return false;
        p.hex(&arg1);
        p.hex(&arg2);
        p.hex(&arg3);
      }
      break;
    }
  }

  e->a = (uint8_t)action;
  e->b = (uint8_t)arg1;
  e->c = (uint8_t)arg2;
  e->d = (uint8_t)arg3;
  return true;
}

static bool parse_version(Parser& p, uint32_t* value) {
  const char* s = p.rest();
  int digit = 3;
  uint32_t version = 0;
  for (;;) {
    if (*s < '0' || *s > '9') return false;
    int n = 0;
    while (*s >= '0' && *s <= '9') {
      n = n * 10 + (*s - '0');
      if (n > 255) return false;
      ++s;
    }
    version |= (uint32_t)n << (digit * 8);
    if (*s == '.') {
      if (--digit < 0) return false;
      ++s;
    } else {
      if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' && *s != 0) return false;
      while (*s == ' ' || *s == '\t') ++s;
      while (*s == '\r' || *s == '\n') ++s;
      p = Parser(s);
      if (value) *value = version;
      return true;
    }
  }
}

static bool parse_key_token(Parser& p, uint8_t* code) {
  const char* s = p.rest();
  char token[32];
  size_t n = 0;
  while (s[n] && s[n] != ' ' && s[n] != '\t' && s[n] != '\r' && s[n] != '\n' &&
         n < sizeof(token) - 1) {
    token[n] = s[n];
    ++n;
  }
  token[n] = 0;
  if (n == 0) return false;

  uint8_t known = key_code_from_name(token);
  if (known) {
    if (!p.word(token)) return false;
    *code = known;
    return true;
  }
  uint32_t numeric = 0;
  if (!p.number(&numeric)) return false;
  *code = (uint8_t)(numeric & 0xff);
  return true;
}

static int parse_keymap_line(const std::string& line, uint8_t override_key) {
  std::string s = trim(line);
  if (s.empty() || s[0] == '#') return 0;

  Parser p(s.c_str());
  uint32_t bind_type = 0;
  if (p.name_value(bind_names, ARRAY_COUNT(bind_names), &bind_type)) {
    uint8_t key = 0;
    if (!parse_key_token(p, &key)) return -1;
    uint8_t code = override_key ? override_key : key;

    if (bind_type == 1 || bind_type == 2) {
      key_bind_t bind;
      if (!parse_event(p, &bind)) return -1;
      if (bind_type == 1) config_bind_add_keydown(code, bind);
      else config_bind_add_keyup(code, bind);
      return 0;
    }
    if (bind_type == 3) {
      std::string label = trim(p.rest());
      config_bind_set_label(code, label.c_str());
      return 0;
    }
    if (bind_type == 4) {
      int r = 0, gg = 0, b = 0, a = 255;
      p.number(&r); p.number(&gg); p.number(&b);
      if (p.number(&a)) {}
      config_bind_set_color(code, (uint32_t)((a << 24) | (r << 16) | (gg << 8) | b));
      return 0;
    }
    return -1;
  }

  key_bind_t bind;
  if (parse_event(p, &bind)) {
    song_output_event(bind.a, bind.b, bind.c, bind.d);
    if (bind.a == SM_SETTING_GROUP_COUNT) {
      for (int i = (int)config_get_setting_group_count() - 1; i >= 0; --i) {
        config_set_setting_group((uint32_t)i);
        config_clear_key_setting();
      }
    }
    return 0;
  }

  if (p.word("FreePiano")) {
    uint32_t version = 0;
    if (parse_version(p, &version)) {
      map_version = version;
      return 0;
    }
  }
  return -1;
}

int config_parse_keymap(const std::string& text, uint8_t override_key, int version) {
  fp_lock lock;
  map_version = (version < 0) ? map_current_version : (uint32_t)version;

  std::istringstream stream(decode_text(text));
  std::string line;
  int result = 0;
  while (std::getline(stream, line)) {
    int r = parse_keymap_line(line, override_key);
    if (r != 0) result = r;
  }
  return result;
}

int config_apply_keymap_text(const std::string& text) {
  fp_lock lock;
  map_version = 0;
  config_set_setting_group_count(0);
  config_clear_key_setting();

  std::istringstream stream(decode_text(text));
  std::string line;
  int errors = 0;
  while (std::getline(stream, line)) {
    if (parse_keymap_line(line, 0) != 0) ++errors;
  }
  config_set_setting_group(0);
  return errors;
}

int config_load_keymap(const std::string& filename) {
  fp_lock lock;
  std::string path = config_media_path(filename);
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    config_set_last_error(("Cannot open key map: " + path).c_str());
    return -1;
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  std::string text = decode_text(ss.str());

  map_version = 0;
  config_set_setting_group_count(0);
  config_clear_key_setting();

  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) parse_keymap_line(line, 0);

  config_set_setting_group(0);
  config_set_last_error("");
  return 0;
}

int config_set_keymap(const std::string& mapname) {
  fp_lock lock;
  if (mapname.empty()) {
    g.keymap.clear();
    config_default_key_setting();
    return 0;
  }
  std::string path = config_media_path(mapname);
  std::ifstream test(path);
  if (!test) {
    config_set_last_error(("Key map not found: " + mapname).c_str());
    return -1;
  }

  // Store only the file name when it lives in one of the standard directories,
  // otherwise keep the absolute path so it still resolves on the next launch.
  std::error_code ec;
  fs::path resolved = fs::weakly_canonical(fs::path(path), ec);
  if (ec) resolved = fs::path(path);
  fs::path data = fs::weakly_canonical(fs::path(data_dir()), ec);
  fs::path user = fs::weakly_canonical(fs::path(config_user_map_dir()), ec);
  std::string parent = resolved.parent_path().string();
  if (parent == data.string() || parent == user.string()) {
    g.keymap = resolved.filename().string();
  } else {
    g.keymap = resolved.string();
  }
  return config_load_keymap(path);
}

// ---------------------------------------------------------------------------
// key map output
// ---------------------------------------------------------------------------
namespace {
std::string print_action_name(uint32_t value) {
  for (const auto& n : action_names) {
    if (n.value == value) return n.name;
  }
  char buff[16];
  std::snprintf(buff, sizeof(buff), "%02x", value);
  return buff;
}
std::string print_op(uint32_t op) {
  for (const auto& n : value_action_names) {
    if (n.value == op) return n.name;
  }
  char buff[16];
  std::snprintf(buff, sizeof(buff), "%02x", op);
  return buff;
}
std::string print_channel(uint32_t value) {
  for (const auto& n : channel_names) {
    if (n.value == value) return n.name;
  }
  return std::to_string(value);
}
std::string print_note(uint32_t note) {
  static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  note &= 0xff;
  if (note > 127) return "C-1";
  char buff[16];
  std::snprintf(buff, sizeof(buff), "%s%d", names[note % 12], (int)(note / 12) - 1);
  return buff;
}

std::string print_event(const key_bind_t& e) {
  std::string out;
  if (e.a >= SM_MIDI_MESSAGE_START) {
    out += "MIDI\t";
    char buff[8];
    std::snprintf(buff, sizeof(buff), "%02x", e.a);
    out += buff;
    if (e.b) { std::snprintf(buff, sizeof(buff), " %02x", e.b); out += buff; }
    if (e.c) { std::snprintf(buff, sizeof(buff), " %02x", e.c); out += buff; }
    if (e.d) { std::snprintf(buff, sizeof(buff), " %02x", e.d); out += buff; }
    return out;
  }

  out += print_action_name(e.a);
  switch (e.a) {
    case SM_KEY_SIGNATURE:
    case SM_VOLUME:
    case SM_SETTING_GROUP:
      out += "\t" + print_op(e.b) + "\t" + std::to_string((int8_t)e.c);
      break;
    case SM_SETTING_GROUP_COUNT:
      out += "\t" + std::to_string(e.b);
      break;
    case SM_OCTAVE:
    case SM_VELOCITY:
    case SM_TRANSPOSE:
    case SM_FOLLOW_KEY:
    case SM_PROGRAM:
    case SM_BANK_MSB:
    case SM_BANK_LSB:
    case SM_SUSTAIN:
    case SM_MODULATION:
    case SM_PRESSURE:
    case SM_PITCH:
    case SM_CHANNEL:
      out += "\t" + print_channel(e.b) + "\t" + print_op(e.c) + "\t" + std::to_string((int8_t)e.d);
      break;
    case SM_NOTE_ON:
    case SM_NOTE_OFF:
    case SM_NOTE_PRESSURE:
      out += "\t" + print_channel(e.b) + "\t" + print_note(e.c);
      if (e.d != 127) out += "\t" + std::to_string(e.d);
      break;
    case SM_PLAY:
    case SM_RECORD:
    case SM_STOP:
      break;
    default:
      out += "\t" + std::to_string(e.b) + "\t" + std::to_string(e.c) + "\t" + std::to_string(e.d);
      break;
  }
  return out;
}

std::string print_key(uint8_t code) {
  std::string name = key_code_name(code);
  return name;
}
}  // namespace

std::string config_dump_keybind(uint8_t code, bool with_bind) {
  fp_lock lock;
  std::string out;
  key_bind_t temp[256];
  int n = config_bind_get_keydown(code, temp, 256);
  for (int i = 0; i < n; ++i) {
    if (!temp[i].a) continue;
    if (with_bind) out += "keydown";
    out += "\t" + print_key(code) + "\t" + print_event(temp[i]) + "\n";
  }
  n = config_bind_get_keyup(code, temp, 256);
  for (int i = 0; i < n; ++i) {
    if (!temp[i].a) continue;
    if (with_bind) out += "keyup";
    out += "\t" + print_key(code) + "\t" + print_event(temp[i]) + "\n";
  }
  return out;
}

static void append_key_settings(std::string& out) {
  uint32_t group = config_get_setting_group();
  out += print_event(key_bind_t(SM_KEY_SIGNATURE, SM_VALUE_SET, (uint8_t)config_get_key_signature(), 0)) + "\n";

  for (int ch = SM_INPUT_0; ch <= SM_INPUT_MAX; ++ch) {
    int v = config_get_key_octshift((uint8_t)ch);
    if (v) out += print_event(key_bind_t(SM_OCTAVE, (uint8_t)ch, SM_VALUE_SET, (uint8_t)v)) + "\n";
  }
  for (int ch = SM_INPUT_0; ch <= SM_INPUT_MAX; ++ch) {
    int v = config_get_key_transpose((uint8_t)ch);
    if (v) out += print_event(key_bind_t(SM_TRANSPOSE, (uint8_t)ch, SM_VALUE_SET, (uint8_t)v)) + "\n";
  }
  for (int ch = SM_INPUT_0; ch <= SM_INPUT_MAX; ++ch) {
    int v = config_get_follow_key((uint8_t)ch);
    if (!v) out += print_event(key_bind_t(SM_FOLLOW_KEY, (uint8_t)ch, SM_VALUE_SET, 0)) + "\n";
  }
  for (int ch = SM_INPUT_0; ch <= SM_INPUT_MAX; ++ch) {
    int v = config_get_key_velocity((uint8_t)ch);
    if (v != 127) out += print_event(key_bind_t(SM_VELOCITY, (uint8_t)ch, SM_VALUE_SET, (uint8_t)v)) + "\n";
  }
  for (int ch = SM_INPUT_0; ch <= SM_INPUT_MAX; ++ch) {
    int v = config_get_output_channel((uint8_t)ch);
    if (v) out += print_event(key_bind_t(SM_CHANNEL, (uint8_t)ch, SM_VALUE_SET, (uint8_t)v)) + "\n";
  }
  for (int ch = SM_OUTPUT_0; ch <= SM_OUTPUT_MAX; ++ch) {
    int v = config_get_program((uint8_t)ch);
    if (v >= 0 && v < 128) out += print_event(key_bind_t(SM_PROGRAM, (uint8_t)ch, SM_VALUE_SET, (uint8_t)v)) + "\n";
  }
  for (int ch = SM_OUTPUT_0; ch <= SM_OUTPUT_MAX; ++ch) {
    int v = config_get_controller((uint8_t)ch, 0);
    if (v >= 0 && v < 128) out += print_event(key_bind_t(SM_BANK_MSB, (uint8_t)ch, SM_VALUE_SET, (uint8_t)v)) + "\n";
  }
  for (int ch = SM_OUTPUT_0; ch <= SM_OUTPUT_MAX; ++ch) {
    int v = config_get_controller((uint8_t)ch, 32);
    if (v >= 0 && v < 128) out += print_event(key_bind_t(SM_BANK_LSB, (uint8_t)ch, SM_VALUE_SET, (uint8_t)v)) + "\n";
  }
  for (int ch = SM_OUTPUT_0; ch <= SM_OUTPUT_MAX; ++ch) {
    int v = config_get_controller((uint8_t)ch, 64);
    if (v >= 0 && v < 128) out += print_event(key_bind_t(SM_SUSTAIN, (uint8_t)ch, SM_VALUE_SET, (uint8_t)v)) + "\n";
  }
  for (int ch = SM_OUTPUT_0; ch <= SM_OUTPUT_MAX; ++ch) {
    int v = config_get_controller((uint8_t)ch, 1);
    if (v >= 0 && v < 128) out += print_event(key_bind_t(SM_MODULATION, (uint8_t)ch, SM_VALUE_SET, (uint8_t)v)) + "\n";
  }

  for (int key = 0; key < 256; ++key) {
    std::string bind = config_dump_keybind((uint8_t)key, true);
    out += bind;
    const char* label = config_bind_get_label((uint8_t)key);
    if (label && *label) out += "label\t" + print_key((uint8_t)key) + "\t" + label + "\n";
    uint32_t color = config_bind_get_color((uint8_t)key);
    if (color) {
      char buff[64];
      std::snprintf(buff, sizeof(buff), "color\t%s\t%d %d %d %d\n", print_key((uint8_t)key).c_str(),
                    (color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff, (color >> 24) & 0xff);
      out += buff;
    }
  }
  (void)group;
}

std::string config_save_keymap() {
  fp_lock lock;
  std::string out = "FreePiano 1.8\n\n";
  out += print_event(key_bind_t(SM_SETTING_GROUP_COUNT, (uint8_t)config_get_setting_group_count(), 0, 0)) + "\n";

  uint32_t restore = config_get_setting_group();
  for (uint32_t i = 0; i < config_get_setting_group_count(); ++i) {
    out += print_event(key_bind_t(SM_SETTING_GROUP, SM_VALUE_SET, (uint8_t)i, 0)) + "\n";
    config_set_setting_group(i);
    append_key_settings(out);
    out += "\n";
  }
  config_set_setting_group(restore);
  return out;
}

// ---------------------------------------------------------------------------
// settings file
// ---------------------------------------------------------------------------
static std::string settings_file_path(const std::string& filename) {
  fs::path p(filename);
  if (p.is_absolute()) return p.string();
  return (fs::path(config_dir()) / p).string();
}

int config_load(const std::string& filename) {
  {
  fp_lock lock;
  config_set_setting_group_count(1);
  config_clear_key_setting();
  g.keymap.clear();
  g.instrument_type = INSTRUMENT_TYPE_MDA_PIANO;
  g.instrument_path.clear();
  g.output_volume = 100;
  g.midi_inputs.clear();
  g.midi_output_port.clear();

  std::string path = settings_file_path(filename);
  std::ifstream file(path);
  if (!file) {
    // first run: keep defaults, then load the default key map outside the lock
  } else {

  std::string line;
  bool has_config_version = false;
  while (std::getline(file, line)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    Parser p(s.c_str());

    if (p.word("config-version")) {
      int version = 0;
      if (p.number(&version)) has_config_version = true;
    } else if (p.word("instrument")) {
      if (p.word("type")) {
        uint32_t v = 0;
        p.value(instrument_type_names, ARRAY_COUNT(instrument_type_names), &v);
        g.instrument_type = (int)v;
      } else if (p.word("path")) {
        g.instrument_path = trim(p.rest());
      }
    } else if (p.word("output")) {
      if (p.word("type")) {
        uint32_t v = 0;
        p.value(output_type_names, ARRAY_COUNT(output_type_names), &v);
        g.output_type = (int)v;
      } else if (p.word("device")) {
        g.output_device = trim(p.rest());
      } else if (p.word("delay")) {
        int v = 0; if (p.number(&v)) g.output_delay = v;
      } else if (p.word("volume")) {
        int v = 0; if (p.number(&v)) g.output_volume = clamp_value(v, 0, 200);
      }
    } else if (p.word("keyboard")) {
      if (p.word("map")) {
        const char* r = p.rest();
        g.keymap = trim(r);
      }
    } else if (p.word("midi")) {
      if (p.word("input")) {
        // midi input "device name" remap
        const char* r = p.rest();
        while (*r == ' ' || *r == '\t') ++r;
        if (*r == '"') {
          ++r;
          const char* end = std::strchr(r, '"');
          if (end) {
            std::string dev(r, end - r);
            Parser q(end + 1);
            midi_input_config_t cfg;
            cfg.enable = true;
            cfg.remap = 0;
            int v = 0; if (q.number(&v)) cfg.remap = v;
            g.midi_inputs[dev] = cfg;
          }
        }
      } else if (p.word("transpose")) {
        uint32_t v = 0; p.value(nullptr, 0, &v); g.midi_transpose = v != 0;
      } else if (p.word("output")) {
        const char* r = p.rest();
        while (*r == ' ' || *r == '\t') ++r;
        g.midi_output_port = trim(r);
      }
    } else if (p.word("resize")) {
      uint32_t v = 1; p.value(boolean_names, ARRAY_COUNT(boolean_names), &v);
      g.enable_resize = v != 0;
    } else if (p.word("hotkey")) {
      uint32_t v = 1; p.value(boolean_names, ARRAY_COUNT(boolean_names), &v);
      g.enable_hotkey = v != 0;
    } else if (p.word("key-fade")) {
      int v = 0; if (p.number(&v)) g.key_fade = clamp_value(v, 0, 100);
    } else if (p.word("gui-auto-color")) {
      int v = 0; if (p.number(&v)) g.auto_color = (uint8_t)clamp_value(v, 0, 2);
    } else if (p.word("gui-preview-color")) {
      int v = 0; if (p.number(&v)) g.preview_color = (uint8_t)clamp_value(v, 0, 100);
    } else if (p.word("gui-note-display")) {
      int v = 0; if (p.number(&v)) g.note_display = (uint8_t)clamp_value(v, 0, 2);
    } else if (p.word("sample-rate")) {
      int v = 0; if (p.number(&v)) g.sample_rate = clamp_value(v, 8000, 192000);
    } else if (p.word("buffer-size")) {
      int v = 0; if (p.number(&v)) g.buffer_size = clamp_value(v, 32, 4096);
    } else if (p.word("language")) {
      const char* r = p.rest();
      std::string v = trim(r);
      if (v == "zh" || v == "chinese") g.language = FP_LANG_SCHINESE;
      else if (v == "en" || v == "english") g.language = FP_LANG_ENGLISH;
    }
  }

  // Configs written before the mdaPiano port defaulted to the simple built-in
  // synth.  Upgrade that implicit default once; an explicit choice made after
  // this version is preserved via config-version.
  if (!has_config_version && g.instrument_type == INSTRUMENT_TYPE_BUILTIN &&
      g.instrument_path.empty()) {
    g.instrument_type = INSTRUMENT_TYPE_MDA_PIANO;
  }

  }  // end settings file

  }  // lock scope

  config_apply();
  return 0;
}

int config_save(const std::string& filename) {
  fp_lock lock;
  std::string path = settings_file_path(filename);
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return -1;

  const char* type_name = "MdaPiano";
  switch (g.instrument_type) {
    case INSTRUMENT_TYPE_NONE: type_name = "None"; break;
    case INSTRUMENT_TYPE_BUILTIN: type_name = "Builtin"; break;
    case INSTRUMENT_TYPE_SOUNDFONT: type_name = "SoundFont"; break;
    default: type_name = "MdaPiano"; break;
  }
  file << "config-version 2\n";
  file << "instrument type " << type_name << "\n";
  if (!g.instrument_path.empty()) file << "instrument path " << g.instrument_path << "\n";
  file << "output type " << (g.output_type == OUTPUT_TYPE_NULL ? "None" : "PortAudio") << "\n";
  if (!g.output_device.empty()) file << "output device " << g.output_device << "\n";
  file << "output volume " << g.output_volume << "\n";
  file << "sample-rate " << g.sample_rate << "\n";
  file << "buffer-size " << g.buffer_size << "\n";
  if (!g.keymap.empty()) file << "keyboard map " << g.keymap << "\n";
  if (g.midi_transpose) file << "midi transpose 1\n";
  if (!g.midi_output_port.empty()) file << "midi output " << g.midi_output_port << "\n";
  for (const auto& it : g.midi_inputs) {
    if (it.second.enable) file << "midi input \"" << it.first << "\" " << it.second.remap << "\n";
  }
  if (!g.enable_hotkey) file << "hotkey disable\n";
  if (!g.enable_resize) file << "resize disable\n";
  if (g.key_fade) file << "key-fade " << g.key_fade << "\n";
  if (g.auto_color) file << "gui-auto-color " << (int)g.auto_color << "\n";
  if (g.preview_color) file << "gui-preview-color " << (int)g.preview_color << "\n";
  if (g.note_display) file << "gui-note-display " << (int)g.note_display << "\n";
  file << "language " << (config_get_language() == FP_LANG_SCHINESE ? "zh" : "en") << "\n";

  return 0;
}

// ---------------------------------------------------------------------------
// default key map (fallback when the data directory is unavailable)
// ---------------------------------------------------------------------------
static const char* kDefaultKeyMap =
  "FreePiano 1.8\n"
  "\n"
  "Velocity In_0 80\n"
  "Velocity In_1 100\n"
  "Sustain In_0 127\n"
  "\n"
  "key\tz\tNoteOn\tIn_0\tc2\n"
  "key\tx\tNoteOn\tIn_0\td2\n"
  "key\tc\tNoteOn\tIn_0\te2\n"
  "key\tv\tNoteOn\tIn_0\tf2\n"
  "key\tb\tNoteOn\tIn_0\tg2\n"
  "key\tn\tNoteOn\tIn_0\ta2\n"
  "key\tm\tNoteOn\tIn_0\tb2\n"
  "key\t,\tNoteOn\tIn_0\tc3\n"
  "key\t.\tNoteOn\tIn_0\td3\n"
  "key\t/\tNoteOn\tIn_0\te3\n"
  "key\ta\tNoteOn\tIn_0\tc3\n"
  "key\ts\tNoteOn\tIn_0\td3\n"
  "key\td\tNoteOn\tIn_0\te3\n"
  "key\tf\tNoteOn\tIn_0\tf3\n"
  "key\tg\tNoteOn\tIn_0\tg3\n"
  "key\th\tNoteOn\tIn_0\ta3\n"
  "key\tj\tNoteOn\tIn_0\tb3\n"
  "key\tk\tNoteOn\tIn_0\tc4\n"
  "key\tl\tNoteOn\tIn_0\td4\n"
  "key\t;\tNoteOn\tIn_0\te4\n"
  "key\tq\tNoteOn\tIn_0\tc4\n"
  "key\tw\tNoteOn\tIn_0\td4\n"
  "key\te\tNoteOn\tIn_0\te4\n"
  "key\tr\tNoteOn\tIn_0\tf4\n"
  "key\tt\tNoteOn\tIn_0\tg4\n"
  "key\ty\tNoteOn\tIn_0\ta4\n"
  "key\tu\tNoteOn\tIn_0\tb4\n"
  "key\ti\tNoteOn\tIn_0\tc5\n"
  "key\to\tNoteOn\tIn_0\td5\n"
  "key\tp\tNoteOn\tIn_0\te5\n"
  "key\t1\tNoteOn\tIn_0\tc5\n"
  "key\t2\tNoteOn\tIn_0\td5\n"
  "key\t3\tNoteOn\tIn_0\te5\n"
  "key\t4\tNoteOn\tIn_0\tf5\n"
  "key\t5\tNoteOn\tIn_0\tg5\n"
  "key\t6\tNoteOn\tIn_0\ta5\n"
  "key\t7\tNoteOn\tIn_0\tb5\n"
  "key\t8\tNoteOn\tIn_0\tc6\n"
  "key\t9\tNoteOn\tIn_0\td6\n"
  "key\t0\tNoteOn\tIn_0\te6\n"
  "\n"
  "keydown F5 Octave in_0 Inc 1\n"
  "keydown F6 Octave in_0 Dec 1\n"
  "keydown F9 Velocity in_0 Inc 1\n"
  "keydown F10 Velocity in_0 Dec 1\n"
  "keydown ScrollLock Record\n"
  "keydown PrintScreen Play\n"
  "keydown Pause Stop\n";

void config_default_key_setting() {
  fp_lock lock;
  config_clear_key_setting();
  map_version = 0;
  config_parse_keymap(kDefaultKeyMap, 0, 0);
}

// ---------------------------------------------------------------------------
// apply
// ---------------------------------------------------------------------------
void config_apply() {
  if (g.keymap.empty()) {
    // try the bundled map, then fall back to the built-in default
    if (config_set_keymap("freepiano.map") != 0) {
      g.keymap.clear();
      config_default_key_setting();
    }
  } else {
    if (config_set_keymap(g.keymap) != 0) {
      config_default_key_setting();
    }
  }

  engine_apply_config();
}

int config_init() {
  fp_lock lock;
  settings[0].clear();
  setting_count = 1;
  current_setting = 0;
  map_version = map_current_version;
  return 0;
}

void config_shutdown() {
  fp_lock lock;
  g.midi_inputs.clear();
}
