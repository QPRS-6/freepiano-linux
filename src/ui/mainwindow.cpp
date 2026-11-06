#include "mainwindow.h"

#include "aboutdialog.h"
#include "core/config.h"
#include "core/engine.h"
#include "core/keyboard.h"
#include "core/keycodes.h"
#include "core/midi.h"
#include "core/song.h"
#include "keyboardwidget.h"
#include "keyeditdialog.h"
#include "keymapeditordialog.h"
#include "lang.h"
#include "pianowidget.h"
#include "qtkeymap.h"
#include "settingsdialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>

namespace {
QString format_time(double ms) {
  if (ms < 0) ms = 0;
  int total = (int)ms;
  int minutes = total / 60000;
  int seconds = (total / 1000) % 60;
  int tenths = (total / 100) % 10;
  return QString("%1:%2.%3")
      .arg(minutes, 2, 10, QChar('0'))
      .arg(seconds, 2, 10, QChar('0'))
      .arg(tenths);
}

QFrame* separator(QWidget* parent) {
  auto* line = new QFrame(parent);
  line->setFrameShape(QFrame::VLine);
  line->setFrameShadow(QFrame::Sunken);
  line->setFixedHeight(22);
  return line;
}

QLabel* control_label(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setStyleSheet("color:#9a9aa2;");
  return label;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("FreePiano Linux");
  resize(1120, 660);
  setFocusPolicy(Qt::StrongFocus);

  QIcon icon(QString::fromStdString(config_media_path("icon.png")));
  if (!icon.isNull()) setWindowIcon(icon);

  buildMenus();
  buildControls();

  timer_ = new QTimer(this);
  timer_->setInterval(33);
  connect(timer_, &QTimer::timeout, this, &MainWindow::tick);
  timer_->start();

  syncControls();
  updateStatusBar();
  updateWindowTitle();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildControls() {
  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setSpacing(8);

  auto* bar = new QHBoxLayout();
  bar->setSpacing(6);

  auto make_button = [&](const QString& text, const QString& tip, const char* slot) {
    auto* button = new QPushButton(text, central);
    button->setFixedSize(36, 30);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tip);
    connect(button, SIGNAL(clicked()), this, slot);
    return button;
  };

  auto* record_button = make_button(QString::fromUtf8("●"), fp_tr("录制", "Record"), SLOT(onRecord()));
  record_button->setStyleSheet("QPushButton{color:#c62828;font-size:18px;}");
  auto* play_button = make_button(QString::fromUtf8("▶"), fp_tr("播放", "Play"), SLOT(onPlay()));
  play_button->setStyleSheet("QPushButton{font-size:14px;}");
  auto* stop_button = make_button(QString::fromUtf8("■"), fp_tr("停止", "Stop"), SLOT(onStop()));
  stop_button->setStyleSheet("QPushButton{font-size:14px;}");

  bar->addWidget(record_button);
  bar->addWidget(play_button);
  bar->addWidget(stop_button);
  bar->addWidget(separator(central));

  bar->addWidget(control_label(fp_tr("分组", "Group"), central));
  group_spin_ = new QSpinBox(central);
  group_spin_->setRange(0, 0);
  group_spin_->setFixedWidth(58);
  group_spin_->setFocusPolicy(Qt::NoFocus);
  connect(group_spin_, SIGNAL(valueChanged(int)), this, SLOT(onGroupChanged(int)));
  bar->addWidget(group_spin_);

  bar->addWidget(control_label(fp_tr("曲调", "Key"), central));
  key_sign_spin_ = new QSpinBox(central);
  key_sign_spin_->setRange(-4, 7);
  key_sign_spin_->setFixedWidth(52);
  key_sign_spin_->setFocusPolicy(Qt::NoFocus);
  key_sign_spin_->setToolTip(fp_tr("跟随曲调的通道使用的移调量", "Key signature offset applied to channels that follow the key"));
  connect(key_sign_spin_, SIGNAL(valueChanged(int)), this, SLOT(onKeySignatureChanged(int)));
  bar->addWidget(key_sign_spin_);

  bar->addWidget(control_label(fp_tr("力度", "Velocity"), central));
  vel0_spin_ = new QSpinBox(central);
  vel1_spin_ = new QSpinBox(central);
  for (QSpinBox* spin : {vel0_spin_, vel1_spin_}) {
    spin->setRange(0, 127);
    spin->setFixedWidth(56);
    spin->setFocusPolicy(Qt::NoFocus);
    connect(spin, SIGNAL(valueChanged(int)), this, SLOT(onVelocityChanged()));
    bar->addWidget(spin);
  }

  bar->addWidget(control_label(fp_tr("八度", "Octave"), central));
  oct0_spin_ = new QSpinBox(central);
  oct1_spin_ = new QSpinBox(central);
  for (QSpinBox* spin : {oct0_spin_, oct1_spin_}) {
    spin->setRange(-1, 1);
    spin->setFixedWidth(50);
    spin->setFocusPolicy(Qt::NoFocus);
    connect(spin, SIGNAL(valueChanged(int)), this, SLOT(onOctaveChanged()));
    bar->addWidget(spin);
  }

  program_label_ = control_label(fp_tr("音色", "Program"), central);
  bar->addWidget(program_label_);
  program_spin_ = new QSpinBox(central);
  program_spin_->setRange(0, 127);
  program_spin_->setFixedWidth(56);
  program_spin_->setFocusPolicy(Qt::NoFocus);
  connect(program_spin_, SIGNAL(valueChanged(int)), this, SLOT(onProgramChanged(int)));
  bar->addWidget(program_spin_);

  bar->addWidget(separator(central));
  bar->addWidget(control_label(fp_tr("音量", "Volume"), central));
  volume_slider_ = new QSlider(Qt::Horizontal, central);
  volume_slider_->setRange(0, 200);
  volume_slider_->setFixedWidth(120);
  volume_slider_->setFocusPolicy(Qt::NoFocus);
  connect(volume_slider_, SIGNAL(valueChanged(int)), this, SLOT(onVolumeChanged(int)));
  bar->addWidget(volume_slider_);

  bar->addStretch(1);

  sustain_button_ = new QPushButton(fp_tr("延音", "SUS"), central);
  sustain_button_->setObjectName("sustainButton");
  sustain_button_->setCheckable(true);
  sustain_button_->setFixedSize(56, 30);
  sustain_button_->setFocusPolicy(Qt::NoFocus);
  sustain_button_->setToolTip(fp_tr("延音踏板开关（会录入到乐曲）", "Sustain pedal toggle (recorded)"));
  sustain_button_->setStyleSheet(
      "QPushButton:checked{background:#ff9d2e;color:#3a2000;font-weight:bold;}");
  connect(sustain_button_, &QPushButton::toggled, this, &MainWindow::onSustainToggled);
  bar->addWidget(sustain_button_);

  instrument_label_ = new QLabel(central);
  instrument_label_->setStyleSheet("color:#9a9aa2;");
  bar->addWidget(instrument_label_);

  layout->addLayout(bar);

  keyboard_ = new KeyboardWidget(central);
  keyboard_->setMinimumHeight(160);
  keyboard_->setMaximumHeight(340);
  layout->addWidget(keyboard_, 0);

  piano_ = new PianoWidget(central);
  layout->addWidget(piano_, 1);

  connect(piano_, &PianoWidget::notePressed, this, &MainWindow::onPianoPressed);
  connect(piano_, &PianoWidget::noteReleased, this, &MainWindow::onPianoReleased);
  connect(keyboard_, &KeyboardWidget::editKeyRequested, this, &MainWindow::onEditKey);

  setCentralWidget(central);

  status_label_ = new QLabel(this);
  status_label_->setStyleSheet("color:#8a8a92;");
  statusBar()->addWidget(status_label_, 1);
  time_label_ = new QLabel(this);
  time_label_->setStyleSheet("color:#c8c8d0;font-family:monospace;");
  statusBar()->addPermanentWidget(time_label_);
}

void MainWindow::buildMenus() {
  QMenu* file = menuBar()->addMenu(fp_tr("文件", "File"));
  file->addAction(fp_tr("打开乐曲...", "Open song..."), QKeySequence::Open, this, &MainWindow::onOpenSong);
  file->addAction(fp_tr("保存乐曲", "Save song"), QKeySequence::Save, this, &MainWindow::onSaveSong);
  file->addAction(fp_tr("另存为...", "Save as..."), QKeySequence("Ctrl+Shift+S"), this, &MainWindow::onSaveSongAs);
  file->addSeparator();
  file->addAction(fp_tr("导出 WAV...", "Export WAV..."), QKeySequence("Ctrl+E"), this, &MainWindow::onExportWav);
  file->addSeparator();
  file->addAction(fp_tr("退出", "Quit"), QKeySequence::Quit, this, &QWidget::close);

  QMenu* song_menu = menuBar()->addMenu(fp_tr("乐曲", "Song"));
  play_action_ = song_menu->addAction(fp_tr("播放", "Play"), QKeySequence("Ctrl+P"), this, &MainWindow::onPlay);
  record_action_ = song_menu->addAction(fp_tr("录制 / 结束录制", "Record / stop recording"),
                                        QKeySequence("Ctrl+R"), this, &MainWindow::onRecord);
  stop_action_ = song_menu->addAction(fp_tr("停止", "Stop"), QKeySequence("Ctrl+."), this, &MainWindow::onStop);

  QMenu* keyboard = menuBar()->addMenu(fp_tr("键位", "Key map"));
  map_menu_ = keyboard->addMenu(fp_tr("键位映射", "Key maps"));
  keyboard->addSeparator();
  keyboard->addAction(fp_tr("打开键位文件...", "Open key map file..."), this, &MainWindow::onOpenMapFile);
  keyboard->addAction(fp_tr("编辑当前键位...", "Edit current key map..."), this, &MainWindow::onEditMap);
  keyboard->addAction(fp_tr("默认键位", "Default key map"), this, &MainWindow::onDefaultMap);
  keyboard->addSeparator();
  keyboard->addAction(fp_tr("设置...", "Settings..."), QKeySequence("Ctrl+,"), this, &MainWindow::onSettings);

  QMenu* instrument = menuBar()->addMenu(fp_tr("乐器", "Instrument"));
  instrument->addAction(fp_tr("使用内置 mdaPiano 钢琴（FreePiano 默认）",
                              "Use built-in mdaPiano (FreePiano default)"),
                        this, [this] {
                          engine_select_instrument_mda_piano();
                          syncControls();
                          updateStatusBar();
                        });

  QMenu* presets = instrument->addMenu(fp_tr("mdaPiano 预设", "mdaPiano presets"));
  static const char* kPresetNames[] = {
      "mda Piano", "Plain Piano", "Compressed Piano", "Dance Piano",
      "Concert Piano", "Dark Piano", "School Piano", "Broken Piano"};
  for (int i = 0; i < 8; ++i) {
    QAction* action = presets->addAction(QString::fromLatin1(kPresetNames[i]));
    connect(action, &QAction::triggered, this, [this, i] {
      engine_select_instrument_mda_piano();
      int out_ch = config_get_output_channel(0);
      song_output_event((byte)(SM_MIDI_PROGRAM | out_ch), (byte)i, 0, 0);
      syncControls();
      updateStatusBar();
      statusBar()->showMessage(
          fp_tr("mdaPiano 预设: ", "mdaPiano preset: ") + QString::fromLatin1(kPresetNames[i]), 3000);
    });
  }

  instrument->addAction(fp_tr("使用内置 GM 合成器", "Use built-in GM synthesizer"), this, [this] {
    engine_select_instrument_builtin();
    syncControls();
    updateStatusBar();
  });
  instrument->addAction(fp_tr("加载 SoundFont...", "Load SoundFont..."), this, [this] {
    QString path = QFileDialog::getOpenFileName(
        this, fp_tr("选择 SoundFont", "Select SoundFont"), QString(),
        fp_tr("SoundFont (*.sf2 *.sf3);;所有文件 (*)", "SoundFont (*.sf2 *.sf3);;All files (*)"));
    if (path.isEmpty()) return;
    engine_select_soundfont(path.toStdString());
    syncControls();
    updateStatusBar();
    if (!engine_error().empty()) warn(QString::fromStdString(engine_error()));
  });
  instrument->addSeparator();
  instrument->addAction(fp_tr("音频与 MIDI 设置...", "Audio and MIDI settings..."), this, &MainWindow::onSettings);

  QMenu* help = menuBar()->addMenu(fp_tr("帮助", "Help"));
  help->addAction(fp_tr("使用说明", "How to play"), this, [this] {
    QMessageBox::information(
        this, fp_tr("使用说明", "How to play"),
        fp_tr(
            "<b>用电脑键盘演奏</b><br>"
            "窗口获得焦点后直接按键即可演奏，屏幕上的键盘会同步高亮。<br>"
            "F5/F6、F9/F10 等按键在默认键位中用来切换八度和力度。<br><br>"
            "<b>鼠标</b><br>左键点击上方屏幕键盘或下方钢琴可以演奏；"
            "右键点击上方屏幕键盘可修改该键的功能（音符/通道/八度/延音等）。<br><br>"
            "<b>乐曲</b><br>● 开始/结束录制，▶ 播放，■ 停止。乐曲保存为 FreePiano 的 .fpm 格式，"
            "可与 Windows 版互相打开。<br><br>"
            "<b>键位文件</b><br>菜单「键位」可以载入 FreePiano 的 .map 键位文件，"
            "也可以用文本方式编辑。",
            "<b>Play with your computer keyboard</b><br>"
            "When the window has focus, simply type. The on-screen keyboard highlights in sync.<br>"
            "F5/F6, F9/F10 etc. change octave and velocity in the default map.<br><br>"
            "<b>Mouse</b><br>Left-click the on-screen computer keyboard or the piano below to play; "
            "right-click a computer-keyboard key to change its function.<br><br>"
            "<b>Songs</b><br>● starts/stops recording, ▶ plays, ■ stops. Songs are stored as FreePiano .fpm files "
            "and are interchangeable with the Windows version.<br><br>"
            "<b>Key maps</b><br>The Key map menu loads original FreePiano .map files, and the editor lets you "
            "edit them as text."));
  });
  help->addAction(fp_tr("关于", "About"), this, &MainWindow::onAbout);

  refreshKeymapMenu();
}

void MainWindow::refreshKeymapMenu() {
  if (!map_menu_) return;
  map_menu_->clear();

  auto* group = new QActionGroup(this);
  group->setExclusive(true);

  std::vector<std::string> maps = keyboard_enum_keymap();
  for (const auto& path : maps) {
    QString name = QFileInfo(QString::fromStdString(path)).fileName();
    QAction* action = map_menu_->addAction(name);
    action->setCheckable(true);
    action->setData(QString::fromStdString(path));
    group->addAction(action);
    if (name.toStdString() == config_get_keymap()) action->setChecked(true);
  }
  if (maps.empty()) {
    QAction* empty = map_menu_->addAction(fp_tr("(没有找到 .map 文件)", "(no .map files found)"));
    empty->setEnabled(false);
  }
  map_menu_->addSeparator();
  map_menu_->addAction(fp_tr("重新扫描", "Rescan"), this, &MainWindow::refreshKeymapMenu);

  connect(group, &QActionGroup::triggered, this, [this](QAction* action) {
    QString path = action->data().toString();
    if (path.isEmpty()) return;
    if (config_set_keymap(path.toStdString()) != 0) {
      warn(QString::fromStdString(config_get_last_error()));
      return;
    }
    syncControls();
    keyboard_->update();
    statusBar()->showMessage(fp_tr("已载入键位: ", "Loaded key map: ") + QFileInfo(path).fileName(), 4000);
  });
}

void MainWindow::syncControls() {
  QSignalBlocker b1(group_spin_);
  QSignalBlocker b2(key_sign_spin_);
  QSignalBlocker b3(vel0_spin_);
  QSignalBlocker b4(vel1_spin_);
  QSignalBlocker b5(oct0_spin_);
  QSignalBlocker b6(oct1_spin_);
  QSignalBlocker b7(program_spin_);
  QSignalBlocker b8(volume_slider_);

  int groups = (int)config_get_setting_group_count();
  group_spin_->setRange(0, std::max(0, groups - 1));
  group_spin_->setValue((int)config_get_setting_group());
  key_sign_spin_->setValue(config_get_key_signature());
  vel0_spin_->setValue(config_get_key_velocity(0));
  vel1_spin_->setValue(config_get_key_velocity(1));
  oct0_spin_->setValue(config_get_key_octshift(0));
  oct1_spin_->setValue(config_get_key_octshift(1));
  int out_ch = config_get_output_channel(0);
  int instrument_type = config_get_instrument_type();
  if (instrument_type == INSTRUMENT_TYPE_MDA_PIANO) {
    program_spin_->setRange(0, 7);
    program_label_->setText(fp_tr("预设", "Preset"));
  } else {
    program_spin_->setRange(0, 127);
    program_label_->setText(fp_tr("音色", "Program"));
  }
  int program = config_get_program((uint8_t)(SM_OUTPUT_0 + out_ch));
  if (program < 0 || program > program_spin_->maximum()) program = 0;
  program_spin_->setValue(program);
  volume_slider_->setValue(config_get_output_volume());

  instrument_label_->setText(QString::fromStdString(engine_instrument_name()));

  bool sustain =
      config_get_controller((uint8_t)(SM_OUTPUT_0 + config_get_output_channel(0)), 64) >= 64;
  QSignalBlocker sustain_block(sustain_button_);
  sustain_button_->setChecked(sustain);
}

void MainWindow::tick() {
  // Follow state changes made by playback / key-map scripts.
  if ((int)config_get_setting_group() != group_spin_->value()) {
    QSignalBlocker block(group_spin_);
    group_spin_->setValue((int)config_get_setting_group());
  }
  if ((int)config_get_key_signature() != key_sign_spin_->value()) {
    QSignalBlocker block(key_sign_spin_);
    key_sign_spin_->setValue(config_get_key_signature());
  }
  if ((int)config_get_key_velocity(0) != vel0_spin_->value()) {
    QSignalBlocker block(vel0_spin_);
    vel0_spin_->setValue(config_get_key_velocity(0));
  }
  if ((int)config_get_key_velocity(1) != vel1_spin_->value()) {
    QSignalBlocker block(vel1_spin_);
    vel1_spin_->setValue(config_get_key_velocity(1));
  }
  if ((int)config_get_key_octshift(0) != oct0_spin_->value()) {
    QSignalBlocker block(oct0_spin_);
    oct0_spin_->setValue(config_get_key_octshift(0));
  }
  if ((int)config_get_key_octshift(1) != oct1_spin_->value()) {
    QSignalBlocker block(oct1_spin_);
    oct1_spin_->setValue(config_get_key_octshift(1));
  }

  bool sustain = config_get_controller((uint8_t)(SM_OUTPUT_0 + config_get_output_channel(0)), 64) >= 64;
  if (sustain_button_ && sustain_button_->isChecked() != sustain) {
    QSignalBlocker block(sustain_button_);
    sustain_button_->setChecked(sustain);
  }

  double timer = song_get_timer();
  if (!song_is_playing() && !song_is_recording()) timer = 0;
  time_label_->setText(format_time(timer));

  int out_ch = config_get_output_channel(0);
  int program = config_get_program((uint8_t)(SM_OUTPUT_0 + out_ch));
  int max_program = config_get_instrument_type() == INSTRUMENT_TYPE_MDA_PIANO ? 7 : 127;
  if (program >= 0 && program < 128) {
    if (program > max_program) program = 0;
    if (program != program_spin_->value()) {
      QSignalBlocker block(program_spin_);
      program_spin_->setValue(program);
    }
  }
  if (config_get_output_volume() != volume_slider_->value()) {
    QSignalBlocker block(volume_slider_);
    volume_slider_->setValue(config_get_output_volume());
  }

  if (play_action_) play_action_->setEnabled(!song_is_empty() && !song_is_recording());
  if (record_action_) record_action_->setEnabled(!song_is_playing());
  if (stop_action_) stop_action_->setEnabled(song_is_playing() || song_is_recording());

  keyboard_->update();
  piano_->update();

  static int counter = 0;
  if (++counter % 30 == 0) updateStatusBar();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  if (event->isAutoRepeat()) {
    event->accept();
    return;
  }
  uint8_t dik = dik_from_qt(event->key(), event->nativeScanCode(), event->nativeVirtualKey());
  if (dik) {
    song_send_event(SM_SYSTEM, SMS_KEY_EVENT, dik, 1, true);
    keyboard_->update();
    event->accept();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event) {
  if (event->isAutoRepeat()) {
    event->accept();
    return;
  }
  uint8_t dik = dik_from_qt(event->key(), event->nativeScanCode(), event->nativeVirtualKey());
  if (dik) {
    song_send_event(SM_SYSTEM, SMS_KEY_EVENT, dik, 0, true);
    keyboard_->update();
    event->accept();
    return;
  }
  QMainWindow::keyReleaseEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  setFocus();
  keyboard_->update();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (keyboard_) {
    int width = centralWidget() ? centralWidget()->width() : this->width();
    int height = std::clamp((int)(width * 0.245), 170, 340);
    keyboard_->setFixedHeight(height);
  }
}

void MainWindow::focusOutEvent(QFocusEvent* event) {
  // Never leave notes stuck when the user switches to another window, and
  // record the releases so a recording does not keep dangling note-ons.
  for (int code = 0; code < 256; ++code) {
    if (keyboard_get_status((byte)code)) {
      song_send_event(SM_SYSTEM, SMS_KEY_EVENT, (byte)code, 0, true);
    }
  }
  keyboard_reset();
  QMainWindow::focusOutEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  song_stop_record();
  song_stop_playback();
  keyboard_reset();
  config_save("freepiano.cfg");
  event->accept();
}

void MainWindow::onPlay() {
  if (song_is_empty()) {
    statusBar()->showMessage(fp_tr("没有可播放的乐曲，请先录制或打开 .fpm 文件。",
                                   "Nothing to play. Record something or open a .fpm file."),
                             5000);
    return;
  }
  song_start_playback();
  setFocus();
}

void MainWindow::onRecord() {
  if (song_is_playing()) song_stop_playback();
  if (song_is_recording()) {
    song_stop_record();
    statusBar()->showMessage(fp_tr("录制结束", "Recording stopped"), 3000);
    updateWindowTitle();
  } else {
    song_start_record();
    statusBar()->showMessage(fp_tr("开始录制", "Recording..."), 3000);
    updateWindowTitle();
  }
  setFocus();
}

void MainWindow::onStop() {
  song_stop_record();
  song_stop_playback();
  updateWindowTitle();
  setFocus();
}

void MainWindow::onOpenSong() {
  QString path = QFileDialog::getOpenFileName(
      this, fp_tr("打开乐曲", "Open song"), QString(),
      fp_tr("FreePiano 乐曲 (*.fpm);;所有文件 (*)", "FreePiano songs (*.fpm);;All files (*)"));
  if (!path.isEmpty()) openSongFile(path);
}

void MainWindow::openSongFile(const QString& path) {
  if (song_open(path.toStdString()) != 0) {
    warn(QString::fromStdString(config_get_last_error()));
    return;
  }
  song_path_ = path;
  song_info_t* info = song_get_info();
  if (info->title[0] == 0) {
    std::snprintf(info->title, sizeof(info->title), "%s",
                  QFileInfo(path).completeBaseName().toStdString().c_str());
  }
  updateWindowTitle();
  statusBar()->showMessage(fp_tr("已打开: ", "Opened: ") + QFileInfo(path).fileName(), 4000);
  setFocus();
}

void MainWindow::onSaveSong() {
  if (song_is_empty()) {
    warn(fp_tr("还没有可保存的内容。", "There is nothing to save yet."));
    return;
  }
  song_info_t* info = song_get_info();
  if (song_path_.isEmpty() || info->write_protected) {
    onSaveSongAs();
    return;
  }
  if (song_save(song_path_.toStdString()) != 0) {
    warn(fp_tr("保存失败。", "Could not save the song."));
    return;
  }
  song_get_info()->write_protected = true;
  statusBar()->showMessage(fp_tr("已保存: ", "Saved: ") + QFileInfo(song_path_).fileName(), 3000);
}

void MainWindow::onSaveSongAs() {
  if (song_is_empty()) {
    warn(fp_tr("还没有可保存的内容。", "There is nothing to save yet."));
    return;
  }
  QString path = QFileDialog::getSaveFileName(
      this, fp_tr("保存乐曲", "Save song"), song_path_.isEmpty() ? "song.fpm" : song_path_,
      fp_tr("FreePiano 乐曲 (*.fpm)", "FreePiano songs (*.fpm)"));
  if (path.isEmpty()) return;
  if (!path.endsWith(".fpm", Qt::CaseInsensitive)) path += ".fpm";

  song_info_t* info = song_get_info();
  if (info->title[0] == 0) {
    std::snprintf(info->title, sizeof(info->title), "%s",
                  QFileInfo(path).completeBaseName().toStdString().c_str());
  }
  if (song_save(path.toStdString()) != 0) {
    warn(fp_tr("保存失败。", "Could not save the song."));
    return;
  }
  info->write_protected = true;
  song_path_ = path;
  updateWindowTitle();
  statusBar()->showMessage(fp_tr("已保存: ", "Saved: ") + QFileInfo(path).fileName(), 3000);
}

bool MainWindow::exportWavFile(const QString& path, QString* error) {
  QApplication::setOverrideCursor(Qt::WaitCursor);
  std::string err;
  bool ok = engine_export_wav(path.toStdString(), &err);
  QApplication::restoreOverrideCursor();
  if (!ok && error) *error = QString::fromStdString(err.empty() ? "Export failed." : err);
  syncControls();
  updateStatusBar();
  return ok;
}

void MainWindow::onExportWav() {
  if (song_is_empty()) {
    warn(fp_tr("没有可导出的乐曲。", "There is no song to export."));
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, fp_tr("导出 WAV", "Export WAV"), "song.wav",
                                              fp_tr("WAV 音频 (*.wav)", "WAV audio (*.wav)"));
  if (path.isEmpty()) return;
  if (!path.endsWith(".wav", Qt::CaseInsensitive)) path += ".wav";

  QString error;
  if (!exportWavFile(path, &error)) {
    warn(error);
    return;
  }
  statusBar()->showMessage(fp_tr("已导出: ", "Exported: ") + QFileInfo(path).fileName(), 5000);
}

void MainWindow::onOpenMapFile() {
  QString path = QFileDialog::getOpenFileName(
      this, fp_tr("打开键位文件", "Open key map"), QString::fromStdString(config_data_dir()),
      fp_tr("FreePiano 键位 (*.map);;所有文件 (*)", "FreePiano key maps (*.map);;All files (*)"));
  if (path.isEmpty()) return;
  if (config_set_keymap(path.toStdString()) != 0) {
    warn(QString::fromStdString(config_get_last_error()));
    return;
  }
  syncControls();
  refreshKeymapMenu();
  keyboard_->update();
}

void MainWindow::onEditMap() {
  KeymapEditorDialog dialog(this);
  dialog.exec();
  syncControls();
  refreshKeymapMenu();
  keyboard_->update();
  setFocus();
}

void MainWindow::onDefaultMap() {
  if (config_set_keymap("freepiano.map") != 0) {
    config_default_key_setting();
  }
  syncControls();
  refreshKeymapMenu();
  keyboard_->update();
}

void MainWindow::onSettings() {
  showSettings();
}

void MainWindow::showSettings() {
  SettingsDialog dialog(this);
  if (dialog.exec() == QDialog::Accepted) {
    syncControls();
    updateStatusBar();
    refreshKeymapMenu();
    keyboard_->update();
  }
  setFocus();
}

void MainWindow::onAbout() {
  AboutDialog dialog(this);
  dialog.exec();
  setFocus();
}

void MainWindow::onGroupChanged(int value) {
  config_set_setting_group((uint32_t)std::max(0, value));
  keyboard_->update();
  setFocus();
}

void MainWindow::onKeySignatureChanged(int value) {
  config_set_key_signature((char)value);
  keyboard_->update();
  setFocus();
}

void MainWindow::onVelocityChanged() {
  config_set_key_velocity(0, (uint8_t)vel0_spin_->value());
  config_set_key_velocity(1, (uint8_t)vel1_spin_->value());
  setFocus();
}

void MainWindow::onOctaveChanged() {
  config_set_key_octshift(0, (char)oct0_spin_->value());
  config_set_key_octshift(1, (char)oct1_spin_->value());
  keyboard_->update();
  setFocus();
}

void MainWindow::onVolumeChanged(int value) {
  config_set_output_volume(value);
  setFocus();
}

void MainWindow::onProgramChanged(int value) {
  int out_ch = config_get_output_channel(0);
  song_output_event((byte)(SM_MIDI_PROGRAM | out_ch), (byte)value, 0, 0);
  setFocus();
}

void MainWindow::onPianoPressed(int note) {
  song_send_event(SM_NOTE_ON, 0, (byte)note, config_get_key_velocity(0), true);
}

void MainWindow::onPianoReleased(int note) {
  song_send_event(SM_NOTE_OFF, 0, (byte)note, 0, true);
}

void MainWindow::onSustainToggled(bool on) {
  // Sustain belongs to the current input channel's output channel (In_0);
  // this matches the "Sustain In_0 127" scripts used by the key maps.
  song_send_event(SM_SUSTAIN, 0, SM_VALUE_SET, on ? 127 : 0, true);
  keyboard_->update();
  setFocus();
}

void MainWindow::onEditKey(uint8_t code, const QPoint& /*global_position*/) {
  KeyEditDialog dialog(code, this);
  if (dialog.exec() == QDialog::Accepted) {
    keyboard_->update();
    statusBar()->showMessage(
        fp_tr("已更新按键: ", "Key updated: ") + QString::fromLatin1(config_get_key_name(code)),
        3000);
  }
  setFocus();
}

void MainWindow::updateStatusBar() {
  if (!status_label_) return;
  QString text = QString::fromStdString(engine_status());
  QString map = QString::fromStdString(config_get_keymap());
  if (!map.isEmpty()) text += QString("  |  ") + fp_tr("键位: ", "Map: ") + map;
  QString problem = QString::fromStdString(engine_error());
  if (problem.isEmpty()) problem = QString::fromUtf8(config_get_last_error());
  if (!problem.isEmpty()) text += QString("  |  ! ") + problem;
  status_label_->setText(text);
  if (instrument_label_) instrument_label_->setText(QString::fromStdString(engine_instrument_name()));
}

void MainWindow::updateWindowTitle() {
  QString title = "FreePiano Linux";
  if (!song_path_.isEmpty()) title += "  —  " + QFileInfo(song_path_).fileName();
  if (song_is_recording()) title += fp_tr("  [录制中]", "  [recording]");
  setWindowTitle(title);
}

void MainWindow::warn(const QString& text) {
  QMessageBox::warning(this, "FreePiano", text);
}
