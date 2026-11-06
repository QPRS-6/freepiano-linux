#include "aboutdialog.h"

#include "core/config.h"
#include "core/engine.h"
#include "lang.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(fp_tr("关于 FreePiano Linux", "About FreePiano Linux"));
  setMinimumWidth(520);

  auto* layout = new QVBoxLayout(this);

  auto* title = new QLabel("<h2>FreePiano Linux " + QString(FP_VERSION_STRING) + "</h2>", this);
  layout->addWidget(title);

  auto* body = new QLabel(
      fp_tr(
          "<p>FreePiano 是一个用电脑键盘演奏 MIDI 乐器的开源软件。</p>"
          "<p>本程序是 FreePiano 1.8 的 Linux 原生移植（BSD-3-Clause）：<br>"
          "界面、音频输出、合成器与 MIDI 后端针对 Linux 重新实现，"
          "同时保持键位文件（.map）、乐曲文件（.fpm）与脚本语义兼容。</p>"
          "<p>原版 FreePiano © 2013 FreePiano，BSD-3-Clause。<br>"
          "原版源码：<a href=\"https://github.com/Energy0124/freepiano\">github.com/Energy0124/freepiano</a></p>"
          "<p>本移植使用 Qt 6、PortAudio、ALSA（可选 FluidSynth、zlib）。</p>"
          "<p>主要差异：Windows 版使用 VSTi 插件与 DirectSound/WASAPI/ASIO；"
          "Linux 版使用内置合成器或 SoundFont，音频经由 PortAudio（PipeWire/PulseAudio/ALSA）。</p>",
          "<p>FreePiano plays MIDI instruments with your computer keyboard.</p>"
          "<p>This is a native Linux port of FreePiano 1.8. The UI, audio output, synthesizer and MIDI "
          "backends are reimplemented for Linux while keeping the key-map (.map), song (.fpm) and script "
          "semantics compatible.</p>"
          "<p>Original FreePiano © 2013 FreePiano, BSD-3-Clause.<br>"
          "Upstream source: <a href=\"https://github.com/Energy0124/freepiano\">github.com/Energy0124/freepiano</a></p>"
          "<p>This port uses Qt 6, PortAudio, ALSA (optional: FluidSynth, zlib).</p>"
          "<p>Main differences: the Windows build hosts VSTi plug-ins and uses DirectSound/WASAPI/ASIO; "
          "the Linux build uses a built-in synthesizer or a SoundFont, with audio through PortAudio "
          "(PipeWire/PulseAudio/ALSA).</p>"),
      this);
  body->setWordWrap(true);
  body->setOpenExternalLinks(true);
  body->setTextInteractionFlags(Qt::TextBrowserInteraction);
  layout->addWidget(body);

  auto* status = new QLabel(QString::fromStdString(engine_status()), this);
  status->setWordWrap(true);
  status->setStyleSheet("color:#666;");
  layout->addWidget(status);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->button(QDialogButtonBox::Close)->setText(fp_tr("关闭", "Close"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
}
