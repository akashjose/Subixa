#include "MpvObject.h"

#include "FboCap.h"
#include "MpvTrackList.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QtCore/QMetaObject>
#include <QtCore/QUrl>
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

    // Qt asks for an FBO the size of the item; on the software rasterizer we hand
    // back a smaller one and let Qt scale it over the item (trap 10, and the CPU
    // cost). That is only safe because MpvObject turns textureFollowsItemSize
    // off: with it on, Qt compares the FBO's size against the item's every frame
    // and destroys any FBO that disagrees, so a capped one would be recreated
    // forever. With it off, recreation is ours to ask for -- see synchronize().
    QSize targetFboSize(const QSize &itemSize) const
    {
        if (!usingSoftwareRasterizer())
            return itemSize;
        // CMP_NO_FBO_CAP=1 renders at full pane size on the software path, which
        // is how to A/B the cap -- both the corruption it prevents and the CPU it
        // saves. Same idea as CMP_NO_SYNC for the glFinish().
        if (qEnvironmentVariableIsSet("CMP_NO_FBO_CAP"))
            return itemSize;
        return FboCap::cappedSize(itemSize, m_videoSize);
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

                // Say which driver we actually got. Two workarounds and their size
                // limits hinge on this being a software rasterizer, and "no
                // software-rasterizer message appeared" is a poor way to learn that
                // hardware GL came up -- especially when porting, or when checking
                // whether WSL picked up D3D12 passthrough instead of llvmpipe.
                if (QOpenGLFunctions *gl = QOpenGLContext::currentContext()
                                               ? QOpenGLContext::currentContext()
                                                     ->functions()
                                               : nullptr) {
                    Q_UNUSED(gl);
                    const char *renderer = reinterpret_cast<const char *>(
                        glGetString(GL_RENDERER));
                    const char *version = reinterpret_cast<const char *>(
                        glGetString(GL_VERSION));
                    QMetaObject::invokeMethod(
                        m_obj, "reportRenderer", Qt::QueuedConnection,
                        Q_ARG(QString, QString::fromUtf8(renderer ? renderer : "?")),
                        Q_ARG(QString, QString::fromUtf8(version ? version : "?")));
                }

                // Queued before onRenderContextReady on purpose: the workaround has
                // to be in place before the pending file starts playing, or the
                // filter chain gets rebuilt mid-playback. The same goes for
                // hwdec, which mpv does accept at runtime but only by tearing the
                // decoder down and rebuilding it.
                if (usingSoftwareRasterizer()) {
                    QMetaObject::invokeMethod(m_obj, "forceEightBitVideo",
                                              Qt::QueuedConnection);
                } else {
                    QMetaObject::invokeMethod(m_obj, "enableHardwareDecoding",
                                              Qt::QueuedConnection);
                }
                // Tell the item it can now safely start playback.
                QMetaObject::invokeMethod(m_obj, "onRenderContextReady",
                                          Qt::QueuedConnection);
            }
        }
        const QSize target = targetFboSize(size);
        if (target != size) {
            QMetaObject::invokeMethod(
                m_obj, "reportFboCap", Qt::QueuedConnection,
                Q_ARG(QSize, size), Q_ARG(QSize, target));
        }
        m_fboSize = target;
        return QQuickFramebufferObject::Renderer::createFramebufferObject(target);
    }

    // Runs on the render thread with the GUI thread blocked, so reading the item
    // is safe. This is where a resize -- or mpv finally reporting the video size --
    // turns into a new framebuffer, since Qt no longer does it for us.
    void synchronize(QQuickFramebufferObject *item) override
    {
        m_videoSize = m_obj->videoSize();

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
    QSize m_fboSize;         // what we actually created, which may be capped
    QSize m_videoSize;       // copied from the item under synchronize()
};

