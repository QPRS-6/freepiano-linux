// Port of FreePiano's MIDI routing (src/midi.cpp).

#include "midi.h"

#include "config.h"
#include "song.h"
#include "sync.h"

#include <cstring>

static byte note_states[16][128] = {};
static byte note_pressure[16][128] = {};

static midi_sink_t synth_sink;
static midi_sink_t external_sink;

void midi_set_synth_sink(midi_sink_t sink) { fp_lock lock; synth_sink = std::move(sink); }
void midi_set_external_sink(midi_sink_t sink) { fp_lock lock; external_sink = std::move(sink); }
void midi_clear_sinks() {
  fp_lock lock;
  synth_sink = nullptr;
  external_sink = nullptr;
}

byte midi_get_note_status(byte ch, byte note) {
  return note_states[ch & 0x0f][note & 0x7f];
}

byte midi_get_note_pressure(byte ch, byte note) {
  return note_pressure[ch & 0x0f][note & 0x7f];
}

int midi_note_velocity(int note) {
  if (note < 0 || note > 127) return 0;
  int velocity = 0;
  for (int ch = 0; ch < 16; ++ch) {
    velocity = std::max<int>(velocity, note_states[ch][note]);
  }
  return velocity;
}

bool midi_any_note_active(int note) { return midi_note_velocity(note) > 0; }

void midi_output_event(byte a, byte b, byte c, byte d) {
  fp_lock lock;

  // A note-on with a tiny velocity is treated as a note-off (as in the original).
  if ((a & 0xf0) == SM_MIDI_NOTEON && c < 5) {
    a = (byte)(SM_MIDI_NOTEOFF | (a & 0x0f));
    c = 0;
  }

  switch (a & 0xf0) {
    case SM_MIDI_NOTEOFF:
      note_states[a & 0x0f][b & 0x7f] = 0;
      break;
    case SM_MIDI_NOTEON:
      note_states[a & 0x0f][b & 0x7f] = c;
      note_pressure[a & 0x0f][b & 0x7f] = c;
      song_trigger_sync(SONG_SYNC_FLAG_MIDI);
      break;
    case SM_MIDI_PRESSURE:
      note_pressure[a & 0x0f][b & 0x7f] = c;
      break;
    case SM_MIDI_CONTROLLER:
      config_set_controller((byte)(SM_OUTPUT_0 + (a & 0x0f)), (byte)(b & 0x7f), c);
      d = 0;
      break;
    case SM_MIDI_PROGRAM:
      config_set_program((byte)(SM_OUTPUT_0 + (a & 0x0f)), b);
      c = 0;
      d = 0;
      break;
    default:
      break;
  }

  if (synth_sink) synth_sink(a, b, c, d);
  if (external_sink) external_sink(a, b, c, d);
}

void midi_reset() {
  fp_lock lock;

  for (int ch = 0; ch < 16; ++ch) {
    for (int note = 0; note < 128; ++note) {
      if (note_states[ch][note]) {
        midi_output_event((byte)(SM_MIDI_NOTEOFF | ch), (byte)note, 0, 0);
      }
    }
    for (int i = 0; i < 128; ++i) {
      int v = config_get_controller((byte)(SM_OUTPUT_0 + ch), (byte)i);
      if (v >= 0 && v < 128) midi_output_event((byte)(SM_MIDI_CONTROLLER | ch), (byte)i, (byte)v, 0);
    }
    int prog = config_get_program((byte)(SM_OUTPUT_0 + ch));
    if (prog >= 0 && prog < 128) midi_output_event((byte)(SM_MIDI_PROGRAM | ch), (byte)prog, 0, 0);
  }
}
