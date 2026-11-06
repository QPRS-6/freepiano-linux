// Built-in port of "mda Piano" by Paul Kellett (MIT licence).
//
// This is the VSTi that the Windows FreePiano distribution uses by default
// (data/vsti/mdaPiano.dll).  Porting its DSP and sample table makes the Linux
// build sound like FreePiano out of the box, without VST, Wine or an external
// SoundFont.
//
// Original: Copyright (c) 1999-2000 Paul Kellett (maxim digital audio).
// Released under the MIT licence (or GPL-2.0-or-later); see
// LICENSES/mdaPiano-MIT.txt.

#include "synth.h"

#include "mda_piano_data.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

constexpr int kNumVoices = 32;
constexpr int kNumPrograms = 8;
constexpr int kNumParams = 12;
constexpr int kSustainNote = 128;
constexpr float kSilence = 0.0001f;

struct Voice {
  int32_t delta = 0;  // sample playback
  int32_t frac = 0;
  int32_t pos = 0;
  int32_t end = 0;
  int32_t loop = 0;

  float env = 0.0f;
  float dec = 0.99f;

  float f0 = 0.0f;  // first-order low-pass
  float f1 = 0.0f;
  float ff = 0.0f;

  float outl = 0.0f;
  float outr = 0.0f;
  int32_t note = 0;
};

struct KeyGroup {
  int32_t root;
  int32_t high;
  int32_t pos;
  int32_t end;
  int32_t loop;
};

struct Program {
  const char* name;
  float param[kNumParams];
};

const Program kPrograms[kNumPrograms] = {
    {"mda Piano", {0.500f, 0.500f, 0.500f, 0.5f, 0.803f, 0.251f, 0.376f, 0.500f, 0.330f, 0.500f, 0.246f, 0.500f}},
    {"Plain Piano", {0.500f, 0.500f, 0.500f, 0.5f, 0.751f, 0.000f, 0.452f, 0.000f, 0.000f, 0.500f, 0.000f, 0.500f}},
    {"Compressed Piano", {0.902f, 0.399f, 0.623f, 0.5f, 1.000f, 0.331f, 0.299f, 0.499f, 0.330f, 0.500f, 0.000f, 0.500f}},
    {"Dance Piano", {0.399f, 0.251f, 1.000f, 0.5f, 0.672f, 0.124f, 0.127f, 0.249f, 0.330f, 0.500f, 0.283f, 0.667f}},
    {"Concert Piano", {0.648f, 0.500f, 0.500f, 0.5f, 0.298f, 0.602f, 0.550f, 0.850f, 0.356f, 0.500f, 0.339f, 0.660f}},
    {"Dark Piano", {0.500f, 0.602f, 0.000f, 0.5f, 0.304f, 0.200f, 0.336f, 0.651f, 0.330f, 0.500f, 0.317f, 0.500f}},
    {"School Piano", {0.450f, 0.598f, 0.626f, 0.5f, 0.603f, 0.500f, 0.174f, 0.331f, 0.330f, 0.500f, 0.421f, 0.801f}},
    {"Broken Piano", {0.050f, 0.957f, 0.500f, 0.5f, 0.299f, 1.000f, 0.000f, 0.500f, 0.330f, 0.450f, 0.718f, 0.000f}},
};

class MdaPianoSynth : public Synth {
 public:
  explicit MdaPianoSynth(double sample_rate) : fs_((float)sample_rate), ifs_(1.0f / (float)sample_rate) {
    cmax_ = fs_ > 64000.0f ? 0xFF : 0x7F;
    comb_.assign(256, 0.0f);
    init_keygroups();
    for (auto& v : voices_) {
      v.env = 0.0f;
      v.dec = 0.99f;
    }
    update();
  }

  const char* name() const override { return "mdaPiano (FreePiano default)"; }
  bool is_ready() const override { return true; }

  void note_on(int /*ch*/, int note, int velocity) override { note_on_impl(note, velocity); }
  void note_off(int /*ch*/, int note) override { note_on_impl(note, 0); }

