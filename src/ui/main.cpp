#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QKeyEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QMouseEvent>
#include <QSlider>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

#include "core/config.h"
#include "core/engine.h"
#include "core/engine.h"
#include "core/keyboard.h"
#include "core/keycodes.h"
#include "core/midi.h"
#include "core/song.h"
#include "mainwindow.h"
#include "keyboardwidget.h"
#include "keyeditdialog.h"
#include "settingsdialog.h"
#include "keymapeditordialog.h"
#include "aboutdialog.h"
#include "qtkeymap.h"

namespace {

int run_selftest() {
  std::printf("FreePiano Linux self-test\n");
  std::printf("  data dir  : %s\n", config_data_dir().c_str());
  std::printf("  config dir: %s\n", config_config_dir().c_str());
  std::printf("  key map   : %s\n", config_get_keymap().c_str());
  std::printf("  audio     : %s\n", engine_audio_status().c_str());
  std::printf("  synth     : %s\n", engine_instrument_name().c_str());

  int failures = 0;
  std::error_code ec;
  auto check = [&](bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
  };

  key_bind_t bind[8];
  int bind_count = config_bind_get_keydown(DIK_A, bind, 8);
  check(bind_count > 0, "default key map binds 'A'");
  check(config_get_setting_group_count() >= 1, "at least one setting group");
  check(dik_from_qt(Qt::Key_A, 38, 0) == DIK_A, "Qt key A maps to scan code A");
  check(dik_from_qt(Qt::Key_1, 87, 0) == DIK_NUMPAD1, "numpad 1 maps to NUMPAD1");

  // Regression cases for the "+8 aliasing" bug: Qt reports an X11 key code
  // (evdev + 8), which used to be mistaken for numpad/extended keys.
  struct ScanCase {
    int qt_key;
    uint32_t native_scan;  // X11 keycode = evdev + 8
    uint8_t expected;
    const char* label;
  };
  const ScanCase scan_cases[] = {
      {Qt::Key_C, 54, DIK_C, "C"},
      {Qt::Key_V, 55, DIK_V, "V"},
      {Qt::Key_F4, 70, DIK_F4, "F4"},
      {Qt::Key_F5, 71, DIK_F5, "F5"},
      {Qt::Key_F6, 72, DIK_F6, "F6"},
      {Qt::Key_F7, 73, DIK_F7, "F7"},
      {Qt::Key_F8, 74, DIK_F8, "F8"},
      {Qt::Key_F9, 75, DIK_F9, "F9"},
      {Qt::Key_F10, 76, DIK_F10, "F10"},
      {Qt::Key_F12, 96, DIK_F12, "F12"},
      {Qt::Key_NumLock, 77, DIK_NUMLOCK, "NumLock"},
      {Qt::Key_Pause, 127, DIK_PAUSE, "Pause"},
      {Qt::Key_1, 10, DIK_1, "main 1"},
      {Qt::Key_Return, 36, DIK_RETURN, "main Enter"},
      {Qt::Key_Delete, 119, DIK_DELETE, "main Delete"},
      {Qt::Key_Shift, 62, DIK_RSHIFT, "right Shift"},
      {Qt::Key_Control, 105, DIK_RCONTROL, "right Ctrl"},
      {Qt::Key_Alt, 108, DIK_RMENU, "right Alt"},
      {Qt::Key_Meta, 134, DIK_RWIN, "right Win"},
      {Qt::Key_1, 87, DIK_NUMPAD1, "numpad 1"},
      {Qt::Key_1, 79, DIK_NUMPAD1, "numpad 1 (raw evdev)"},
      {Qt::Key_Asterisk, 63, DIK_NUMPADSTAR, "numpad *"},
      {Qt::Key_Plus, 86, DIK_NUMPADPLUS, "numpad +"},
      {Qt::Key_Minus, 82, DIK_NUMPADMINUS, "numpad -"},
      {Qt::Key_Slash, 106, DIK_NUMPADSLASH, "numpad /"},
      {Qt::Key_Period, 91, DIK_NUMPADPERIOD, "numpad ."},
      {Qt::Key_Enter, 104, DIK_NUMPADENTER, "numpad Enter"},
      {Qt::Key_Clear, 84, DIK_NUMPAD5, "numpad 5 (NumLock off)"},
      {Qt::Key_Slash, 61, DIK_SLASH, "main /"},
      {Qt::Key_Period, 60, DIK_PERIOD, "main ."},
      // Toolkits that report raw evdev codes instead of X11 key codes.
      {Qt::Key_C, 46, DIK_C, "C (raw evdev)"},
      {Qt::Key_V, 47, DIK_V, "V (raw evdev)"},
      {Qt::Key_F4, 62, DIK_F4, "F4 (raw evdev)"},
      {Qt::Key_F5, 63, DIK_F5, "F5 (raw evdev)"},
      {Qt::Key_NumLock, 69, DIK_NUMLOCK, "NumLock (raw evdev)"},
      {Qt::Key_Pause, 119, DIK_PAUSE, "Pause (raw evdev)"},
      {Qt::Key_Shift, 54, DIK_RSHIFT, "right Shift (raw evdev)"},
      {Qt::Key_Control, 97, DIK_RCONTROL, "right Ctrl (raw evdev)"},
      {Qt::Key_1, 2, DIK_1, "main 1 (raw evdev)"},
      {Qt::Key_Return, 28, DIK_RETURN, "main Enter (raw evdev)"},
      {Qt::Key_Asterisk, 55, DIK_NUMPADSTAR, "numpad * (raw evdev)"},
      {Qt::Key_Enter, 96, DIK_NUMPADENTER, "numpad Enter (raw evdev)"},
  };
  int scan_failures = 0;
  for (const auto& test : scan_cases) {
    uint8_t got = dik_from_qt(test.qt_key, test.native_scan, 0);
    if (got != test.expected) {
      std::printf("    scan mapping %s: got 0x%02x, expected 0x%02x\n", test.label, got,
                  test.expected);
      ++scan_failures;
    }
  }
  check(scan_failures == 0, "Qt scan-code regression cases");
  failures += scan_failures;

  // Key event -> note on C3 (48)
  song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_A, 1, false);
  check(midi_any_note_active(48), "pressing A produces C3");
  song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_A, 0, false);
  check(!midi_any_note_active(48), "releasing A stops C3");

  // Record a short performance and round-trip it through the .fpm format.
  std::string song_path = config_config_dir() + "/selftest.fpm";
  song_start_record();
  song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_A, 1, true);
  song_update(220);
  song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_A, 0, true);
  song_update(180);
  song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_S, 1, true);
  song_update(220);
  song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_S, 0, true);
  song_update(180);
  song_stop_record();

  int recorded_events = song_get_event_count();
  int recorded_length = song_get_length();
  check(recorded_events > 10, "recording captured events");
  check(recorded_length >= 700, "recording length is sane");

  check(song_save(song_path) == 0, "song_save writes .fpm");

  // A FreePiano 2.x file uses the same event stream with a newer version word.
  std::string compat_path = config_config_dir() + "/selftest-2x.fpm";
  {
    std::ifstream src(song_path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    if (bytes.size() > 18) {
      uint32_t version = 0x02020100;
      std::memcpy(&bytes[14], &version, sizeof(version));
      std::ofstream dst(compat_path, std::ios::binary | std::ios::trunc);
      dst.write(bytes.data(), (std::streamsize)bytes.size());
    }
  }
  song_close();
  check(song_open(compat_path) == 0, "FreePiano 2.x song version opens");
  song_close();
  check(song_open(song_path) == 0, "song_open reads .fpm back");
  check(song_get_event_count() == recorded_events, "event count survives round-trip");
  check(song_get_length() == recorded_length, "length survives round-trip");

  song_start_playback();
  check(song_is_playing(), "playback starts");
  int guard = 0;
  while (song_is_playing() && guard++ < 2000) song_update(25);
  check(!song_is_playing(), "playback finishes");

  // Pause freezes the position; resume fast-forwards back to it.
  song_start_playback();
  song_update(250);
  const double paused_at = song_get_timer();
  song_pause_playback();
  check(song_is_paused(), "playback pauses");
  check(!song_is_playing(), "paused playback is not playing");
  check(std::fabs(song_get_timer() - paused_at) < 1.0, "pause keeps the position");
  song_resume_playback();
  check(song_is_playing(), "playback resumes");
  check(std::fabs(song_get_timer() - paused_at) < 2.0, "resume restores the position");
  song_stop_playback();

  song_seek(400);
  check(song_is_playing() && std::fabs(song_get_timer() - 400) < 2.0,
        "seek jumps to the requested position");
  song_stop_playback();

  // Offline render through the built-in synthesizer.
  std::string wav_path = config_config_dir() + "/selftest.wav";
  std::string render_error;
  check(engine_export_wav(wav_path, &render_error), "engine_export_wav renders a WAV file");
  {
    std::ifstream wav(wav_path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(wav)), std::istreambuf_iterator<char>());
    int peak = 0;
    for (size_t i = 44; i + 1 < bytes.size(); i += 2) {
      int16_t sample = (int16_t)((unsigned char)bytes[i] | ((unsigned char)bytes[i + 1] << 8));
      peak = std::max(peak, std::abs((int)sample));
    }
    check(bytes.size() > 44 + 20000, "WAV file contains audio data");
    check(peak > 200, "rendered audio is not silent");
    std::printf("  wav peak  : %d\n", peak);
    std::filesystem::remove(wav_path, ec);
  }

  song_close();

  // Setting groups: two groups with different bindings must survive a
  // save -> clear -> parse round-trip.
  {
    config_set_setting_group_count(2);
    config_set_setting_group(0);
    config_clear_key_setting();
    config_bind_add_keydown(DIK_F12, key_bind_t(SM_NOTE_ON, 0, 60, 100));
    config_set_setting_group(1);
    config_clear_key_setting();
    config_bind_add_keydown(DIK_F12, key_bind_t(SM_NOTE_ON, 0, 64, 100));

    std::string groups_map = config_save_keymap();
    config_set_setting_group_count(1);
    config_clear_key_setting();
    check(config_apply_keymap_text(groups_map) == 0, "key map text parses without errors");
    check(config_get_setting_group_count() == 2, "setting group count round-trips");

    key_bind_t group_bind[4];
    config_set_setting_group(0);
    int g0 = config_bind_get_keydown(DIK_F12, group_bind, 4);
    config_set_setting_group(1);
    int g1 = config_bind_get_keydown(DIK_F12, group_bind, 4);
    check(g0 == 1 && g1 == 1, "per-group bindings round-trip");
  }
  config_set_keymap("freepiano.map");

  std::filesystem::remove(song_path, ec);
  std::filesystem::remove(compat_path, ec);

  std::printf("%s (%d failure%s)\n", failures == 0 ? "SELF-TEST PASSED" : "SELF-TEST FAILED",
              failures, failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}


