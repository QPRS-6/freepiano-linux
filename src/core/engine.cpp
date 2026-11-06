// Engine wiring: config + song + synth + audio + MIDI backends.

#include "engine.h"

#include "alsa_midi.h"
#include "audio.h"
#include "config.h"
#include "keyboard.h"
#include "midi.h"
#include "song.h"
#include "synth.h"
#include "sync.h"
#include "wav_export.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<Synth> g_synth;
int g_synth_type = -1;
std::string g_synth_path;
double g_synth_rate = 0;

bool g_ready = false;
bool g_inited = false;
std::string g_error;

std::string g_audio_error;
std::string g_audio_device;
int g_audio_rate = 0;
int g_audio_frames = 0;
bool g_audio_real = false;

std::string g_midi_out;
std::string g_midi_in;

void synth_sink(byte a, byte b, byte c, byte d) {
  if (!g_synth) return;
  int ch = a & 0x0f;
  switch (a & 0xf0) {
    case 0x80: g_synth->note_off(ch, b); break;
    case 0x90:
      if (c == 0) g_synth->note_off(ch, b);
      else g_synth->note_on(ch, b, c);
      break;
    case 0xa0: break;  // polyphonic aftertouch is not modelled
    case 0xb0: g_synth->control_change(ch, b, c); break;
    case 0xc0: g_synth->program_change(ch, b); break;
    case 0xd0: g_synth->channel_pressure(ch, b); break;
    case 0xe0: g_synth->pitch_bend(ch, ((c & 0x7f) << 7) | (b & 0x7f)); break;
    default: break;
  }
}

void external_sink(byte a, byte b, byte c, byte d) { alsa_midi_send(a, b, c, d); }

void render_callback(float* out, int frames) {
  fp_lock lock;
  double sr = g_audio_rate > 0 ? g_audio_rate : 48000.0;
  double dt = 1000.0 * frames / sr;
  song_update(dt);

  int volume = config_get_output_volume();
  if (g_synth) {
    g_synth->set_master_volume(volume / 100.0f);
    g_synth->render(out, frames);
  } else {
    std::memset(out, 0, sizeof(float) * frames * 2);
  }
}

void ensure_instrument() {
  int type = config_get_instrument_type();
  std::string path = config_get_instrument_path();
  double sr = config_get_sample_rate();

  if (g_synth && type == g_synth_type && path == g_synth_path && sr == g_synth_rate) {
    return;
  }

  std::unique_ptr<Synth> next;
  std::string error;
  int loaded_type = type;

  if (type == INSTRUMENT_TYPE_SOUNDFONT) {
#ifdef FP_HAVE_FLUIDSYNTH
    char err[256] = {};
    next.reset(synth_create_fluidsynth(path.c_str(), sr, err, sizeof(err)));
    if (!next) {
      error = err[0] ? err : "Cannot load SoundFont.";
      next.reset(synth_create_builtin(sr));
      if (next) loaded_type = INSTRUMENT_TYPE_BUILTIN;
    }
#else
    error = "This build has no FluidSynth support; using the built-in synthesizer.";
    next.reset(synth_create_builtin(sr));
    if (next) loaded_type = INSTRUMENT_TYPE_BUILTIN;
#endif
  } else if (type == INSTRUMENT_TYPE_BUILTIN) {
    next.reset(synth_create_builtin(sr));
  }

  {
    fp_lock lock;
    g_synth = std::move(next);
    // Remember the *requested* type/path so a failed SoundFont is not reloaded
    // on every settings change; the fallback synth is still used for sound.
    g_synth_type = type;
    g_synth_path = path;
    g_synth_rate = sr;
  }
  (void)loaded_type;
  g_error = error;
  if (!error.empty()) config_set_last_error(error.c_str());
}

void apply_audio() {
  int type = config_get_output_type();
  std::string device = config_get_output_device();
  int sr = config_get_sample_rate();
  int frames = config_get_buffer_size();

  bool params_match = (g_audio_real && device == g_audio_device && sr == g_audio_rate &&
                       frames == g_audio_frames && audio_running());

  if (type == OUTPUT_TYPE_NULL) {
    if (g_audio_real || !audio_running() || sr != g_audio_rate || frames != g_audio_frames) {
      audio_close();
      g_audio_real = false;
      g_audio_device.clear();
      g_audio_rate = sr;
      g_audio_frames = frames;
      audio_start_null(sr, frames);
    }
    return;
  }

  if (params_match) return;

  audio_close();
  g_audio_rate = sr;
  g_audio_frames = frames;

  std::string err;
  if (audio_open(device, sr, frames, &err)) {
    g_audio_real = true;
    g_audio_device = device;
    g_audio_error.clear();
  } else {
    g_audio_real = false;
    g_audio_error = err;
    audio_start_null(sr, frames);
  }
}

