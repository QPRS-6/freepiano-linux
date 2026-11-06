cmake_minimum_required(VERSION 3.20)
project(freepiano-linux VERSION 1.0.7 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(FP_WITH_FLUIDSYNTH "Build optional FluidSynth (SoundFont) backend" ON)
option(FP_WITH_MIDI "Build optional ALSA sequencer MIDI backend" ON)
option(FP_EMBED_SOURCE_DATA_DIR "Embed the absolute source assets path (development builds)" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)

option(FP_WITH_PULSE "Build PulseAudio/PipeWire output backend" ON)
if(FP_WITH_PULSE)
  pkg_check_modules(PULSE IMPORTED_TARGET libpulse-simple)
endif()

if(FP_WITH_FLUIDSYNTH)
  pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)
endif()
if(FP_WITH_MIDI)
  pkg_check_modules(ALSA IMPORTED_TARGET alsa)
endif()

set(FP_CORE_SOURCES
  src/core/keycodes.cpp
  src/core/config.cpp
  src/core/song.cpp
  src/core/keyboard.cpp
  src/core/midi.cpp
  src/core/synth_builtin.cpp
  src/core/synth_mda_piano.cpp
  src/core/fluidsynth_engine.cpp
  src/core/audio.cpp
  src/core/alsa_midi.cpp
  src/core/wav_export.cpp
  src/core/engine.cpp
  src/core/sync.cpp
)

set(FP_UI_SOURCES
  src/ui/main.cpp
  src/ui/mainwindow.cpp
  src/ui/keyboardwidget.cpp
  src/ui/pianowidget.cpp
  src/ui/qtkeymap.cpp
  src/ui/settingsdialog.cpp
  src/ui/keymapeditordialog.cpp
  src/ui/keyeditdialog.cpp
  src/ui/aboutdialog.cpp
)

add_executable(freepiano ${FP_CORE_SOURCES} ${FP_UI_SOURCES})
target_include_directories(freepiano PRIVATE src)
target_link_libraries(freepiano PRIVATE Qt6::Widgets ZLIB::ZLIB PkgConfig::PORTAUDIO)

if(FLUIDSYNTH_FOUND)
  target_compile_definitions(freepiano PRIVATE FP_HAVE_FLUIDSYNTH=1)
  target_link_libraries(freepiano PRIVATE PkgConfig::FLUIDSYNTH)
endif()

if(ALSA_FOUND)
  target_compile_definitions(freepiano PRIVATE FP_HAVE_ALSA=1)
  target_link_libraries(freepiano PRIVATE PkgConfig::ALSA)
endif()

if(PULSE_FOUND)
  target_compile_definitions(freepiano PRIVATE FP_HAVE_PULSE=1)
  target_link_libraries(freepiano PRIVATE PkgConfig::PULSE)
endif()

# Where the source tree keeps its runtime data (used when running from a build dir)
target_compile_definitions(freepiano PRIVATE
  FP_INSTALL_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/freepiano"
  FP_VERSION_STRING="${PROJECT_VERSION}")
if(FP_EMBED_SOURCE_DATA_DIR)
  target_compile_definitions(freepiano PRIVATE
    FP_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()

install(TARGETS freepiano RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/freepiano
        PATTERN "freepiano.desktop" EXCLUDE
        PATTERN "freepiano.svg" EXCLUDE)
install(FILES assets/freepiano.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/freepiano.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE LICENSES/mdaPiano-MIT.txt README.md PORTING.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
