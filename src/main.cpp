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

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("custom_media_player"));
    app.setOrganizationName(QStringLiteral("custom_media_player"));

    const QStringList args = app.arguments();
    const QString initialFile = args.size() > 1 ? args.at(1) : QString();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("initialFile"), initialFile);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    engine.loadFromModule("CustomMediaPlayer", "Main");

    return app.exec();
}
