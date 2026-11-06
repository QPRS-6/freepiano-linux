# FreePiano → Linux 移植说明

FreePiano 1.8 原本是 Windows / Win32 程序：DirectInput 低级键盘钩子、Direct3D 皮肤渲染、
DirectSound/WASAPI/ASIO 音频、VST2 宿主、WinMM MIDI、以及用 x264/faac/mp4v2 导出 MP4。
本移植保留**事件模型、脚本语义和文件格式**，把平台层全部换成 Linux 原生实现。

上游：<https://github.com/Energy0124/freepiano>（`src/`、`data/`、`res/`）。

## 模块对照

| 原版（Windows） | 本移植（Linux） |
| --- | --- |
| `src/config.cpp`（设置、`.map` 解析、键位映射） | `src/core/config.cpp`（重写为可移植 C++，键位/脚本语法保持一致） |
| `src/song.cpp`（事件引擎、录音、`.fpm`、`.lyt`） | `src/core/song.cpp`（`.fpm` 完整移植；`.lyt` 未实现） |
| `src/keyboard.cpp`（低级键盘钩子、自动 NoteOff） | `src/core/keyboard.cpp` + Qt 键事件（`src/ui/qtkeymap.cpp`） |
| `src/midi.cpp`（WinMM MIDI 与 VST 路由） | `src/core/midi.cpp`（统一事件分发，去掉 WinMM） |
| `src/synthesizer_vst.cpp` + `src/vst/*` | `src/core/synth_mda_piano.cpp`（内置 mdaPiano，默认）+ `synth_builtin.cpp`（GM 合成器）+ `fluidsynth_engine.cpp`（SoundFont） |
| `src/output_dsound/wasapi/asio.cpp` | `src/core/audio.cpp`（PortAudio + PulseAudio/PipeWire） |
| `-`（Windows 的 MIDI 设备） | `src/core/alsa_midi.cpp`（ALSA sequencer 输入/输出） |
| `src/display.cpp`（Direct3D 皮肤、按键渲染） | `src/ui/keyboardwidget.cpp`、`pianowidget.cpp`（Qt 绘制，沿用原版键位坐标） |
| `src/gui.cpp`（Win32 自绘控件/菜单） | `src/ui/mainwindow.cpp` + `settingsdialog.cpp` + `keymapeditordialog.cpp` |
| `src/export_wav.cpp` | `src/core/wav_export.cpp` + `engine_export_wav()` |
| `src/export_mp4.cpp`（x264/faac/mp4v2） | 未移植（当前导出 WAV；可用 ffmpeg 再合成视频） |
| `src/language.cpp` + `res/*.txt` | `src/ui/lang.h`（中/英双语） |
| `src/update.cpp`（自动更新） | 未移植（发行版包管理负责升级） |

## 保持兼容的部分

### 事件元组

FreePiano 的所有脚本、录音事件都是 `(a, b, c, d)` 四字节元组。本移植使用同样的
`key_bind_t` 和常量（`src/core/events.h`），因此：

* `.map` 中的 `keydown a NoteOn In_0 c3`、`keydown F5 Octave In_0 Inc 1` 等语义完全一致；
* `.fpm` 中的事件流可以被原版读取，反过来也一样；
* `Group / KeySignature / Transpose / FollowKey / Octave / Velocity / Channel / Program /
  BankMSB / BankLSB / Sustain / Modulation / Pressure / Pitch / Play / Record / Stop /
  Sync* / Press` 等修饰符都按原逻辑实现。

### 键位编号

原版用 DirectInput 的 PC 扫描码（IBM set 1）作为键号。本移植保留这套编号
（`src/core/keycodes.h`），因此 `.map` 里的数字键号、`.fpm` 里记录的键事件仍然有效。
Qt 键事件通过 `dik_from_qt()` 转换成扫描码：

* Qt 的 `nativeScanCode` 在不同平台/后端可能是 X11 keycode（evdev + 8），也可能是原始 evdev；
  因此同时计算两个候选，并**先用 Qt 逻辑键做精确匹配**；
* 只有 Qt 逻辑键无法区分时才使用扫描码：左右 Ctrl/Alt/Shift/Win 用扫描码区分左右；
  小键盘用「Qt 键 → 小键盘索引」与两个候选比对（同时支持 NumLock 开/关）；
* 最后才回退到 Qt::Key 表。

> 早期版本把「+8 候选恰好是小键盘/扩展键」当成真键，导致 C→右 Shift、F5→小键盘 7 等问题；
> 现在由 `--selftest` / `--test-ui` 中的回归用例覆盖。

### `.fpm` 格式

```
"FreePianoSong\0"          14 bytes
uint32 version             (0x01080000 = 1.8)
uint32 len + title
uint32 len + author
uint32 len + comment
uint32 len + instrument
uint32 compressed_size
zlib(compressed_size)  ->  16-byte events {double time; u8 a,b,c,d;}
```

