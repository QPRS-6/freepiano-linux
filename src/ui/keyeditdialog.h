#pragma once

#include <QDialog>

#include <cstdint>

#include "core/fp_types.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

// Edit the function bound to one computer-keyboard key.
class KeyEditDialog : public QDialog {
  Q_OBJECT
 public:
  explicit KeyEditDialog(uint8_t key_code, QWidget* parent = nullptr);

 private slots:
  void onActionChanged();
  void clearBinding();

 private:
  void buildUi();
  void loadCurrentBinding();
  void updateFields();
  void showRow(QWidget* field, bool visible);
  void apply();

  uint8_t key_code_;
  key_bind_t current_binding_;

  QFormLayout* form_ = nullptr;
  QComboBox* action_ = nullptr;
  QComboBox* channel_ = nullptr;
  QComboBox* note_ = nullptr;
  QComboBox* op_ = nullptr;
  QSpinBox* value_ = nullptr;
  QSpinBox* velocity_ = nullptr;
  QCheckBox* group_velocity_ = nullptr;
  QLineEdit* label_ = nullptr;
  QLabel* hint_ = nullptr;
  QLabel* current_text_ = nullptr;

  int kind_ = 0;
  bool loading_ = false;
};