  void control_change(int /*ch*/, int controller, int value) override {
    value = std::clamp(value, 0, 127);
    switch (controller) {
      case 0x01:  // mod wheel
      case 0x43:  // soft pedal
        muff_ = 0.01f * (float)((127 - value) * (127 - value));
        break;
      case 0x07:  // volume
        volume_ = 0.00002f * (float)(value * value);
        break;
      case 0x40:  // sustain pedal
      case 0x42:  // sostenuto
        sustain_ = value & 0x40;
        if (sustain_ == 0) note_on_impl(kSustainNote, 0);
        break;
      default:
        if (controller > 0x7A) {
          for (auto& v : voices_) v.dec = 0.99f;
          sustain_ = 0;
          muff_ = 160.0f;
        }
        break;
    }
  }

  void program_change(int /*ch*/, int program) override {
    if (program >= 0 && program < kNumPrograms) set_program(program);
  }

  void pitch_bend(int /*ch*/, int /*value*/) override {}  // not supported by mdaPiano either
  void channel_pressure(int /*ch*/, int /*value*/) override {}

  void all_notes_off() override {
    for (auto& v : voices_) v.dec = 0.99f;
    sustain_ = 0;
    muff_ = 160.0f;
  }

  void reset() override {
    for (auto& v : voices_) {
      v = Voice();
      v.dec = 0.99f;
    }
    active_voices_ = 0;
    cpos_ = 0;
    std::fill(comb_.begin(), comb_.end(), 0.0f);
    volume_ = 0.2f;
    muff_ = 160.0f;
    sustain_ = 0;
    set_program(0);
  }

  void set_master_volume(float volume) override { master_ = std::clamp(volume, 0.0f, 2.0f); }

  void render(float* out, int frames) override {
    for (int n = 0; n < frames; ++n) {
      float l = 0.0f;
      float r = 0.0f;

      for (int v = 0; v < active_voices_; ++v) {
        Voice& V = voices_[v];
        V.frac += V.delta;
        V.pos += V.frac >> 16;
        V.frac &= 0xFFFF;
        if (V.pos > V.end) V.pos -= V.loop;

        int32_t i = (int32_t)pianoData[V.pos] +
                    ((V.frac * (pianoData[V.pos + 1] - pianoData[V.pos])) >> 16);
        float x = V.env * (float)i / 32768.0f;

        V.env *= V.dec;
        V.f0 += V.ff * (x + V.f1 - V.f0);
        V.f1 = x;

        l += V.outl * V.f0;
        r += V.outr * V.f0;
      }

      comb_[cpos_] = l + r;
      cpos_ = (cpos_ + 1) & cmax_;
      float x = cdep_ * comb_[cpos_];

      out[n * 2] = std::clamp((l + x) * master_, -1.0f, 1.0f);
      out[n * 2 + 1] = std::clamp((r - x) * master_, -1.0f, 1.0f);
    }

    for (int v = 0; v < active_voices_; ++v) {
      if (voices_[v].env < kSilence) voices_[v] = voices_[--active_voices_];
    }
  }

 private:
  void init_keygroups() {
    kgrp_[0] = {36, 37, 0, 36275, 14774};
    kgrp_[1] = {40, 41, 36278, 83135, 16268};
    kgrp_[2] = {43, 45, 83137, 146756, 33541};
    kgrp_[3] = {48, 49, 146758, 204997, 21156};
    kgrp_[4] = {52, 53, 204999, 244908, 17191};
    kgrp_[5] = {55, 57, 244910, 290978, 23286};
    kgrp_[6] = {60, 61, 290980, 342948, 18002};
    kgrp_[7] = {64, 65, 342950, 391750, 19746};
    kgrp_[8] = {67, 69, 391752, 436915, 22253};
    kgrp_[9] = {72, 73, 436917, 468807, 8852};
    kgrp_[10] = {76, 77, 468809, 492772, 9693};
    kgrp_[11] = {79, 81, 492774, 532293, 10596};
    kgrp_[12] = {84, 85, 532295, 560192, 6011};
    kgrp_[13] = {88, 89, 560194, 574121, 3414};
    kgrp_[14] = {93, 999, 574123, 586343, 2399};
  }

  void set_program(int program) {
    current_program_ = std::clamp(program, 0, kNumPrograms - 1);
    update();
  }

