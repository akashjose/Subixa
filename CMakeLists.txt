cmake_minimum_required(VERSION 3.21)
project(subixa VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SUBIXA_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})

# 6.5 rather than 6.9. Nothing here needs 6.9, and requiring a 2025 release put
# the project out of reach of every current LTS distribution -- which is the
# difference between "packageable" and "build Qt yourself first".
# QtQuick.Shapes -- which draws every icon in the app -- deliberately absent.
# Qt 6.9 ships it as a private module (only Qt6QuickShapesPrivate has a CMake
# config), and a dynamically linked build resolves the QML plugin through the
# import path at runtime with no link-time dependency at all. A future *static*
# build will need Qt6QuickShapesPrivate here and qt_import_qml_plugins.
set(SUBIXA_QT_COMPONENTS Core Gui Quick Qml QuickControls2 OpenGL Svg)
if(SUBIXA_BUILD_TESTS)
    list(APPEND SUBIXA_QT_COMPONENTS Test)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${SUBIXA_QT_COMPONENTS})

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

# Everything but main.cpp, shared verbatim between the app and the QML harness.
# Listed once because a source that reaches only one of the two produces a link
# error in the other, at a place that says nothing about the omission.
set(SUBIXA_APP_SOURCES
    src/MpvEngine.cpp
    src/MpvEngine.h
    src/MpvVideoItem.cpp
    src/MpvVideoItem.h
    src/MpvTrackList.h
    src/FboCap.h
    src/SubtitleTypes.h
    src/SubtitleExtractor.cpp
    src/SubtitleExtractor.h
    src/SubtitleCache.cpp
    src/SubtitleCache.h
    src/SubtitleManager.cpp
    src/SubtitleManager.h
    src/SubtitleLineModel.cpp
    src/SubtitleLineModel.h
    src/SubtitleStyle.cpp
    src/SubtitleStyle.h
    src/SubtitleText.cpp
    src/SubtitleText.h
    src/SubtitleFilterModel.cpp
    src/SubtitleFilterModel.h
    src/PlaybackHistory.cpp
    src/PlaybackHistory.h
    src/Playlist.cpp
    src/Playlist.h
    src/ShortcutRegistry.cpp
    src/ShortcutRegistry.h
    src/PaintedText.cpp
    src/PaintedText.h
    src/GraphicsSetup.cpp
    src/GraphicsSetup.h
)

# The QML files, likewise declared once. tst_qmlpanel re-declares the module
# under the same URI, and a file added to only one list fails at *runtime* with
# a type-resolution error rather than at build time -- which is a long way from
# the missing line.
set(SUBIXA_QML_FILES
    qml/Main.qml
    qml/TransportBar.qml
    qml/SubtitlePanel.qml
    qml/SubtitleRow.qml
    qml/SettingsWindow.qml
    qml/Theme.qml
    qml/ui/Icons.qml
    qml/ui/Icon.qml
    qml/ui/AppText.qml
    qml/ui/IconButton.qml
    qml/ui/TextButton.qml
    qml/ui/ToolTipBubble.qml
    qml/ui/Chip.qml
    qml/ui/SearchField.qml
    qml/ui/AppMenu.qml
    qml/ui/AppMenuItem.qml
    qml/ui/AppMenuSeparator.qml
    qml/ui/AppScrollBar.qml
    qml/ui/AppSlider.qml
    qml/ui/AppSwitch.qml
    qml/ui/AppSpinBox.qml
    qml/ui/AppComboBox.qml
    qml/ui/Segmented.qml
    qml/ui/SeekBar.qml
    qml/ui/Banner.qml
    qml/ui/EmptyState.qml
    qml/ui/Divider.qml
    qml/ui/SectionCard.qml
    qml/ui/FormRow.qml
    qml/ui/ColorSwatch.qml
)

qt_add_executable(subixa
    src/main.cpp
    ${SUBIXA_APP_SOURCES}
)

target_compile_definitions(subixa PRIVATE
    SUBIXA_VERSION="${PROJECT_VERSION}")

# The window icon, compiled in. The desktop entry points at the installed copy
# below, but that only covers a launcher: a window needs the icon at runtime,
# and a player run straight from a build tree has no installed files at all.
qt_add_resources(subixa "subixa_icon"
    PREFIX "/"
    FILES icons/subixa.svg
)

