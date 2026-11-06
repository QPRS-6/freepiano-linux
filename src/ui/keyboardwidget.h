#pragma once

#include <QWidget>

#include <vector>

class KeyboardWidget : public QWidget {
  Q_OBJECT
 public:
  explicit KeyboardWidget(QWidget* parent = nullptr);
  QSize sizeHint() const override { return QSize(752, 178); }
  QSize minimumSizeHint() const override { return QSize(480, 120); }

  // Screen rectangle of one key (for tests/automation); empty when absent.
  QRectF keyRect(uint8_t code) const;

 signals:
  // Right-click: edit this key's function.
  void editKeyRequested(uint8_t code, const QPoint& global_position);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 private:
  struct Key {
    QRectF rect;
    uint8_t code;
  };
  void rebuild_geometry();
  QString label_for_key(uint8_t code, bool* is_note) const;
  QRectF scaled_rect(const Key& key) const;
  int key_at(const QPointF& position) const;
  void press_key(uint8_t code);
  void release_key(uint8_t code);

  std::vector<Key> keys_;
  double sx_ = 1.0;
  double sy_ = 1.0;
  double ox_ = 0.0;
  double oy_ = 0.0;

  int mouse_code_ = -1;
  bool mouse_down_ = false;
};