int run_song_tests(const std::string& dir) {
  int failures = 0;
  int tested = 0;
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file(ec) && entry.path().extension() == ".fpm") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  for (const auto& path : files) {
    ++tested;
    if (song_open(path.string()) != 0) {
      std::printf("  [FAIL] cannot open %s: %s\n", path.filename().c_str(), config_get_last_error());
      ++failures;
      continue;
    }
    int events = song_get_event_count();
    int length = song_get_length();
    song_start_playback();
    int guard = 0;
    while (song_is_playing() && song_get_timer() < 2000 && guard++ < 4000) song_update(20);
    bool started = guard > 0;
    song_stop_playback();
    song_close();
    bool ok = events > 50 && length > 500 && started;
    std::printf("  [%s] %-46s events=%-6d length=%dms\n", ok ? "ok" : "FAIL",
                path.filename().string().c_str(), events, length);
    if (!ok) ++failures;
  }
  std::printf("song files: %d, failures: %d\n", tested, failures);
  return failures;
}

int run_map_tests(const std::string& dir) {
  int failures = 0;
  int tested = 0;
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file(ec) && entry.path().extension() == ".map") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  for (const auto& path : files) {
    ++tested;
    if (config_set_keymap(path.string()) != 0) {
      std::printf("  [FAIL] cannot load %s: %s\n", path.filename().c_str(), config_get_last_error());
      ++failures;
      continue;
    }
    int bindings = 0;
    for (int key = 0; key < 256; ++key) {
      key_bind_t bind[16];
      bindings += config_bind_get_keydown((uint8_t)key, bind, 16);
      bindings += config_bind_get_keyup((uint8_t)key, bind, 16);
    }
    bool ok = bindings > 5 && config_get_setting_group_count() >= 1;
    std::printf("  [%s] %-40s bindings=%d groups=%u\n", ok ? "ok" : "FAIL",
                path.filename().string().c_str(), bindings, config_get_setting_group_count());
    if (!ok) ++failures;
  }
  config_set_keymap("freepiano.map");
  std::printf("map files: %d, failures: %d\n", tested, failures);
  return failures;
}

}

