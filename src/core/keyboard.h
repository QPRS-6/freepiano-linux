#pragma once

#include "fp_types.h"

#include <string>
#include <vector>

int keyboard_init();
void keyboard_shutdown();
void keyboard_enable(bool enable);
void keyboard_reset();

// code is a DirectInput scan code, keydown is 1/0.
void keyboard_send_event(int code, int keydown);

void keyboard_update(double time_elapsed);
byte keyboard_get_status(byte code);

std::vector<std::string> keyboard_enum_keymap();
