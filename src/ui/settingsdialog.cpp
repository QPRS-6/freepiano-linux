#include "settingsdialog.h"

#include "core/config.h"
#include "core/engine.h"
#include "lang.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(fp_tr("设置", "Settings"));
  setMinimumWidth(560);
  buildUi();
  loadValues();
}

void SettingsDialog::buildUi() {
  auto* layout = new QVBoxLayout(this);
  auto* tabs = new QTabWidget(this);

  // ---- audio ----
  auto* audio_page = new QWidget(tabs);
  auto* audio_form = new QFormLayout(audio_page);

  output_type_ = new QComboBox(audio_page);
  output_type_->addItem(fp_tr("PortAudio（真实音频输出）", "PortAudio (real audio output)"), OUTPUT_TYPE_PORTAUDIO);
  output_type_->addItem(fp_tr("无音频（仅计时）", "No audio (timer only)"), OUTPUT_TYPE_NULL);
  audio_form->addRow(fp_tr("输出方式", "Output"), output_type_);

  device_ = new QComboBox(audio_page);
  device_->setEditable(false);
  audio_form->addRow(fp_tr("音频设备", "Audio device"), device_);

  rate_ = new QComboBox(audio_page);
  for (int rate : {44100, 48000, 96000}) rate_->addItem(QString::number(rate) + " Hz", rate);
  audio_form->addRow(fp_tr("采样率", "Sample rate"), rate_);

  buffer_ = new QComboBox(audio_page);
  for (int frames : {128, 256, 512, 1024}) buffer_->addItem(QString::number(frames), frames);
  audio_form->addRow(fp_tr("缓冲帧数", "Buffer frames"), buffer_);

  volume_ = new QSlider(Qt::Horizontal, audio_page);
  volume_->setRange(0, 200);
  audio_form->addRow(fp_tr("总音量", "Master volume"), volume_);

  auto* audio_note = new QLabel(
      fp_tr("提示：较小的缓冲帧数延迟更低，但更容易产生爆音。",
            "Tip: smaller buffers lower latency but are more prone to dropouts."),
      audio_page);
  audio_note->setWordWrap(true);
  audio_note->setStyleSheet("color:#777;");
  audio_form->addRow(audio_note);

  tabs->addTab(audio_page, fp_tr("音频", "Audio"));

  // ---- instrument ----
  auto* inst_page = new QWidget(tabs);
  auto* inst_layout = new QVBoxLayout(inst_page);

  auto* inst_box = new QGroupBox(fp_tr("内部音源", "Internal instrument"), inst_page);
  auto* inst_form = new QFormLayout(inst_box);
  builtin_ = new QRadioButton(fp_tr("内置合成器（无需外部文件）", "Built-in synthesizer (no external file)"), inst_box);
  soundfont_ = new QRadioButton(fp_tr("SoundFont (.sf2 / .sf3)", "SoundFont (.sf2 / .sf3)"), inst_box);
  inst_form->addRow(builtin_);
  inst_form->addRow(soundfont_);

  auto* sf_row = new QWidget(inst_box);
  auto* sf_layout = new QHBoxLayout(sf_row);
  sf_layout->setContentsMargins(0, 0, 0, 0);
  soundfont_path_ = new QLineEdit(sf_row);
  auto* browse = new QPushButton(fp_tr("浏览...", "Browse..."), sf_row);
  sf_layout->addWidget(soundfont_path_, 1);
  sf_layout->addWidget(browse);
  inst_form->addRow(fp_tr("SoundFont 文件", "SoundFont file"), sf_row);

  auto* inst_status = new QLabel(QString::fromStdString(engine_instrument_name()), inst_box);
  inst_status->setStyleSheet("color:#777;");
  inst_form->addRow(fp_tr("当前音源", "Current"), inst_status);

  auto* inst_note = new QLabel(
      fp_tr("FreePiano 在 Windows 上使用 VSTi 插件；Linux 版用内置合成器或 SoundFont 代替，"
            "推荐安装 FluidR3_GM / GeneralUser 等 GM 音色库获得钢琴音色。",
            "The Windows version hosts VSTi plug-ins. On Linux, use the built-in synthesizer or a "
            "SoundFont (e.g. FluidR3_GM / GeneralUser GS) for a realistic piano."),
      inst_box);
  inst_note->setWordWrap(true);
  inst_note->setStyleSheet("color:#777;");
  inst_form->addRow(inst_note);

  inst_layout->addWidget(inst_box);
  inst_layout->addStretch(1);
  tabs->addTab(inst_page, fp_tr("音源", "Instrument"));

  connect(browse, &QPushButton::clicked, this, &SettingsDialog::browseSoundfont);
  connect(builtin_, &QRadioButton::toggled, this, [this](bool on) {
    if (on) soundfont_path_->setEnabled(false);
  });
  connect(soundfont_, &QRadioButton::toggled, this, [this](bool on) {
    if (on) soundfont_path_->setEnabled(true);
  });

  // ---- MIDI ----
  auto* midi_page = new QWidget(tabs);
  auto* midi_form = new QFormLayout(midi_page);

  midi_out_ = new QComboBox(midi_page);
  midi_form->addRow(fp_tr("MIDI 输出端口", "MIDI output port"), midi_out_);
  auto* midi_out_note = new QLabel(
      fp_tr("只有把音源设为「无」时才会向外部 MIDI 端口发送（与 Windows 版行为一致）。",
            "External MIDI output is only used when the instrument is set to None (same as the Windows build)."),
      midi_page);
  midi_out_note->setWordWrap(true);
  midi_out_note->setStyleSheet("color:#777;");
  midi_form->addRow(midi_out_note);

  midi_in_enable_ = new QCheckBox(fp_tr("启用 MIDI 输入", "Enable MIDI input"), midi_page);
  midi_form->addRow(midi_in_enable_);

  midi_in_ = new QComboBox(midi_page);
  midi_form->addRow(fp_tr("MIDI 输入端口", "MIDI input port"), midi_in_);

  midi_remap_ = new QSpinBox(midi_page);
  midi_remap_->setRange(0, 16);
  midi_remap_->setToolTip(fp_tr("0 = 保持原通道；1-16 = 映射到对应的 In 通道",
                                "0 = keep channel; 1-16 = map to In_0..In_15"));
  midi_form->addRow(fp_tr("通道映射", "Channel remap"), midi_remap_);

  tabs->addTab(midi_page, fp_tr("MIDI", "MIDI"));

  // ---- display ----
  auto* disp_page = new QWidget(tabs);
  auto* disp_form = new QFormLayout(disp_page);

  note_display_ = new QComboBox(disp_page);
  note_display_->addItem(fp_tr("简谱数字 (1-7)", "Numbered notation (1-7)"), NOTE_DISPLAY_DOH);
  note_display_->addItem(fp_tr("固定简谱", "Fixed numbered notation"), NOTE_DIAPLAY_FIXED_DOH);
  note_display_->addItem(fp_tr("音名 (C4)", "Note names (C4)"), NOTE_DISPLAY_NAME);
  disp_form->addRow(fp_tr("按键标注", "Key labels"), note_display_);

  auto_color_ = new QComboBox(disp_page);
  auto_color_->addItem(fp_tr("经典（橙色）", "Classic (orange)"), AUTO_COLOR_CLASSIC);
  auto_color_->addItem(fp_tr("按通道着色", "By channel"), AUTO_COLOR_CHANNEL);
  auto_color_->addItem(fp_tr("按力度着色", "By velocity"), AUTO_COLOR_VELOCITY);
  disp_form->addRow(fp_tr("按键颜色", "Key colour"), auto_color_);

  groups_ = new QSpinBox(disp_page);
  groups_->setRange(1, MAX_SETTING_GROUP);
  disp_form->addRow(fp_tr("分组数量", "Setting groups"), groups_);

  tabs->addTab(disp_page, fp_tr("显示", "Display"));

  layout->addWidget(tabs);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                           QDialogButtonBox::Apply,
                                       this);
  buttons->button(QDialogButtonBox::Ok)->setText(fp_tr("确定", "OK"));
  buttons->button(QDialogButtonBox::Cancel)->setText(fp_tr("取消", "Cancel"));
  buttons->button(QDialogButtonBox::Apply)->setText(fp_tr("应用", "Apply"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::apply);
}

