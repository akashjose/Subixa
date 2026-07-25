#pragma once

#include <QtCore/QPointer>
#include <QtCore/QSize>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickFramebufferObject>

class MpvEngine;

// The video surface: libmpv's OpenGL render API drawing into an FBO we own, so
// the picture is a normal scene-graph node and QML (the transport, the subtitle
// browser) composites over it. The alternative, handing mpv a native window via
// --wid, puts video in a separate surface above the scene graph and makes
// overlays unreliable -- and QML chrome over video is the whole premise here.
//
// Owns the mpv_render_context and nothing else. Playback lives in MpvEngine,
// which is created before the QML engine and therefore outlives every item that
// draws from it -- which is what makes the teardown order right: this item's
// renderer frees the render context, and only afterwards is the mpv handle
// destroyed.
class MpvVideoItem : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(MpvEngine *engine READ engine WRITE setEngine NOTIFY engineChanged)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);
    ~MpvVideoItem() override;

    Renderer *createRenderer() const override;

    MpvEngine *engine() const { return m_engine; }
    void setEngine(MpvEngine *engine);

signals:
    void engineChanged();

protected:
    // Where the window is learned, on the GUI thread. Doing it in
    // createRenderer() -- which runs on the *render* thread -- meant calling
    // QQuickWindow setters while the GUI thread was live.
    void itemChange(ItemChange change, const ItemChangeData &data) override;

private slots:
    // All invoked from the render thread by queued connection, so each of these
    // runs on the GUI thread with the arguments copied.
    void onRenderContextCreated(bool software);
    void reportRenderer(const QString &renderer, const QString &version);
    void reportFboCap(const QSize &pane, const QSize &fbo);

private:
    QPointer<MpvEngine> m_engine;

    friend class MpvRenderer;
};
