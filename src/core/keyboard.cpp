// Port of FreePiano's keyboard engine (src/keyboard.cpp).

#include "keyboard.h"

#include "config.h"
#include "song.h"
#include "sync.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

static std::multimap<byte, key_bind_t> key_up_map;
static byte keyboard_status[256] = {0};
static bool keyboard_enabled = true;

int keyboard_init() { return 0; }
void keyboard_shutdown() { keyboard_reset(); }

void keyboard_enable(bool enable) {
  fp_lock lock;
  keyboard_enabled = enable;
  if (!enable) keyboard_reset();
}

void keyboard_reset() {
  fp_lock lock;
  for (int i = 0; i < 256; ++i) {
    if (keyboard_status[i]) {
      keyboard_send_event(i, 0);
      keyboard_status[i] = 0;
    }
  }
}

std::vector<std::string> keyboard_enum_keymap() {
  std::vector<std::string> result;
  std::error_code ec;
  auto scan = [&](const fs::path& dir) {
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      if (!entry.is_regular_file(ec)) continue;
      if (entry.path().extension() == ".map") result.push_back(entry.path().string());
    }
  };
  scan(fs::path(config_data_dir()));
  scan(fs::path(config_user_map_dir()));
  std::sort(result.begin(), result.end());
  return result;
}

void keyboard_send_event(int code, int keydown) {
  fp_lock lock;
  if (!keyboard_enabled) return;
  if (code < 0 || code > 255) return;

  keyboard_status[code] = (byte)(keydown ? 1 : 0);

  if (keydown) {
    key_bind_t temp[256];

    int num_keydown = config_bind_get_keydown((byte)code, temp, 256);
    for (int i = 0; i < num_keydown; ++i) {
      key_bind_t down = temp[i];
      if (!down.a) continue;

      song_translate_note(down.a, down.b, down.c, down.d);

      if ((down.a & 0xf0) == SM_MIDI_NOTEON) {
        key_bind_t up;
        up.a = (byte)(SM_MIDI_NOTEOFF | (down.a & 0x0f));
        up.b = down.b;
        up.c = down.c;
        up.d = down.d;
        key_up_map.insert({(byte)code, up});
      }

      song_output_event(down.a, down.b, down.c, down.d);
    }

    int num_keyup = config_bind_get_keyup((byte)code, temp, 256);
    for (int i = 0; i < num_keyup; ++i) {
      key_bind_t up = temp[i];
      if (!up.a) continue;
      song_translate_note(up.a, up.b, up.c, up.d);
      key_up_map.insert({(byte)code, up});
      if ((up.a & 0xf0) == SM_MIDI_NOTEON) {
        up.a = (byte)(SM_MIDI_NOTEOFF | (up.a & 0x0f));
        key_up_map.insert({(byte)code, up});
      }
    }

    song_trigger_sync(SONG_SYNC_FLAG_KEYDOWN);
  } else {
    auto range = key_up_map.equal_range((byte)code);
    for (auto it = range.first; it != range.second; ++it) {
      key_bind_t& up = it->second;
      if (up.a) song_output_event(up.a, up.b, up.c, up.d);
    }
    key_up_map.erase((byte)code);
  }
}

void keyboard_update(double /*time_elapsed*/) {}

byte keyboard_get_status(byte code) { return keyboard_status[code]; }
