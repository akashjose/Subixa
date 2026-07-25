cmake_minimum_required(VERSION 3.21)
project(custom_media_player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 OpenGL Test)

# libmpv comes from apt (0.37 on 24.04). pkg-config keeps us off a hardcoded path.
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

# Subtitle text is demuxed and decoded directly rather than read from mpv: mpv
# only exposes the line currently on screen, which cannot build a browsable list.
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(custom_media_player
    src/main.cpp
    src/MpvObject.cpp
    src/MpvObject.h
    src/SubtitleTypes.h
    src/SubtitleExtractor.cpp
    src/SubtitleExtractor.h
    src/SubtitleManager.cpp
    src/SubtitleManager.h
    src/SubtitleLineModel.cpp
    src/SubtitleLineModel.h
    src/SubtitleFilterModel.cpp
    src/SubtitleFilterModel.h
)

qt_add_qml_module(custom_media_player
    URI CustomMediaPlayer
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

# qmltyperegistrar emits `#if __has_include(<MpvObject.h>)`. Without src/ on the
# include path that guard is silently false, the include is skipped, and the
# generated file fails with "QQuickItem was not declared" rather than anything
# pointing at the real cause.
target_include_directories(custom_media_player PRIVATE src)

target_link_libraries(custom_media_player PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::OpenGL
    PkgConfig::MPV
    PkgConfig::FFMPEG
)

# Tests. Both are headless by construction -- no window, no GL context -- because
# WSLg can degrade into painting stale frames while everything underneath keeps
# working, which makes a screenshot the least reliable evidence available. See
# CLAUDE.md.
#
# The sources under test are compiled into the test targets rather than shared
# through a static library: the QML-exposed types are registered by
# qt_add_qml_module against the sources listed in qt_add_executable, and moving
# them out breaks that registration for the app. Recompiling four files is the
# cheaper trade.
enable_testing()

set(CMP_MODEL_SOURCES
    src/SubtitleExtractor.cpp
    src/SubtitleManager.cpp
    src/SubtitleLineModel.cpp
    src/SubtitleFilterModel.cpp
)

qt_add_executable(tst_subtitles tests/tst_subtitles.cpp ${CMP_MODEL_SOURCES})
target_include_directories(tst_subtitles PRIVATE src)
target_compile_definitions(tst_subtitles PRIVATE
    CMP_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
target_link_libraries(tst_subtitles PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Test
    PkgConfig::FFMPEG
)

# Talks to libmpv directly with vo=null: verifies that selecting a track changes
# what mpv would render, read back as text from `sub-text` instead of pixels.
qt_add_executable(tst_mpvtracks tests/tst_mpvtracks.cpp)
target_compile_definitions(tst_mpvtracks PRIVATE
    CMP_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
target_link_libraries(tst_mpvtracks PRIVATE
    Qt6::Core
    Qt6::Test
    PkgConfig::MPV
)

add_test(NAME subtitles COMMAND tst_subtitles)
add_test(NAME mpvtracks COMMAND tst_mpvtracks)

# Neither test needs a display; make sure they never try to open one.
set_tests_properties(subtitles mpvtracks PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=minimal"
    TIMEOUT 120
)
