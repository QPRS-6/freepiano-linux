#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QKeyEvent>
#include <QTimer>

#include <cstdio>
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
  song_close();
  check(song_open(song_path) == 0, "song_open reads .fpm back");
  check(song_get_event_count() == recorded_events, "event count survives round-trip");
  check(song_get_length() == recorded_length, "length survives round-trip");

  song_start_playback();
  check(song_is_playing(), "playback starts");
  int guard = 0;
  while (song_is_playing() && guard++ < 2000) song_update(25);
  check(!song_is_playing(), "playback finishes");

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

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setApplicationName("FreePiano Linux");
  app.setApplicationVersion(FP_VERSION_STRING);
  app.setOrganizationName("freepiano-linux");

  const QStringList args = app.arguments();

  if (args.contains("--help") || args.contains("-h")) {
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
    return 0;
  }
  if (args.contains("--version")) {
    std::printf("FreePiano Linux %s\n", FP_VERSION_STRING);
    return 0;
  }

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
      QApplication::processEvents();
      settings.grab().save(shot_dialogs + "/settings.png");
      settings.close();

      KeymapEditorDialog editor(&window);
      editor.resize(760, 620);
      editor.show();
      QApplication::processEvents();
      editor.grab().save(shot_dialogs + "/keymap.png");
      editor.close();

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
