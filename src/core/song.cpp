// Portable re-implementation of FreePiano's song engine.
// Based on the original FreePiano sources (BSD-3-Clause),
// https://github.com/Energy0124/freepiano , src/song.cpp.

#include "song.h"

#include "config.h"
#include "keyboard.h"
#include "midi.h"
#include "sync.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <list>
#include <string>
#include <vector>
#include <zlib.h>

struct song_event_t {
  double time;
  byte a;
  byte b;
  byte c;
  byte d;
};

static_assert(sizeof(song_event_t) == 16, "FreePiano song events are 16 bytes");

static std::vector<song_event_t> song_events;
static int play_position = -1;
static int record_position = -1;
static int song_event_count = 0;

static double song_timer = 0;
static double song_play_speed = 1;
static double song_clock = 0;
static song_info_t song_info{};

static const uint32_t current_version = 0x01080000;

// ---------------------------------------------------------------------------
// sync and delayed events
// ---------------------------------------------------------------------------
struct sync_event_t {
  uint32_t flag;
  uint32_t id;
  key_bind_t map;
};

struct delay_event_t {
  double timer;
  key_bind_t map;
};

static std::list<sync_event_t> sync_events;
static std::list<delay_event_t> delay_events;
static uint32_t sync_trigger = 0;
static uint32_t sync_id = 0;

static const double fixed_delay_timer = 20;
static const double fixed_smooth_timer = 2;

static void sync_event_reset() {
  sync_events.clear();
  sync_trigger = 0;
  sync_id = 0;
}

static void sync_event_add(uint32_t flag, byte a, byte b, byte c, byte d) {
  sync_events.push_back({flag, sync_id, key_bind_t(a, b, c, d)});
}

static void sync_event_update(double /*time*/) {
  std::vector<key_bind_t> output;
  for (auto it = sync_events.begin(); it != sync_events.end();) {
    if (it->id != sync_id && (it->flag & sync_trigger)) {
      output.push_back(it->map);
      it = sync_events.erase(it);
    } else {
      ++it;
    }
  }
  if (sync_trigger) {
    ++sync_id;
    sync_trigger = 0;
  }
  for (const auto& e : output) song_output_event(e.a, e.b, e.c, e.d);
}

static void delay_event_reset() { delay_events.clear(); }

static void delay_event_add(double timer, byte a, byte b, byte c, byte d) {
  delay_events.push_back({timer, key_bind_t(a, b, c, d)});
}

