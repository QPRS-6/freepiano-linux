#pragma once

#include <QDialog>

class QLabel;
class QPlainTextEdit;

class KeymapEditorDialog : public QDialog {
  Q_OBJECT
 public:
  explicit KeymapEditorDialog(QWidget* parent = nullptr);

 private slots:
  void applyText();
  void reload();
  void loadFile();
  void saveFile();

 private:
  QPlainTextEdit* editor_ = nullptr;
  QLabel* status_ = nullptr;
};
