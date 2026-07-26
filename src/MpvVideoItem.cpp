// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "MpvVideoItem.h"

#include "FboCap.h"
#include "MpvEngine.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QtCore/QMetaObject>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
// Qt 6 moved QOpenGLFramebufferObject out of QtGui into the QtOpenGL module.
#include <QtOpenGL/QOpenGLFramebufferObject>
#include <QtQuick/QQuickWindow>

namespace {

void *getProcAddressMpv(void *ctx, const char *name)
{
    Q_UNUSED(ctx);
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx)
        return nullptr;
    return reinterpret_cast<void *>(glctx->getProcAddress(QByteArray(name)));
}

// Mesa's software rasterizers render 10-bit planes wrong: a yuv420p10 file
// comes out either black or heavily striped, while its 8-bit twin is fine.
// Detected from GL_RENDERER rather than assumed, so a real GPU keeps the
// native 10-bit path.
bool rendererIsSoftware()
{
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx)
        return false;

    const auto *name = reinterpret_cast<const char *>(
        glctx->functions()->glGetString(GL_RENDERER));
    if (!name)
        return false;

    // Covers llvmpipe, softpipe, swrast, and Mesa's classic "Software
    // Rasterizer" string, which contains none of the driver names. zink
    // reports the Vulkan device it sits on, so "zink Vulkan 1.3(llvmpipe ...)"
    // matches while "zink Vulkan 1.3(AMD Radeon ...)" correctly does not.
    const QByteArray renderer = QByteArray(name).toLower();
    return renderer.contains("llvmpipe") || renderer.contains("softpipe")
           || renderer.contains("swrast") || renderer.contains("software");
}

}  // namespace