static void delay_event_update(double time) {
  std::vector<key_bind_t> output;
  for (auto it = delay_events.begin(); it != delay_events.end();) {
    it->timer -= time;
    if (it->timer <= 0) {
      output.push_back(it->map);
      it = delay_events.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& e : output) song_output_event(e.a, e.b, e.c, e.d);
}

// ---------------------------------------------------------------------------
// event translation
// ---------------------------------------------------------------------------
static inline byte translate_channel(byte ch) {
  return (byte)(config_get_output_channel(ch) & 0x0f);
}

static inline byte translate_note(byte ch, byte note) {
  int value = note;
  value += config_get_key_octshift(ch) * 12;
  value += config_get_key_transpose(ch);
  if (config_get_follow_key(ch)) value += config_get_key_signature();
  return (byte)clamp_value(value, 0, 127);
}

static inline byte translate_pressure(byte ch, byte pressure) {
  return (byte)clamp_value((int)pressure * config_get_key_velocity(ch) / 127, 0, 127);
}

static inline int default_value(int v, int dv = 0) {
  if (v > 127) return dv;
  if (v < -127) return dv;
  return v;
}

static int translate_value(byte action, int value, int change) {
  switch (action & 0x0f) {
    case SM_VALUE_SET: value = change; break;
    case SM_VALUE_INC: value = value + change; break;
    case SM_VALUE_DEC: value = value - change; break;
    case SM_VALUE_FLIP: value = change - value; break;
    case SM_VALUE_PRESS: value = change; break;
    case SM_VALUE_SET10: value = change / 10 * 10 + (value % 10); break;
    case SM_VALUE_SET1: value = value / 10 * 10 + (change % 10); break;
    default: break;
  }
  return value;
}

bool song_translate_note(byte& a, byte& b, byte& c, byte& d) {
  switch (a) {
    case SM_NOTE_ON: {
      byte ch = b;
      byte note = c;
      byte pressure = d;
      a = SM_MIDI_NOTEON | translate_channel(ch);
      b = translate_note(ch, note);
      c = translate_pressure(ch, pressure);
      d = 0;
      return true;
    }
    case SM_NOTE_OFF: {
      byte ch = b;
      a = SM_MIDI_NOTEOFF | translate_channel(ch);
      b = translate_note(ch, c);
      c = 0;
      d = 0;
      return true;
    }
    case SM_NOTE_PRESSURE: {
      byte ch = b;
      byte note = c;
      byte pressure = d;
      a = SM_MIDI_PRESSURE | translate_channel(ch);
      b = translate_note(ch, note);
      c = translate_pressure(ch, pressure);
      d = 0;
      return true;
    }
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// smoothed pitch bend
// ---------------------------------------------------------------------------
struct smooth_param_t {
  char target;
  char current;
  double timer;
};

static inline bool smooth_param_update(smooth_param_t& param, double time_elapsed) {
  param.timer -= time_elapsed;
  if (param.timer < 0) {
    if (param.target != param.current) {
      param.timer = fixed_smooth_timer;
      if (param.current > param.target) --param.current;
      if (param.current < param.target) ++param.current;
      return true;
    }
  }
  return false;
}

static smooth_param_t pitch_smooth[16] = {};

static void pitch_update(double time) {
  for (int ch = 0; ch < 16; ++ch) {
    if (smooth_param_update(pitch_smooth[ch], time)) {
      byte a = SM_MIDI_PITCH_BEND | (byte)ch;
      byte c = (byte)(64 + clamp_value<int>(pitch_smooth[ch].current, -64, 63));
      midi_output_event(a, 0, c, 0);
    }
  }
}

// ---------------------------------------------------------------------------
// event parser (dynamic key mapping, labels, colours)
// ---------------------------------------------------------------------------
static void song_add_event(double time, byte a, byte b, byte c, byte d);

static byte keyboard_map_key_code = 0;
static byte keyboard_map_key_type = 0;
static byte keyboard_label_key_code = 0;
static byte keyboard_label_key_size = 0;
static char keyboard_label_text[256];
static byte keyboard_color_key_code = 0;

static void keyboard_event_map(int code, int type) {
  keyboard_map_key_code = (byte)code;
  keyboard_map_key_type = (byte)type;
}
static void keyboard_event_label(int code, int length) {
  keyboard_label_key_code = (byte)code;
  keyboard_label_key_size = (byte)length;
  keyboard_label_text[0] = 0;
}
static void keyboard_event_color(int code, int /*length*/) { keyboard_color_key_code = (byte)code; }

void song_send_event(byte a, byte b, byte c, byte d, bool record) {
  fp_lock lock;

  if (record) {
    if (a != SM_PLAY && a != SM_RECORD && a != SM_STOP) song_add_event(song_timer, a, b, c, d);
  }

  if (keyboard_label_key_size) {
    char* ch = keyboard_label_text + std::strlen(keyboard_label_text);
    char data[4] = {(char)a, (char)b, (char)c, (char)d};
    for (int i = 0; i < 4; ++i) {
      ch[i] = data[i];
      ch[i + 1] = 0;
      if (--keyboard_label_key_size == 0) {
        config_bind_set_label(keyboard_label_key_code, keyboard_label_text);
        break;
      }
    }
    return;
  }

  if (keyboard_map_key_code) {
    if (keyboard_map_key_type == 0) config_bind_add_keydown(keyboard_map_key_code, key_bind_t(a, b, c, d));
    else config_bind_add_keyup(keyboard_map_key_code, key_bind_t(a, b, c, d));
    keyboard_map_key_code = 0;
    return;
  }

  if (keyboard_color_key_code) {
    uint32_t color = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
    config_bind_set_color(keyboard_color_key_code, color);
    keyboard_color_key_code = 0;
    return;
  }

  if (a == SM_SYSTEM) {
    switch (b) {
      case SMS_KEY_EVENT: keyboard_send_event(c, d); break;
      case SMS_KEY_MAP: keyboard_event_map(c, d); break;
      case SMS_KEY_LABEL: keyboard_event_label(c, d); break;
      case SMS_KEY_COLOR: keyboard_event_color(c, d); break;
      default: break;
    }
    return;
  }

  song_output_event(a, b, c, d);
}

static void output_controller(byte a, byte b, byte c, byte d, byte id) {
  byte ch = b;
  byte op = c;
  int change = (int8_t)d;

  int value = config_get_controller(ch, id);
  value = default_value(value);
  value = translate_value(op, value, change);
  value = clamp_value(value, 0, 127);

  if (op & SM_VALUE_SYNC) {
    sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, b, (byte)(c & ~SM_VALUE_SYNC), d);
    return;
  }

  if ((op & 0x0f) == SM_VALUE_PRESS) {
    delay_event_add(fixed_delay_timer, a, b, SM_VALUE_SET, (byte)config_get_controller(ch, id));
  }

  midi_output_event(SM_MIDI_CONTROLLER | translate_channel(ch), id, (byte)value, 0);
}

void song_output_event(byte a, byte b, byte c, byte d) {
  fp_lock lock;

  if (a >= SM_MIDI_MESSAGE_START) {
    midi_output_event(a, b, c, d);
    return;
  }

  switch (a) {
    case SM_KEY_SIGNATURE: {
      byte op = b;
      char change = (char)c;
      int value = config_get_key_signature();
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, (byte)(op & ~SM_VALUE_SYNC), c, 0);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, SM_VALUE_SET, (byte)value, 0);
      value = translate_value(op, value, change);
      value = wrap_value(value, -4, 7);
      config_set_key_signature((char)value);
      break;
    }

    case SM_TRANSPOSE: {
      int ch = b;
      byte op = c;
      int value = config_get_key_transpose((byte)ch);
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, b, (byte)(op & ~SM_VALUE_SYNC), d);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, b, SM_VALUE_SET, (byte)value);
      value = translate_value(op, value, (int8_t)d);
      value = clamp_value(value, -64, 64);
      config_set_key_transpose((byte)ch, (char)value);
      break;
    }

    case SM_FOLLOW_KEY: {
      int ch = b;
      byte op = c;
      int value = config_get_follow_key((byte)ch);
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, b, (byte)(op & ~SM_VALUE_SYNC), d);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, b, SM_VALUE_SET, (byte)value);
      value = translate_value(op, value, (int8_t)d);
      value = clamp_value(value, 0, 1);
      config_set_follow_key((byte)ch, (byte)value);
      break;
    }

    case SM_OCTAVE: {
      int ch = b;
      byte op = c;
      int value = config_get_key_octshift((byte)ch);
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, b, (byte)(op & ~SM_VALUE_SYNC), d);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, b, SM_VALUE_SET, (byte)value);
      value = translate_value(op, value, (int8_t)d);
      value = wrap_value(value, -1, 1);
      config_set_key_octshift((byte)ch, (char)value);
      break;
    }

    case SM_VELOCITY: {
      int ch = b;
      byte op = c;
      int value = config_get_key_velocity((byte)ch);
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, b, (byte)(op & ~SM_VALUE_SYNC), d);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, b, SM_VALUE_SET, (byte)value);
      value = translate_value(op, value, (int8_t)d);
      value = clamp_value(value, 0, 127);
      config_set_key_velocity((byte)ch, (byte)value);
      break;
    }

    case SM_CHANNEL: {
      int ch = b;
      byte op = c;
      int value = config_get_output_channel((byte)ch);
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, b, (byte)(op & ~SM_VALUE_SYNC), d);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, b, SM_VALUE_SET, (byte)value);
      value = translate_value(op, value, (int8_t)d);
      value = clamp_value(value, 0, 15);
      config_set_output_channel((byte)ch, (byte)value);
      break;
    }

    case SM_VOLUME: {
      byte op = b;
      int value = config_get_output_volume();
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, (byte)(op & ~SM_VALUE_SYNC), c, 0);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, SM_VALUE_SET, (byte)value, 0);
      value = translate_value(op, value, c);
      value = clamp_value(value, 0, 200);
      config_set_output_volume(value);
      break;
    }

    case SM_PLAY:
      if (song_allow_input()) song_start_playback();
      break;

    case SM_RECORD:
      if (song_allow_input()) {
        if (song_is_recording()) song_stop_record();
        else song_start_record();
      }
      break;

    case SM_STOP:
      if (song_is_recording()) song_stop_record();
      if (song_is_playing()) song_stop_playback();
      break;

    case SM_SETTING_GROUP: {
      byte op = b;
      int value = (int)config_get_setting_group();
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, (byte)(op & ~SM_VALUE_SYNC), c, 0);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, SM_VALUE_SET, (byte)value, 0);
      value = translate_value(op, value, c);
      value = wrap_value(value, 0, (int)config_get_setting_group_count() - 1);
      config_set_setting_group((uint32_t)value);
      break;
    }

    case SM_SETTING_GROUP_COUNT:
      config_set_setting_group_count(b);
      break;

    case SM_NOTE_ON:
    case SM_NOTE_OFF:
    case SM_NOTE_PRESSURE:
      if (song_translate_note(a, b, c, d)) midi_output_event(a, b, c, d);
      break;

    case SM_PRESSURE: {
      byte ch = translate_channel(b);
      byte op = c;
      int value = 0;
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, b, (byte)(op & ~SM_VALUE_SYNC), d);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, b, SM_VALUE_SET, 0);
      value = translate_value(op, value, (int8_t)d);
      value = translate_pressure(b, (byte)value);
      midi_output_event(SM_MIDI_CHANNEL_PRESSURE | ch, (byte)value, 0, 0);
      break;
    }

    case SM_PITCH: {
      byte ch = translate_channel(b);
      byte op = c;
      int value = pitch_smooth[ch].target;
      value = default_value(value);
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, b, (byte)(op & ~SM_VALUE_SYNC), d);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, b, SM_VALUE_SET, (byte)value);
      value = translate_value(op, value, (int8_t)d);
      value = clamp_value(value, -64, 63);
      pitch_smooth[ch].target = (char)value;
      break;
    }

    case SM_PROGRAM: {
      int ch = b;
      byte op = c;
      int value = config_get_program((byte)ch);
      value = default_value(value);
      if (op & SM_VALUE_SYNC) {
        sync_event_add(SONG_SYNC_FLAG_KEYDOWN, a, b, (byte)(op & ~SM_VALUE_SYNC), d);
        return;
      }
      if ((op & 0x0f) == SM_VALUE_PRESS) delay_event_add(fixed_delay_timer, a, b, SM_VALUE_SET, (byte)value);
      value = translate_value(op, value, (int8_t)d);
      value = clamp_value(value, 0, 127);
      midi_output_event(SM_MIDI_PROGRAM | translate_channel((byte)ch), (byte)value, 0, 0);
      break;
    }

    case SM_BANK_LSB: output_controller(a, b, c, d, 32); break;
    case SM_BANK_MSB: output_controller(a, b, c, d, 0); break;
    case SM_SUSTAIN: output_controller(a, b, c, d, 64); break;
    case SM_MODULATION: output_controller(a, b, c, d, 1); break;

    default: break;
  }
}