MpvObject::MpvObject(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    // The renderer may hand back a framebuffer smaller than this item (see
    // FboCap.h). With textureFollowsItemSize left on, Qt compares the FBO's size
    // against the item's on every frame and destroys any that disagrees, so a
    // capped framebuffer would be recreated forever. Off, recreation is asked for
    // explicitly in MpvRenderer::synchronize(); Qt still stretches the texture
    // across the item either way, which is what makes the cap invisible except as
    // a softer picture.
    setTextureFollowsItemSize(false);

    m_mpv = mpv_create();
    if (!m_mpv)
        qFatal("could not create mpv context");

    // REQUIRED for the render API. Without it mpv picks a native VO at init
    // (wlshm under WSLg), spawns its own window, and never renders into our
    // FBO -- video ends up floating outside the app entirely.
    mpv_set_option_string(m_mpv, "vo", "libmpv");

    mpv_set_option_string(m_mpv, "terminal", "no");

    // Decode starts on the CPU and is only moved off it once GL_RENDERER has
    // proved there is a real GPU underneath (see enableHardwareDecoding). Doing
    // it the other way round means probing hardware decoders on a software
    // rasterizer, which wastes time at startup and fails in confusing ways.
    //
    // CMP_HWDEC pins the setting to any value mpv accepts -- no, auto, auto-safe,
    // vaapi -- and switches the automatic choice off, which is how to test a
    // decoder this machine would not have picked.
    const QByteArray forcedHwdec = qgetenv("CMP_HWDEC");
    m_hwdecForced = !forcedHwdec.isEmpty();
    mpv_set_option_string(m_mpv, "hwdec",
                          m_hwdecForced ? forcedHwdec.constData() : "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");

    if (mpv_initialize(m_mpv) < 0)
        qFatal("could not initialise mpv");

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    // The notification is all that is needed here; the list itself is re-read as
    // a node, which MPV_FORMAT_NODE observation would not simplify.
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NONE);
    // As strings, because both are "no" when off and "auto" before a file loads;
    // MPV_FORMAT_INT64 simply fails on those and the change would be missed.
    mpv_observe_property(m_mpv, 0, "sid", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "aid", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE);
    // Decoded size, used to avoid rendering a 720p file into a 2560 px surface on
    // the software rasterizer. dwidth/dheight are the display size, so anamorphic
    // content gives the size actually drawn rather than the stored one.
    mpv_observe_property(m_mpv, 0, "dwidth", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "dheight", MPV_FORMAT_INT64);
    // What mpv actually ended up using, which is the only honest answer about
    // whether decode left the CPU -- asking for hardware decoding and getting it
    // are different things, and mpv falls back silently.
    mpv_observe_property(m_mpv, 0, "hwdec-current", MPV_FORMAT_STRING);

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
        if (!prop)
            break;
        const QByteArray name(prop->name);

        // Observed with MPV_FORMAT_NONE, so prop->data is null by design -- this
        // has to come before the guard below or the notification is dropped.
        if (name == "track-list") {
            refreshTracks();
            break;
        }

        if (!prop->data)
            break;
        if (name == "hwdec-current") {
            // Logged rather than exposed: nothing in the UI depends on it, and
            // the question it answers -- did decode actually leave the CPU --
            // only ever comes up while looking at a log anyway.
            const QString value =
                QString::fromUtf8(*static_cast<char **>(prop->data));
            if (!value.isEmpty())
                emit logMessage(QStringLiteral("decoder: hwdec-current = %1").arg(value));
            break;
        }
        if (name == "sid" || name == "aid") {
            // "no" when off, "auto" before a track is chosen: both mean "no
            // selection" to QML, which wants a number.
            const QString value =
                QString::fromUtf8(*static_cast<char **>(prop->data));
            bool ok = false;
            const int id = value.toInt(&ok);
            if (name == "sid") {
                m_subtitleTrack = ok ? id : -1;
                emit subtitleTrackChanged();
            } else {
                m_audioTrack = ok ? id : -1;
                emit audioTrackChanged();
            }
            break;
        }
        if (name == "time-pos" && prop->format == MPV_FORMAT_DOUBLE) {
            m_position = *static_cast<double *>(prop->data);
            emit positionChanged();
        } else if (name == "duration" && prop->format == MPV_FORMAT_DOUBLE) {
            m_duration = *static_cast<double *>(prop->data);
            emit durationChanged();
        } else if (name == "pause" && prop->format == MPV_FORMAT_FLAG) {
            m_paused = *static_cast<int *>(prop->data) != 0;
            emit pausedChanged();
        } else if (name == "volume" && prop->format == MPV_FORMAT_DOUBLE) {
            m_volume = *static_cast<double *>(prop->data);
            emit volumeChanged();
        } else if (name == "mute" && prop->format == MPV_FORMAT_FLAG) {
            m_muted = *static_cast<int *>(prop->data) != 0;
            emit mutedChanged();
        } else if (name == "speed" && prop->format == MPV_FORMAT_DOUBLE) {
            m_speed = *static_cast<double *>(prop->data);
            emit speedChanged();
        } else if (name == "dwidth" && prop->format == MPV_FORMAT_INT64) {
            m_videoSize.setWidth(int(*static_cast<int64_t *>(prop->data)));
        } else if (name == "dheight" && prop->format == MPV_FORMAT_INT64) {
            m_videoSize.setHeight(int(*static_cast<int64_t *>(prop->data)));
        }
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        emit fileLoaded();
        break;
    case MPV_EVENT_END_FILE: {
        // Reaching the end of a file is not news; failing to play one is. mpv
        // reports both through this event, distinguished only by the reason.
        auto *end = static_cast<mpv_event_end_file *>(event->data);
        if (end && end->reason == MPV_END_FILE_REASON_ERROR)
            emit playbackFailed(QString::fromUtf8(mpv_error_string(end->error)));
        break;
    }
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

