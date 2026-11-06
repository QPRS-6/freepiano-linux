#pragma once

#include <QMainWindow>
#include <QString>

class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class KeyboardWidget;
class PianoWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  void openSongFile(const QString& path);
  bool exportWavFile(const QString& path, QString* error);
  void showSettings();
  void refreshKeymapMenu();
  void syncControls();

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

 private slots:
  void tick();
  void onPlay();
  void onPause();
  void onSeekReleased();
  void onSpeedChanged(int index);
  void onRecord();
  void onStop();
  void onOpenSong();
  void onSaveSong();
  void onSaveSongAs();
  void onExportWav();
  void onOpenMapFile();
  void onEditMap();
  void onDefaultMap();
  void onSettings();
  void onAbout();
  void onGroupChanged(int value);
  void onKeySignatureChanged(int value);
  void onVelocityChanged();
  void onOctaveChanged();
  void onVolumeChanged(int value);
  void onProgramChanged(int value);
  void onPianoPressed(int note);
  void onPianoReleased(int note);
  void onSustainToggled(bool on);
  void onEditKey(uint8_t code, const QPoint& global_position);

 private:
  void buildControls();
  void buildMenus();
  void updateStatusBar();
  void updateWindowTitle();
  void warn(const QString& text);

  KeyboardWidget* keyboard_ = nullptr;
  PianoWidget* piano_ = nullptr;
  QTimer* timer_ = nullptr;

  QLabel* instrument_label_ = nullptr;
  QLabel* program_label_ = nullptr;
  QPushButton* sustain_button_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QPushButton* pause_button_ = nullptr;
  QPushButton* stop_button_ = nullptr;
  QLabel* time_label_ = nullptr;
  QLabel* status_label_ = nullptr;

  QWidget* progress_row_ = nullptr;
  QSlider* progress_slider_ = nullptr;
  QLabel* progress_elapsed_ = nullptr;
  QLabel* progress_total_ = nullptr;
  QComboBox* speed_combo_ = nullptr;
  bool seeking_ = false;

  QSpinBox* group_spin_ = nullptr;
  QSpinBox* key_sign_spin_ = nullptr;
  QSpinBox* vel0_spin_ = nullptr;
  QSpinBox* vel1_spin_ = nullptr;
  QSpinBox* oct0_spin_ = nullptr;
  QSpinBox* oct1_spin_ = nullptr;
  QSpinBox* program_spin_ = nullptr;
  QSlider* volume_slider_ = nullptr;

  QAction* play_action_ = nullptr;
  QAction* record_action_ = nullptr;
  QAction* stop_action_ = nullptr;

  QMenu* map_menu_ = nullptr;

  QString song_path_;
};