// ---------------------------------------------------------------------------
// recording
// ---------------------------------------------------------------------------
static void song_add_event(double time, byte a, byte b, byte c, byte d) {
  if (record_position < 0) return;
  song_event_t e{time, a, b, c, d};
  if ((size_t)record_position >= song_events.size()) song_events.resize(record_position + 1024);
  song_events[record_position] = e;
  ++record_position;
  song_event_count = record_position;
}

static void song_init_record() {
  song_timer = 0;
  song_clock = 0;
  record_position = 0;
  play_position = -1;
  song_event_count = 0;
  song_info.version = current_version;
  song_info.author[0] = 0;
  song_info.title[0] = 0;
  song_info.comment[0] = 0;
  song_info.write_protected = false;
  song_info.compatibility = true;
  song_play_speed = 1;
  if (song_events.size() < 1024) song_events.resize(1024);
}

static void song_reset_event() {
  keyboard_map_key_code = 0;
  keyboard_label_key_size = 0;
  keyboard_reset();
  sync_event_reset();
  delay_event_reset();
  midi_reset();
}

void song_start_record() {
  fp_lock lock;
  song_stop_playback();
  if (record_position >= 0) song_stop_record();
  song_init_record();

  int current_group = (int)config_get_setting_group();
  int total_groups = (int)config_get_setting_group_count();

  song_add_event(0, SM_SETTING_GROUP_COUNT, (byte)total_groups, 0, 0);

  for (int i = 0; i < total_groups; ++i) {
    config_set_setting_group((uint32_t)i);
    song_add_event(0, SM_SETTING_GROUP, SM_VALUE_SET, (byte)i, 0);
    song_add_event(0, SM_KEY_SIGNATURE, SM_VALUE_SET, (byte)config_get_key_signature(), 0);

    for (int ch = SM_INPUT_0; ch <= SM_INPUT_MAX; ++ch) {
      if (config_get_key_octshift((byte)ch))
        song_add_event(0, SM_OCTAVE, (byte)ch, SM_VALUE_SET, (byte)config_get_key_octshift((byte)ch));
      if (config_get_key_transpose((byte)ch))
        song_add_event(0, SM_TRANSPOSE, (byte)ch, SM_VALUE_SET, (byte)config_get_key_transpose((byte)ch));
      if (config_get_key_velocity((byte)ch) != 127)
        song_add_event(0, SM_VELOCITY, (byte)ch, SM_VALUE_SET, config_get_key_velocity((byte)ch));
      if (config_get_output_channel((byte)ch))
        song_add_event(0, SM_CHANNEL, (byte)ch, SM_VALUE_SET, (byte)config_get_output_channel((byte)ch));
    }

    for (int ch = SM_OUTPUT_0; ch <= SM_OUTPUT_MAX; ++ch) {
      int prog = config_get_program((byte)ch);
      if (prog >= 0 && prog < 128) song_add_event(0, SM_PROGRAM, (byte)ch, SM_VALUE_SET, (byte)prog);
      int bank_msb = config_get_controller((byte)ch, 0);
      if (bank_msb >= 0 && bank_msb < 128) song_add_event(0, SM_BANK_MSB, (byte)ch, SM_VALUE_SET, (byte)bank_msb);
      int bank_lsb = config_get_controller((byte)ch, 32);
      if (bank_lsb >= 0 && bank_lsb < 128) song_add_event(0, SM_BANK_LSB, (byte)ch, SM_VALUE_SET, (byte)bank_lsb);
      int sustain = config_get_controller((byte)ch, 64);
      if (sustain >= 0 && sustain < 128) song_add_event(0, SM_SUSTAIN, (byte)ch, SM_VALUE_SET, (byte)sustain);
      int modulation = config_get_controller((byte)ch, 1);
      if (modulation >= 0 && modulation < 128) song_add_event(0, SM_MODULATION, (byte)ch, SM_VALUE_SET, (byte)modulation);
    }

    for (int key = 0; key < 256; ++key) {
      key_bind_t bind[256];
      int count = config_bind_get_keydown((byte)key, bind, 256);
      for (int j = 0; j < count; ++j) {
        if (bind[j].a) {
          song_add_event(0, SM_SYSTEM, SMS_KEY_MAP, (byte)key, 0);
          song_add_event(0, bind[j].a, bind[j].b, bind[j].c, bind[j].d);
        }
      }
      count = config_bind_get_keyup((byte)key, bind, 256);
      for (int j = 0; j < count; ++j) {
        if (bind[j].a) {
          song_add_event(0, SM_SYSTEM, SMS_KEY_MAP, (byte)key, 1);
          song_add_event(0, bind[j].a, bind[j].b, bind[j].c, bind[j].d);
        }
      }

      const char* label = config_bind_get_label((byte)key);
      if (label && *label) {
        int size = (int)std::strlen(label);
        song_add_event(0, SM_SYSTEM, SMS_KEY_LABEL, (byte)key, (byte)size);
        const char* p = label;
        for (int j = 0; j < (size + 3) / 4; ++j) {
          song_add_event(0, (byte)(p[0] ? p[0] : ' '), (byte)(p[0] ? p[1] : ' '),
                         (byte)(p[0] ? p[2] : ' '), (byte)(p[0] ? p[3] : ' '));
          p += 4;
        }
      }

      uint32_t color = config_bind_get_color((byte)key);
      if (color) {
        song_add_event(0, SM_SYSTEM, SMS_KEY_COLOR, (byte)key, 1);
        song_add_event(0, (byte)(color >> 24), (byte)(color >> 16), (byte)(color >> 8), (byte)color);
      }
    }
  }

  config_set_setting_group((uint32_t)current_group);
  song_add_event(0, SM_SETTING_GROUP, 0, (byte)current_group, 0);
}