// Lives on the render thread. Created lazily on first FBO creation, because the
// mpv render context needs a current GL context to initialise against.
class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer(MpvVideoItem *item) : m_item(item) {}

    ~MpvRenderer() override
    {
        if (!m_mpvGL)
            return;
        mpv_render_context_free(m_mpvGL);
        m_mpvGL = nullptr;
        // Decremented here, on the render thread, right after the free. The
        // counter is atomic for that reason, and the engine's destructor reads
        // it to catch a future reordering of main() that would otherwise be a
        // silent use-after-free. `m_engine` is sampled under synchronize() and
        // is valid as long as the ordering this checks for holds -- which is the
        // honest limit of what a diagnostic inside the broken case can do.
        if (m_engine)
            m_engine->renderContextDestroyed();
    }

    // Qt asks for an FBO the size of the item; on the software rasterizer we hand
    // back a smaller one and let Qt scale it over the item (trap 10, and the CPU
    // cost). That is only safe because MpvVideoItem turns textureFollowsItemSize
    // off: with it on, Qt compares the FBO's size against the item's every frame
    // and destroys any FBO that disagrees, so a capped one would be recreated
    // forever. With it off, recreation is ours to ask for -- see synchronize().
    QSize targetFboSize(const QSize &itemSize) const
    {
        if (!m_software)
            return itemSize;
        // SUBIXA_NO_FBO_CAP=1 renders at full pane size on the software path, which
        // is how to A/B the cap -- both the corruption it prevents and the CPU it
        // saves. Same idea as SUBIXA_NO_SYNC for the glFinish().
        if (qEnvironmentVariableIsSet("SUBIXA_NO_FBO_CAP"))
            return itemSize;
        return FboCap::cappedSize(itemSize, m_videoSize);
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        if (!m_mpvGL && m_handle) {
            mpv_opengl_init_params glInit{getProcAddressMpv, nullptr};
            mpv_render_param params[]{
                {MPV_RENDER_PARAM_API_TYPE,
                 const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                {MPV_RENDER_PARAM_INVALID, nullptr}};

            const int rc = mpv_render_context_create(&m_mpvGL, m_handle, params);
            if (rc < 0) {
                qCritical("mpv_render_context_create failed: %s", mpv_error_string(rc));
                m_mpvGL = nullptr;
            } else {
                mpv_render_context_set_update_callback(m_mpvGL, &MpvRenderer::onRedraw,
                                                       this);

                // GL_RENDERER decides three separate things -- the 10-bit
                // workaround, the glFinish, and the framebuffer cap -- so it is
                // read once, here, and everything downstream is told the answer
                // rather than asking again.
                m_software = rendererIsSoftware();
                if (m_engine)
                    m_engine->renderContextCreated();

                // Say which driver we actually got. "No software-rasterizer
                // message appeared" is a poor way to learn that hardware GL came
                // up, especially when checking whether WSL picked up D3D12
                // passthrough instead of llvmpipe.
                const char *renderer =
                    reinterpret_cast<const char *>(glGetString(GL_RENDERER));
                const char *version =
                    reinterpret_cast<const char *>(glGetString(GL_VERSION));
                QMetaObject::invokeMethod(
                    m_item, "reportRenderer", Qt::QueuedConnection,
                    Q_ARG(QString, QString::fromUtf8(renderer ? renderer : "?")),
                    Q_ARG(QString, QString::fromUtf8(version ? version : "?")));

                // The workaround has to be in place before the pending file
                // starts playing, or the filter chain gets rebuilt mid-playback.
                // The same goes for hwdec, which mpv does accept at runtime but
                // only by tearing the decoder down and rebuilding it. Both are
                // decided inside onRenderContextCreated, which also releases the
                // queued file -- in that order.
                QMetaObject::invokeMethod(m_item, "onRenderContextCreated",
                                          Qt::QueuedConnection,
                                          Q_ARG(bool, m_software));
            }
        }
        const QSize target = targetFboSize(size);
        if (target != size) {
            QMetaObject::invokeMethod(
                m_item, "reportFboCap", Qt::QueuedConnection,
                Q_ARG(QSize, size), Q_ARG(QSize, target));
        }
        m_fboSize = target;
        return QQuickFramebufferObject::Renderer::createFramebufferObject(target);
    }

    // Runs on the render thread with the GUI thread blocked, so reading the item
    // is safe. This is where a resize -- or mpv finally reporting the video size --
    // turns into a new framebuffer, since Qt no longer does it for us. It is also
    // the only place the window pointer and the mpv handle may be sampled.
    void synchronize(QQuickFramebufferObject *item) override
    {
        auto *video = static_cast<MpvVideoItem *>(item);
        m_engine = video->engine();
        m_handle = m_engine ? m_engine->handle() : nullptr;
        m_videoSize = m_engine ? m_engine->videoSize() : QSize();
        m_window = item->window();

        const QSize itemSize = QSize(int(item->width()), int(item->height()));
        if (itemSize.isEmpty())
            return;
        if (targetFboSize(itemSize) != m_fboSize)
            invalidateFramebufferObject();
    }

    void render() override
    {
        if (!m_mpvGL)
            return;

        QOpenGLFramebufferObject *fbo = framebufferObject();
        if (!fbo)
            return;

        mpv_opengl_fbo mpfbo;
        mpfbo.fbo = static_cast<int>(fbo->handle());
        mpfbo.w = fbo->width();
        mpfbo.h = fbo->height();
        mpfbo.internal_format = 0;

        // Qt's FBO origin matches mpv's expectation here. If video shows up
        // vertically mirrored on some driver, flip this to 1.
        int flipY = 0;

        mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
                                     {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                                     {MPV_RENDER_PARAM_INVALID, nullptr}};

        // mpv issues raw GL calls, so Qt's RHI has to be told its cached state
        // is no longer trustworthy. m_window is sampled in synchronize() rather
        // than read from the item here: the GUI thread is running by now and may
        // be reparenting the item.
        if (m_window)
            m_window->beginExternalCommands();
        mpv_render_context_render(m_mpvGL, params);

        // mpv's rendering must have *completed* before Qt samples this FBO as a
        // texture. A conformant driver tracks that render-to-texture dependency
        // itself; llvmpipe does not, so the scene graph composites a partially
        // rasterised surface. Evidence: same GL context, no GL error, and the
        // FBO reads back perfectly via toImage() -- whose readback is itself the
        // missing synchronisation.
        //
        // It hides below roughly 2048 px, where mpv's render finishes before Qt
        // gets there. Above that the frame arrives black, or streaked, or with a
        // fine mesh of unwritten pixels depending on how far rasterisation got.
        //
        // glFinish, not glFlush: flushing only submits the work, which visibly
        // improves the frame without fixing it. Restricted to software
        // rasterizers so a real GPU is not stalled every frame for a bug it
        // does not have.
        if (m_software && !qEnvironmentVariableIsSet("SUBIXA_NO_SYNC")) {
            if (QOpenGLContext *c = QOpenGLContext::currentContext())
                c->functions()->glFinish();
        }

        if (m_window)
            m_window->endExternalCommands();
    }

private:
    // mpv asks for a repaint from one of its own threads. Hopping through the
    // item rather than calling update() directly keeps that on the GUI thread.
    static void onRedraw(void *ctx)
    {
        auto *self = static_cast<MpvRenderer *>(ctx);
        if (!self || !self->m_item)
            return;
        MpvVideoItem *item = self->m_item;
        QMetaObject::invokeMethod(item, [item] { item->update(); },
                                  Qt::QueuedConnection);
    }

    // Raw, not QPointer: QPointer is not thread-safe and this is read from the
    // render thread. Qt destroys the Renderer as part of tearing the item's
    // scene-graph state down, so the item outlives it.
    MpvVideoItem *m_item = nullptr;
    MpvEngine *m_engine = nullptr;       // sampled under synchronize()
    mpv_handle *m_handle = nullptr;      // likewise
    QQuickWindow *m_window = nullptr;    // likewise
    mpv_render_context *m_mpvGL = nullptr;
    bool m_software = false;
    QSize m_fboSize;    // what we actually created, which may be capped
    QSize m_videoSize;  // copied from the engine under synchronize()
};

