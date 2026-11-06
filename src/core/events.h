#pragma once

// FreePiano event/message constants (identical values to the original so that
// recorded .fpm songs and .map scripts stay interoperable).

// key map messages (a == SM_SYSTEM)
#define SMS_KEY_EVENT             0x00
#define SMS_KEY_NOP               0x00
#define SMS_KEY_MAP               0x01
#define SMS_KEY_LABEL             0x02
#define SMS_KEY_COLOR             0x03

// system events
#define SM_SYSTEM                 0x00
#define SM_KEY_SIGNATURE          0x01
#define SM_OCTAVE                 0x02
#define SM_VELOCITY               0x03
#define SM_CHANNEL                0x04
#define SM_VOLUME                 0x05
#define SM_PLAY                   0x06
#define SM_RECORD                 0x07
#define SM_STOP                   0x08

#define SM_SETTING_GROUP          0x09
#define SM_SETTING_GROUP_COUNT    0x0a
#define SM_AUTO_PEDAL_OBSOLETE    0x0b
#define SM_DELAY_KEYUP_OBSOLETE   0x0c
#define SM_TRANSPOSE              0x0d
#define SM_CONTROLLER_DEPRECATED  0x0e

#define SM_NOTE_OFF               0x10
#define SM_NOTE_ON                0x11
#define SM_NOTE_PRESSURE          0x12
#define SM_PRESSURE               0x13
#define SM_PITCH                  0x14
#define SM_PROGRAM                0x15
#define SM_BANK_MSB               0x16
#define SM_BANK_LSB               0x17
#define SM_SUSTAIN                0x18
#define SM_MODULATION             0x19
#define SM_FOLLOW_KEY             0x1a

// MIDI messages
#define SM_MIDI_MASK_MSG          0xf0
#define SM_MIDI_MASK_CHANNEL      0x0f
#define SM_MIDI_MESSAGE_START     0x80
#define SM_MIDI_NOTEOFF           0x80
#define SM_MIDI_NOTEON            0x90
#define SM_MIDI_PRESSURE          0xa0
#define SM_MIDI_CONTROLLER        0xb0
#define SM_MIDI_PROGRAM           0xc0
#define SM_MIDI_CHANNEL_PRESSURE  0xd0
#define SM_MIDI_PITCH_BEND        0xe0
#define SM_MIDI_SYSEX             0xf0

// value operators (lower nibble of an "op" argument)
#define SM_VALUE_SET              0x00
#define SM_VALUE_INC              0x01
#define SM_VALUE_DEC              0x02
#define SM_VALUE_FLIP             0x03
#define SM_VALUE_PRESS            0x04
#define SM_VALUE_SET10            0x0a
#define SM_VALUE_SET1             0x0b
#define SM_VALUE_SYNC             0x10

// tracks
#define SM_INPUT_0                0x00
#define SM_INPUT_1                0x01
#define SM_INPUT_2                0x02
#define SM_INPUT_3                0x03
#define SM_INPUT_MAX              0x0f

#define SM_OUTPUT_0               0x10
#define SM_OUTPUT_1               0x11
#define SM_OUTPUT_2               0x12
#define SM_OUTPUT_3               0x13
#define SM_OUTPUT_MAX             0x1f

// song sync flags
#define SONG_SYNC_FLAG_MIDI       0x0f
#define SONG_SYNC_FLAG_KEYDOWN    0x10
