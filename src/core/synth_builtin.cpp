// Built-in polyphonic software synthesizer.
//
// FreePiano itself relies on Windows VSTi instruments (mdaPiano by default).
// VST binaries cannot be used on Linux, so the port ships a small self-contained
// GM-flavoured synthesizer: 12 instrument models driven by GM program numbers,
// per-channel volume/expression/pan, sustain pedal, pitch bend and velocity.

#include "synth.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kMaxVoices = 64;
constexpr int kMaxPartials = 8;
constexpr int kSineSize = 2048;

const float* sine_table() {
  static float table[kSineSize + 1];
  static bool init = [] {
    for (int i = 0; i <= kSineSize; ++i) {
      table[i] = (float)std::sin(2.0 * M_PI * i / kSineSize);
    }
    return true;
  }();
  (void)init;
  return table;
}

inline float fast_sine(double phase) {
  const float* t = sine_table();
  double p = phase * kSineSize;
  int i = (int)p;
  float frac = (float)(p - i);
  return t[i] + (t[i + 1] - t[i]) * frac;
}

inline float soft_clip(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct ModelSpec {
  int partials;
  double ratio[kMaxPartials];
  double amp[kMaxPartials];
  double attack;
  double decay;    // time constant in seconds (0 = hold)
  double sustain;  // 0..1 level
  double release;
  double vibrato;  // depth in fraction of a semitone-ish
  double noise;
  double decay_scale;  // per-partial extra decay (1/s per partial index)
};

const ModelSpec kModels[] = {
  // 0 acoustic piano
  { 6, {1, 2, 3, 4, 5, 6, 0, 0}, {1.0, 0.55, 0.32, 0.18, 0.10, 0.06, 0, 0},
    0.004, 2.4, 0.0, 0.16, 0.0, 0.0, 1.4 },
  // 1 electric piano
  { 4, {1, 2, 3, 4, 0, 0, 0, 0}, {1.0, 0.32, 0.12, 0.05, 0, 0, 0, 0},
    0.003, 2.0, 0.0, 0.22, 0.0, 0.0, 1.0 },
  // 2 organ
  { 6, {1, 2, 3, 4, 6, 8, 0, 0}, {0.85, 0.55, 0.38, 0.28, 0.18, 0.11, 0, 0},
    0.018, 0.0, 1.0, 0.055, 0.0, 0.0, 0.0 },
  // 3 strings
  { 8, {1, 2, 3, 4, 5, 6, 7, 8}, {1.0, 0.5, 0.33, 0.25, 0.2, 0.16, 0.14, 0.12},
    0.16, 0.0, 0.82, 0.35, 0.0035, 0.0, 0.0 },
  // 4 pad
  { 5, {1, 2, 3, 4, 6, 0, 0, 0}, {1.0, 0.6, 0.34, 0.2, 0.12, 0, 0, 0},
    0.45, 0.0, 0.8, 0.7, 0.0025, 0.0, 0.0 },
  // 5 brass
  { 8, {1, 2, 3, 4, 5, 6, 7, 8}, {1.0, 0.62, 0.42, 0.3, 0.22, 0.16, 0.12, 0.09},
    0.035, 0.0, 0.78, 0.16, 0.0015, 0.0, 0.0 },
  // 6 flute
  { 2, {1, 2, 0, 0, 0, 0, 0, 0}, {1.0, 0.06, 0, 0, 0, 0, 0, 0},
    0.055, 0.0, 0.9, 0.12, 0.004, 0.05, 0.0 },
  // 7 bell
  { 4, {1, 2.76, 5.4, 8.93, 0, 0, 0, 0}, {1.0, 0.5, 0.24, 0.11, 0, 0, 0, 0},
    0.001, 1.6, 0.0, 0.75, 0.0, 0.0, 2.0 },
  // 8 plucked / guitar
  { 6, {1, 2, 3, 4, 5, 6, 0, 0}, {1.0, 0.45, 0.25, 0.15, 0.09, 0.05, 0, 0},
    0.002, 0.55, 0.0, 0.1, 0.0, 0.02, 2.4 },
  // 9 bass
  { 4, {1, 2, 3, 4, 0, 0, 0, 0}, {1.0, 0.5, 0.2, 0.08, 0, 0, 0, 0},
    0.004, 1.2, 0.0, 0.11, 0.0, 0.0, 1.6 },
  // 10 lead
  { 4, {1, 3, 5, 7, 0, 0, 0, 0}, {1.0, 0.5, 0.34, 0.24, 0, 0, 0, 0},
    0.006, 0.0, 0.85, 0.12, 0.005, 0.0, 0.0 },
  // 11 sound effect / percussion
  { 3, {1, 2.4, 4.9, 0, 0, 0, 0, 0}, {1.0, 0.5, 0.3, 0, 0, 0, 0, 0},
    0.001, 0.9, 0.0, 0.4, 0.0, 0.15, 2.0 },
};

int model_for_program(int p) {
  if (p < 0) p = 0;
  if (p < 8) return 0;
  if (p < 16) return 1;
  if (p < 24) return 2;
  if (p < 32) return 8;
  if (p < 40) return 9;
  if (p < 56) return 3;
  if (p < 64) return 5;
  if (p < 72) return 5;
  if (p < 80) return 6;
  if (p < 88) return 10;
  if (p < 96) return 4;
  if (p < 104) return 7;
  if (p < 112) return 8;
  if (p < 120) return 7;
  return 11;
}

struct Voice {
  bool active = false;
  bool key_down = false;
  bool deferred = false;  // released while the sustain pedal is down
  bool releasing = false;
  bool attacking = true;
  int channel = 0;
  int note = 60;
  int model = 0;

  double f0 = 440.0;
  double phase[kMaxPartials] = {};
  double amp[kMaxPartials] = {};
  double pdecay[kMaxPartials] = {};
  double ratio[kMaxPartials] = {};
  double vibrato = 0.0;

  double env = 0.0;
  double attack_inc = 0.0;
  double decay_mul = 0.0;
  double sustain = 0.0;
  double release_mul = 0.0;

  double gain = 0.0;
  double pan = 0.5;
  double lfo = 0.0;
  double lfo_inc = 0.0;
  double age = 0.0;
  double noise_mix = 0.0;
  uint32_t rng = 0x1234567u;
};

struct ChannelState {
  int program = 0;
  int volume = 100;
  int expression = 127;
  int pan = 64;
  int sustain = 0;
  int bend = 8192;
  int modulation = 0;
};

}  // namespace

