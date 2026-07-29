# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Akash Jose

cmake_minimum_required(VERSION 3.21)
project(subixa VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# No compiler extensions: -std=c++23 rather than -std=gnu++23, so what builds
# under gcc here builds under the MSYS2 UCRT64 toolchain too.
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SUBIXA_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})

# QtQuick.Shapes -- which draws every icon in the app -- deliberately absent.
# Qt 6.9 ships it as a private module (only Qt6QuickShapesPrivate has a CMake
# config), and a dynamically linked build resolves the QML plugin through the
# import path at runtime with no link-time dependency at all. A future *static*
# build will need Qt6QuickShapesPrivate here and qt_import_qml_plugins.
set(SUBIXA_QT_COMPONENTS Core Gui Quick Qml QuickControls2 OpenGL Svg)
if(SUBIXA_BUILD_TESTS)
    list(APPEND SUBIXA_QT_COMPONENTS Test)
endif()
# The floor is the current release, not the oldest thing that happens to
# compile. Nothing has shipped yet, so there is no installed base to carry, and
# a floor that tracks the release lets the tree use the current spelling of an
# API rather than the deprecated one -- QSortFilterProxyModel's begin/end
# FilterChange pair being the first case. The MSYS2 side moves with it.
find_package(Qt6 6.12 REQUIRED COMPONENTS ${SUBIXA_QT_COMPONENTS})

# ---- libmpv and FFmpeg --------------------------------------------------
# Subtitle text is demuxed and decoded directly rather than read from mpv: mpv
# only exposes the line currently on screen, which cannot build a browsable list.
# So both libraries are needed, and both are found the same way.
#
# Found two different ways, because pkg-config is only the right answer on Unix:
#
#   Unix     pkg-config is authoritative. It knows the multiarch library
#            directories and the transitive private dependencies, and every
#            distribution ships the .pc files. libmpv comes from apt (0.37 on
#            24.04); this keeps us off a hardcoded path.
#
#   Windows  pkg-config is either absent or actively wrong. Under MSYS2 the .pc
#            files hardcode `prefix=/ucrt64`, so a *native* CMake is handed
#            `-I/ucrt64/include` -- an MSYS path with no drive letter -- and the
#            generate step fails with "includes non-existent path". Neither
#            PKG_CONFIG_SYSTEM_INCLUDE_PATH nor --define-prefix suppresses it
#            (checked against pkgconf 3.0.4). Looking the libraries up directly
#            sidesteps the path translation, and has the larger benefit that a
#            Windows build works against a plain libmpv SDK, which ships headers
#            and an import library but no .pc files at all.
#
# Both paths end at the same two targets, so nothing downstream has to care.
function(subixa_import_library name header)
    string(TOUPPER ${name} prefix)

    find_path(${prefix}_INCLUDE_DIR NAMES ${header})
    if(NOT ${prefix}_INCLUDE_DIR)
        message(FATAL_ERROR
            "Could not find ${header}. Point CMAKE_PREFIX_PATH at the ${name} SDK.")
    endif()

    add_library(Subixa::${name} INTERFACE IMPORTED)
    target_include_directories(Subixa::${name} INTERFACE ${${prefix}_INCLUDE_DIR})

    foreach(lib IN LISTS ARGN)
        find_library(${prefix}_${lib}_LIBRARY NAMES ${lib})
        if(NOT ${prefix}_${lib}_LIBRARY)
            message(FATAL_ERROR
                "Found ${header} but not the ${lib} library that goes with it.")
        endif()
        target_link_libraries(Subixa::${name} INTERFACE ${${prefix}_${lib}_LIBRARY})
    endforeach()
endfunction()

# Where tools/build-deps.sh put the media stack, if it was used.
#
# The distribution's packages cannot reach the versions this targets -- Ubuntu
# 24.04 is on FFmpeg 6.1.1 with no upgrade in apt, against a target of 8.1.x --
# so on Linux the stack is normally built from source into a prefix of its own.
# Pointing CMake at it here rather than exporting PKG_CONFIG_PATH by hand means
# one flag configures a build, and means the six suites find those libraries
# under ctest, which does not inherit an LD_LIBRARY_PATH from the shell.
#
# Left empty, everything below behaves exactly as it did: system pkg-config on
# Unix, find_path/find_library on Windows, where MSYS2 packages the stack
# against itself and no prefix is wanted.
set(SUBIXA_DEPS_PREFIX "" CACHE PATH
    "Prefix containing libmpv/FFmpeg built by tools/build-deps.sh (Linux only)")

