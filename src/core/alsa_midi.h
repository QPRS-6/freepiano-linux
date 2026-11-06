#pragma once

#include "fp_types.h"

#include <functional>
#include <string>
#include <vector>

bool alsa_midi_available();
std::vector<std::string> alsa_midi_output_ports();
std::vector<std::string> alsa_midi_input_ports();

bool alsa_midi_open_output(const std::string& port);
void alsa_midi_close_output();
void alsa_midi_send(byte a, byte b, byte c, byte d);
bool alsa_midi_output_open();

using alsa_midi_input_callback_t = std::function<void(byte, byte, byte, byte)>;
bool alsa_midi_open_input(const std::string& port, alsa_midi_input_callback_t callback);
void alsa_midi_close_input();
bool alsa_midi_input_open();

void alsa_midi_shutdown();
