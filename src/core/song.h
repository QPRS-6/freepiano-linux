#pragma once

#include "events.h"
#include "fp_types.h"

#include <string>

struct song_info_t {
  uint32_t version;
  char title[256];
  char author[256];
  char comment[1024];
  bool write_protected;
  bool compatibility;
};

// Send an event (and record it when recording is active).
void song_send_event(byte a, byte b, byte c, byte d, bool record = false);

// Translate and output an event (no recording).
void song_output_event(byte a, byte b, byte c, byte d);

void song_start_record();
void song_stop_record();
bool song_is_recording();

void song_start_playback();
void song_stop_playback();
bool song_is_playing();
// Pause keeps the position and resumes by fast-forwarding the event stream.
void song_pause_playback();
void song_resume_playback();
bool song_is_paused();
// Jump to a position (milliseconds) and continue playing from there.
void song_seek(int milliseconds);

bool song_allow_save();
bool song_allow_input();
bool song_is_empty();

void song_update(double time_elapsed);

int song_get_length();
int song_get_time();
double song_get_clock();
double song_get_play_speed();
void song_set_play_speed(double speed);

int song_open(const std::string& filename);
int song_save(const std::string& filename);
void song_close();
int song_open_lyt(const std::string& filename);

song_info_t* song_get_info();

bool song_translate_note(byte& a, byte& b, byte& c, byte& d);

void song_trigger_sync(uint32_t flags);
void song_update_pressure(byte ch, byte pressure);

// Reset playback/recording state and release all notes (used at shutdown).
void song_reset_all();

// Current song position while recording/playing, in milliseconds.
double song_get_timer();

// Number of stored events (0 when no song is loaded/recorded).
int song_get_event_count();
