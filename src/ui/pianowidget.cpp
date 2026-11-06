#include "pianowidget.h"

#include "core/config.h"
#include "core/midi.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace {
bool is_black_note(int note) {
  switch (note % 12) {
    case 1: case 3: case 6: case 8: case 10: return true;
    default: return false;
  }
}
}  // namespace

PianoWidget::PianoWidget(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::NoFocus);
  setMinimumHeight(90);
  rebuild();
}

void PianoWidget::rebuild() {
  keys_.clear();
  const double margin = 8.0;
  const int first = 21;
  const int last = 108;

  int white_count = 0;
  for (int n = first; n <= last; ++n) {
    if (!is_black_note(n)) ++white_count;
  }
  if (white_count == 0) return;

  double w = std::max(1.0, (width() - margin * 2) / white_count);
  double h = std::max(1.0, (double)height() - 8.0);
  double top = 4.0;

  int white_index = 0;
  for (int n = first; n <= last; ++n) {
    if (!is_black_note(n)) {
      keys_.push_back({QRectF(margin + white_index * w, top, w - 1.0, h), n, false});
      ++white_index;
    }
  }

  // black keys are drawn on top, centred on the boundary after the preceding white key
  white_index = 0;
  for (int n = first; n <= last; ++n) {
    if (is_black_note(n)) {
      double bw = w * 0.62;
      double bh = h * 0.62;
      double x = margin + white_index * w - bw / 2.0;
      keys_.push_back({QRectF(x, top, bw, bh), n, true});
    } else {
      ++white_index;
    }
  }
}

void PianoWidget::paintEvent(QPaintEvent* /*event*/) {
  rebuild();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(30, 30, 34));

  // white keys
  for (const auto& key : keys_) {
    if (key.black) continue;
    int velocity = midi_note_velocity(key.note);
    QLinearGradient g(key.rect.topLeft(), key.rect.bottomLeft());
    if (velocity > 0) {
      float v = std::clamp(velocity / 127.0f, 0.15f, 1.0f);
      g.setColorAt(0, QColor::fromRgbF(1.0f, 0.62f + 0.2f * (1 - v), 0.22f));
      g.setColorAt(1, QColor::fromRgbF(0.92f, 0.42f, 0.05f));
    } else {
      g.setColorAt(0, QColor(252, 252, 253));
      g.setColorAt(1, QColor(216, 216, 222));
    }
    QPainterPath path;
    path.addRoundedRect(key.rect, 3, 3);
    p.fillPath(path, g);
    p.setPen(QPen(QColor(58, 58, 64), 1));
    p.drawPath(path);

    if (key.note % 12 == 0) {
      QFont f = font();
      f.setPixelSize(9);
      p.setFont(f);
      p.setPen(QColor(110, 110, 116));
      p.drawText(key.rect.adjusted(1, 0, -1, -4), Qt::AlignBottom | Qt::AlignHCenter,
                 QString::fromLatin1(config_get_note_name((uint8_t)key.note)));
    }
  }

  // black keys
  for (const auto& key : keys_) {
    if (!key.black) continue;
    int velocity = midi_note_velocity(key.note);
    QLinearGradient g(key.rect.topLeft(), key.rect.bottomLeft());
    if (velocity > 0) {
      g.setColorAt(0, QColor(255, 170, 70));
      g.setColorAt(1, QColor(196, 86, 8));
    } else {
      g.setColorAt(0, QColor(48, 48, 54));
      g.setColorAt(1, QColor(14, 14, 17));
    }
    QPainterPath path;
    path.addRoundedRect(key.rect, 2, 2);
    p.fillPath(path, g);
    p.setPen(QPen(QColor(8, 8, 10), 1));
    p.drawPath(path);
  }
}

int PianoWidget::note_at(const QPointF& pos) const {
  for (const auto& key : keys_) {
    if (key.black && key.rect.contains(pos)) return key.note;
  }
  for (const auto& key : keys_) {
    if (!key.black && key.rect.contains(pos)) return key.note;
  }
  return -1;
}

void PianoWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) return;
  int note = note_at(event->position());
  mouse_note_ = note;
  if (note >= 0) emit notePressed(note);
}

void PianoWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!(event->buttons() & Qt::LeftButton)) return;
  int note = note_at(event->position());
  if (note == mouse_note_) return;
  if (mouse_note_ >= 0) emit noteReleased(mouse_note_);
  mouse_note_ = note;
  if (note >= 0) emit notePressed(note);
}

void PianoWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) return;
  if (mouse_note_ >= 0) emit noteReleased(mouse_note_);
  mouse_note_ = -1;
}

void PianoWidget::leaveEvent(QEvent* /*event*/) {
  if (mouse_note_ >= 0) {
    emit noteReleased(mouse_note_);
    mouse_note_ = -1;
  }
}
