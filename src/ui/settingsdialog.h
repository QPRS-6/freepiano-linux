#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
class QSlider;
class QSpinBox;

class SettingsDialog : public QDialog {
  Q_OBJECT
 public:
  explicit SettingsDialog(QWidget* parent = nullptr);

 private slots:
  void browseSoundfont();
  void apply();
  void accept() override;

 private:
  void buildUi();
  void loadValues();

  QComboBox* device_ = nullptr;
  QComboBox* output_type_ = nullptr;
  QComboBox* rate_ = nullptr;
  QComboBox* buffer_ = nullptr;
  QSlider* volume_ = nullptr;

  QRadioButton* mda_piano_ = nullptr;
  QRadioButton* builtin_ = nullptr;
  QRadioButton* soundfont_ = nullptr;
  QLineEdit* soundfont_path_ = nullptr;

  QComboBox* midi_out_ = nullptr;
  QComboBox* midi_in_ = nullptr;
  QCheckBox* midi_in_enable_ = nullptr;
  QSpinBox* midi_remap_ = nullptr;

  QComboBox* note_display_ = nullptr;
  QComboBox* auto_color_ = nullptr;
  QSpinBox* groups_ = nullptr;
};