if(SUBIXA_DEPS_PREFIX AND NOT WIN32)
    if(NOT IS_DIRECTORY "${SUBIXA_DEPS_PREFIX}")
        message(FATAL_ERROR
            "SUBIXA_DEPS_PREFIX=${SUBIXA_DEPS_PREFIX} is not a directory. "
            "Run tools/build-deps.sh first, or leave it unset to use system packages.")
    endif()
    # Both spellings: meson installs to lib/<triplet> and autotools to lib/.
    set(ENV{PKG_CONFIG_PATH}
        "${SUBIXA_DEPS_PREFIX}/lib/pkgconfig:${SUBIXA_DEPS_PREFIX}/lib/${CMAKE_LIBRARY_ARCHITECTURE}/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    # An rpath rather than an LD_LIBRARY_PATH, so the binary and every test
    # target find the stack with no environment set. Without this a suite links
    # against the prefix and then loads the distribution's older libraries at
    # run time, which is worse than failing to link at all.
    list(APPEND CMAKE_BUILD_RPATH
        "${SUBIXA_DEPS_PREFIX}/lib"
        "${SUBIXA_DEPS_PREFIX}/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
    message(STATUS "Using media stack from ${SUBIXA_DEPS_PREFIX}")
endif()

if(WIN32)
    subixa_import_library(mpv mpv/client.h mpv)
    subixa_import_library(ffmpeg libavformat/avformat.h avformat avcodec avutil)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
    )
    add_library(Subixa::mpv ALIAS PkgConfig::MPV)
    add_library(Subixa::ffmpeg ALIAS PkgConfig::FFMPEG)
endif()

qt_standard_project_setup(REQUIRES 6.12)

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

# Link as a GUI binary on Windows. qt_add_executable does not set this, so the
# default is the console subsystem and every launch -- including a double-click
# from Explorer -- pops a black console window alongside the player. The property
# is ignored on other platforms, so it needs no guard.
#
# The cost is that qInfo() output no longer has a console to land on: Qt's
# default handler sends it to the debugger instead. `QT_FORCE_STDERR_LOGGING=1`
# puts it back on stderr, which is how to read the GL_RENDERER and hwdec lines
# when diagnosing a Windows install. The test targets are deliberately left as
# console programs -- ctest reads their stdout.
set_target_properties(subixa PROPERTIES WIN32_EXECUTABLE TRUE)

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
    Subixa::mpv
    Subixa::ffmpeg
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
# The desktop entry, the icon and metainfo all install under the application id
# rather than the binary's name, which is what AppStream and every freedesktop
# consumer key on. The binary itself stays `subixa`, and so do the QSettings
# paths -- the id is the freedesktop identity, not the program's name.
install(FILES com.akashjose.Subixa.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)
install(FILES com.akashjose.Subixa.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo
)
# hicolor/scalable is what the entry's `Icon=com.akashjose.Subixa` resolves
# against. The source keeps the short name; only the installed copy is renamed.
install(FILES icons/subixa.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
    RENAME com.akashjose.Subixa.svg
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
    Subixa::ffmpeg
)

# The ASS [V4+ Styles] table: parsing it, and threading a base style through to
# the markup the browser renders. Its own target because tst_subtitles is the
# single-writer file this wave was arranged to keep out of.
qt_add_executable(tst_assstyles tests/tst_assstyles.cpp ${SUBIXA_MODEL_SOURCES})
target_include_directories(tst_assstyles PRIVATE src)
target_compile_definitions(tst_assstyles PRIVATE
    SUBIXA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
target_link_libraries(tst_assstyles PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test Subixa::ffmpeg)

# The search proxy on its own. Split out of tst_subtitles rather than appended to
# it: that file is 1165 lines and is the single-writer bottleneck the audit
# named, and the proxy is now a class in its own right rather than a
# QSortFilterProxyModel subclass.
qt_add_executable(tst_searchproxy tests/tst_searchproxy.cpp ${SUBIXA_MODEL_SOURCES})
target_include_directories(tst_searchproxy PRIVATE src)
target_compile_definitions(tst_searchproxy PRIVATE
    SUBIXA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
target_link_libraries(tst_searchproxy PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test Subixa::ffmpeg)

# The golden-file check over the committed corpus. Separate from tst_subtitles
# because it is the only suite whose inputs are bytes in git rather than media
# muxed at test time -- which is what lets a Linux result and a Windows result be
# compared at all. Links mpv purely to print its client API version in the
# banner, so a red after a dependency bump names which library moved.
qt_add_executable(tst_conformance tests/tst_conformance.cpp ${SUBIXA_MODEL_SOURCES})
target_include_directories(tst_conformance PRIVATE src)
target_compile_definitions(tst_conformance PRIVATE
    SUBIXA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
target_link_libraries(tst_conformance PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Test
    Subixa::ffmpeg
    Subixa::mpv
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
    Subixa::mpv
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
    Subixa::mpv
    Subixa::ffmpeg
)

# The same warnings the app target gets. tst_qmlpanel recompiles the entire app
# source list, so without this more than half of what compiles here was built
# with warnings off while the tree was described as clean under -Wall -Wextra.
foreach(t tst_playbackhistory tst_playlist tst_shortcuts tst_subtitles
          tst_mpvtracks tst_qmlpanel tst_conformance tst_searchproxy tst_assstyles)
    target_compile_options(${t} PRIVATE -Wall -Wextra)
endforeach()

add_test(NAME subtitles COMMAND tst_subtitles)
add_test(NAME mpvtracks COMMAND tst_mpvtracks)
add_test(NAME playbackhistory COMMAND tst_playbackhistory)
add_test(NAME playlist COMMAND tst_playlist)
add_test(NAME shortcuts COMMAND tst_shortcuts)
add_test(NAME qmlpanel COMMAND tst_qmlpanel)
add_test(NAME conformance COMMAND tst_conformance)
add_test(NAME searchproxy COMMAND tst_searchproxy)
add_test(NAME assstyles COMMAND tst_assstyles)

# None of these needs a display; make sure they never try to open one.
set_tests_properties(subtitles mpvtracks playbackhistory playlist shortcuts conformance searchproxy assstyles PROPERTIES
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

# ---- fixtures are not optional ------------------------------------------
# Three suites -- ~2080 lines of assertions -- open a fixture in initTestCase and
# QSKIP when it is absent. QSKIP exits 0, so a checkout that has never run
# make-fixtures.sh reported six green having asserted almost nothing, and every
# "verified by ctest" claim in this repository was unfalsifiable.
#
# REQUIRED_FILES is the fix: ctest marks a test NOT RUN and *fails* it when a
# listed file is missing, so the absence is loud and the exit code is non-zero.
# The lists are the fixtures each suite actually opens -- for `subtitles` that
# includes the three sidecars, which are discovered beside sidecar.mp4 rather
# than named in the source.
set(SUBIXA_TESTDATA ${CMAKE_CURRENT_SOURCE_DIR}/testdata)

set_tests_properties(subtitles PROPERTIES REQUIRED_FILES
    "${SUBIXA_TESTDATA}/subs.mkv;${SUBIXA_TESTDATA}/movtext.mp4;${SUBIXA_TESTDATA}/shifted.mkv;${SUBIXA_TESTDATA}/shifted.mp4;${SUBIXA_TESTDATA}/sidecar.mp4;${SUBIXA_TESTDATA}/sidecar.srt;${SUBIXA_TESTDATA}/sidecar.ass;${SUBIXA_TESTDATA}/sidecar.fr.srt")

set_tests_properties(mpvtracks PROPERTIES REQUIRED_FILES
    "${SUBIXA_TESTDATA}/subs.mkv;${SUBIXA_TESTDATA}/en.srt;${SUBIXA_TESTDATA}/sidecar.srt")

set_tests_properties(qmlpanel PROPERTIES REQUIRED_FILES
    "${SUBIXA_TESTDATA}/subs.mkv")

# The conformance corpus is committed, so these are present in any complete
# checkout. Listing them anyway turns "somebody deleted a fixture" into a named
# missing file rather than a decode error thirty lines into a golden diff.
set_tests_properties(conformance PROPERTIES REQUIRED_FILES
    "${SUBIXA_TESTDATA}/conformance/basic.mkv;${SUBIXA_TESTDATA}/conformance/entities.mkv;${SUBIXA_TESTDATA}/conformance/drawing.mkv;${SUBIXA_TESTDATA}/conformance/shifted.mkv;${SUBIXA_TESTDATA}/conformance/noshift.mp4;${SUBIXA_TESTDATA}/conformance/manytracks.mkv;${SUBIXA_TESTDATA}/conformance/sidecar.mp4;${SUBIXA_TESTDATA}/conformance/golden.tsv")

# There is deliberately no SKIP_REGULAR_EXPRESSION here, and it must not come
# back. It reads like a safety net and is the opposite of one: the expression is
# matched against the whole output, so a run that prints the phrase AND fails an
# assertion AND exits 1 is reported ***Skipped, is not counted in "tests failed",
# and ctest exits 0. Measured, not assumed -- a one-test project whose command is
# `echo 'fixtures missing'; echo FAIL; exit 1` reports "100% tests passed" under
# it. That is the exact defect the REQUIRED_FILES lists above exist to close, so
# adding this alongside them reopened it by another route.
#
# A missing fixture is caught by REQUIRED_FILES before the binary runs, and by a
# hard QVERIFY2 inside the suite if it disappears after that.

# The render canary needs a window, a compositor and a capture path, so it is a
# tool rather than a test. Registering it DISABLED gives it a name in `ctest -N`
# and a way to be run deliberately -- `ctest -L manual --test-dir build
# --force-new-ctest-process -R rendercanary` -- without ever running in a
# headless batch, where it would fail for the wrong reason.
add_test(NAME rendercanary COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/render-canary.sh)
set_tests_properties(rendercanary PROPERTIES
    DISABLED TRUE
    RUN_SERIAL TRUE
    LABELS manual
    TIMEOUT 300
)

endif()  # SUBIXA_BUILD_TESTS