void song_stop_record() {
  fp_lock lock;
  if (record_position >= 0) {
    song_add_event(song_timer, SM_STOP, 0, 0, 0);
    song_reset_event();
    record_position = -1;
  }
}

bool song_is_recording() { fp_lock lock; return record_position >= 0; }

void song_start_playback() {
  fp_lock lock;
  if (song_event_count <= 0) return;
  if (record_position >= 0) song_stop_record();
  song_stop_playback();

  song_timer = 0;
  song_clock = 0;
  play_position = 0;

  config_set_setting_group_count(1);
  config_clear_key_setting();

  song_update(0);
}

void song_stop_playback() {
  fp_lock lock;
  song_reset_event();
  play_position = -1;
}

bool song_is_playing() { fp_lock lock; return play_position >= 0; }

int song_get_length() {
  fp_lock lock;
  if (song_event_count > 0) return (int)song_events[song_event_count - 1].time;
  return 0;
}

bool song_allow_save() {
  fp_lock lock;
  return song_event_count > 0 && !song_info.write_protected && record_position < 0;
}

bool song_allow_input() { fp_lock lock; return play_position < 0; }

bool song_is_empty() { fp_lock lock; return song_event_count <= 0; }

int song_get_time() {
  fp_lock lock;
  return (int)song_timer;
}

