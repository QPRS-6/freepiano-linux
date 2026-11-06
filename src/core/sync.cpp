#include "sync.h"

std::recursive_mutex& fp_engine_mutex() {
  static std::recursive_mutex mutex;
  return mutex;
}

fp_lock::fp_lock() { fp_engine_mutex().lock(); }
fp_lock::~fp_lock() { fp_engine_mutex().unlock(); }
