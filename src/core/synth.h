#pragma once

#include <cstdint>

// A MIDI-rendering backend (built-in synthesizer or SoundFont via FluidSynth).
// All calls happen under the engine lock; render() must not allocate.
class Synth {
 public:
  virtual ~Synth() = default;

  virtual const char* name() const = 0;
  virtual bool is_ready() const = 0;

  virtual void note_on(int ch, int note, int velocity) = 0;
  virtual void note_off(int ch, int note) = 0;
  virtual void control_change(int ch, int controller, int value) = 0;
  virtual void program_change(int ch, int program) = 0;
  virtual void pitch_bend(int ch, int value) = 0;  // 0..16383, centre 8192
  virtual void channel_pressure(int ch, int value) = 0;

  virtual void all_notes_off() = 0;
  virtual void reset() = 0;
  virtual void set_master_volume(float volume) = 0;

  // Fill frames * 2 floats (stereo interleaved).
  virtual void render(float* out, int frames) = 0;

  // SoundFont backends expose the loaded bank name (may be empty).
  virtual const char* soundfont_name() const { return ""; }
};

Synth* synth_create_builtin(double sample_rate);
#ifdef FP_HAVE_FLUIDSYNTH
// Returns nullptr and fills error when the SoundFont cannot be loaded.
Synth* synth_create_fluidsynth(const char* soundfont_path, double sample_rate, char* error, int error_size);
#endif