# Must precede qt_add_qml_module: the property is read as the module is defined,
# and a Theme.qml registered without it is an ordinary type that every user would
# instantiate a private copy of.
set_source_files_properties(qml/Theme.qml qml/ui/Icons.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(subixa
    URI Subixa
    VERSION 1.0
    QML_FILES ${SUBIXA_QML_FILES}
)

# qmltyperegistrar emits `#if __has_include(<MpvEngine.h>)`. Without src/ on the
# include path that guard is silently false, the include is skipped, and the
# generated file fails with "QQuickItem was not declared" rather than anything
# pointing at the real cause.
target_include_directories(subixa PRIVATE src)

target_link_libraries(subixa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::OpenGL
    # For QIcon to rasterise the SVG application icon.
    Qt6::Svg
    PkgConfig::MPV
    PkgConfig::FFMPEG
)

# Warnings on. The tree is clean at this level and staying that way is cheaper
# than getting back to it.
target_compile_options(subixa PRIVATE -Wall -Wextra)

# ---- install ------------------------------------------------------------
# Enough to package. The desktop entry and its MIME list are what make the
# player appear in a file manager's "Open with", which is how most people will
# reach it.
include(GNUInstallDirs)

install(TARGETS subixa
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES subixa.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)
# hicolor/scalable is what the desktop entry's `Icon=subixa` resolves against.
install(FILES icons/subixa.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
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
if(SUBIXA_BUILD_TESTS)
enable_testing()

set(SUBIXA_MODEL_SOURCES
    src/SubtitleExtractor.cpp
    src/SubtitleCache.cpp
    src/SubtitleManager.cpp
    src/SubtitleLineModel.cpp
    src/SubtitleFilterModel.cpp
    src/SubtitleStyle.cpp
    src/SubtitleText.cpp
)

# Resume-position store. Its policy is what the tests are really about, so it is
# built as its own target rather than folded into the subtitle suite.
qt_add_executable(tst_playbackhistory tests/tst_playbackhistory.cpp src/PlaybackHistory.cpp)
target_include_directories(tst_playbackhistory PRIVATE src)
target_link_libraries(tst_playbackhistory PRIVATE Qt6::Core Qt6::Qml Qt6::Test)

# What plays next. Pure file-system reasoning, so it needs neither mpv nor a
# window -- and the ordering it has to get right ("ep2" before "ep10") is exactly
# the kind of thing that looks fine until a folder of episodes proves otherwise.
qt_add_executable(tst_playlist tests/tst_playlist.cpp src/Playlist.cpp)
target_include_directories(tst_playlist PRIVATE src)
target_link_libraries(tst_playlist PRIVATE Qt6::Core Qt6::Qml Qt6::Test)

# The hotkey table and its overrides. Pure policy over a temporary ini file.
qt_add_executable(tst_shortcuts tests/tst_shortcuts.cpp src/ShortcutRegistry.cpp)
target_include_directories(tst_shortcuts PRIVATE src)
target_link_libraries(tst_shortcuts PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test)

qt_add_executable(tst_subtitles tests/tst_subtitles.cpp ${SUBIXA_MODEL_SOURCES})
target_include_directories(tst_subtitles PRIVATE src)
target_compile_definitions(tst_subtitles PRIVATE
    SUBIXA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
target_link_libraries(tst_subtitles PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Test
    PkgConfig::FFMPEG
)

# Talks to libmpv directly with vo=null: verifies that selecting a track changes
# what mpv would render, read back as text from `sub-text` instead of pixels.
qt_add_executable(tst_mpvtracks tests/tst_mpvtracks.cpp)
target_include_directories(tst_mpvtracks PRIVATE src)
target_compile_definitions(tst_mpvtracks PRIVATE
    SUBIXA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
target_link_libraries(tst_mpvtracks PRIVATE
    Qt6::Core
    Qt6::Test
    PkgConfig::MPV
)

# The QML layer, loaded for real and driven headlessly. It needs the whole app:
# the same sources and the same QML module, because the point is to test the
# wiring in Main.qml rather than a mock of it.
#
# The module is declared a second time under the same URI, which is why it needs
# its own OUTPUT_DIRECTORY -- two targets in one directory would otherwise write
# their qmldir and type registrations over each other. They are separate
# binaries, so sharing the URI at runtime costs nothing.
qt_add_executable(tst_qmlpanel
    tests/tst_qmlpanel.cpp
    ${SUBIXA_APP_SOURCES}
)
target_compile_definitions(tst_qmlpanel PRIVATE SUBIXA_VERSION="${PROJECT_VERSION}")
qt_add_qml_module(tst_qmlpanel
    URI Subixa
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qmltest/Subixa
    QML_FILES ${SUBIXA_QML_FILES}
)
target_include_directories(tst_qmlpanel PRIVATE src)
target_compile_definitions(tst_qmlpanel PRIVATE
    SUBIXA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
target_link_libraries(tst_qmlpanel PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::OpenGL
    Qt6::Test
    PkgConfig::MPV
    PkgConfig::FFMPEG
)

add_test(NAME subtitles COMMAND tst_subtitles)
add_test(NAME mpvtracks COMMAND tst_mpvtracks)
add_test(NAME playbackhistory COMMAND tst_playbackhistory)
add_test(NAME playlist COMMAND tst_playlist)
add_test(NAME shortcuts COMMAND tst_shortcuts)
add_test(NAME qmlpanel COMMAND tst_qmlpanel)

# None of these needs a display; make sure they never try to open one.
set_tests_properties(subtitles mpvtracks playbackhistory playlist shortcuts PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=minimal"
    TIMEOUT 120
)

# The QML harness does build an object tree, so it needs a platform that can
# create a window -- offscreen, which never renders. `minimal` has no scene
# graph at all and Loaders never instantiate under it.
set_tests_properties(qmlpanel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 180
)

endif()  # SUBIXA_BUILD_TESTS
