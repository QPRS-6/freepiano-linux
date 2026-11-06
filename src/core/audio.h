#pragma once

#include <functional>
#include <string>
#include <vector>

bool audio_subsystem_init();
void audio_subsystem_shutdown();

std::vector<std::string> audio_devices();
bool audio_open(const std::string& device, int sample_rate, int frames, std::string* error);
void audio_close();
bool audio_running();
const std::string& audio_current_device();
std::string audio_status();

void audio_set_render_callback(std::function<void(float*, int)> callback);

// Fallback clock/render loop when no real audio device is available.
bool audio_start_null(int sample_rate, int frames);
