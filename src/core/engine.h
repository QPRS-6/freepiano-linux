#pragma once

#include <string>
#include <vector>

bool engine_init(const std::string& config_file = "freepiano.cfg");
void engine_shutdown();

// Called by config_apply(); reloads instrument/audio/MIDI per the config.
void engine_apply_config();

bool engine_ready();
std::string engine_status();
std::string engine_audio_status();
std::string engine_instrument_name();
std::string engine_error();

std::vector<std::string> engine_audio_devices();
std::vector<std::string> engine_midi_output_ports();
std::vector<std::string> engine_midi_input_ports();

void engine_select_audio_device(const std::string& device);
void engine_select_instrument_builtin();
void engine_select_soundfont(const std::string& path);
void engine_select_midi_output(const std::string& port);
void engine_select_midi_input(const std::string& port, bool enable, int remap);

bool engine_export_wav(const std::string& path, std::string* error);