void SettingsDialog::loadValues() {
  output_type_->setCurrentIndex(config_get_output_type() == OUTPUT_TYPE_NULL ? 1 : 0);

  device_->clear();
  device_->addItem(fp_tr("（系统默认）", "(system default)"), QString());
  for (const auto& name : engine_audio_devices()) {
    device_->addItem(QString::fromStdString(name), QString::fromStdString(name));
  }
  QString current_device = QString::fromStdString(config_get_output_device());
  int device_index = device_->findData(current_device);
  if (device_index < 0 && !current_device.isEmpty()) {
    device_->addItem(current_device, current_device);
    device_index = device_->count() - 1;
  }
  device_->setCurrentIndex(std::max(0, device_index));

  int rate_index = rate_->findData(config_get_sample_rate());
  if (rate_index < 0) {
    rate_->addItem(QString::number(config_get_sample_rate()) + " Hz", config_get_sample_rate());
    rate_index = rate_->count() - 1;
  }
  rate_->setCurrentIndex(rate_index);

  int buffer_index = buffer_->findData(config_get_buffer_size());
  if (buffer_index < 0) {
    buffer_->addItem(QString::number(config_get_buffer_size()), config_get_buffer_size());
    buffer_index = buffer_->count() - 1;
  }
  buffer_->setCurrentIndex(buffer_index);

  volume_->setValue(config_get_output_volume());

  bool is_sf2 = config_get_instrument_type() == INSTRUMENT_TYPE_SOUNDFONT;
  builtin_->setChecked(!is_sf2);
  soundfont_->setChecked(is_sf2);
  soundfont_path_->setText(QString::fromStdString(config_get_instrument_path()));
  soundfont_path_->setEnabled(is_sf2);

  midi_out_->clear();
  midi_out_->addItem(fp_tr("（不使用）", "(none)"), QString());
  for (const auto& name : engine_midi_output_ports()) {
    midi_out_->addItem(QString::fromStdString(name), QString::fromStdString(name));
  }
  QString current_out = QString::fromStdString(config_get_midi_output_port());
  int out_index = midi_out_->findData(current_out);
  if (out_index < 0 && !current_out.isEmpty()) {
    midi_out_->addItem(current_out, current_out);
    out_index = midi_out_->count() - 1;
  }
  midi_out_->setCurrentIndex(std::max(0, out_index));

  midi_in_->clear();
  midi_in_->addItem(fp_tr("（不使用）", "(none)"), QString());
  for (const auto& name : engine_midi_input_ports()) {
    midi_in_->addItem(QString::fromStdString(name), QString::fromStdString(name));
  }
  midi_in_enable_->setChecked(false);
  midi_remap_->setValue(0);
  for (const auto& kv : config_get_midi_inputs()) {
    if (kv.second.enable) {
      int index = midi_in_->findData(QString::fromStdString(kv.first));
      if (index < 0) {
        midi_in_->addItem(QString::fromStdString(kv.first), QString::fromStdString(kv.first));
        index = midi_in_->count() - 1;
      }
      midi_in_->setCurrentIndex(index);
      midi_in_enable_->setChecked(true);
      midi_remap_->setValue(kv.second.remap);
      break;
    }
  }

  note_display_->setCurrentIndex(note_display_->findData(config_get_note_display()));
  auto_color_->setCurrentIndex(auto_color_->findData(config_get_auto_color()));
  groups_->setValue((int)config_get_setting_group_count());
}