double song_get_timer() {
  fp_lock lock;
  return song_timer;
}

int song_get_event_count() {
  fp_lock lock;
  return song_event_count;
}

double song_get_clock() {
  fp_lock lock;
  return song_clock;
}

double song_get_play_speed() {
  fp_lock lock;
  return song_play_speed;
}

void song_set_play_speed(double speed) {
  fp_lock lock;
  song_play_speed = speed < 0 ? 0 : speed;
}

void song_update(double time_elapsed) {
  fp_lock lock;

  if (play_position >= 0) time_elapsed *= song_play_speed;
  if (play_position >= 0 || record_position >= 0) song_timer += time_elapsed;

  while (play_position >= 0 && play_position < song_event_count) {
    if (song_events[play_position].time <= song_timer) {
      song_event_t e = song_events[play_position];
      song_send_event(e.a, e.b, e.c, e.d);
      if (play_position >= 0) {
        ++play_position;
        if (play_position >= song_event_count) song_stop_playback();
      }
    } else {
      break;
    }
  }

  song_clock += time_elapsed;
  keyboard_update(time_elapsed);
  sync_event_update(time_elapsed);
  delay_event_update(time_elapsed);
  pitch_update(time_elapsed);
}

song_info_t* song_get_info() { return &song_info; }

void song_trigger_sync(uint32_t flags) {
  fp_lock lock;
  sync_trigger |= flags;
}