void apply_midi() {
  // External MIDI output follows the original behaviour: it is used when no
  // internal instrument is selected.
  std::string out = config_get_midi_output_port();
  bool want_out = config_get_instrument_type() == INSTRUMENT_TYPE_NONE && !out.empty();
  if (want_out) {
    if (!alsa_midi_output_open() || out != g_midi_out) {
      if (!alsa_midi_open_output(out)) g_error = "Cannot open MIDI output port.";
      g_midi_out = out;
    }
  } else if (alsa_midi_output_open()) {
    alsa_midi_close_output();
    g_midi_out.clear();
  }

  std::string in;
  int remap = 0;
  for (const auto& kv : config_get_midi_inputs()) {
    if (kv.second.enable) {
      in = kv.first;
      remap = kv.second.remap;
      break;
    }
  }

  if (!in.empty()) {
    if (in != g_midi_in) {
      alsa_midi_close_input();
      bool ok = alsa_midi_open_input(in, [in, remap](byte a, byte b, byte c, byte d) {
        midi_input_config_t cfg;
        int r = remap;
        if (config_get_midi_input_config(in, &cfg)) r = cfg.remap;
        if (r >= 1 && r <= 16) {
          byte op = a & 0xf0;
          byte ch = (byte)config_get_output_channel((byte)(r - 1));
          a = (byte)(op | ch);
          if ((op == 0x80 || op == 0x90) && config_get_midi_transpose()) {
            int note = b + config_get_key_signature();
            b = (byte)std::clamp(note, 0, 127);
          }
        }
        song_send_event(a, b, c, d, true);
      });
      g_midi_in = ok ? in : "";
      if (!ok) g_error = "Cannot open MIDI input port.";
    }
  } else if (alsa_midi_input_open()) {
    alsa_midi_close_input();
    g_midi_in.clear();
  }
}

}  // namespace

bool engine_init(const std::string& config_file) {
  if (g_inited) return true;

  audio_subsystem_init();
  config_init();

  g_synth.reset(synth_create_builtin(config_get_sample_rate()));
  g_synth_type = INSTRUMENT_TYPE_BUILTIN;
  g_synth_rate = config_get_sample_rate();

  midi_set_synth_sink(synth_sink);
  midi_set_external_sink(external_sink);
  audio_set_render_callback(render_callback);

  g_ready = true;
  config_load(config_file);
  keyboard_init();

  if (!audio_running()) engine_apply_config();

  g_inited = true;
  return true;
}

void engine_apply_config() {
  if (!g_ready) return;
  ensure_instrument();
  apply_audio();
  apply_midi();
}

void engine_shutdown() {
  if (!g_inited) return;
  g_inited = false;
  g_ready = false;

  alsa_midi_shutdown();
  audio_close();
  audio_subsystem_shutdown();
  midi_clear_sinks();
  {
    fp_lock lock;
    g_synth.reset();
  }
  config_shutdown();
}

bool engine_ready() { return g_inited; }

std::string engine_status() {
  std::string s = "Audio: " + engine_audio_status();
  s += "  |  Synth: " + engine_instrument_name();
  if (alsa_midi_output_open()) s += "  |  MIDI out: " + g_midi_out;
  if (alsa_midi_input_open()) s += "  |  MIDI in: " + g_midi_in;
  return s;
}

std::string engine_audio_status() {
  std::string s = audio_status();
  if (!g_audio_error.empty()) s += " (" + g_audio_error + ")";
  return s;
}

std::string engine_instrument_name() {
  fp_lock lock;
  if (g_synth) {
    if (g_synth->soundfont_name() && *g_synth->soundfont_name()) {
      const char* base = std::strrchr(g_synth->soundfont_name(), '/');
      return std::string(g_synth->name()) + ": " + (base ? base + 1 : g_synth->soundfont_name());
    }
    return g_synth->name();
  }
  return "None (MIDI output)";
}

