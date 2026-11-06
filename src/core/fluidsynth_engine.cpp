// Optional SoundFont backend (FluidSynth).  This replaces FreePiano's VSTi
// hosting: instead of loading Windows .dll instruments, Linux users can load
// any .sf2 SoundFont.

#include "synth.h"

#ifdef FP_HAVE_FLUIDSYNTH

#include <fluidsynth.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

class FluidSynthEngine : public Synth {
 public:
  FluidSynthEngine(double sample_rate, const char* soundfont, char* error, int error_size)
      : sr_(sample_rate) {
    settings_ = new_fluid_settings();
    if (!settings_) {
      set_error(error, error_size, "FluidSynth: cannot create settings.");
      return;
    }
    fluid_settings_setnum(settings_, "synth.sample-rate", sample_rate);
    fluid_settings_setint(settings_, "synth.threadsafe-api", 0);
    fluid_settings_setint(settings_, "synth.reverb.active", 1);
    fluid_settings_setint(settings_, "synth.chorus.active", 1);

    synth_ = new_fluid_synth(settings_);
    if (!synth_) {
      set_error(error, error_size, "FluidSynth: cannot create synthesizer.");
      return;
    }

    int id = fluid_synth_sfload(synth_, soundfont, 1);
    if (id == FLUID_FAILED) {
      set_error(error, error_size, std::string("Cannot load SoundFont: ") + soundfont);
      return;
    }
    sfont_id_ = id;
    fluid_synth_set_gain(synth_, 0.8f);
    sf_name_ = soundfont;
    ready_ = true;
  }

  ~FluidSynthEngine() override {
    if (synth_) delete_fluid_synth(synth_);
    if (settings_) delete_fluid_settings(settings_);
  }

  const char* name() const override { return "FluidSynth (SoundFont)"; }
  bool is_ready() const override { return ready_; }
  const char* soundfont_name() const override { return sf_name_.c_str(); }

  void note_on(int ch, int note, int velocity) override {
    if (!ready_ || ch < 0 || ch > 15) return;
    fluid_synth_noteon(synth_, ch, note, velocity);
  }
  void note_off(int ch, int note) override {
    if (!ready_ || ch < 0 || ch > 15) return;
    fluid_synth_noteoff(synth_, ch, note);
  }
  void control_change(int ch, int cc, int value) override {
    if (!ready_ || ch < 0 || ch > 15) return;
    fluid_synth_cc(synth_, ch, cc, value);
  }
  void program_change(int ch, int program) override {
    if (!ready_ || ch < 0 || ch > 15) return;
    fluid_synth_program_change(synth_, ch, program);
  }
  void pitch_bend(int ch, int value) override {
    if (!ready_ || ch < 0 || ch > 15) return;
    fluid_synth_pitch_bend(synth_, ch, value);
  }
  void channel_pressure(int ch, int value) override {
    if (!ready_ || ch < 0 || ch > 15) return;
    fluid_synth_channel_pressure(synth_, ch, value);
  }
  void all_notes_off() override {
    if (!ready_) return;
    for (int ch = 0; ch < 16; ++ch) fluid_synth_all_notes_off(synth_, ch);
  }
  void reset() override {
    if (!ready_) return;
    fluid_synth_system_reset(synth_);
  }
  void set_master_volume(float volume) override {
    if (!ready_) return;
    fluid_synth_set_gain(synth_, volume * 0.8f);
  }

  void render(float* out, int frames) override {
    if (!ready_) {
      std::memset(out, 0, sizeof(float) * frames * 2);
      return;
    }
    fluid_synth_write_float(synth_, frames, out, 0, 2, out, 1, 2);
  }

 private:
  static void set_error(char* error, int size, const std::string& text) {
    if (error && size > 0) std::snprintf(error, size, "%s", text.c_str());
  }

  double sr_;
  fluid_settings_t* settings_ = nullptr;
  fluid_synth_t* synth_ = nullptr;
  int sfont_id_ = -1;
  bool ready_ = false;
  std::string sf_name_;
};

}  // namespace

Synth* synth_create_fluidsynth(const char* soundfont_path, double sample_rate, char* error, int error_size) {
  if (error && error_size > 0) error[0] = 0;
  if (!soundfont_path || !*soundfont_path) {
    if (error && error_size > 0) std::snprintf(error, error_size, "No SoundFont selected.");
    return nullptr;
  }
  auto* engine = new FluidSynthEngine(sample_rate, soundfont_path, error, error_size);
  if (!engine->is_ready()) {
    delete engine;
    return nullptr;
  }
  return engine;
}

#endif  // FP_HAVE_FLUIDSYNTH
