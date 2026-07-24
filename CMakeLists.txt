cmake_minimum_required(VERSION 3.21)
project(custom_media_player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 OpenGL)

# libmpv comes from apt (0.37 on 24.04). pkg-config keeps us off a hardcoded path.
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(custom_media_player
    src/main.cpp
    src/MpvObject.cpp
    src/MpvObject.h
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
)
