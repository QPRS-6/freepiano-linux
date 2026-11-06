#pragma once

#include <QMainWindow>
#include <QString>

class QAction;
class QLabel;
class QMenu;
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
  void focusOutEvent(QFocusEvent* event) override;

 private slots:
  void tick();
  void onPlay();
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
  QLabel* sustain_label_ = nullptr;
  QLabel* time_label_ = nullptr;
  QLabel* status_label_ = nullptr;

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
