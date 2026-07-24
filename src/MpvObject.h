#pragma once

#include <QtCore/QStringList>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickFramebufferObject>

struct mpv_handle;

// Video surface backed by libmpv's OpenGL render API. mpv draws into an FBO we
// own, so the whole thing is a normal scene-graph node -- QML (controls, the
// subtitle browser) composites over it. The alternative, handing mpv a native
// window via --wid, puts video in a separate surface above the scene graph and
// makes overlays unreliable.
class MpvObject : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)

public:
    explicit MpvObject(QQuickItem *parent = nullptr);
    ~MpvObject() override;

    Renderer *createRenderer() const override;

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }

    Q_INVOKABLE void loadFile(const QString &file);
    Q_INVOKABLE void command(const QStringList &args);
    Q_INVOKABLE void setOption(const QString &name, const QString &value);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void fileLoaded();
    void logMessage(const QString &text);

private slots:
    void doUpdate();
    void onMpvEvents();
    void onRenderContextReady();

private:
    static void onMpvRedraw(void *ctx);
    static void onMpvWakeup(void *ctx);
    void handleMpvEvent(void *event);

    mpv_handle *m_mpv = nullptr;
    double m_position = 0.0;
    double m_duration = 0.0;
    bool m_paused = true;

    // The render context only exists once the item has been rendered at least
    // once. Loading before that makes mpv's VO fail with "No render context
    // set" and silently drop video, so early requests are queued.
    bool m_renderReady = false;
    QString m_pendingFile;

    friend class MpvRenderer;
};