MpvVideoItem::MpvVideoItem(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    // The renderer may hand back a framebuffer smaller than this item (see
    // FboCap.h). With textureFollowsItemSize left on, Qt compares the FBO's size
    // against the item's on every frame and destroys any that disagrees, so a
    // capped framebuffer would be recreated forever. Off, recreation is asked for
    // explicitly in MpvRenderer::synchronize(); Qt still stretches the texture
    // across the item either way, which is what makes the cap invisible except as
    // a softer picture.
    setTextureFollowsItemSize(false);
}

MpvVideoItem::~MpvVideoItem() = default;

void MpvVideoItem::setEngine(MpvEngine *engine)
{
    if (m_engine == engine)
        return;
    m_engine = engine;
    emit engineChanged();
    update();
}

void MpvVideoItem::itemChange(ItemChange change, const ItemChangeData &data)
{
    if (change == ItemSceneChange && data.window) {
        // Both of these are GUI-thread QQuickWindow setters. They used to be
        // called from createRenderer(), which runs on the render thread -- so
        // they were reaching into QQuickWindowPrivate while the GUI thread was
        // live. Nothing was observed to break, which is the usual way with a
        // race like that.
        data.window->setPersistentGraphics(true);
        data.window->setPersistentSceneGraph(true);
    }
    QQuickFramebufferObject::itemChange(change, data);
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const
{
    return new MpvRenderer(const_cast<MpvVideoItem *>(this));
}

void MpvVideoItem::onRenderContextCreated(bool software)
{
    if (!m_engine)
        return;
    // Order matters: the driver-dependent options have to be applied before the
    // queued file is released, or the filter chain is rebuilt mid-playback.
    m_engine->setSoftwareRendering(software);
    m_engine->notifyRenderContextReady();
}

void MpvVideoItem::reportRenderer(const QString &renderer, const QString &version)
{
    if (!m_engine)
        return;
    m_engine->setRendererName(renderer);
    m_engine->log(QStringLiteral("GL_RENDERER: %1 | %2").arg(renderer, version));
}

void MpvVideoItem::reportFboCap(const QSize &pane, const QSize &fbo)
{
    if (m_engine)
        m_engine->log(
            QStringLiteral("software rasterizer: rendering %1x%2 into a "
                           "%3x%4 framebuffer, scaled to fit")
                .arg(pane.width())
                .arg(pane.height())
                .arg(fbo.width())
                .arg(fbo.height()));
}