std::string engine_error() { return g_error; }

std::vector<std::string> engine_audio_devices() { return audio_devices(); }
std::vector<std::string> engine_midi_output_ports() { return alsa_midi_output_ports(); }
std::vector<std::string> engine_midi_input_ports() { return alsa_midi_input_ports(); }

void engine_select_audio_device(const std::string& device) {
  config_set_output_device(device);
  config_set_output_type(OUTPUT_TYPE_PORTAUDIO);
  engine_apply_config();
}

void engine_select_instrument_builtin() {
  config_select_instrument(INSTRUMENT_TYPE_BUILTIN, "");
  engine_apply_config();
}

void engine_select_soundfont(const std::string& path) {
  config_select_instrument(INSTRUMENT_TYPE_SOUNDFONT, path);
  engine_apply_config();
}

void engine_select_midi_output(const std::string& port) {
  config_set_midi_output_port(port);
  engine_apply_config();
}

void engine_select_midi_input(const std::string& port, bool enable, int remap) {
  midi_input_config_t cfg;
  cfg.enable = enable;
  cfg.remap = remap;
  if (port.empty()) {
    std::vector<std::string> names;
    for (const auto& kv : config_get_midi_inputs()) names.push_back(kv.first);
    for (const auto& name : names) {
      midi_input_config_t disabled;
      disabled.enable = false;
      disabled.remap = 0;
      config_set_midi_input_config(name, disabled);
    }
  } else {
    config_set_midi_input_config(port, cfg);
  }
  engine_apply_config();
}

bool engine_export_wav(const std::string& path, std::string* error) {
  if (song_is_empty()) {
    if (error) *error = "There is nothing to export. Record or open a song first.";
    return false;
  }
  if (config_get_instrument_type() == INSTRUMENT_TYPE_NONE) {
    if (error) *error = "Select the built-in synthesizer or a SoundFont before exporting audio.";
    return false;
  }

  const int sr = config_get_sample_rate();
  std::string snapshot = config_save_keymap();
  uint32_t group = config_get_setting_group();

  audio_close();

  // Make sure an instrument exists at the current sample rate.
  ensure_instrument();
  {
    fp_lock lock;
    if (g_synth) g_synth->reset();
  }

  WavWriter writer;
  if (!writer.open(path, sr)) {
    if (error) *error = "Cannot open output file: " + path;
    engine_apply_config();
    return false;
  }

  const int block = 512;
  std::vector<float> buffer((size_t)block * 2);
  bool ok = true;

  song_start_playback();
  if (!song_is_playing()) {
    if (error) *error = "Playback could not be started.";
    writer.close();
    engine_apply_config();
    return false;
  }

  double limit_ms = std::max(60000.0, song_get_length() * 1.5 + 2000.0);

  double rendered_ms = 0;
  while (song_is_playing() && rendered_ms < limit_ms) {
    {
      fp_lock lock;
      song_update(1000.0 * block / sr);
      if (g_synth) {
        g_synth->set_master_volume(config_get_output_volume() / 100.0f);
        g_synth->render(buffer.data(), block);
      } else {
        std::memset(buffer.data(), 0, buffer.size() * sizeof(float));
      }
    }
    writer.write(buffer.data(), block);
    rendered_ms += 1000.0 * block / sr;
  }

  // tail so releases/reverb are not cut off
  for (int i = 0; i < sr; i += block) {
    {
      fp_lock lock;
      if (g_synth) {
        g_synth->set_master_volume(config_get_output_volume() / 100.0f);
        g_synth->render(buffer.data(), block);
      } else {
        std::memset(buffer.data(), 0, buffer.size() * sizeof(float));
      }
    }
    writer.write(buffer.data(), block);
  }
  ok = writer.close();

  {
    fp_lock lock;
    if (g_synth) g_synth->reset();
  }

  // Restore the key map that playback overwrote.
  if (!snapshot.empty()) {
    config_set_setting_group_count(1);
    config_clear_key_setting();
    config_parse_keymap(snapshot, 0, (int)0x01080000);
    config_set_setting_group(group);
  }

  engine_apply_config();

  if (!ok && error) *error = "Failed while writing the WAV file.";
  return ok;
}
