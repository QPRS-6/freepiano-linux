#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// Minimal 16-bit PCM stereo WAV writer.
class WavWriter {
 public:
  ~WavWriter();
  bool open(const std::string& path, int sample_rate);
  void write(const float* interleaved, int frames);
  bool close();

 private:
  std::FILE* file_ = nullptr;
  int sample_rate_ = 48000;
  uint32_t data_bytes_ = 0;
  bool ok_ = true;
};