namespace {

// mpv_node -> QVariant. track-list is an array of maps of scalars, so this only
// has to cover those; anything unexpected lands as an invalid QVariant rather
// than being guessed at.
QVariant nodeToVariant(const mpv_node &node)
{
    switch (node.format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node.u.string);
    case MPV_FORMAT_FLAG:
        return node.u.flag != 0;
    case MPV_FORMAT_INT64:
        return QVariant::fromValue(node.u.int64);
    case MPV_FORMAT_DOUBLE:
        return node.u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        list.reserve(node.u.list->num);
        for (int i = 0; i < node.u.list->num; ++i)
            list.append(nodeToVariant(node.u.list->values[i]));
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < node.u.list->num; ++i) {
            map.insert(QString::fromUtf8(node.u.list->keys[i]),
                       nodeToVariant(node.u.list->values[i]));
        }
        return map;
    }
    default:
        return QVariant();
    }
}

}  // namespace

void MpvObject::refreshTracks()
{
    m_tracks.clear();
    if (!m_mpv) {
        emit tracksChanged();
        return;
    }

    mpv_node node;
    if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &node) < 0) {
        emit tracksChanged();
        return;
    }

    const QVariantList raw = nodeToVariant(node).toList();
    mpv_free_node_contents(&node);

    m_tracks.reserve(raw.size());
    for (const QVariant &entry : raw) {
        const QVariantMap in = entry.toMap();
        // Renamed to camelCase on the way through, both to match the rest of the
        // QML-facing API and so the keys MpvTrackList works with are fixed here
        // rather than spread across mpv's spelling.
        QVariantMap out;
        out[QStringLiteral("id")] = in.value(QStringLiteral("id")).toInt();
        out[QStringLiteral("type")] = in.value(QStringLiteral("type"));
        out[QStringLiteral("language")] = in.value(QStringLiteral("lang"));
        out[QStringLiteral("title")] = in.value(QStringLiteral("title"));
        out[QStringLiteral("codec")] = in.value(QStringLiteral("codec"));
        out[QStringLiteral("ffIndex")] =
            in.value(QStringLiteral("ff-index"), -1).toInt();
        out[QStringLiteral("selected")] =
            in.value(QStringLiteral("selected"), false).toBool();
        out[QStringLiteral("default")] =
            in.value(QStringLiteral("default"), false).toBool();
        out[QStringLiteral("external")] =
            in.value(QStringLiteral("external"), false).toBool();
        out[QStringLiteral("externalFilename")] =
            in.value(QStringLiteral("external-filename"));
        m_tracks.append(out);
    }

    emit tracksChanged();
}

