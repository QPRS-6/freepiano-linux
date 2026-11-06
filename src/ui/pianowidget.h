#pragma once

#include <QWidget>

class PianoWidget : public QWidget {
  Q_OBJECT
 public:
  explicit PianoWidget(QWidget* parent = nullptr);
  QSize sizeHint() const override { return QSize(752, 132); }
  QSize minimumSizeHint() const override { return QSize(480, 90); }

 signals:
  void notePressed(int note);
  void noteReleased(int note);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  int note_at(const QPointF& pos) const;
  struct KeyRect {
    QRectF rect;
    int note;
    bool black;
  };
  std::vector<KeyRect> keys_;
  void rebuild();

  int mouse_note_ = -1;
};