class BuiltinSynth : public Synth {
 public:
  explicit BuiltinSynth(double sample_rate) : sr_(sample_rate), dt_(1.0 / sample_rate) {
    for (int ch = 0; ch < 16; ++ch) {
      channels_[ch].program = (ch == 9) ? 0 : 0;
      channels_[ch].bend = 8192;
    }
    master_ = 0.8f;
  }

  const char* name() const override { return "Built-in synthesizer"; }
  bool is_ready() const override { return true; }

  void note_on(int ch, int note, int velocity) override {
    if (velocity <= 0) {
      note_off(ch, note);
      return;
    }
    if (ch < 0 || ch > 15 || note < 0 || note > 127) return;
    velocity = std::clamp(velocity, 1, 127);

    // avoid stacking the same note
    for (auto& v : voices_) {
      if (v.active && v.channel == ch && v.note == note && v.key_down) {
        v.key_down = false;
        v.releasing = true;
      }
    }

    Voice* v = allocate_voice();
    if (!v) return;
    *v = Voice();
    v->active = true;
    v->key_down = true;
    v->channel = ch;
    v->note = note;
    v->model = model_for_program(channels_[ch].program);
    v->f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);

    const ModelSpec& spec = kModels[v->model];
    double vel = velocity / 127.0;
    double brightness = 0.55 + 0.45 * vel;
    double amp_sum = 0.0;
    double decay_tc = spec.decay;
    if (v->model == 0) {
      decay_tc = 2.6 * std::pow(0.80, (note - 60) / 12.0);
      decay_tc = std::clamp(decay_tc, 0.5, 6.0);
    }