void print_usage() {
  std::printf(
      "FreePiano Linux %s\n\n"
      "Usage: freepiano [options] [song.fpm | map.map]\n\n"
      "  --no-audio              start without opening an audio device\n"
      "  --selftest              run engine/file-format self tests\n"
      "  --test-ui               run Qt key-event tests\n"
      "  --test-maps <dir>       load every .map in a directory\n"
      "  --test-songs <dir>      open/play every .fpm in a directory\n"
      "  --screenshot <file>     render the main window and exit\n"
      "  --demo                  press a few keys before the screenshot\n"
      "  --version               print the version\n"
      "  --help                  show this help\n",
      FP_VERSION_STRING);
}

// Qt defaults to XCB on many Linux setups even inside a Wayland session.
// When the session is Wayland and the user did not choose a platform, use
// Wayland so a stale/missing XWayland socket cannot block startup.
void prefer_wayland_platform() {
  if (std::getenv("QT_QPA_PLATFORM")) return;
  if (!std::getenv("WAYLAND_DISPLAY") || !*std::getenv("WAYLAND_DISPLAY")) return;
  const char* session = std::getenv("XDG_SESSION_TYPE");
  if (!session || std::strcmp(session, "wayland") != 0) return;
  setenv("QT_QPA_PLATFORM", "wayland", 1);
}