void song_update_pressure(byte /*ch*/, byte /*pressure*/) {}

void song_reset_all() {
  fp_lock lock;
  song_stop_record();
  song_stop_playback();
  song_event_count = 0;
  song_events.clear();
}

void song_close() {
  fp_lock lock;
  song_stop_record();
  song_stop_playback();
  song_event_count = 0;
  song_events.clear();
}

// ---------------------------------------------------------------------------
// .fpm (FreePianoSong) file format
// ---------------------------------------------------------------------------
namespace {

bool read_bytes(void* buff, size_t size, FILE* fp) { return std::fread(buff, 1, size, fp) == size; }
bool write_bytes(const void* buff, size_t size, FILE* fp) { return std::fwrite(buff, 1, size, fp) == size; }

bool read_string(std::string& out, FILE* fp) {
  uint32_t count = 0;
  if (!read_bytes(&count, sizeof(count), fp)) return false;
  if (count > 1024 * 1024) return false;
  out.resize(count);
  if (count && !read_bytes(out.data(), count, fp)) return false;
  return true;
}

bool write_string(const std::string& s, FILE* fp) {
  uint32_t count = (uint32_t)s.size();
  return write_bytes(&count, sizeof(count), fp) && (count == 0 || write_bytes(s.data(), count, fp));
}

}  // namespace

