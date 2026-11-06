#include "keyeditdialog.h"

#include "core/config.h"
#include "core/events.h"
#include "core/keycodes.h"
#include "lang.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum Kind {
  KIND_NOTE,
  KIND_CHANNEL,
  KIND_GLOBAL,
  KIND_GROUP_COUNT,
  KIND_TRANSPORT,
};

struct ActionDef {
  int value;
  Kind kind;
  const char* zh;
  const char* en;
  int min_value;
  int max_value;
  int default_value;
};

const ActionDef kActions[] = {
    {SM_NOTE_ON, KIND_NOTE, "音符（按下发声）", "Note on", 0, 127, 100},
    {SM_NOTE_OFF, KIND_NOTE, "止音（按下止音）", "Note off", 0, 127, 0},
    {SM_NOTE_PRESSURE, KIND_NOTE, "音符力度（按下）", "Note pressure", 0, 127, 100},

    {SM_PROGRAM, KIND_CHANNEL, "切换音色", "Program change", 0, 127, 0},
    {SM_BANK_MSB, KIND_CHANNEL, "音色库 MSB", "Bank MSB", 0, 127, 0},
    {SM_BANK_LSB, KIND_CHANNEL, "音色库 LSB", "Bank LSB", 0, 127, 0},
    {SM_SUSTAIN, KIND_CHANNEL, "延音踏板", "Sustain", 0, 127, 127},
    {SM_MODULATION, KIND_CHANNEL, "颤音", "Modulation", 0, 127, 0},
    {SM_PITCH, KIND_CHANNEL, "弯音", "Pitch bend", -64, 63, 0},
    {SM_PRESSURE, KIND_CHANNEL, "通道力度", "Channel pressure", 0, 127, 0},
    {SM_TRANSPOSE, KIND_CHANNEL, "移调", "Transpose", -64, 64, 0},
    {SM_FOLLOW_KEY, KIND_CHANNEL, "跟随曲调", "Follow key", 0, 1, 1},
    {SM_OCTAVE, KIND_CHANNEL, "八度", "Octave", -1, 1, 1},
    {SM_VELOCITY, KIND_CHANNEL, "力度", "Velocity", 0, 127, 100},
    {SM_CHANNEL, KIND_CHANNEL, "输出通道", "Output channel", 0, 15, 0},

    {SM_KEY_SIGNATURE, KIND_GLOBAL, "曲调", "Key signature", -4, 7, 0},
    {SM_VOLUME, KIND_GLOBAL, "总音量", "Master volume", 0, 200, 100},
    {SM_SETTING_GROUP, KIND_GLOBAL, "切换分组", "Setting group", 0, MAX_SETTING_GROUP - 1, 0},

    {SM_SETTING_GROUP_COUNT, KIND_GROUP_COUNT, "分组总数", "Group count", 1, MAX_SETTING_GROUP, 1},

    {SM_PLAY, KIND_TRANSPORT, "播放", "Play", 0, 0, 0},
    {SM_RECORD, KIND_TRANSPORT, "录制", "Record", 0, 0, 0},
    {SM_STOP, KIND_TRANSPORT, "停止", "Stop", 0, 0, 0},
};

const ActionDef* findAction(int value) {
  for (const auto& a : kActions) {
    if (a.value == value) return &a;
  }
  return nullptr;
}

int actionKind(int value) {
  const ActionDef* a = findAction(value);
  return a ? a->kind : KIND_NOTE;
}

// KeySignature/Octave/Transpose/Pitch store signed bytes.
bool signed_action(int action) {
  return action == SM_KEY_SIGNATURE || action == SM_OCTAVE || action == SM_TRANSPOSE ||
         action == SM_PITCH;
}

}  // namespace

KeyEditDialog::KeyEditDialog(uint8_t key_code, QWidget* parent)
    : QDialog(parent), key_code_(key_code) {
  setWindowTitle(fp_tr("修改按键功能", "Edit key function"));
  setMinimumWidth(560);
  setMinimumHeight(430);
  buildUi();
  loadCurrentBinding();
  updateFields();
}