事件结构在 x86-64 与 MSVC 下都是 16 字节（`double` 8 字节对齐），移植用
`static_assert(sizeof(song_event_t) == 16)` 固定下来。

### 脚本时序

* 音频回调每块调用 `song_update(1000 * frames / sample_rate)`，与原版
  DirectSound/WASAPI 回调驱动播放的方式一致；
* 录音时间戳、`Sync`/`Press` 延迟事件、弯音平滑（每 2ms 一步）都沿用原实现；
* 关闭音频时使用"静音计时器"线程驱动同样的步进，保证无音频设备时也能播放/导出。

### 键位文件解析

原版的解析器支持大量别名、中文命令和旧版本兼容：

* 动作/通道/音符/控制器名称大小写不敏感；
* `Set/Inc/Dec/Flip/Press/Set10/Set1/Sync*` 与中文别名；
* `key`（旧写法）= `keydown` + 自动 NoteOff；
* `FreePiano <version>` 决定旧音符编号偏移（<1.7 时 -12）；
* `GroupCount` 会清空所有分组（与原文一致）。

移植重新实现了词法分析器，但返回值与副作用保持一致；`.map` 文本保存格式也兼容原版
（`GroupCount`、`Group Set n`、各通道设置、`keydown`/`label`/`color` 行）。

### 中文编码

上游工程本身是 GBK，但用户键位文件可能是 UTF-8 或 GBK。加载 `.map` 时先检测 UTF-8，
非法时用 `iconv` 按 GB18030 转成 UTF-8，再解析中文指令。

## 内置 mdaPiano 音源

FreePiano 的 Windows 发行版默认加载 `vsti/mdaPiano.dll`。该插件由 Paul Kellett 以
MIT（或 GPL）许可发布，Linux 上无需 VST 宿主即可使用：

* `src/core/synth_mda_piano.cpp` 是 DSP 的原生移植：15 个 keygroup、8 个原厂预设、
  32 复音、采样插值、包络、muffle 滤波、立体声模拟；
* `src/core/mda_piano_data.h` 是原始 16-bit 采样表（约 1.17 MB 采样，源码表约 3.2 MB 文本，已编入二进制）；
* 行为与原 VST 一致：CC1/CC67 控制 muffle，CC7 音量，CC64/66 延音，Program 0–7 切换预设；
  原插件不支持 pitch bend，移植同样忽略；
* 默认音源改为 mdaPiano；旧配置中的 `instrument type VSTI` 会映射到它，旧版本写下的
  Builtin 默认值会迁移一次（配置带 `config-version` 后不再自动改动）。

移植代码与采样表按 MIT 许可分发，见 `LICENSES/mdaPiano-MIT.txt`；其余代码仍为 BSD-3-Clause。

## 平台相关替换

### 界面交互

* `KeyboardWidget` 保存缩放后的键位矩形，`key_at()` 做命中测试：
  * 左键按下发送 `SM_SYSTEM / SMS_KEY_EVENT`，与物理键盘完全同一条事件链（录音、自动 note-off、
    分组/曲调脚本都生效）；按住拖动时先释放旧键再按新键，可滑奏；
  * 右键发出 `editKeyRequested`，由 `KeyEditDialog` 修改当前分组中该键的绑定。
* `KeyEditDialog` 用表单覆盖常用脚本动作（音符、通道、八度、移调、延音、播放/录制等），
  写入 `config_bind_add_keydown()`；支持“跟随分组力度”（velocity=0）和自定义键帽文字。
* 延音踏板在改键对话框中有两个子模式：
  * **按住延音**：keydown 写 `Sustain In_x Set 127`，keyup 写 `Sustain In_x Set 0`；
  * **切换延音**：keydown 写 `Sustain In_x Flip 127`（用 127 作为翻转基准，0/127 之间切换）。
  这利用了键盘层已有的 keyup 事件表，物理键盘、鼠标点击和 `.fpm` 回放都走同一条路径。
* 工具栏「延音」是 checkable 按钮：切换时发送 `SM_SUSTAIN`（可录制），每帧从 CC64 状态同步，
  因此脚本/回放改变延音时按钮也会跟着变。

### 播放进度与暂停

`KeyboardWidget` 与 `PianoWidget` 之间插入一行播放控制（`progressRow`）：播放、暂停、停止、
当前时间、可拖动进度条、总时长。`song_is_empty()` 为真或正在录制时整行隐藏；有乐曲时显示。

暂停是“冻结”而不是重置：`song_pause_playback()` 只把 `song_timer`/事件位置停住，不发送
note-off，也不清空合成器。恢复时 `song_resume_playback()` 直接从冻结位置继续，因此不会
把暂停前按住的音符重新触发成一声大和弦。