void MpvObject::setSubtitleTrack(int id)
{
    if (!m_mpv)
        return;
    const QByteArray value =
        id < 0 ? QByteArrayLiteral("no") : QByteArray::number(id);
    mpv_set_property_string(m_mpv, "sid", value.constData());
}

void MpvObject::setAudioTrack(int id)
{
    if (!m_mpv)
        return;
    const QByteArray value =
        id < 0 ? QByteArrayLiteral("no") : QByteArray::number(id);
    mpv_set_property_string(m_mpv, "aid", value.constData());
}

bool MpvObject::selectSubtitleStream(int ffIndex)
{
    const int id = MpvTrackList::subtitleIdForStream(m_tracks, ffIndex);
    if (id < 0)
        return false;

    // Selecting what is already selected would be a no-op for mpv, but skipping
    // it keeps this safe to call from a tracksChanged handler -- which is exactly
    // where the retry path calls it from.
    if (!MpvTrackList::isSelected(m_tracks, id))
        setSubtitleTrack(id);
    return true;
}

void MpvObject::selectSubtitleFile(const QString &path)
{
    const int existing = MpvTrackList::subtitleIdForFile(m_tracks, path);
    if (existing >= 0) {
        if (!MpvTrackList::isSelected(m_tracks, existing))
            setSubtitleTrack(existing);
        return;
    }

    // `cached` selects the file if it has already been added and adds it
    // otherwise, so clicking a sidecar tab repeatedly does not stack duplicate
    // tracks. mpv emits a track-list change once it lands.
    command({QStringLiteral("sub-add"), path, QStringLiteral("cached")});
}

bool MpvObject::subtitleTrackMatches(int id, int ffIndex,
                                     const QString &sidecarPath) const
{
    if (sidecarPath.isEmpty())
        return MpvTrackList::subtitleIdForStream(m_tracks, ffIndex) == id;
    return MpvTrackList::subtitleIdForFile(m_tracks, sidecarPath) == id;
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

void MpvObject::reportRenderer(const QString &renderer, const QString &version)
{
    emit logMessage(QStringLiteral("GL_RENDERER: %1 | %2").arg(renderer, version));
}

void MpvObject::reportFboCap(const QSize &pane, const QSize &fbo)
{
    emit logMessage(QStringLiteral("software rasterizer: rendering %1x%2 into a "
                                   "%3x%4 framebuffer, scaled to fit")
                        .arg(pane.width())
                        .arg(pane.height())
                        .arg(fbo.width())
                        .arg(fbo.height()));
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

void MpvObject::enableHardwareDecoding()
{
    if (!m_mpv || m_hwdecForced)
        return;

    // auto-safe rather than auto: it only picks methods known to work with the
    // current VO, and falls back to software rather than producing a black
    // picture when the driver claims a decoder it cannot deliver. Whether
    // anything is available at all is mpv's problem -- under WSL there is no
    // /dev/dri render node, so this is expected to come to nothing there and to
    // pick up vaapi or nvdec on a native Linux desktop.
    mpv_set_property_string(m_mpv, "hwdec", "auto-safe");
    emit logMessage(QStringLiteral(
        "hardware GL: asking mpv for hardware decoding (hwdec=auto-safe)"));
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

void MpvObject::seekRelative(double seconds)
{
    command({QStringLiteral("seek"), QString::number(seconds),
             QStringLiteral("relative")});
}

void MpvObject::setVolume(double volume)
{
    if (!m_mpv)
        return;
    // mpv would accept more, but nothing here offers amplification and a
    // keyboard repeat should stop at the ends rather than wrap or error.
    double clamped = qBound(0.0, volume, 100.0);
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &clamped);
}

void MpvObject::toggleMute()
{
    command({QStringLiteral("cycle"), QStringLiteral("mute")});
}

void MpvObject::setSpeed(double speed)
{
    if (!m_mpv)
        return;
    // Below ~0.25 audio filters start dropping out and above 4 it is unusable;
    // both ends are mpv's practical limits rather than hard ones.
    double clamped = qBound(0.25, speed, 4.0);
    mpv_set_property(m_mpv, "speed", MPV_FORMAT_DOUBLE, &clamped);
}

QString MpvObject::localFile(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}