void KeyEditDialog::buildUi() {
  auto* layout = new QVBoxLayout(this);

  auto* title = new QLabel(
      fp_tr("电脑按键：", "Computer key: ") + QString::fromLatin1(config_get_key_name(key_code_)) +
          QString("    ") + fp_tr("当前分组：", "Group: ") +
          QString::number(config_get_setting_group()),
      this);
  title->setStyleSheet("font-weight:bold;");
  layout->addWidget(title);

  current_text_ = new QLabel(this);
  current_text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  current_text_->setWordWrap(true);
  current_text_->setStyleSheet("color:#666; font-family:monospace;");
  layout->addWidget(current_text_);

  form_ = new QFormLayout();

  action_ = new QComboBox(this);
  action_->setObjectName("actionCombo");
  for (const auto& a : kActions) {
    action_->addItem(QString::fromUtf8(fp_tr(a.zh, a.en)), a.value);
  }
  form_->addRow(fp_tr("功能", "Function"), action_);

  channel_ = new QComboBox(this);
  channel_->setObjectName("channelCombo");
  for (int i = 0; i < 16; ++i) channel_->addItem(QString("In_%1").arg(i), i);
  form_->addRow(fp_tr("输入通道", "Input channel"), channel_);

  note_ = new QComboBox(this);
  note_->setObjectName("noteCombo");
  for (int i = 0; i < 128; ++i) note_->addItem(QString::fromLatin1(config_get_note_name((uint8_t)i)), i);
  form_->addRow(fp_tr("音符", "Note"), note_);

  velocity_ = new QSpinBox(this);
  velocity_->setObjectName("velocitySpin");
  velocity_->setRange(1, 127);
  form_->addRow(fp_tr("力度", "Velocity"), velocity_);

  group_velocity_ = new QCheckBox(
      fp_tr("跟随分组力度（写入 0，由分组力度决定）", "Use group velocity (write 0)"), this);
  form_->addRow(group_velocity_);
  connect(group_velocity_, &QCheckBox::toggled, this,
          [this](bool on) { velocity_->setEnabled(!on); });

  op_ = new QComboBox(this);
  op_->setObjectName("opCombo");
  static const struct {
    int value;
    const char* zh;
    const char* en;
  } kOps[] = {
      {SM_VALUE_SET, "设置为", "Set"},
      {SM_VALUE_INC, "增加", "Increase"},
      {SM_VALUE_DEC, "减少", "Decrease"},
      {SM_VALUE_FLIP, "反转", "Flip"},
      {SM_VALUE_PRESS, "脉冲", "Press"},
      {SM_VALUE_SET | SM_VALUE_SYNC, "同步设置", "Sync set"},
      {SM_VALUE_INC | SM_VALUE_SYNC, "同步增加", "Sync increase"},
      {SM_VALUE_DEC | SM_VALUE_SYNC, "同步减少", "Sync decrease"},
      {SM_VALUE_FLIP | SM_VALUE_SYNC, "同步反转", "Sync flip"},
      {SM_VALUE_PRESS | SM_VALUE_SYNC, "同步脉冲", "Sync press"},
  };
  for (const auto& o : kOps) op_->addItem(QString::fromUtf8(fp_tr(o.zh, o.en)), o.value);
  form_->addRow(fp_tr("操作", "Operation"), op_);

  value_ = new QSpinBox(this);
  value_->setObjectName("valueSpin");
  value_->setRange(0, 127);
  form_->addRow(fp_tr("数值", "Value"), value_);

  label_ = new QLineEdit(this);
  label_->setObjectName("keyLabelEdit");
  label_->setPlaceholderText(fp_tr("留空则自动显示音符名", "Empty = automatic note label"));
  form_->addRow(fp_tr("按键标签", "Key label"), label_);

  layout->addLayout(form_);

  hint_ = new QLabel(this);
  hint_->setWordWrap(true);
  hint_->setStyleSheet("color:#a05a00;");
  layout->addWidget(hint_);

  auto* buttons = new QDialogButtonBox(this);
  QPushButton* clear = buttons->addButton(fp_tr("清除此键", "Clear key"), QDialogButtonBox::DestructiveRole);
  QPushButton* ok = buttons->addButton(fp_tr("确定", "OK"), QDialogButtonBox::AcceptRole);
  ok->setDefault(true);
  ok->setAutoDefault(true);
  QPushButton* cancel = buttons->addButton(fp_tr("取消", "Cancel"), QDialogButtonBox::RejectRole);
  layout->addWidget(buttons);

  connect(action_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &KeyEditDialog::onActionChanged);
  connect(clear, &QPushButton::clicked, this, &KeyEditDialog::clearBinding);
  connect(ok, &QPushButton::clicked, this, [this] { apply(); });
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void KeyEditDialog::showRow(QWidget* field, bool visible) {
  field->setVisible(visible);
  if (auto* label = form_->labelForField(field)) label->setVisible(visible);
}

void KeyEditDialog::loadCurrentBinding() {
  key_bind_t keydown[16];
  key_bind_t keyup[16];
  int nd = config_bind_get_keydown(key_code_, keydown, 16);
  int nu = config_bind_get_keyup(key_code_, keyup, 16);

  current_binding_ = key_bind_t();
  if (nd > 0) {
    current_binding_ = keydown[0];
  } else if (nu > 0) {
    current_binding_ = keyup[0];
  }

  std::string dump = config_dump_keybind(key_code_, true);
  current_text_->setText(dump.empty() ? fp_tr("（未绑定）", "(not bound)")
                                      : QString::fromUtf8(dump.c_str()));

  int action = current_binding_.a;
  const ActionDef* def = findAction(action);
  if (!def) {
    if (current_binding_.a) {
      hint_->setText(fp_tr("当前是脚本/MIDI 绑定，保存后会替换为下面选择的功能。",
                           "This key has a script/MIDI binding; saving will replace it."));
    }
    action = SM_NOTE_ON;
  } else if (nd + nu > 1) {
    hint_->setText(fp_tr("该键有多个绑定，保存后只保留当前编辑的一个。",
                         "This key has several bindings; saving keeps only this one."));
  }

  loading_ = true;
  int index = action_->findData(action);
  action_->setCurrentIndex(index >= 0 ? index : 0);
  kind_ = actionKind(action);

  int value = 0;
  int note = 60;
  int velocity = 100;
  int channel = 0;
  int op = SM_VALUE_SET;

  switch (kind_) {
    case KIND_NOTE:
      channel = current_binding_.b;
      note = current_binding_.c;
      velocity = current_binding_.d;
      break;
    case KIND_CHANNEL:
      channel = current_binding_.b;
      op = current_binding_.c;
      value = signed_action(action) ? (int)(int8_t)current_binding_.d : (int)current_binding_.d;
      break;
    case KIND_GLOBAL:
      op = current_binding_.b;
      value = signed_action(action) ? (int)(int8_t)current_binding_.c : (int)current_binding_.c;
      break;
    case KIND_GROUP_COUNT:
      value = current_binding_.b;
      break;
    default:
      break;
  }

  channel_->setCurrentIndex(std::clamp(channel, 0, 15));
  note_->setCurrentIndex(std::clamp(note, 0, 127));
  bool follow_velocity = kind_ == KIND_NOTE && velocity == 0 && action != SM_NOTE_OFF;
  group_velocity_->setChecked(follow_velocity);
  velocity_->setEnabled(!follow_velocity);
  if (follow_velocity) velocity = config_get_key_velocity((uint8_t)channel);
  velocity_->setValue(std::clamp(velocity, 1, 127));
  int op_index = op_->findData(op);
  op_->setCurrentIndex(op_index >= 0 ? op_index : 0);
  value_->setValue(value);
  label_->setText(QString::fromUtf8(config_bind_get_label(key_code_)));

  if (def && current_binding_.a) {
    QString summary = action_->currentText();
    switch (kind_) {
      case KIND_NOTE:
        summary += QString(" · In_%1 · %2").arg(channel).arg(note_->currentText());
        if (action != SM_NOTE_OFF) {
          summary += velocity == 0 ? fp_tr(" · 力度跟随分组", " · group velocity")
                                   : QString(" · %1 %2").arg(fp_tr("力度", "velocity")).arg(velocity);
        }
        break;
      case KIND_CHANNEL:
        summary += QString(" · In_%1 · %2").arg(channel).arg(value);
        break;
      case KIND_GLOBAL:
      case KIND_GROUP_COUNT:
        summary += QString(" · %1").arg(value);
        break;
      default:
        break;
    }
    current_text_->setText(summary);
  }
  loading_ = false;
}

void KeyEditDialog::onActionChanged() {
  if (loading_) return;
  int action = action_->currentData().toInt();
  kind_ = actionKind(action);
  const ActionDef* def = findAction(action);
  if (def) {
    value_->setRange(def->min_value, def->max_value);
    if (value_->value() < def->min_value || value_->value() > def->max_value) {
      value_->setValue(def->default_value);
    }
  }
  updateFields();
}

void KeyEditDialog::updateFields() {
  bool note = kind_ == KIND_NOTE;
  bool channel = kind_ == KIND_CHANNEL;
  bool global = kind_ == KIND_GLOBAL;
  bool count = kind_ == KIND_GROUP_COUNT;

  int action = action_->currentData().toInt();
  bool has_velocity = note && action != SM_NOTE_OFF;
  showRow(channel_, note || channel);
  showRow(note_, note);
  showRow(velocity_, has_velocity);
  showRow(group_velocity_, has_velocity);
  showRow(op_, channel || global);
  showRow(value_, channel || global || count);

  if (auto* l = qobject_cast<QLabel*>(form_->labelForField(velocity_))) {
    l->setText(action == SM_NOTE_PRESSURE ? fp_tr("压力", "Pressure") : fp_tr("力度", "Velocity"));
  }
  if (auto* l = qobject_cast<QLabel*>(form_->labelForField(value_))) {
    l->setText(channel && action == SM_CHANNEL ? fp_tr("目标通道", "Target channel")
                                               : fp_tr("数值", "Value"));
  }
  if (count) value_->setRange(1, MAX_SETTING_GROUP);
  if (global && action == SM_SETTING_GROUP) {
    value_->setRange(0, std::max(0, (int)config_get_setting_group_count() - 1));
  }
  if (auto* l = qobject_cast<QLabel*>(form_->labelForField(action_))) l->setText(fp_tr("功能", "Function"));
}

void KeyEditDialog::apply() {
  int action = action_->currentData().toInt();
  int kind = actionKind(action);
  int channel = channel_->currentData().toInt();
  int note = note_->currentData().toInt();
  int value = value_->value();
  int velocity = velocity_->value();
  int op = op_->currentData().toInt();

  key_bind_t bind;
  switch (kind) {
    case KIND_NOTE:
      if (action != SM_NOTE_OFF && group_velocity_->isChecked()) velocity = 0;
      bind = key_bind_t((uint8_t)action, (uint8_t)channel, (uint8_t)note, (uint8_t)velocity);
      break;
    case KIND_CHANNEL:
      bind = key_bind_t((uint8_t)action, (uint8_t)channel, (uint8_t)op, (uint8_t)value);
      break;
    case KIND_GLOBAL:
      bind = key_bind_t((uint8_t)action, (uint8_t)op, (uint8_t)value, 0);
      break;
    case KIND_GROUP_COUNT:
      bind = key_bind_t((uint8_t)action, (uint8_t)value, 0, 0);
      break;
    default:
      bind = key_bind_t((uint8_t)action, 0, 0, 0);
      break;
  }

  config_bind_clear_keydown(key_code_);
  config_bind_clear_keyup(key_code_);
  config_bind_add_keydown(key_code_, bind);
  config_bind_set_label(key_code_, label_->text().toUtf8().constData());
  accept();
}

void KeyEditDialog::clearBinding() {
  config_bind_clear_keydown(key_code_);
  config_bind_clear_keyup(key_code_);
  accept();
}
