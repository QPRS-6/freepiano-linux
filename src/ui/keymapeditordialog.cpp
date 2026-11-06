#include "keymapeditordialog.h"

#include "core/config.h"
#include "core/keyboard.h"
#include "lang.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

KeymapEditorDialog::KeymapEditorDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(fp_tr("编辑键位", "Edit key map"));
  resize(760, 620);

  auto* layout = new QVBoxLayout(this);

  auto* hint = new QLabel(
      fp_tr("键位文件是 FreePiano 的纯文本 .map 格式，可以直接修改后点击「应用」。\n"
            "例如：keydown a NoteOn In_0 c3    /    keydown F5 Octave In_0 Inc 1",
            "Key maps use FreePiano's plain-text .map format. Edit the text and press Apply.\n"
            "Example: keydown a NoteOn In_0 c3    /    keydown F5 Octave In_0 Inc 1"),
      this);
  hint->setWordWrap(true);
  hint->setStyleSheet("color:#666;");
  layout->addWidget(hint);

  editor_ = new QPlainTextEdit(this);
  editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  editor_->setPlainText(QString::fromStdString(config_save_keymap()));
  layout->addWidget(editor_, 1);

  status_ = new QLabel(this);
  status_->setStyleSheet("color:#666;");
  layout->addWidget(status_);

  auto* buttons = new QDialogButtonBox(this);
  QPushButton* apply = buttons->addButton(fp_tr("应用", "Apply"), QDialogButtonBox::ApplyRole);
  QPushButton* reload = buttons->addButton(fp_tr("重新载入当前键位", "Reload current"), QDialogButtonBox::ResetRole);
  QPushButton* load = buttons->addButton(fp_tr("打开文件...", "Open file..."), QDialogButtonBox::ActionRole);
  QPushButton* save = buttons->addButton(fp_tr("另存为...", "Save as..."), QDialogButtonBox::ActionRole);
  QPushButton* close = buttons->addButton(QDialogButtonBox::Close);
  close->setText(fp_tr("关闭", "Close"));
  layout->addWidget(buttons);

  connect(apply, &QPushButton::clicked, this, &KeymapEditorDialog::applyText);
  connect(reload, &QPushButton::clicked, this, &KeymapEditorDialog::reload);
  connect(load, &QPushButton::clicked, this, &KeymapEditorDialog::loadFile);
  connect(save, &QPushButton::clicked, this, &KeymapEditorDialog::saveFile);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
}

void KeymapEditorDialog::applyText() {
  std::string text = editor_->toPlainText().toStdString();
  int errors = config_apply_keymap_text(text);
  if (errors == 0) {
    status_->setText(fp_tr("已应用。", "Applied."));
  } else {
    status_->setText(QString::fromUtf8(fp_tr("已应用，但有 %1 行无法解析。", "Applied, but %1 line(s) could not be parsed.")).arg(errors));
  }
}

void KeymapEditorDialog::reload() {
  editor_->setPlainText(QString::fromStdString(config_save_keymap()));
  status_->setText(fp_tr("已从当前状态重新生成。", "Regenerated from the current state."));
}

void KeymapEditorDialog::loadFile() {
  QString path = QFileDialog::getOpenFileName(
      this, fp_tr("打开键位文件", "Open key map"), QString::fromStdString(config_data_dir()),
      fp_tr("FreePiano 键位 (*.map);;所有文件 (*)", "FreePiano key maps (*.map);;All files (*)"));
  if (path.isEmpty()) return;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, "FreePiano", fp_tr("无法打开文件。", "Cannot open the file."));
    return;
  }
  editor_->setPlainText(QString::fromUtf8(file.readAll()));
  status_->setText(fp_tr("已载入文件，点击「应用」生效：", "Loaded; press Apply to use it: ") +
                   QFileInfo(path).fileName());
}

void KeymapEditorDialog::saveFile() {
  QString default_dir = QString::fromStdString(config_user_map_dir());
  QString path = QFileDialog::getSaveFileName(
      this, fp_tr("保存键位", "Save key map"), default_dir + "/custom.map",
      fp_tr("FreePiano 键位 (*.map)", "FreePiano key maps (*.map)"));
  if (path.isEmpty()) return;
  if (!path.endsWith(".map", Qt::CaseInsensitive)) path += ".map";

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, "FreePiano", fp_tr("无法写入文件。", "Cannot write the file."));
    return;
  }
  file.write(editor_->toPlainText().toUtf8());
  file.close();

  if (config_set_keymap(path.toStdString()) == 0) {
    status_->setText(fp_tr("已保存并载入: ", "Saved and loaded: ") + QFileInfo(path).fileName());
  }
}
