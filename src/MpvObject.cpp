#include "MpvObject.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QtCore/QMetaObject>
#include <QtCore/QVarLengthArray>
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

} // namespace

// Lives on the render thread. Created lazily on first FBO creation, because the
// mpv render context needs a current GL context to initialise against.
class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer(MpvObject *obj) : m_obj(obj) {}

    ~MpvRenderer() override
    {
        if (m_mpvGL)
            mpv_render_context_free(m_mpvGL);
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        if (!m_mpvGL) {
            mpv_opengl_init_params glInit{getProcAddressMpv, nullptr};
            mpv_render_param params[]{
                {MPV_RENDER_PARAM_API_TYPE,
                 const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                {MPV_RENDER_PARAM_INVALID, nullptr}};

            const int rc = mpv_render_context_create(&m_mpvGL, m_obj->m_mpv, params);
            if (rc < 0) {
                qCritical("mpv_render_context_create failed: %s", mpv_error_string(rc));
                m_mpvGL = nullptr;
            } else {
                mpv_render_context_set_update_callback(m_mpvGL, &MpvObject::onMpvRedraw,
                                                       m_obj);
                // Queued before onRenderContextReady on purpose: the workaround has
                // to be in place before the pending file starts playing, or the
                // filter chain gets rebuilt mid-playback.
                if (usingSoftwareRasterizer()) {
                    QMetaObject::invokeMethod(m_obj, "forceEightBitVideo",
                                              Qt::QueuedConnection);
                }
                // Tell the item it can now safely start playback.
                QMetaObject::invokeMethod(m_obj, "onRenderContextReady",
                                          Qt::QueuedConnection);
            }
        }
        return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
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
        // is no longer trustworthy.
        QQuickWindow *win = m_obj->window();
        if (win)
            win->beginExternalCommands();
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
        if (m_needsFinish < 0) {
            m_needsFinish = (usingSoftwareRasterizer()
                             && !qEnvironmentVariableIsSet("CMP_NO_SYNC"))
                                ? 1
                                : 0;
        }
        if (m_needsFinish == 1) {
            if (QOpenGLContext *c = QOpenGLContext::currentContext())
                c->functions()->glFinish();
        }

        if (win)
            win->endExternalCommands();
    }

private:
    // Mesa's software rasterizers render 10-bit planes wrong: a yuv420p10 file
    // comes out either black or heavily striped, while its 8-bit twin is fine.
    // Detected from GL_RENDERER rather than assumed, so a real GPU keeps the
    // native 10-bit path.
    static bool usingSoftwareRasterizer()
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

    MpvObject *m_obj = nullptr;
    mpv_render_context *m_mpvGL = nullptr;
    int m_needsFinish = -1;  // -1 until GL_RENDERER has been read
};

MpvObject::MpvObject(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    m_mpv = mpv_create();
    if (!m_mpv)
        qFatal("could not create mpv context");

    // REQUIRED for the render API. Without it mpv picks a native VO at init
    // (wlshm under WSLg), spawns its own window, and never renders into our
    // FBO -- video ends up floating outside the app entirely.
    mpv_set_option_string(m_mpv, "vo", "libmpv");

    mpv_set_option_string(m_mpv, "terminal", "no");
    // WSL has no usable GPU decode path; probing just wastes time and can fail
    // in confusing ways. Software decode is fine for development.
    mpv_set_option_string(m_mpv, "hwdec", "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");

    if (mpv_initialize(m_mpv) < 0)
        qFatal("could not initialise mpv");

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);

    mpv_request_log_messages(m_mpv, "info");
    mpv_set_wakeup_callback(m_mpv, &MpvObject::onMpvWakeup, this);
}

MpvObject::~MpvObject()
{
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

QQuickFramebufferObject::Renderer *MpvObject::createRenderer() const
{
    if (QQuickWindow *win = window()) {
        win->setPersistentGraphics(true);
        win->setPersistentSceneGraph(true);
    }
    return new MpvRenderer(const_cast<MpvObject *>(this));
}

void MpvObject::onMpvRedraw(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvObject *>(ctx), "doUpdate",
                              Qt::QueuedConnection);
}

void MpvObject::onMpvWakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvObject *>(ctx), "onMpvEvents",
                              Qt::QueuedConnection);
}

void MpvObject::doUpdate()
{
    update();
}

void MpvObject::onMpvEvents()
{
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;
        handleMpvEvent(event);
    }
}

void MpvObject::handleMpvEvent(void *ev)
{
    mpv_event *event = static_cast<mpv_event *>(ev);

    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *prop = static_cast<mpv_event_property *>(event->data);
        if (!prop || !prop->data)
            break;
        const QByteArray name(prop->name);
        if (name == "time-pos" && prop->format == MPV_FORMAT_DOUBLE) {
            m_position = *static_cast<double *>(prop->data);
            emit positionChanged();
        } else if (name == "duration" && prop->format == MPV_FORMAT_DOUBLE) {
            m_duration = *static_cast<double *>(prop->data);
            emit durationChanged();
        } else if (name == "pause" && prop->format == MPV_FORMAT_FLAG) {
            m_paused = *static_cast<int *>(prop->data) != 0;
            emit pausedChanged();
        }
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        emit fileLoaded();
        break;
    case MPV_EVENT_LOG_MESSAGE: {
        auto *msg = static_cast<mpv_event_log_message *>(event->data);
        if (msg)
            emit logMessage(QString::fromUtf8(msg->text).trimmed());
        break;
    }
    default:
        break;
    }
}

void MpvObject::command(const QStringList &args)
{
    if (!m_mpv || args.isEmpty())
        return;

    QList<QByteArray> owned;
    owned.reserve(args.size());
    for (const QString &a : args)
        owned.append(a.toUtf8());

    QVarLengthArray<const char *, 8> argv;
    for (const QByteArray &b : owned)
        argv.append(b.constData());
    argv.append(nullptr);

    mpv_command(m_mpv, argv.data());
}

void MpvObject::loadFile(const QString &file)
{
    if (!m_renderReady) {
        m_pendingFile = file;
        return;
    }
    command({QStringLiteral("loadfile"), file});
}

void MpvObject::onRenderContextReady()
{
    m_renderReady = true;
    if (!m_pendingFile.isEmpty()) {
        const QString file = m_pendingFile;
        m_pendingFile.clear();
        command({QStringLiteral("loadfile"), file});
    }
}

void MpvObject::forceEightBitVideo()
{
    if (!m_mpv)
        return;

    // Costs nothing for 8-bit content -- the filter is a no-op when the input is
    // already yuv420p. For 10-bit it converts on the CPU, which is the price of
    // getting a correct picture out of a software rasterizer at all.
    mpv_set_property_string(m_mpv, "vf", "format=yuv420p");
    emit logMessage(QStringLiteral(
        "software rasterizer detected: converting video to 8-bit before upload"));
}

void MpvObject::setOption(const QString &name, const QString &value)
{
    if (m_mpv)
        mpv_set_option_string(m_mpv, name.toUtf8().constData(),
                              value.toUtf8().constData());
}

void MpvObject::togglePause()
{
    command({QStringLiteral("cycle"), QStringLiteral("pause")});
}

void MpvObject::seek(double seconds)
{
    command({QStringLiteral("seek"), QString::number(seconds),
             QStringLiteral("absolute")});
}