  void update() {
    const float* p = kPrograms[current_program_].param;
    size_ = (int)(12.0f * p[2] - 6.0f);
    sizevel_ = 0.12f * p[3];
    muffvel_ = p[5] * p[5] * 5.0f;

    velsens_ = 1.0f + p[6] + p[6];
    if (p[6] < 0.25f) velsens_ -= 0.75f - 3.0f * p[6];

    fine_ = p[9] - 0.5f;
    random_ = 0.077f * p[10] * p[10];
    stretch_ = 0.000434f * (p[11] - 0.5f);

    cdep_ = p[7] * p[7];
    trim_ = 1.50f - 0.79f * cdep_;
    width_ = 0.04f * p[7];
    if (width_ > 0.03f) width_ = 0.03f;

    poly_ = 8 + (int)(24.9f * p[8]);
  }

  void note_on_impl(int note, int velocity) {
    const float* param = kPrograms[current_program_].param;
    float l = 99.0f;
    int vl = 0;

    if (velocity > 0) {
      if (active_voices_ < poly_) {
        vl = active_voices_;
        ++active_voices_;
      } else {
        for (int v = 0; v < poly_; ++v) {
          if (voices_[v].env < l) {
            l = voices_[v].env;
            vl = v;
          }
        }
      }

      int k = (note - 60) * (note - 60);
      l = fine_ + random_ * ((float)(k % 13) - 6.5f);
      if (note > 60) l += stretch_ * (float)k;

      int s = size_;
      if (velocity > 40) s += (int)(sizevel_ * (float)(velocity - 40));

      int group = 0;
      while (note > (kgrp_[group].high + s) && group < 14) ++group;

      l += (float)(note - kgrp_[group].root);
      l = 22050.0f * ifs_ * (float)std::exp(0.05776226505 * l);

      Voice& V = voices_[vl];
      V.delta = (int32_t)(65536.0f * l);
      V.frac = 0;
      V.pos = kgrp_[group].pos;
      V.end = kgrp_[group].end;
      V.loop = kgrp_[group].loop;

      V.env = (0.5f + velsens_) * (float)std::pow(0.0078f * velocity, velsens_);

      l = 50.0f + param[4] * param[4] * muff_ + muffvel_ * (float)(velocity - 64);
      if (l < (55.0f + 0.25f * (float)note)) l = 55.0f + 0.25f * (float)note;
      if (l > 210.0f) l = 210.0f;
      V.ff = l * l * ifs_;
      V.f0 = V.f1 = 0.0f;

      V.note = note;
      int pan_note = note;
      if (pan_note < 12) pan_note = 12;
      if (pan_note > 108) pan_note = 108;
      l = volume_ * trim_;
      V.outr = l + l * width_ * (float)(pan_note - 60);
      V.outl = l + l - V.outr;

      int decay_note = note;
      if (decay_note < 44) decay_note = 44;
      l = 2.0f * param[0];
      if (l < 1.0f) l += 0.25f - 0.5f * param[0];
      V.dec = (float)std::exp(-ifs_ * std::exp(-0.6 + 0.033 * (double)decay_note - l));
    } else {
      for (int v = 0; v < kNumVoices; ++v) {
        if (voices_[v].note == note) {
          if (sustain_ == 0) {
            if (note < 94 || note == kSustainNote) {
              voices_[v].dec = (float)std::exp(
                  -ifs_ * std::exp(2.0 + 0.017 * (double)note - 2.0 * (double)param[1]));
            }
          } else {
            voices_[v].note = kSustainNote;
          }
        }
      }
    }
  }

  float fs_;
  float ifs_;
  int current_program_ = 0;
  KeyGroup kgrp_[15];
  Voice voices_[kNumVoices];
  int active_voices_ = 0;
  int poly_ = 16;
  int cpos_ = 0;
  int cmax_ = 0x7F;
  int sustain_ = 0;
  int size_ = 0;
  float cdep_ = 0.0f;
  float width_ = 0.0f;
  float trim_ = 1.0f;
  float fine_ = 0.0f;
  float random_ = 0.0f;
  float stretch_ = 0.0f;
  float muff_ = 160.0f;
  float muffvel_ = 0.0f;
  float sizevel_ = 0.0f;
  float velsens_ = 1.0f;
  float volume_ = 0.2f;
  float master_ = 1.0f;
  std::vector<float> comb_;
};

}  // namespace

Synth* synth_create_mda_piano(double sample_rate) { return new MdaPianoSynth(sample_rate); }