int song_open(const std::string& filename) {
  fp_lock lock;
  song_close();

  FILE* fp = std::fopen(filename.c_str(), "rb");
  if (!fp) return -1;

  char magic[sizeof("FreePianoSong")] = {};
  bool ok = read_bytes(magic, sizeof(magic), fp) &&
            std::memcmp(magic, "FreePianoSong", sizeof(magic)) == 0;
  uint32_t version = 0;
  if (ok) ok = read_bytes(&version, sizeof(version), fp);
  if (!ok) {
    std::fclose(fp);
    config_set_last_error("Not a FreePiano song (.fpm).");
    return -1;
  }
  if (version > current_version) {
    std::fclose(fp);
    config_set_last_error("This song was written by a newer FreePiano version.");
    return -2;
  }

  song_info.version = version;
  std::string title, author, comment, instrument;
  ok = read_string(title, fp) && read_string(author, fp) && read_string(comment, fp) &&
       read_string(instrument, fp);
  uint32_t compressed_size = 0;
  if (ok) ok = read_bytes(&compressed_size, sizeof(compressed_size), fp);
  if (!ok || compressed_size > 256u * 1024 * 1024) {
    std::fclose(fp);
    config_set_last_error("Corrupted song file.");
    return -1;
  }

  std::vector<unsigned char> compressed(compressed_size);
  if (compressed_size && !read_bytes(compressed.data(), compressed_size, fp)) {
    std::fclose(fp);
    config_set_last_error("Corrupted song file.");
    return -1;
  }
  std::fclose(fp);

  // Inflate into a growable buffer.
  std::vector<unsigned char> raw(compressed_size ? compressed_size * 8 + 4096 : 16);
  uLongf raw_size = (uLongf)raw.size();
  int zr = Z_BUF_ERROR;
  for (int attempt = 0; attempt < 24; ++attempt) {
    zr = uncompress(raw.data(), &raw_size, compressed.data(), (uLong)compressed_size);
    if (zr != Z_BUF_ERROR) break;
    raw.resize(raw.size() * 2);
    raw_size = (uLongf)raw.size();
  }
  if (zr != Z_OK) {
    config_set_last_error("Failed to decompress song data.");
    return -1;
  }
  if (raw_size % sizeof(song_event_t) != 0) {
    config_set_last_error("Corrupted song event stream.");
    return -1;
  }

  song_events.resize(raw_size / sizeof(song_event_t) + 1);
  std::memcpy(song_events.data(), raw.data(), raw_size);
  song_event_count = (int)(raw_size / sizeof(song_event_t));

  std::snprintf(song_info.title, sizeof(song_info.title), "%s", title.c_str());
  std::snprintf(song_info.author, sizeof(song_info.author), "%s", author.c_str());
  std::snprintf(song_info.comment, sizeof(song_info.comment), "%s", comment.c_str());
  song_info.write_protected = true;
  play_position = -1;
  record_position = -1;
  song_timer = 0;
  song_clock = 0;

  // Upgrade 1.7 notes to the 1.8 script model (same as the original).
  if (song_info.version <= 0x01070000) {
    for (int i = 0; i < song_event_count; ++i) {
      song_event_t& e = song_events[i];
      byte a = e.a, b = e.b, c = e.c, d = e.d;

      if (a == SM_SYSTEM) {
        if (b == SMS_KEY_LABEL) i += (d + 3) / 4;
        else if (b == SMS_KEY_COLOR) ++i;
      } else if (a == SM_AUTO_PEDAL_OBSOLETE) {
        if (b != 0 || c != 0) song_info.compatibility = false;
      } else if (a == SM_DELAY_KEYUP_OBSOLETE) {
        if (c != 0 || d != 0) song_info.compatibility = false;
      } else {
        switch (a & 0xf0) {
          case SM_MIDI_NOTEON:
            e.a = SM_NOTE_ON; e.b = (byte)(a & 0x0f); e.c = b; e.d = c;
            break;
          case SM_MIDI_NOTEOFF:
            e.a = SM_NOTE_OFF; e.b = (byte)(a & 0x0f); e.c = b; e.d = 0;
            break;
          case SM_MIDI_PROGRAM:
            e.a = SM_PROGRAM; e.b = (byte)(a & 0x0f); e.c = c; e.d = b;
            break;
          case SM_MIDI_CONTROLLER:
            if (b == 64) {
              e.a = SM_SUSTAIN; e.b = (byte)(a & 0x0f); e.c = d; e.d = c;
              if (d == SM_VALUE_FLIP) e.d = 127;
            }
            break;
          default:
            break;
        }
      }
    }
  }

  return 0;
}