void SettingsDialog::browseSoundfont() {
  QString path = QFileDialog::getOpenFileName(
      this, fp_tr("选择 SoundFont", "Select SoundFont"), soundfont_path_->text(),
      fp_tr("SoundFont (*.sf2 *.sf3);;所有文件 (*)", "SoundFont (*.sf2 *.sf3);;All files (*)"));
  if (!path.isEmpty()) {
    soundfont_path_->setText(path);
    soundfont_->setChecked(true);
  }
}

void SettingsDialog::apply() {
  config_set_output_type(output_type_->currentData().toInt());
  config_set_output_device(device_->currentData().toString().toStdString());
  config_set_sample_rate(rate_->currentData().toInt());
  config_set_buffer_size(buffer_->currentData().toInt());
  config_set_output_volume(volume_->value());

  if (soundfont_->isChecked() && !soundfont_path_->text().isEmpty()) {
    config_select_instrument(INSTRUMENT_TYPE_SOUNDFONT, soundfont_path_->text().toStdString());
  } else {
    config_select_instrument(INSTRUMENT_TYPE_BUILTIN, "");
  }

  config_set_midi_output_port(midi_out_->currentData().toString().toStdString());

  // only one input device can be subscribed at a time
  std::vector<std::string> names;
  for (const auto& kv : config_get_midi_inputs()) names.push_back(kv.first);
  for (const auto& name : names) {
    midi_input_config_t cfg;
    cfg.enable = false;
    cfg.remap = 0;
    config_set_midi_input_config(name, cfg);
  }
  if (midi_in_enable_->isChecked()) {
    QString port = midi_in_->currentData().toString();
    if (!port.isEmpty()) {
      midi_input_config_t cfg;
      cfg.enable = true;
      cfg.remap = midi_remap_->value();
      config_set_midi_input_config(port.toStdString(), cfg);
    }
  }

  config_set_note_display((uint8_t)note_display_->currentData().toInt());
  config_set_auto_color((uint8_t)auto_color_->currentData().toInt());
  if ((int)config_get_setting_group_count() != groups_->value()) {
    config_set_setting_group_count((uint32_t)groups_->value());
  }

  engine_apply_config();
}

void SettingsDialog::accept() {
  apply();
  QDialog::accept();
}
