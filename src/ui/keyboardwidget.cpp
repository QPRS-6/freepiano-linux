#include "keyboardwidget.h"

#include "core/config.h"
#include "core/keyboard.h"
#include "core/keycodes.h"
#include "core/song.h"
#include "keyboardlayout.h"
#include "lang.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace {

QString doh_name(int note) {
  static const char* names[12] = {"1", "#1", "2", "#2", "3", "4", "#4", "5", "#5", "6", "#6", "7"};
  if (note < 0) note = 0;
  if (note > 127) note = 127;
  return QString::fromLatin1(names[note % 12]);
}

}  // namespace

KeyboardWidget::KeyboardWidget(QWidget* parent) : QWidget(parent) {
  setFocusPolicy(Qt::NoFocus);
  setMinimumSize(480, 120);
  rebuild_geometry();
}

void KeyboardWidget::rebuild_geometry() {
  keys_.clear();
  float x = 0, y = 0;
  const float kw = 32.0f, kh = 29.0f;

  for (const auto& entry : kKeyboardLayout) {
    if (entry.code) {
      keys_.push_back(Key{QRectF(x * kw, y * kh, entry.x * kw, entry.y * kh), entry.code});
      x += entry.x;
    } else {
      if (entry.action & 1) x = entry.x;
      else x += entry.x;
      if (entry.action & 2) y = entry.y;
      else y += entry.y;
    }
  }
}

void KeyboardWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  float max_x = 0, max_y = 0;
  for (const auto& k : keys_) {
    max_x = std::max(max_x, (float)(k.rect.right()));
    max_y = std::max(max_y, (float)(k.rect.bottom()));
  }
  if (max_x <= 0 || max_y <= 0) return;
  double avail_w = std::max(1, width() - 16);
  double avail_h = std::max(1, height() - 12);
  sx_ = sy_ = std::min(avail_w / (max_x + 10), avail_h / (max_y + 8));
  ox_ = (width() - (max_x + 10) * sx_) / 2.0;
  oy_ = (height() - (max_y + 8) * sy_) / 2.0;
}

QString KeyboardWidget::label_for_key(uint8_t code, bool* is_note) const {
  *is_note = false;

  const char* custom = config_bind_get_label(code);
  if (custom && *custom) return QString::fromUtf8(custom);

  key_bind_t bind[8];
  int count = config_bind_get_keydown(code, bind, sizeof(bind) / sizeof(bind[0]));
  if (count > 0) {
    key_bind_t e = bind[0];
    if (e.a == SM_NOTE_ON || e.a == SM_NOTE_OFF) {
      int ch = e.b;
      int note = e.c + config_get_key_octshift((uint8_t)ch) * 12 + config_get_key_transpose((uint8_t)ch);
      int display = config_get_note_display();
      if (display == NOTE_DIAPLAY_FIXED_DOH) note += config_get_key_signature();
      if (display == NOTE_DISPLAY_NAME) {
        return QString::fromLatin1(config_get_note_name((uint8_t)clamp_value(note, 0, 127)));
      }
      *is_note = true;
      return doh_name(note);
    }
    return QString::fromStdString(config_default_keylabel(e));
  }

  count = config_bind_get_keyup(code, bind, sizeof(bind) / sizeof(bind[0]));
  if (count > 0) {
    key_bind_t e = bind[0];
    if (e.a == SM_NOTE_ON || e.a == SM_NOTE_OFF) {
      *is_note = true;
      return doh_name(e.c);
    }
    return QString::fromStdString(config_default_keylabel(e));
  }
  return QString();
}

void KeyboardWidget::paintEvent(QPaintEvent* /*event*/) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);

  p.fillRect(rect(), QColor(30, 30, 34));

  for (const auto& key : keys_) {
    QRectF r(ox_ + key.rect.x() * sx_, oy_ + key.rect.y() * sy_,
             key.rect.width() * sx_ - 2.0, key.rect.height() * sy_ - 2.0);
    if (r.width() < 2 || r.height() < 2) continue;

    bool pressed = keyboard_get_status(key.code) != 0;
    bool is_note = false;
    QString label = label_for_key(key.code, &is_note);
    bool bound = !label.isEmpty();

    QColor top, bottom, border, text;
    if (pressed) {
      top = QColor(255, 190, 90);
      bottom = QColor(243, 132, 18);
      border = QColor(184, 92, 12);
      text = QColor(64, 32, 0);
    } else if (bound) {
      top = QColor(244, 244, 248);
      bottom = QColor(216, 216, 222);
      border = QColor(112, 112, 120);
      text = QColor(60, 60, 64);
    } else {
      top = QColor(198, 198, 204);
      bottom = QColor(176, 176, 184);
      border = QColor(120, 120, 128);
      text = QColor(96, 96, 100);
    }

    QLinearGradient gradient(r.topLeft(), r.bottomLeft());
    gradient.setColorAt(0, top);
    gradient.setColorAt(1, bottom);
    QPainterPath path;
    path.addRoundedRect(r, 4, 4);
    p.fillPath(path, gradient);
    p.setPen(QPen(border, 1));
    p.drawPath(path);

    if (!label.isEmpty()) {
      QFont f = font();
      int size = (int)std::clamp(r.height() * 0.38, 8.0, 15.0);
      f.setPixelSize(size);
      f.setBold(is_note);
      p.setFont(f);
      p.setPen(text);
      QRectF text_rect = r.adjusted(1, 1, -1, -1);
      QFontMetrics fm(f);
      if (fm.horizontalAdvance(label) > text_rect.width() - 2) {
        f.setPixelSize(std::max(7, size - 2));
        p.setFont(f);
      }
      p.drawText(text_rect, Qt::AlignCenter, label);
    }
  }
}
