#include "GraphicsSetup.h"

#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

int main(int argc, char *argv[])
{
    // The mpv render API here is the OpenGL one, so the scene graph must also be
    // OpenGL. Qt 6 can otherwise pick a different RHI backend and the FBO handle
    // we hand mpv would be meaningless. Must run before QGuiApplication.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    // Names first: the graphics probe caches its answer in QSettings, which needs
    // them, and it has to run before any GL context exists.
    QCoreApplication::setApplicationName(QStringLiteral("custom_media_player"));
    QCoreApplication::setOrganizationName(QStringLiteral("custom_media_player"));

    // Probe mode is this same binary re-run by the selector below: it creates a
    // context, prints GL_RENDERER and exits without ever loading the UI.
    if (GraphicsSetup::isProbeRequest(argc, argv)) {
        QGuiApplication probeApp(argc, argv);
        return GraphicsSetup::runProbe();
    }

    // Mesa reads GALLIUM_DRIVER when it loads the driver, which happens on the
    // first context -- so this has to run before QGuiApplication.
    const GraphicsSetup::Choice graphics = GraphicsSetup::configure(argc, argv);

    QGuiApplication app(argc, argv);

    qInfo().noquote() << "graphics:" << graphics.reason;

    const QStringList args = app.arguments();
    // Skip option-looking arguments so --gl-probe is never taken as a filename.
    const QString initialFile =
        args.size() > 1 && !args.at(1).startsWith(QLatin1String("--")) ? args.at(1)
                                                                      : QString();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("initialFile"), initialFile);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    engine.loadFromModule("CustomMediaPlayer", "Main");

    return app.exec();
}
