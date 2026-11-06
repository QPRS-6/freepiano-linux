#include "wav_export.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
void put_u32(std::FILE* f, uint32_t v) {
  uint8_t b[4] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff), (uint8_t)((v >> 16) & 0xff),
                  (uint8_t)((v >> 24) & 0xff)};
  std::fwrite(b, 1, 4, f);
}
void put_u16(std::FILE* f, uint16_t v) {
  uint8_t b[2] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff)};
  std::fwrite(b, 1, 2, f);
}
}  // namespace

WavWriter::~WavWriter() {
  if (file_) std::fclose(file_);
}

bool WavWriter::open(const std::string& path, int sample_rate) {
  file_ = std::fopen(path.c_str(), "wb");
  if (!file_) return false;
  sample_rate_ = sample_rate;

  std::fwrite("RIFF", 1, 4, file_);
  put_u32(file_, 0);  // patched on close
  std::fwrite("WAVE", 1, 4, file_);
  std::fwrite("fmt ", 1, 4, file_);
  put_u32(file_, 16);
  put_u16(file_, 1);  // PCM
  put_u16(file_, 2);  // channels
  put_u32(file_, (uint32_t)sample_rate_);
  put_u32(file_, (uint32_t)sample_rate_ * 4);
  put_u16(file_, 4);
  put_u16(file_, 16);
  std::fwrite("data", 1, 4, file_);
  put_u32(file_, 0);
  data_bytes_ = 0;
  return true;
}

void WavWriter::write(const float* interleaved, int frames) {
  if (!file_ || !ok_) return;
  for (int i = 0; i < frames * 2; ++i) {
    float x = interleaved[i];
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    int16_t s = (int16_t)std::lrint(x * 32767.0f);
    uint8_t b[2] = {(uint8_t)(s & 0xff), (uint8_t)((s >> 8) & 0xff)};
    if (std::fwrite(b, 1, 2, file_) != 2) {
      ok_ = false;
      return;
    }
    data_bytes_ += 2;
  }
}

bool WavWriter::close() {
  if (!file_) return false;
  bool ok = ok_;
  std::fseek(file_, 4, SEEK_SET);
  put_u32(file_, 36 + data_bytes_);
  std::fseek(file_, 40, SEEK_SET);
  put_u32(file_, data_bytes_);
  std::fclose(file_);
  file_ = nullptr;
  return ok;
}
