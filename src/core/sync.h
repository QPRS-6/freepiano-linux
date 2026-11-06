#pragma once

#include <mutex>

// Single recursive engine mutex.  The original FreePiano uses one critical
// section per subsystem; a single recursive lock keeps lock ordering trivial
// between the audio callback, the MIDI input thread and the GUI.
std::recursive_mutex& fp_engine_mutex();

class fp_lock {
 public:
  fp_lock();
  ~fp_lock();
  fp_lock(const fp_lock&) = delete;
  fp_lock& operator=(const fp_lock&) = delete;
};