    for (int k = 0; k < kMaxPartials; ++k) {
      v->ratio[k] = spec.ratio[k];
      double a = k < spec.partials ? spec.amp[k] : 0.0;
      if (k > 0) a *= brightness;
      v->amp[k] = a;
      amp_sum += a;
      v->phase[k] = 0.0;
      double rate = spec.decay_scale * k;
      if (decay_tc > 0 && k > 0) rate += 0.15 / decay_tc;
      v->pdecay[k] = std::exp(-rate * dt_);
    }
    if (amp_sum <= 0.0) amp_sum = 1.0;
    for (int k = 0; k < kMaxPartials; ++k) v->amp[k] /= amp_sum;

    v->sustain = spec.sustain;
    v->attack_inc = 1.0 / std::max(0.0008, spec.attack) * dt_;
    v->decay_mul = spec.decay > 0 ? std::exp(-dt_ / decay_tc) : 1.0;
    v->release_mul = std::exp(-dt_ / std::max(0.02, spec.release));
    v->gain = 0.55 * std::pow(vel, 1.25);
    v->pan = std::clamp(channels_[ch].pan / 127.0, 0.0, 1.0);
    v->vibrato = spec.vibrato + channels_[ch].modulation / 127.0 * 0.006;
    v->lfo_inc = 5.2 * dt_;
    v->noise_mix = spec.noise;
    v->env = 0.0;
    v->attacking = true;
    v->releasing = false;
    v->deferred = false;
  }

  void note_off(int ch, int note) override {
    if (ch < 0 || ch > 15) return;
    bool sustained = channels_[ch].sustain >= 64;
    for (auto& v : voices_) {
      if (v.active && v.channel == ch && v.note == note && v.key_down) {
        v.key_down = false;
        if (sustained) {
          v.deferred = true;
        } else {
          v.releasing = true;
        }
      }
    }
  }

  void control_change(int ch, int controller, int value) override {
    if (ch < 0 || ch > 15) return;
    value = std::clamp(value, 0, 127);
    ChannelState& c = channels_[ch];
    switch (controller) {
      case 1: c.modulation = value; break;
      case 7: c.volume = value; break;
      case 10: c.pan = value; break;
      case 11: c.expression = value; break;
      case 64:
        c.sustain = value;
        if (value < 64) {
          for (auto& v : voices_) {
            if (v.active && v.channel == ch && v.deferred) {
              v.deferred = false;
              v.releasing = true;
            }
          }
        }
        break;
      case 120:  // all sound off
      case 123:  // all notes off
        for (auto& v : voices_) {
          if (v.active && v.channel == ch) {
            v.key_down = false;
            v.deferred = false;
            v.releasing = true;
          }
        }
        break;
      case 121:  // reset all controllers
        c.volume = 100;
        c.expression = 127;
        c.pan = 64;
        c.sustain = 0;
        c.bend = 8192;
        c.modulation = 0;
        break;
      default: break;
    }
  }

  void program_change(int ch, int program) override {
    if (ch >= 0 && ch < 16) channels_[ch].program = std::clamp(program, 0, 127);
  }

  void pitch_bend(int ch, int value) override {
    if (ch >= 0 && ch < 16) channels_[ch].bend = std::clamp(value, 0, 16383);
  }

  void channel_pressure(int ch, int /*value*/) override { (void)ch; }

  void all_notes_off() override {
    for (auto& v : voices_) {
      if (v.active) {
        v.key_down = false;
        v.deferred = false;
        v.releasing = true;
        v.release_mul = std::exp(-dt_ / 0.03);
      }
    }
  }

  void reset() override {
    for (auto& v : voices_) v.active = false;
    for (int ch = 0; ch < 16; ++ch) {
      channels_[ch] = ChannelState();
      channels_[ch].bend = 8192;
    }
  }

  void set_master_volume(float volume) override { master_ = std::clamp(volume, 0.0f, 2.0f); }

  void render(float* out, int frames) override {
    std::memset(out, 0, sizeof(float) * frames * 2);

    for (auto& v : voices_) {
      if (!v.active) continue;

      const float* table = sine_table();
      ChannelState& cs = channels_[v.channel];
      double chan_gain = (cs.volume / 127.0) * (cs.expression / 127.0);
      if (chan_gain < 0.0001) {
        continue;
      }
      double bend = (cs.bend - 8192) / 8192.0 * 2.0;  // +/- 2 semitones
      double bend_factor = std::pow(2.0, bend / 12.0);
      double pan = std::clamp(cs.pan / 127.0, 0.0, 1.0);
      double gl = std::cos(pan * M_PI / 2.0);
      double gr = std::sin(pan * M_PI / 2.0);

      double base_inc = v.f0 * bend_factor / sr_;
      double lfo = v.lfo;
      double lfo_inc = v.lfo_inc;

      for (int i = 0; i < frames; ++i) {
        if (v.attacking) {
          v.env += v.attack_inc;
          if (v.env >= 1.0) {
            v.env = 1.0;
            v.attacking = false;
          }
        } else if (v.releasing) {
          v.env *= v.release_mul;
          if (v.env < 0.00035) {
            v.active = false;
            break;
          }
        } else {
          v.env = v.sustain + (v.env - v.sustain) * v.decay_mul;
        }

        double vib = v.vibrato * std::sin(lfo * 2.0 * M_PI);
        lfo += lfo_inc;
        if (lfo >= 1.0) lfo -= 1.0;

        double s = 0.0;
        for (int k = 0; k < kMaxPartials; ++k) {
          double a = v.amp[k];
          if (a <= 0.0) continue;
          v.amp[k] = a * v.pdecay[k];
          v.phase[k] += base_inc * v.ratio[k] * (1.0 + vib);
          if (v.phase[k] >= 1.0) v.phase[k] -= std::floor(v.phase[k]);
          double p = v.phase[k] * kSineSize;
          int idx = (int)p;
          float f = (float)(p - idx);
          s += (table[idx] + (table[idx + 1] - table[idx]) * f) * a;
        }

        if (v.noise_mix > 0.0) {
          v.rng = v.rng * 1664525u + 1013904223u;
          s += ((v.rng >> 9) / 8388608.0 - 1.0) * v.noise_mix;
        }

        double sample = s * v.env * v.gain * chan_gain;
        out[i * 2] += (float)(sample * gl);
        out[i * 2 + 1] += (float)(sample * gr);
        ++v.age;
      }
      v.lfo = lfo;
    }

    // master gain + soft limiting
    float g = master_ * 0.85f;
    for (int i = 0; i < frames * 2; ++i) out[i] = soft_clip(out[i] * g);
  }

 private:
  Voice* allocate_voice() {
    for (auto& v : voices_) {
      if (!v.active) return &v;
    }
    // steal the quietest/oldest voice
    Voice* best = &voices_[0];
    double best_score = 1e9;
    for (auto& v : voices_) {
      double score = v.env * (v.key_down ? 1.5 : 0.6);
      if (score < best_score) {
        best_score = score;
        best = &v;
      }
    }
    return best;
  }

  double sr_;
  double dt_;
  float master_ = 0.8f;
  ChannelState channels_[16];
  Voice voices_[kMaxVoices];
};

Synth* synth_create_builtin(double sample_rate) { return new BuiltinSynth(sample_rate); }