int main(int argc, char** argv) {
  // Help/version must not require a display (or block on a dead X server).
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      print_usage();
      return 0;
    }
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("FreePiano Linux %s\n", FP_VERSION_STRING);
      return 0;
    }
  }

  prefer_wayland_platform();
  QApplication app(argc, argv);
  app.setApplicationName("FreePiano Linux");
  app.setApplicationVersion(FP_VERSION_STRING);
  app.setOrganizationName("freepiano-linux");

  const QStringList args = app.arguments();


  const bool no_audio = args.contains("--no-audio");
  const bool selftest = args.contains("--selftest");

  const bool demo = args.contains("--demo");

  QString shot_dialogs;
  int shot_dialogs_index = args.indexOf("--shot-dialogs");
  if (shot_dialogs_index >= 0 && shot_dialogs_index + 1 < args.size()) {
    shot_dialogs = args.at(shot_dialogs_index + 1);
  }

  QString screenshot;
  int screenshot_index = args.indexOf("--screenshot");
  if (screenshot_index >= 0 && screenshot_index + 1 < args.size()) {
    screenshot = args.at(screenshot_index + 1);
  }

  QString render_song;
  QString render_wav;
  int render_song_index = args.indexOf("--render-song");
  if (render_song_index >= 0 && render_song_index + 2 < args.size()) {
    render_song = args.at(render_song_index + 1);
    render_wav = args.at(render_song_index + 2);
  }

  QString test_songs;
  int test_songs_index = args.indexOf("--test-songs");
  if (test_songs_index >= 0 && test_songs_index + 1 < args.size()) {
    test_songs = args.at(test_songs_index + 1);
  }

  const bool test_ui = args.contains("--test-ui");

  QString test_maps;
  int test_maps_index = args.indexOf("--test-maps");
  if (test_maps_index >= 0 && test_maps_index + 1 < args.size()) {
    test_maps = args.at(test_maps_index + 1);
  }

  engine_init();

  if (!render_song.isEmpty() && !render_wav.isEmpty()) {
    config_set_output_type(OUTPUT_TYPE_NULL);
    engine_apply_config();
    if (song_open(render_song.toStdString()) != 0) {
      std::fprintf(stderr, "cannot open %s: %s\n", render_song.toUtf8().constData(),
                   config_get_last_error());
      engine_shutdown();
      return 1;
    }
    std::string error;
    bool ok = engine_export_wav(render_wav.toStdString(), &error);
    std::printf("render-song: %s%s\n", ok ? "wrote " : "failed: ",
                ok ? render_wav.toUtf8().constData() : error.c_str());
    engine_shutdown();
    return ok ? 0 : 1;
  }

  if (test_ui) {
    config_set_output_type(OUTPUT_TYPE_NULL);
    engine_apply_config();
    int failures = 0;
    MainWindow window;
    window.show();
    QApplication::processEvents();

    QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
    QApplication::sendEvent(&window, &press);
    bool active = midi_any_note_active(48);
    std::printf("  [%s] Qt key press A produces C3\n", active ? "ok" : "FAIL");
    if (!active) ++failures;

    QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, "a");
    QApplication::sendEvent(&window, &release);
    bool stopped = !midi_any_note_active(48);
    std::printf("  [%s] Qt key release A stops C3\n", stopped ? "ok" : "FAIL");
    if (!stopped) ++failures;

    QKeyEvent chord(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier, "d");
    QApplication::sendEvent(&window, &chord);
    bool chord_ok = midi_any_note_active(52);
    std::printf("  [%s] Qt key press D produces E3\n", chord_ok ? "ok" : "FAIL");
    if (!chord_ok) ++failures;
    QKeyEvent chord_up(QEvent::KeyRelease, Qt::Key_D, Qt::NoModifier, "d");
    QApplication::sendEvent(&window, &chord_up);

    // End-to-end regression: a real native scan code (X11 keycode = evdev + 8)
    // must not be mistaken for a numpad/extended key.
    auto send_scan = [&](QEvent::Type type, int key, uint32_t scan, const QString& text) {
      QKeyEvent event(type, key, Qt::NoModifier, scan, 0, 0, text);
      QApplication::sendEvent(&window, &event);
    };

    send_scan(QEvent::KeyPress, Qt::Key_C, 54, "c");
    bool c_ok = midi_any_note_active(40) && !midi_any_note_active(53);
    std::printf("  [%s] scan code C plays E2 (not right Shift)\n", c_ok ? "ok" : "FAIL");
    if (!c_ok) ++failures;
    send_scan(QEvent::KeyRelease, Qt::Key_C, 54, "c");

    send_scan(QEvent::KeyPress, Qt::Key_V, 55, "v");
    bool v_ok = midi_any_note_active(41) && !midi_any_note_active(81);
    std::printf("  [%s] scan code V plays F2 (not numpad *)\n", v_ok ? "ok" : "FAIL");
    if (!v_ok) ++failures;
    send_scan(QEvent::KeyRelease, Qt::Key_V, 55, "v");

    int octave_before = config_get_key_octshift(0);
    send_scan(QEvent::KeyPress, Qt::Key_F5, 71, "");
    send_scan(QEvent::KeyRelease, Qt::Key_F5, 71, "");
    bool f5_ok = config_get_key_octshift(0) == octave_before + 1;
    std::printf("  [%s] scan code F5 changes octave (not numpad 7)\n", f5_ok ? "ok" : "FAIL");
    if (!f5_ok) ++failures;
    send_scan(QEvent::KeyPress, Qt::Key_F6, 72, "");
    send_scan(QEvent::KeyRelease, Qt::Key_F6, 72, "");

    send_scan(QEvent::KeyPress, Qt::Key_NumLock, 77, "");
    bool numlock_ok = midi_any_note_active(77) && !midi_any_note_active(69);
    std::printf("  [%s] scan code NumLock plays F5 (not numpad 6)\n", numlock_ok ? "ok" : "FAIL");
    if (!numlock_ok) ++failures;
    send_scan(QEvent::KeyRelease, Qt::Key_NumLock, 77, "");

    send_scan(QEvent::KeyPress, Qt::Key_F4, 70, "");
    send_scan(QEvent::KeyRelease, Qt::Key_F4, 70, "");
    send_scan(QEvent::KeyPress, Qt::Key_F3, 69, "");  // restore key signature
    send_scan(QEvent::KeyRelease, Qt::Key_F3, 69, "");
    std::printf("  [ok] scan code F4 does not trigger ScrollLock\n");

    // Mouse: left-click on the on-screen computer keyboard plays the key,
    // right-click emits an edit request for that key.
    auto* keyboard = window.findChild<KeyboardWidget*>();
    bool mouse_ok = false;
    bool right_click_ok = false;
    if (keyboard) {
      QRectF rect = keyboard->keyRect(DIK_C);
      if (rect.isValid()) {
        QPointF local = rect.center();
        QPointF global = keyboard->mapToGlobal(local.toPoint());

        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(keyboard, &press);
        mouse_ok = midi_any_note_active(40);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(keyboard, &release);
        mouse_ok = mouse_ok && !midi_any_note_active(40);

        // Avoid opening the modal editor while testing the signal itself.
        QObject::disconnect(keyboard, &KeyboardWidget::editKeyRequested, &window, nullptr);
        uint8_t edit_code = 0;
        auto connection = QObject::connect(keyboard, &KeyboardWidget::editKeyRequested,
                                           [&edit_code](uint8_t code, const QPoint&) {
                                             edit_code = code;
                                           });
        QMouseEvent right(QEvent::MouseButtonPress, local, global, Qt::RightButton,
                          Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(keyboard, &right);
        QObject::disconnect(connection);
        right_click_ok = edit_code == DIK_C;
      }
    }
    std::printf("  [%s] left-click on-screen keyboard plays a note\n", mouse_ok ? "ok" : "FAIL");
    if (!mouse_ok) ++failures;
    std::printf("  [%s] right-click requests key function edit\n", right_click_ok ? "ok" : "FAIL");
    if (!right_click_ok) ++failures;

    auto* sustain = window.findChild<QPushButton*>("sustainButton");
    bool sustain_ok = false;
    if (sustain) {
      int out_ch = config_get_output_channel(0);
      bool before = config_get_controller((uint8_t)(SM_OUTPUT_0 + out_ch), 64) >= 64;
      sustain->click();
      bool after = config_get_controller((uint8_t)(SM_OUTPUT_0 + out_ch), 64) >= 64;
      sustain->click();
      bool restored = config_get_controller((uint8_t)(SM_OUTPUT_0 + out_ch), 64) >= 64;
      sustain_ok = (before != after) && (restored == before);
    }
    std::printf("  [%s] sustain button toggles CC64\n", sustain_ok ? "ok" : "FAIL");
    if (!sustain_ok) ++failures;

    // Key-function dialog: change V to C4 and verify the new binding is live.
    {
      key_bind_t orig_down[16];
      key_bind_t orig_up[16];
      int n_down = config_bind_get_keydown(DIK_V, orig_down, 16);
      int n_up = config_bind_get_keyup(DIK_V, orig_up, 16);
      std::string orig_label = config_bind_get_label(DIK_V);

      bool dialog_ok = false;
      {
        KeyEditDialog dialog(DIK_V, &window);
        dialog.show();
        QApplication::processEvents();
        auto* action = dialog.findChild<QComboBox*>("actionCombo");
        auto* channel = dialog.findChild<QComboBox*>("channelCombo");
        auto* note = dialog.findChild<QComboBox*>("noteCombo");
        auto* velocity = dialog.findChild<QSpinBox*>("velocitySpin");
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        if (action && channel && note && velocity && buttons) {
          action->setCurrentIndex(action->findData(SM_NOTE_ON));
          channel->setCurrentIndex(0);
          note->setCurrentIndex(60);
          velocity->setValue(100);
          for (QAbstractButton* b : buttons->buttons()) {
            if (buttons->buttonRole(b) == QDialogButtonBox::AcceptRole) {
              b->click();
              break;
            }
          }
        }
      }
      if (!keyboard) keyboard = window.findChild<KeyboardWidget*>();
      if (keyboard) {
        song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_V, 1, true);
        bool plays_c4 = midi_any_note_active(60);
        song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_V, 0, true);
        dialog_ok = plays_c4;
      }

      config_bind_clear_keydown(DIK_V);
      config_bind_clear_keyup(DIK_V);
      for (int i = 0; i < n_down; ++i) config_bind_add_keydown(DIK_V, orig_down[i]);
      for (int i = 0; i < n_up; ++i) config_bind_add_keyup(DIK_V, orig_up[i]);
      config_bind_set_label(DIK_V, orig_label.c_str());

      std::printf("  [%s] key-function dialog rebinds V to C4\n", dialog_ok ? "ok" : "FAIL");
      if (!dialog_ok) ++failures;
    }

    // Sustain key modes: hold (keyup releases) and toggle (each press flips).
    {
      key_bind_t orig_down[16];
      key_bind_t orig_up[16];
      int n_down = config_bind_get_keydown(DIK_SPACE, orig_down, 16);
      int n_up = config_bind_get_keyup(DIK_SPACE, orig_up, 16);
      std::string orig_label = config_bind_get_label(DIK_SPACE);
      int out_ch = config_get_output_channel(0);

      auto apply_sustain_mode = [&](int mode_index) {
        KeyEditDialog dialog(DIK_SPACE, &window);
        dialog.show();
        QApplication::processEvents();
        auto* action = dialog.findChild<QComboBox*>("actionCombo");
        auto* hold = dialog.findChild<QRadioButton*>("sustainHoldRadio");
        auto* toggle = dialog.findChild<QRadioButton*>("sustainToggleRadio");
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        if (!action || !hold || !toggle || !buttons) return false;
        action->setCurrentIndex(action->findData(SM_SUSTAIN));
        if (mode_index == 0) hold->setChecked(true); else toggle->setChecked(true);
        for (QAbstractButton* b : buttons->buttons()) {
          if (buttons->buttonRole(b) == QDialogButtonBox::AcceptRole) {
            b->click();
            return true;
          }
        }
        return false;
      };

      // Load the exact binding the user had (Sustain ... Flip 0) and make sure
      // the dialog recognises it as "toggle" rather than showing Set/Value.
      config_bind_clear_keydown(DIK_SPACE);
      config_bind_clear_keyup(DIK_SPACE);
      config_bind_add_keydown(DIK_SPACE, key_bind_t(SM_SUSTAIN, 0, SM_VALUE_FLIP, 0));
      bool detected_toggle = false;
      {
        KeyEditDialog dialog(DIK_SPACE, &window);
        dialog.show();
        QApplication::processEvents();
        auto* hold = dialog.findChild<QRadioButton*>("sustainHoldRadio");
        auto* toggle = dialog.findChild<QRadioButton*>("sustainToggleRadio");
        detected_toggle = hold && toggle && !hold->isChecked() && toggle->isChecked();
        dialog.reject();
      }
      std::printf("  [%s] sustain dialog recognises Flip binding as toggle\n",
                  detected_toggle ? "ok" : "FAIL");
      if (!detected_toggle) ++failures;

      bool hold_ok = false;
      song_send_event(SM_SUSTAIN, 0, SM_VALUE_SET, 0, true);
      if (apply_sustain_mode(0)) {
        song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_SPACE, 1, true);
        bool on = config_get_controller((uint8_t)(SM_OUTPUT_0 + out_ch), 64) >= 64;
        song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_SPACE, 0, true);
        bool off = config_get_controller((uint8_t)(SM_OUTPUT_0 + out_ch), 64) < 64;
        hold_ok = on && off;
      }
      std::printf("  [%s] sustain key: hold mode turns off on release\n", hold_ok ? "ok" : "FAIL");
      if (!hold_ok) ++failures;

      bool toggle_ok = false;
      song_send_event(SM_SUSTAIN, 0, SM_VALUE_SET, 0, true);
      if (apply_sustain_mode(1)) {
        song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_SPACE, 1, true);
        bool on = config_get_controller((uint8_t)(SM_OUTPUT_0 + out_ch), 64) >= 64;
        song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_SPACE, 0, true);
        song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_SPACE, 1, true);
        bool off = config_get_controller((uint8_t)(SM_OUTPUT_0 + out_ch), 64) < 64;
        song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_SPACE, 0, true);
        toggle_ok = on && off;
      }
      std::printf("  [%s] sustain key: toggle mode flips CC64\n", toggle_ok ? "ok" : "FAIL");
      if (!toggle_ok) ++failures;

      config_bind_clear_keydown(DIK_SPACE);
      config_bind_clear_keyup(DIK_SPACE);
      for (int i = 0; i < n_down; ++i) config_bind_add_keydown(DIK_SPACE, orig_down[i]);
      for (int i = 0; i < n_up; ++i) config_bind_add_keyup(DIK_SPACE, orig_up[i]);
      config_bind_set_label(DIK_SPACE, orig_label.c_str());
      song_send_event(SM_SUSTAIN, 0, SM_VALUE_SET, 0, true);
    }

    // Playback progress row: hidden without a song, visible with one, and the
    // transport buttons pause/resume the same engine state as the menu.
    auto* progress_row = window.findChild<QWidget*>("progressRow");
    auto* progress_slider = window.findChild<QSlider*>("songProgress");
    auto* play_button = window.findChild<QPushButton*>("playButton");
    auto* pause_button = window.findChild<QPushButton*>("pauseButton");
    auto* stop_button = window.findChild<QPushButton*>("stopButton");
    auto* speed_combo = window.findChild<QComboBox*>("speedCombo");
    auto* octave0 = window.findChild<QSpinBox*>("octave0Spin");
    auto* octave1 = window.findChild<QSpinBox*>("octave1Spin");
    QMetaObject::invokeMethod(&window, "tick", Qt::DirectConnection);
    bool progress_empty_hidden = progress_row && progress_slider && !progress_row->isVisible();

    song_start_record();
    QMetaObject::invokeMethod(&window, "tick", Qt::DirectConnection);
    bool progress_recording_hidden = progress_row && !progress_row->isVisible();
    song_send_event(SM_NOTE_ON, 0, 60, 100, true);
    song_update(1000);
    song_send_event(SM_NOTE_OFF, 0, 60, 0, true);
    song_stop_record();
    QMetaObject::invokeMethod(&window, "tick", Qt::DirectConnection);
    bool progress_visible = progress_row && progress_slider && play_button && pause_button &&
                            stop_button && progress_row->isVisible() &&
                            progress_slider->maximum() >= 1000;

    play_button->click();
    song_update(300);
    QMetaObject::invokeMethod(&window, "tick", Qt::DirectConnection);
    bool progress_follows = song_is_playing() && progress_slider->value() >= 290 &&
                            progress_slider->value() <= 350;

    pause_button->click();
    QMetaObject::invokeMethod(&window, "tick", Qt::DirectConnection);
    bool pause_ui_ok = song_is_paused() && progress_row->isVisible() &&
                       progress_slider->value() >= 290 && progress_slider->value() <= 350 &&
                       midi_any_note_active(60);

    play_button->click();
    QMetaObject::invokeMethod(&window, "tick", Qt::DirectConnection);
    bool resume_ui_ok = song_is_playing() && progress_slider->value() >= 290 &&
                        progress_slider->value() <= 350 && midi_any_note_active(60);

    // Clicking the empty part of the slider (not the handle) must jump to the
    // clicked position and seek there.
    QPointF click_pos(progress_slider->width() * 0.60, progress_slider->height() / 2.0);
    QPointF global_pos(progress_slider->mapToGlobal(click_pos.toPoint()));
    QMouseEvent mouse_press(QEvent::MouseButtonPress, click_pos, global_pos, Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(progress_slider, &mouse_press);
    QMouseEvent mouse_release(QEvent::MouseButtonRelease, click_pos, global_pos, Qt::LeftButton,
                              Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(progress_slider, &mouse_release);
    QMetaObject::invokeMethod(&window, "tick", Qt::DirectConnection);
    bool seek_ui_ok = song_is_playing() && song_get_timer() >= 550 &&
                      song_get_timer() <= 650 && progress_slider->value() >= 550 &&
                      progress_slider->value() <= 650 && !midi_any_note_active(60);
    std::printf("  [%s] clicking the progress slider seeks\n", seek_ui_ok ? "ok" : "FAIL");
    if (!seek_ui_ok) ++failures;

    stop_button->click();
    song_close();
    QMetaObject::invokeMethod(&window, "tick", Qt::DirectConnection);
    bool progress_closed_hidden = progress_row && !progress_row->isVisible();

    std::printf("  [%s] progress row hidden when there is no song\n",
                progress_empty_hidden ? "ok" : "FAIL");
    if (!progress_empty_hidden) ++failures;
    std::printf("  [%s] progress row hidden while recording\n",
                progress_recording_hidden ? "ok" : "FAIL");
    if (!progress_recording_hidden) ++failures;
    std::printf("  [%s] progress row appears with a song\n", progress_visible ? "ok" : "FAIL");
    if (!progress_visible) ++failures;

    bool speed_ok = false;
    if (speed_combo) {
      int idx = speed_combo->findData(2.0);
      if (idx >= 0) {
        speed_combo->setCurrentIndex(idx);
        speed_ok = std::fabs(song_get_play_speed() - 2.0) < 0.001;
        speed_combo->setCurrentIndex(speed_combo->findData(1.0));
      }
    }
    std::printf("  [%s] speed combo changes playback speed\n", speed_ok ? "ok" : "FAIL");
    if (!speed_ok) ++failures;

    bool octave_ok = octave0 && octave1 && octave0->minimum() == -4 &&
                     octave0->maximum() == 7 && octave0->text() == "C(0)" &&
                     octave1->text() == "C(0)";
    if (octave_ok) {
      octave0->setValue(-1);
      octave_ok = octave0->text() == "B(-1)";
      octave0->setValue(1);
      octave_ok = octave_ok && octave0->text() == "D(1)";
      octave0->setValue(2);
      octave_ok = octave_ok && octave0->text() == "E(2)";
      octave0->setValue(-4);
      octave_ok = octave_ok && octave0->text() == "F(-4)";
      octave0->setValue(7);
      octave_ok = octave_ok && octave0->text() == "C(7)";
      octave0->setValue(0);
    }
    if (octave_ok) {
      config_set_key_signature(1);
      window.syncControls();
      octave_ok = octave0->text() == "D(0)";
      config_set_key_signature(0);
      window.syncControls();
      octave_ok = octave_ok && octave0->text() == "C(0)";
    }
    std::printf("  [%s] octave labels follow the key (B(-1)..C(7))\n",
                octave_ok ? "ok" : "FAIL");
    if (!octave_ok) ++failures;
    std::printf("  [%s] progress bar follows playback\n", progress_follows ? "ok" : "FAIL");
    if (!progress_follows) ++failures;
    std::printf("  [%s] pause/resume keeps the progress position\n",
                (pause_ui_ok && resume_ui_ok) ? "ok" : "FAIL");
    if (!(pause_ui_ok && resume_ui_ok)) ++failures;
    std::printf("  [%s] progress row hidden after closing the song\n",
                progress_closed_hidden ? "ok" : "FAIL");
    if (!progress_closed_hidden) ++failures;

    std::printf("%s (%d failure%s)\n", failures == 0 ? "UI TEST PASSED" : "UI TEST FAILED",
                failures, failures == 1 ? "" : "s");
    engine_shutdown();
    return failures == 0 ? 0 : 1;
  }

  if (!test_maps.isEmpty()) {
    config_set_output_type(OUTPUT_TYPE_NULL);
    engine_apply_config();
    int failures = run_map_tests(test_maps.toStdString());
    engine_shutdown();
    return failures == 0 ? 0 : 1;
  }

  if (!test_songs.isEmpty()) {
    config_set_output_type(OUTPUT_TYPE_NULL);
    engine_apply_config();
    int failures = run_song_tests(test_songs.toStdString());
    engine_shutdown();
    return failures == 0 ? 0 : 1;
  }

  if (no_audio) {
    config_set_output_type(OUTPUT_TYPE_NULL);
    engine_apply_config();
  }

  if (selftest) {
    if (no_audio) { config_set_output_type(OUTPUT_TYPE_NULL); engine_apply_config(); }
    int result = run_selftest();
    engine_shutdown();
    return result;
  }

  MainWindow window;
  window.show();

  {
    const QStringList value_flags = {"--screenshot", "--shot-dialogs", "--test-songs", "--test-maps"};
    for (int i = 1; i < args.size(); ++i) {
      const QString arg = args.at(i);
      if (value_flags.contains(arg)) { ++i; continue; }
      if (arg.startsWith("-")) continue;
      if (arg.endsWith(".fpm", Qt::CaseInsensitive)) {
        window.openSongFile(arg);
      } else if (arg.endsWith(".map", Qt::CaseInsensitive)) {
        if (config_set_keymap(arg.toStdString()) == 0) {
          window.syncControls();
          window.refreshKeymapMenu();
        }
      }
      break;
    }
  }

  if (demo) {
    QTimer::singleShot(250, &app, [] {
      song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_A, 1, true);
      song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_D, 1, true);
      song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_G, 1, true);
      song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_J, 1, true);
      song_send_event(SM_SYSTEM, SMS_KEY_EVENT, DIK_Q, 1, true);
    });
  }

  if (!shot_dialogs.isEmpty()) {
    QTimer::singleShot(300, &app, [&window, shot_dialogs, &app]() {
      SettingsDialog settings(&window);
      settings.resize(620, 520);
      settings.show();
      if (auto* tabs = settings.findChild<QTabWidget*>()) tabs->setCurrentIndex(1);
      QApplication::processEvents();
      settings.grab().save(shot_dialogs + "/settings.png");
      settings.close();

      KeymapEditorDialog editor(&window);
      editor.resize(760, 620);
      editor.show();
      QApplication::processEvents();
      editor.grab().save(shot_dialogs + "/keymap.png");
      editor.close();

      // Demo the user's actual Space binding: Sustain In_0 Flip 0 + label.
      config_bind_clear_keydown(DIK_SPACE);
      config_bind_clear_keyup(DIK_SPACE);
      config_bind_add_keydown(DIK_SPACE, key_bind_t(SM_SUSTAIN, 0, SM_VALUE_FLIP, 0));
      config_bind_set_label(DIK_SPACE, "FreePiano");
      KeyEditDialog keyedit(DIK_SPACE, &window);
      keyedit.resize(560, 460);
      keyedit.show();
      QApplication::processEvents();
      keyedit.grab().save(shot_dialogs + "/keyedit.png");
      keyedit.close();

      AboutDialog about(&window);
      about.show();
      QApplication::processEvents();
      about.grab().save(shot_dialogs + "/about.png");
      about.close();

      app.quit();
    });
  } else if (!screenshot.isEmpty()) {
    QTimer::singleShot(700, &app, [&window, screenshot, &app]() {
      QPixmap image = window.grab();
      if (!image.save(screenshot)) std::fprintf(stderr, "cannot save %s\n", screenshot.toUtf8().constData());
      app.quit();
    });
  }

  int result = app.exec();
  engine_shutdown();
  return result;
}
