// PortAudio output (replaces DirectSound/WASAPI/ASIO from the Windows build).

#include "audio.h"

#include "sync.h"

#include <portaudio.h>

#ifdef FP_HAVE_PULSE
#include <pulse/error.h>
#include <pulse/simple.h>
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

std::function<void(float*, int)> g_render;
std::mutex g_render_mutex;
PaStream* g_stream = nullptr;
bool g_pa_initialized = false;
std::string g_device_name;
std::string g_error;
std::atomic<bool> g_running{false};

std::thread g_timer_thread;
std::atomic<bool> g_timer_running{false};

#ifdef FP_HAVE_PULSE
pa_simple* g_pulse_stream = nullptr;
std::thread g_pulse_thread;
std::atomic<bool> g_pulse_running{false};
std::string g_pulse_name;
std::string g_pulse_error;

std::function<void(float*, int)> current_render() {
  std::lock_guard<std::mutex> lock(g_render_mutex);
  return g_render;
}

void pulse_loop(int frames) {
  std::vector<float> buffer((size_t)frames * 2);
  while (g_pulse_running) {
    auto cb = current_render();
    if (cb) cb(buffer.data(), frames);
    else std::memset(buffer.data(), 0, buffer.size() * sizeof(float));
    int error = 0;
    if (pa_simple_write(g_pulse_stream, buffer.data(), buffer.size() * sizeof(float), &error) < 0) {
      g_pulse_error = pa_strerror(error);
      break;
    }
  }
  g_pulse_running = false;
}

bool open_pulse(int sample_rate, int frames, std::string* error) {
  pa_sample_spec ss;
  ss.format = PA_SAMPLE_FLOAT32LE;
  ss.rate = (uint32_t)sample_rate;
  ss.channels = 2;

  pa_buffer_attr attr;
  attr.maxlength = (uint32_t)-1;
  attr.tlength = (uint32_t)(frames * 2 * sizeof(float) * 3);
  attr.prebuf = (uint32_t)-1;
  attr.minreq = (uint32_t)(frames * 2 * sizeof(float));
  attr.fragsize = (uint32_t)-1;

  int error_code = 0;
  if (std::getenv("FP_AUDIO_DEBUG")) std::fprintf(stderr, "[audio] trying PulseAudio\n");
  g_pulse_stream = pa_simple_new(nullptr, "FreePiano", PA_STREAM_PLAYBACK, nullptr, "piano", &ss,
                                 nullptr, &attr, &error_code);
  if (!g_pulse_stream) {
    g_pulse_error = pa_strerror(error_code);
    if (std::getenv("FP_AUDIO_DEBUG")) std::fprintf(stderr, "[audio] pulse failed: %s\n", g_pulse_error.c_str());
    if (error) *error = "PulseAudio/PipeWire: " + g_pulse_error;
    return false;
  }
  g_pulse_name = "PulseAudio / PipeWire";
  g_pulse_error.clear();
  g_pulse_running = true;
  g_pulse_thread = std::thread(pulse_loop, frames);
  return true;
}
#endif

int pa_callback(const void* /*input*/, void* output, unsigned long frames,
                const PaStreamCallbackTimeInfo* /*time_info*/, PaStreamCallbackFlags /*flags*/,
                void* /*user*/) {
  float* out = static_cast<float*>(output);
  std::function<void(float*, int)> cb;
  {
    std::lock_guard<std::mutex> lock(g_render_mutex);
    cb = g_render;
  }
  if (cb) {
    cb(out, (int)frames);
  } else {
    std::memset(out, 0, sizeof(float) * frames * 2);
  }
  return paContinue;
}

void timer_loop(int sample_rate, int frames) {
  std::vector<float> scratch((size_t)frames * 2);
  auto last = std::chrono::steady_clock::now();
  double accumulator = 0.0;
  while (g_timer_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last).count();
    last = now;
    accumulator += elapsed * sample_rate;

    if (accumulator < frames) continue;

    std::function<void(float*, int)> cb;
    {
      std::lock_guard<std::mutex> lock(g_render_mutex);
      cb = g_render;
    }
    while (accumulator >= frames) {
      if (cb) cb(scratch.data(), frames);
      else std::memset(scratch.data(), 0, scratch.size() * sizeof(float));
      accumulator -= frames;
    }
  }
}

}  // namespace

bool audio_subsystem_init() {
  if (g_pa_initialized) return true;
  PaError err = Pa_Initialize();
  if (err != paNoError) {
    g_error = Pa_GetErrorText(err);
    return false;
  }
  g_pa_initialized = true;
  return true;
}

void audio_subsystem_shutdown() {
  audio_close();
  if (g_pa_initialized) {
    Pa_Terminate();
    g_pa_initialized = false;
  }
}

std::vector<std::string> audio_devices() {
  std::vector<std::string> result;
#ifdef FP_HAVE_PULSE
  result.push_back("PulseAudio / PipeWire (default)");
#endif
  if (!g_pa_initialized) return result;
  int count = Pa_GetDeviceCount();
  for (int i = 0; i < count; ++i) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (!info || info->maxOutputChannels <= 0) continue;
    const PaHostApiInfo* api = Pa_GetHostApiInfo(info->hostApi);
    std::string name = info->name ? info->name : "";
    if (api && api->name) name += std::string(" (") + api->name + ")";
    if (i == Pa_GetDefaultOutputDevice()) name += "  [default]";
    result.push_back(name);
  }
  return result;
}

