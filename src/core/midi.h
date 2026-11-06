#pragma once

#include "fp_types.h"

#include <functional>

// MIDI event fan-out.  The engine installs a synth sink and (optionally) an
// external ALSA sequencer sink; both receive the same channel messages.
using midi_sink_t = std::function<void(byte, byte, byte, byte)>;

void midi_output_event(byte a, byte b, byte c, byte d);
void midi_reset();

byte midi_get_note_status(byte ch, byte note);
byte midi_get_note_pressure(byte ch, byte note);

// Convenience queries for the piano display.
int midi_note_velocity(int note);  // 0 when the note is not sounding
bool midi_any_note_active(int note);

void midi_set_synth_sink(midi_sink_t sink);
void midi_set_external_sink(midi_sink_t sink);
void midi_clear_sinks();