拖动进度条调用 `song_seek()`：从曲首重新开始并快进事件流到目标位置，重建分组、音色、
控制器和活动音符，然后继续播放。快进是静态处理，不会把中间过程渲染成音频。

### .fpm 版本兼容

写文件固定使用 FreePiano 1.8 格式（`0x01080000`），保证旧版 FreePiano 也能打开。
读取时接受 1.x 和 2.x（最高 `0x02FFFFFF`）；2.x 的键位映射、键标签和按键事件与 1.8
的事件流结构一致，未知的新事件在 `song_output_event()` 的默认分支被忽略。

### 音频

```
MIDI 事件 → midi_output_event()
            ├─ synth_sink  → 内置合成器 / FluidSynth
            └─ external_sink → ALSA sequencer（仅当音源设为 None）
```

音频输出有两种后端，按配置自动选择：

1. **PortAudio**（`audio.cpp`）：优先；支持 ALSA/JACK 设备，低延迟；
2. **PulseAudio / PipeWire**（libpulse-simple）：没有 ALSA 声卡或 PortAudio 打开失败时回退；
3. **静音计时器**：全部失败时仍然按真实时间推进播放/录音。

内置 mdaPiano 是默认音源，采样已编入二进制；内置 GM 合成器作为备用，不依赖任何资产：
12 种 GM 音色模型（钢琴、电钢、风琴、弦乐、Pad、铜管、长笛、钟琴、拨弦、贝斯、Lead、音效），
带 ADSR、力度亮度、延音踏板、弯音、通道音量/表情/声像、64 复音与声部抢占。

### MIDI

ALSA sequencer 同时提供输入和输出：

* 输出：创建 "FreePiano Out" 端口，订阅用户在设置中选择的端口，发送 note/CC/program/
  pitchbend；
* 输入：创建 "FreePiano In" 端口并在独立线程 poll 事件，按原版 WinMM 回调的规则做通道
  重映射与曲调移调，然后 `song_send_event(..., record=true)` 进入录音。

### 键盘

原版用 `SetWindowsHookEx(WH_KEYBOARD_LL)` 做全局钩子，可以在任何前台窗口演奏。
Linux 下没有等价且可移植的机制：

* X11 理论上可以 `XGrabKeyboard` 全局抓键，但 Wayland 明确禁止；
* 需要全局热键时应使用桌面环境的快捷键或 `uinput`/`evdev` 权限方案，超出当前移植范围。

因此本移植在窗口获得焦点时演奏；窗口失焦时会释放所有按下的音符，并把释放事件写入录音，
避免出现悬挂的音符。

### 皮肤

原版用 `res/*.png` 贴图 + Direct3D 渲染。Qt 版本直接绘制同布局的键盘（坐标表从
`display.cpp` 的 `keys1[]` 提取，见 `src/ui/keyboardlayout.h`），配色参照原版截图。
不加载原版皮肤，避免依赖 Direct3D 纹理格式。

## 已知限制 / 后续工作

* **MP4 导出**：原版用 x264 + faac + mp4v2 合成"键盘画面 + 音频"视频。Linux 版目前只导出
  WAV；可用 `ffmpeg` 将 WAV 与屏幕录制合成为视频。后续可基于 Qt 离屏渲染 + libav 实现。
* **VST/VSTi**：没有移植 VST2 宿主。Linux 上 VST 生态与 Windows DLL 不通用，改用
  SoundFont/FluidSynth；如果需要，可以对接 Carla / jalv 或 Linux 原生 VST3。
* **`.lyt`（iDreamPiano）导入**：未实现（原格式是固定结构的遗留二进制）。`.fpm` 已完整支持。
* **全局键盘钩子**：受限（见上）；可在 X11 下增加可选 `XGrabKeyboard` 模式。
* **自动更新**：未实现，交由发行版包管理。
* **皮肤**：未实现原版 PNG 皮肤。
* **音频设备热插拔**：设置中重新选择设备即可；未监听系统设备变化。
* **高采样率/多声道**：固定立体声输出。

## 测试

```bash
# 引擎 + 文件格式 + WAV
./build/bin/freepiano --selftest --no-audio

# 所有键位文件
./build/bin/freepiano --test-maps ./assets

# 上游 demo 乐曲（需要 freepiano 源码树）
./build/bin/freepiano --test-songs /path/to/freepiano/data/song

# 无显示环境截图（开发用）
QT_QPA_PLATFORM=offscreen ./build/bin/freepiano --screenshot /tmp/ui.png --no-audio --demo
```

`--selftest` 覆盖：默认键位、Qt 键码映射、按键→音符、录音、`.fpm` 保存/打开往返、
播放、分组往返、WAV 离线渲染（检查峰值非静音）。