bool audio_open(const std::string& device, int sample_rate, int frames, std::string* error) {
  if (std::getenv("FP_AUDIO_DEBUG")) std::fprintf(stderr, "[audio] open device='%s' sr=%d frames=%d\n", device.c_str(), sample_rate, frames);
  audio_close();

#ifdef FP_HAVE_PULSE
  if (device.rfind("PulseAudio", 0) == 0) {
    return open_pulse(sample_rate, frames, error);
  }
#endif

  if (!g_pa_initialized && !audio_subsystem_init()) {
    if (std::getenv("FP_AUDIO_DEBUG")) std::fprintf(stderr, "[audio] Pa init failed: %s\n", g_error.c_str());
    if (error) *error = g_error;
#ifdef FP_HAVE_PULSE
    if (device.empty() && open_pulse(sample_rate, frames, error)) return true;
#endif
    return false;
  }

  int device_index = Pa_GetDefaultOutputDevice();
  if (!device.empty()) {
    int found = -1;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
      const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
      if (!info || info->maxOutputChannels <= 0) continue;
      const PaHostApiInfo* api = Pa_GetHostApiInfo(info->hostApi);
      std::string name = info->name ? info->name : "";
      if (api && api->name) name += std::string(" (") + api->name + ")";
      if (i == Pa_GetDefaultOutputDevice()) name += "  [default]";
      if (name == device || (info->name && device == info->name)) {
        found = i;
        break;
      }
    }
    if (found < 0) {
      if (error) *error = "Audio device not found: " + device;
      return false;
    }
    device_index = found;
  }

  if (device_index < 0) {
    if (std::getenv("FP_AUDIO_DEBUG")) std::fprintf(stderr, "[audio] no PortAudio device (count=%d)\n", Pa_GetDeviceCount());
    if (error) *error = "No audio output device available.";
#ifdef FP_HAVE_PULSE
    if (device.empty() && open_pulse(sample_rate, frames, error)) return true;
#endif
    return false;
  }

  PaStreamParameters params;
  std::memset(&params, 0, sizeof(params));
  params.device = device_index;
  params.channelCount = 2;
  params.sampleFormat = paFloat32;
  const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index);
  params.suggestedLatency = info ? info->defaultLowOutputLatency : 0.02;
  params.hostApiSpecificStreamInfo = nullptr;

  PaError err = Pa_OpenStream(&g_stream, nullptr, &params, sample_rate, frames, paClipOff,
                              pa_callback, nullptr);
  if (err != paNoError) {
    g_stream = nullptr;
    if (std::getenv("FP_AUDIO_DEBUG")) std::fprintf(stderr, "[audio] Pa_OpenStream failed: %s\n", Pa_GetErrorText(err));
    if (error) *error = Pa_GetErrorText(err);
#ifdef FP_HAVE_PULSE
    if (device.empty() && open_pulse(sample_rate, frames, error)) return true;
#endif
    return false;
  }

  err = Pa_StartStream(g_stream);
  if (err != paNoError) {
    Pa_CloseStream(g_stream);
    g_stream = nullptr;
    if (error) *error = Pa_GetErrorText(err);
#ifdef FP_HAVE_PULSE
    if (device.empty() && open_pulse(sample_rate, frames, error)) return true;
#endif
    return false;
  }

  const PaDeviceInfo* opened = Pa_GetDeviceInfo(device_index);
  const PaHostApiInfo* api = opened ? Pa_GetHostApiInfo(opened->hostApi) : nullptr;
  g_device_name = opened && opened->name ? opened->name : "";
  if (api && api->name) g_device_name += std::string(" (") + api->name + ")";
  g_running = true;
  return true;
}

void audio_close() {
  g_timer_running = false;
  if (g_timer_thread.joinable()) g_timer_thread.join();

#ifdef FP_HAVE_PULSE
  g_pulse_running = false;
  if (g_pulse_thread.joinable()) g_pulse_thread.join();
  if (g_pulse_stream) {
    pa_simple_free(g_pulse_stream);
    g_pulse_stream = nullptr;
  }
  g_pulse_name.clear();
#endif

  if (g_stream) {
    Pa_StopStream(g_stream);
    Pa_CloseStream(g_stream);
    g_stream = nullptr;
  }
  g_running = false;
  g_device_name.clear();
}

bool audio_running() {
#ifdef FP_HAVE_PULSE
  if (g_pulse_running) return true;
#endif
  return g_running || g_timer_running;
}

const std::string& audio_current_device() {
#ifdef FP_HAVE_PULSE
  if (!g_pulse_name.empty()) return g_pulse_name;
#endif
  return g_device_name;
}

std::string audio_status() {
#ifdef FP_HAVE_PULSE
  if (g_pulse_running) return g_pulse_name + (g_pulse_error.empty() ? "" : " (" + g_pulse_error + ")");
#endif
  if (g_running) return g_device_name;
  if (g_timer_running) return "silent timer (no audio device)";
  return "stopped";
}

void audio_set_render_callback(std::function<void(float*, int)> callback) {
  std::lock_guard<std::mutex> lock(g_render_mutex);
  g_render = std::move(callback);
}

bool audio_start_null(int sample_rate, int frames) {
  audio_close();
  g_timer_running = true;
  g_timer_thread = std::thread(timer_loop, sample_rate, frames);
  return true;
}
