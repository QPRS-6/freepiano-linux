#pragma once

#include <QWidget>

#include <vector>

class KeyboardWidget : public QWidget {
  Q_OBJECT
 public:
  explicit KeyboardWidget(QWidget* parent = nullptr);
  QSize sizeHint() const override { return QSize(752, 178); }
  QSize minimumSizeHint() const override { return QSize(480, 120); }

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  struct Key {
    QRectF rect;
    uint8_t code;
  };
  void rebuild_geometry();
  QString label_for_key(uint8_t code, bool* is_note) const;

  std::vector<Key> keys_;
  double sx_ = 1.0;
  double sy_ = 1.0;
  double ox_ = 0.0;
  double oy_ = 0.0;
};
