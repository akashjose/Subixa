// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "GraphicsSetup.h"
#include "MpvEngine.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QDebug>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#include <QtQuickControls2/QQuickStyle>

#ifdef Q_OS_WIN
// After the Qt headers, and with both guards: windows.h defines min and max as
// macros, which breaks anything including <algorithm> behind it.
// Guarded: Qt's own Windows build already defines both, and redefining them is a
// warning in a tree that is otherwise clean under -Wall -Wextra.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#endif

namespace {

#ifdef Q_OS_WIN
// The player is linked as a GUI binary -- see WIN32_EXECUTABLE in CMakeLists.txt
// -- so Windows gives it no console and Qt sends qInfo() to the debugger, where
// nobody launching from a terminal will find it. That is the right default for a
// double-click, but it also hides the two lines most worth reading when a
// Windows install misbehaves: the GL_RENDERER the driver reported and which
// hardware decoder mpv settled on.
//
// So borrow the console of whatever started us, if there is one. Launched from
// Explorer there is no parent console, AttachConsole fails, and nothing changes
// -- which is the point. No black window on a double-click.
void attachParentConsole()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;

    // Only the streams that are not already pointed somewhere. `subixa.exe >
    // log.txt` leaves a perfectly good file handle here, and reopening that onto
    // the console would quietly discard the redirection the user asked for.
    const auto adopt = [](DWORD stdHandle, FILE *stream, const char *mode) {
        const HANDLE existing = GetStdHandle(stdHandle);
        if (existing && existing != INVALID_HANDLE_VALUE)
            return;

        // Qt chooses between stderr and the debugger by looking at the standard
        // *handle*, while fprintf goes through the CRT's own stream, so both
        // have to be repointed or the attach only half works.
        const HANDLE console =
            CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
        if (console == INVALID_HANDLE_VALUE)
            return;
        SetStdHandle(stdHandle, console);
        std::freopen("CONOUT$", mode, stream);
    };

    adopt(STD_OUTPUT_HANDLE, stdout, "w");
    adopt(STD_ERROR_HANDLE, stderr, "w");
}
#endif  // Q_OS_WIN

}  // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Before anything logs, which means before the graphics probe below.
    attachParentConsole();
#endif

    // The mpv render API here is the OpenGL one, so the scene graph must also be
    // OpenGL. Qt 6 can otherwise pick a different RHI backend and the FBO handle
    // we hand mpv would be meaningless. Must run before QGuiApplication.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    // Grayscale-antialiased text, not the platform font engine's.
    //
    // Qt 6 defaults Qt Quick to NativeTextRendering, which on Linux honours
    // fontconfig -- and fontconfig here reports `rgba: 1`, meaning RGB subpixel
    // antialiasing. Every glyph then carries coloured fringes on its edges, and
    // WSLg composites the window to the Windows desktop over RDP, whose chroma
    // subsampling smears those fringes into whole saturated pixels. The result
    // is light text arriving as bright green and yellow, and small dim text --
    // the 11px timestamp column -- losing so much luminance that it reads as
    // black on a dark row and disappears entirely.
    //
    // QtTextRendering uses distance fields and antialiases in grayscale, so
    // there are no colour fringes for the transport to destroy. It also scales
    // better, which matters for a panel whose row text is user-adjustable from
    // 9 to 22 px. The cost is slightly softer glyphs at small sizes, which is a
    // fair trade for text that is the right colour.
    QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);

    // Names first: the graphics probe caches its answer in QSettings, which needs
    // them, and it has to run before any GL context exists.
    QCoreApplication::setApplicationName(QStringLiteral("subixa"));
    QCoreApplication::setOrganizationName(QStringLiteral("subixa"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SUBIXA_VERSION));

    // Probe mode is this same binary re-run by the selector below: it creates a
    // context, prints GL_RENDERER and exits without ever loading the UI.
    if (GraphicsSetup::isProbeRequest(argc, argv)) {
        QGuiApplication probeApp(argc, argv);
        return GraphicsSetup::runProbe();
    }

    // Mesa reads GALLIUM_DRIVER when it loads the driver, which happens on the
    // first context -- so this has to run before QGuiApplication.
    const GraphicsSetup::Choice graphics = GraphicsSetup::configure(argc, argv);

    // Pinned rather than left to Qt's per-platform default. Every control in the
    // app is drawn by components under qml/ui/ against the theme, and those need
    // a style that imposes neither geometry nor a palette of its own. Left unset,
    // a Windows build would silently select FluentWinUI3 and the two platforms
    // would stop looking like the same program.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);

    // Compiled in rather than read from the install prefix, so a build tree run
    // looks like an installed one. The desktop entry names the installed copy
    // separately; this is what the window and the task switcher use.
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/subixa.svg")));

    // Ties the window to subixa.desktop. The only mechanism under Wayland, which
    // has no WM_CLASS for a shell to match on. Name goes without the suffix.
    app.setDesktopFileName(QStringLiteral("subixa"));

    // Grayscale glyphs, belt and braces with the render type above.
    //
    // fontconfig here reports `rgba: 1`, so Qt's font engine produces per-channel
    // (subpixel) coverage, and on Mesa's D3D12 driver that comes out as solid
    // saturated colour rather than antialiased text: near-white body text renders
    // bright yellow, grey-blue renders bright green, and the dim 11px timestamp
    // column loses so much luminance it reads as black and disappears. Verified
    // it is the glyph path and not the display transport -- with no video loaded
    // at all, a vector icon of the same weight sitting directly above the text
    // renders perfectly while the text does not, and the same build on llvmpipe
    // is correct.
    QFont appFont = app.font();
    appFont.setStyleStrategy(QFont::StyleStrategy(appFont.styleStrategy()
                                                  | QFont::NoSubpixelAntialias));
    app.setFont(appFont);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A media player built around a docked subtitle browser."));
    parser.addHelpOption();
    parser.addVersionOption();
    // Declared so --help lists it. The flag itself is consumed above, before
    // QGuiApplication exists, because Mesa must be configured before any GL.
    parser.addOption({QStringLiteral("gl-probe"),
                      QStringLiteral("Internal: report GL_RENDERER and exit.")});
    parser.addPositionalArgument(
        QStringLiteral("files"),
        QStringLiteral("Media files to play. Several become a queue, in the "
                       "order given."),
        QStringLiteral("[files...]"));
    parser.process(app);

    qInfo().noquote() << "graphics:" << graphics.reason;

    // Declared before the QML engine, and that is load-bearing rather than
    // stylistic: destruction runs in reverse, so the QML engine -- and with it
    // the window, the video item and its render context -- is torn down first,
    // and every mpv_render_context_free() has completed before the mpv handle is
    // destroyed here. libmpv requires exactly that order. The other way round is
    // a use-after-free on the render thread that no test reaches, because an
    // offscreen platform never creates a render context at all.
    MpvEngine mpvEngine;
    if (!mpvEngine.isValid()) {
        qCritical().noquote()
            << "could not start the media engine:" << mpvEngine.initError();
        return 1;
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mpvEngine"), &mpvEngine);
    engine.rootContext()->setContextProperty(QStringLiteral("initialFiles"),
                                             parser.positionalArguments());

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    engine.loadFromModule("Subixa", "Main");

    return app.exec();
}
