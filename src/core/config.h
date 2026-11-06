#pragma once

#include "events.h"
#include "fp_types.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Instrument backends
#define INSTRUMENT_TYPE_NONE        0  // external MIDI only
#define INSTRUMENT_TYPE_BUILTIN     1  // built-in software synthesizer
#define INSTRUMENT_TYPE_SOUNDFONT   2  // FluidSynth + .sf2

#define OUTPUT_TYPE_AUTO            0
#define OUTPUT_TYPE_PORTAUDIO       1
#define OUTPUT_TYPE_NULL            2  // no audio device (offline/timer driven)

#define AUTO_COLOR_CLASSIC          0
#define AUTO_COLOR_CHANNEL          1
#define AUTO_COLOR_VELOCITY         2

#define NOTE_DISPLAY_DOH            0
#define NOTE_DIAPLAY_FIXED_DOH      1
#define NOTE_DISPLAY_NAME           2

#define FP_LANG_ENGLISH             0
#define FP_LANG_SCHINESE            1

#define MAX_SETTING_GROUP           64

struct key_label_t {
  char text[256];
  uint32_t color;
};

struct midi_input_config_t {
  bool enable = false;
  int remap = 0;
};

// Initialize the configuration subsystem (defaults only).
int config_init();
void config_shutdown();

// Load/save the application settings file (freepiano.cfg).
int config_load(const std::string& filename);
int config_save(const std::string& filename);

// Paths.
const std::string& config_data_dir();
const std::string& config_config_dir();
std::string config_media_path(const std::string& path);
std::string config_user_map_dir();

// Key map.
const std::string& config_get_keymap();
int config_set_keymap(const std::string& mapname);
int config_load_keymap(const std::string& filename);
int config_parse_keymap(const std::string& text, uint8_t override_key = 0, int version = -1);

// Replace the current key map with the given text (used by the editor).
int config_apply_keymap_text(const std::string& text);
std::string config_save_keymap();
std::string config_dump_keybind(uint8_t code, bool with_bind = true);

// Key bindings.
int config_bind_get_keydown(uint8_t code, key_bind_t* buffer, int size);
int config_bind_get_keyup(uint8_t code, key_bind_t* buffer, int size);
void config_bind_clear_keydown(uint8_t code);
void config_bind_clear_keyup(uint8_t code);
void config_bind_add_keydown(uint8_t code, key_bind_t bind);
void config_bind_add_keyup(uint8_t code, key_bind_t bind);
void config_bind_set_label(uint8_t code, const char* label);
const char* config_bind_get_label(uint8_t code);
void config_bind_set_color(uint8_t code, uint32_t color);
uint32_t config_bind_get_color(uint8_t code);

void config_clear_key_setting();
void config_copy_key_setting();
void config_paste_key_setting();
void config_default_key_setting();

// Per-group keyboard settings.
void config_set_key_signature(char key);
char config_get_key_signature();
void config_set_key_transpose(uint8_t channel, char transpose);
char config_get_key_transpose(uint8_t channel);
void config_set_key_octshift(uint8_t channel, char shift);
char config_get_key_octshift(uint8_t channel);
void config_set_key_velocity(uint8_t channel, uint8_t velocity);
uint8_t config_get_key_velocity(uint8_t channel);
void config_set_follow_key(uint8_t channel, uint8_t value);
uint8_t config_get_follow_key(uint8_t channel);
int config_get_output_channel(uint8_t channel);
void config_set_output_channel(uint8_t channel, uint8_t value);
int config_get_program(uint8_t channel);
void config_set_program(uint8_t channel, uint8_t value);
int config_get_controller(uint8_t channel, uint8_t id);
void config_set_controller(uint8_t channel, uint8_t id, uint8_t value);

uint32_t config_get_setting_group();
void config_set_setting_group(uint32_t id);
void config_insert_setting_group(uint32_t pos);
void config_delete_setting_group(uint32_t id);
uint32_t config_get_setting_group_count();
void config_set_setting_group_count(uint32_t count);

// Names.
const char* config_get_key_name(uint8_t code);
const char* config_get_note_name(uint8_t note);
const char* config_get_channel_name(uint8_t ch);
int config_default_keylabel(char* buff, size_t size, key_bind_t bind);
std::string config_default_keylabel(key_bind_t bind);

// Instrument / output.
int config_select_instrument(int type, const std::string& name);
int config_get_instrument_type();
const std::string& config_get_instrument_path();
void config_set_soundfont(const std::string& path);

int config_select_output(int type, const std::string& device);
int config_get_output_type();
void config_set_output_type(int type);
const std::string& config_get_output_device();
void config_set_output_device(const std::string& device);
int config_get_sample_rate();
void config_set_sample_rate(int rate);
int config_get_buffer_size();
void config_set_buffer_size(int frames);
int config_get_output_delay();
void config_set_output_delay(int value);

int config_get_output_volume();
void config_set_output_volume(int volume);

// Misc UI settings.
bool config_get_enable_resize_window();
void config_set_enable_resize_window(bool enable);
bool config_get_enable_hotkey();
void config_set_enable_hotkey(bool enable);
bool config_get_midi_transpose();
void config_set_midi_transpose(bool value);
int config_get_key_fade();
void config_set_key_fade(int value);
uint8_t config_get_auto_color();
void config_set_auto_color(uint8_t value);
uint8_t config_get_preview_color();
void config_set_preview_color(uint8_t value);
uint8_t config_get_note_display();
void config_set_note_display(uint8_t value);
int config_get_language();
void config_set_language(int lang);

// MIDI input device settings.
void config_set_midi_input_config(const std::string& device, const midi_input_config_t& config);
bool config_get_midi_input_config(const std::string& device, midi_input_config_t* config);
const std::map<std::string, midi_input_config_t>& config_get_midi_inputs();

// External MIDI output port.
const std::string& config_get_midi_output_port();
void config_set_midi_output_port(const std::string& port);

// Last error text for the UI.
const char* config_get_last_error();
void config_set_last_error(const char* text);

// Apply side effects after loading a settings file (key map, instrument, audio).
void config_apply();