int song_save(const std::string& filename) {
  fp_lock lock;
  if (record_position >= 0) song_stop_record();
  if (song_event_count <= 0) return -1;

  FILE* fp = std::fopen(filename.c_str(), "wb");
  if (!fp) return -1;

  bool ok = write_bytes("FreePianoSong", sizeof("FreePianoSong"), fp);
  uint32_t version = current_version;
  ok = ok && write_bytes(&version, sizeof(version), fp);
  ok = ok && write_string(song_info.title, fp);
  ok = ok && write_string(song_info.author, fp);
  ok = ok && write_string(song_info.comment, fp);

  std::string instrument = config_get_instrument_path();
  auto slash = instrument.find_last_of('/');
  if (slash != std::string::npos) instrument = instrument.substr(slash + 1);
  auto dot = instrument.find_last_of('.');
  if (dot != std::string::npos) instrument = instrument.substr(0, dot);
  ok = ok && write_string(instrument, fp);

  size_t raw_size = (size_t)song_event_count * sizeof(song_event_t);
  uLongf bound = compressBound((uLong)raw_size);
  std::vector<unsigned char> compressed(bound);
  int zr = compress2(compressed.data(), &bound, (const Bytef*)song_events.data(), (uLong)raw_size, Z_BEST_COMPRESSION);
  if (zr != Z_OK) {
    std::fclose(fp);
    return -1;
  }
  uint32_t compressed_size = (uint32_t)bound;
  ok = ok && write_bytes(&compressed_size, sizeof(compressed_size), fp);
  ok = ok && write_bytes(compressed.data(), compressed_size, fp);

  std::fclose(fp);
  if (!ok) return -1;
  song_info.write_protected = false;
  return 0;
}

int song_open_lyt(const std::string& /*filename*/) { return -1; }
