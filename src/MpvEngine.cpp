#include "MpvEngine.h"

#include "MpvTrackList.h"

#include <mpv/client.h>

#include <QtCore/QDebug>
#include <QtCore/QMetaObject>
#include <QtCore/QSet>
#include <QtCore/QUrl>
#include <QtCore/QVarLengthArray>

#include <algorithm>

namespace {

// mpv_node -> QVariant. track-list and chapter-list are arrays of maps of
// scalars, so this only has to cover those; anything unexpected lands as an
// invalid QVariant rather than being guessed at.
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

// Subtitle rendering options the settings window is allowed to write.
//
// An allow-list rather than a general "set any mpv property" invokable. The
// names all come from our own UI today, but a general setter is a much wider
// surface than this needs -- mpv has properties that load files and run
// commands, and one of them reachable from a string would be a poor thing to
// have shipped in a player that opens files people downloaded.
const QSet<QString> &allowedSubtitleOptions()
{
    static const QSet<QString> allowed = {
        QStringLiteral("sub-font"),          QStringLiteral("sub-font-size"),
        QStringLiteral("sub-bold"),          QStringLiteral("sub-italic"),
        QStringLiteral("sub-color"),         QStringLiteral("sub-border-color"),
        QStringLiteral("sub-border-size"),   QStringLiteral("sub-shadow-offset"),
        QStringLiteral("sub-shadow-color"),  QStringLiteral("sub-back-color"),
        QStringLiteral("sub-pos"),           QStringLiteral("sub-margin-y"),
        QStringLiteral("sub-margin-x"),      QStringLiteral("sub-align-x"),
        QStringLiteral("sub-align-y"),       QStringLiteral("sub-scale"),
        QStringLiteral("sub-scale-by-window"), QStringLiteral("sub-ass-override"),
        QStringLiteral("sub-spacing"),       QStringLiteral("sub-blur"),
        QStringLiteral("sub-use-margins"),
    };
    return allowed;
}

const QSet<QString> &allowedVideoAdjustments()
{
    static const QSet<QString> allowed = {
        QStringLiteral("contrast"), QStringLiteral("brightness"),
        QStringLiteral("saturation"), QStringLiteral("gamma"),
        QStringLiteral("hue"),
    };
    return allowed;
}

}  // namespace

MpvEngine::MpvEngine(QObject *parent) : QObject(parent)
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        m_initError = QStringLiteral("could not create an mpv context");
        return;
    }

    // REQUIRED for the render API. Without it mpv picks a native VO at init
    // (wlshm under WSLg), spawns its own window, and never renders into our
    // FBO -- video ends up floating outside the app entirely.
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "terminal", "no");

    // Decode starts on the CPU and is only moved off it once GL_RENDERER has
    // proved there is a real GPU underneath (see applyHardwareDecoding). Doing
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
    // Pitch-corrected speed. Without it a language learner at 0.75x hears a
    // detuned voice, which is worse than no speed control.
    mpv_set_option_string(m_mpv, "audio-pitch-correction", "yes");

    const int rc = mpv_initialize(m_mpv);
    if (rc < 0) {
        // Deliberately not qFatal. mpv init fails in the field for reasons a
        // user can sometimes fix -- no audio device, a container with no
        // /dev/snd -- and aborting with a core dump tells them nothing.
        m_initError = QString::fromUtf8(mpv_error_string(rc));
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "idle-active", MPV_FORMAT_FLAG);
    // The notification is all that is needed here; the list itself is re-read as
    // a node, which MPV_FORMAT_NODE observation would not simplify.
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NONE);
    mpv_observe_property(m_mpv, 0, "chapter-list", MPV_FORMAT_NONE);
    // As strings, because both are "no" when off and "auto" before a file loads;
    // MPV_FORMAT_INT64 simply fails on those and the change would be missed.
    mpv_observe_property(m_mpv, 0, "sid", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "aid", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "sub-delay", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "audio-delay", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "sub-visibility", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "ab-loop-a", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "ab-loop-b", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "demuxer-cache-time", MPV_FORMAT_DOUBLE);
    // Decoded size, used to avoid rendering a 720p file into a 2560 px surface on
    // the software rasterizer. dwidth/dheight are the display size, so anamorphic
    // content gives the size actually drawn rather than the stored one.
    mpv_observe_property(m_mpv, 0, "dwidth", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "dheight", MPV_FORMAT_INT64);
    // What mpv actually ended up using, which is the only honest answer about
    // whether decode left the CPU -- asking for hardware decoding and getting it
    // are different things, and mpv falls back silently.
    mpv_observe_property(m_mpv, 0, "hwdec-current", MPV_FORMAT_STRING);
    // How the end of a file is noticed here. With keep-open=yes mpv does *not*
    // unload the file and so never emits MPV_EVENT_END_FILE for a normal
    // finish -- it pauses on the last frame and sets this instead. Watching for
    // the event would mean waiting for something that by construction never
    // arrives.
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);

    mpv_request_log_messages(m_mpv, "info");
    mpv_set_wakeup_callback(m_mpv, &MpvEngine::onMpvWakeup, this);
}

MpvEngine::~MpvEngine()
{
    if (!m_mpv)
        return;

    // The callback fires from mpv's own threads. Clearing it first is what stops
    // a wakeup arriving mid-teardown and queueing a call into an object whose
    // destructor is already running.
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);

    // Every render context must be freed before the handle is destroyed --
    // libmpv is explicit about the order and the reverse is a use-after-free on
    // the render thread. The ordering holds because the engine is created in
    // main() ahead of the QML engine, so the window (and with it the scene
    // graph, which blocks on the render thread as it tears down) is gone by the
    // time this runs. That is an invariant of main()'s declaration order and
    // nothing else, so it is worth saying out loud when it stops being true.
    if (m_liveRenderContexts.loadAcquire() != 0) {
        qWarning("MpvEngine is being destroyed with %d render context(s) still "
                 "alive -- the mpv handle is about to be freed out from under "
                 "the render thread. Create the engine before the QML engine.",
                 m_liveRenderContexts.loadAcquire());
    }

    mpv_terminate_destroy(m_mpv);
    m_mpv = nullptr;
}

void MpvEngine::setRendererName(const QString &name)
{
    if (m_rendererName == name)
        return;
    m_rendererName = name;
    emit rendererNameChanged();
}

bool MpvEngine::glyphRenderingSuspect() const
{
    // Mesa's D3D12 Gallium driver, which exists only for WSL. Qt Quick's text
    // materials come out in the wrong colour there -- #aab2c2 as pure green,
    // #e8eaf0 as yellow, an 11px #7e93b5 timestamp as black and invisible --
    // while rectangles, images and Shapes geometry in the same frame are exact
    // to the byte. Reproduced in Qt's own qml binary with no application code,
    // on Mesa 25.2.8 and 26.1.5 alike, and reported upstream.
    //
    // Matched by name rather than probed. A readback probe would be the better
    // instrument in principle, but trap 10 already records this environment
    // returning a *perfect* frame from toImage() while the screen was wrong, so
    // a readback cannot be trusted to tell the truth about what is displayed.
    return m_rendererName.contains(QLatin1String("D3D12"), Qt::CaseInsensitive);
}

void MpvEngine::log(const QString &text)
{
    emit logMessage(text);
}

void MpvEngine::onMpvWakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvEngine *>(ctx), "onMpvEvents",
                              Qt::QueuedConnection);
}

void MpvEngine::onMpvEvents()
{
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;
        handleMpvEvent(event);
    }
}

bool MpvEngine::checked(int rc, const QString &what)
{
    if (rc >= 0)
        return true;
    const QString reason = QString::fromUtf8(mpv_error_string(rc));
    emit logMessage(QStringLiteral("mpv refused %1: %2").arg(what, reason));
    emit commandFailed(what, reason);
    return false;
}

void MpvEngine::setPropertyDouble(const QString &name, double value,
                                  const QString &what)
{
    if (!m_mpv)
        return;
    double v = value;
    checked(mpv_set_property(m_mpv, name.toUtf8().constData(), MPV_FORMAT_DOUBLE, &v),
            what);
}

void MpvEngine::setPropertyFlag(const QString &name, bool value, const QString &what)
{
    if (!m_mpv)
        return;
    int flag = value ? 1 : 0;
    checked(mpv_set_property(m_mpv, name.toUtf8().constData(), MPV_FORMAT_FLAG, &flag),
            what);
}

void MpvEngine::handleMpvEvent(void *ev)
{
    mpv_event *event = static_cast<mpv_event *>(ev);

    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *prop = static_cast<mpv_event_property *>(event->data);
        if (!prop)
            break;
        const QByteArray name(prop->name);

        // Observed with MPV_FORMAT_NONE, so prop->data is null by design -- these
        // have to come before the guard below or the notification is dropped.
        if (name == "track-list") {
            refreshTracks();
            break;
        }
        if (name == "chapter-list") {
            refreshChapters();
            break;
        }

        // ab-loop-a/b report MPV_FORMAT_NONE when unset rather than a number, so
        // a null payload is meaningful for them: it is how "cleared" arrives.
        if (name == "ab-loop-a" || name == "ab-loop-b") {
            const bool isA = name == "ab-loop-a";
            const double value =
                (prop->data && prop->format == MPV_FORMAT_DOUBLE)
                    ? *static_cast<double *>(prop->data)
                    : -1.0;
            if (isA)
                m_loopStart = value;
            else
                m_loopEnd = value;
            emit loopChanged();
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
        } else if (name == "idle-active" && prop->format == MPV_FORMAT_FLAG) {
            m_idle = *static_cast<int *>(prop->data) != 0;
            emit idleChanged();
        } else if (name == "volume" && prop->format == MPV_FORMAT_DOUBLE) {
            m_volume = *static_cast<double *>(prop->data);
            emit volumeChanged();
        } else if (name == "mute" && prop->format == MPV_FORMAT_FLAG) {
            m_muted = *static_cast<int *>(prop->data) != 0;
            emit mutedChanged();
        } else if (name == "speed" && prop->format == MPV_FORMAT_DOUBLE) {
            m_speed = *static_cast<double *>(prop->data);
            emit speedChanged();
        } else if (name == "sub-delay" && prop->format == MPV_FORMAT_DOUBLE) {
            m_subtitleDelay = *static_cast<double *>(prop->data);
            emit subtitleDelayChanged();
        } else if (name == "audio-delay" && prop->format == MPV_FORMAT_DOUBLE) {
            m_audioDelay = *static_cast<double *>(prop->data);
            emit audioDelayChanged();
        } else if (name == "sub-visibility" && prop->format == MPV_FORMAT_FLAG) {
            m_subtitleVisible = *static_cast<int *>(prop->data) != 0;
            emit subtitleVisibleChanged();
        } else if (name == "demuxer-cache-time" && prop->format == MPV_FORMAT_DOUBLE) {
            m_cacheEnd = *static_cast<double *>(prop->data);
            emit cacheEndChanged();
        } else if (name == "eof-reached" && prop->format == MPV_FORMAT_FLAG) {
            const bool reached = *static_cast<int *>(prop->data) != 0;
            // Only the transition into it: mpv sets this false again on the
            // seek that a playlist advance performs, and a second edge would
            // skip a file.
            if (reached && !m_endOfFile)
                emit endOfFile();
            m_endOfFile = reached;
        } else if (name == "dwidth" && prop->format == MPV_FORMAT_INT64) {
            m_videoSize.setWidth(int(*static_cast<int64_t *>(prop->data)));
            emit videoSizeChanged();
        } else if (name == "dheight" && prop->format == MPV_FORMAT_INT64) {
            m_videoSize.setHeight(int(*static_cast<int64_t *>(prop->data)));
            emit videoSizeChanged();
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

void MpvEngine::refreshTracks()
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

void MpvEngine::refreshChapters()
{
    m_chapters.clear();
    if (!m_mpv) {
        emit chaptersChanged();
        return;
    }

    mpv_node node;
    if (mpv_get_property(m_mpv, "chapter-list", MPV_FORMAT_NODE, &node) < 0) {
        emit chaptersChanged();
        return;
    }

    const QVariantList raw = nodeToVariant(node).toList();
    mpv_free_node_contents(&node);

    m_chapters.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QVariantMap in = raw.at(i).toMap();
        QVariantMap out;
        out[QStringLiteral("index")] = i;
        out[QStringLiteral("time")] = in.value(QStringLiteral("time"), 0.0).toDouble();
        const QString title = in.value(QStringLiteral("title")).toString();
        out[QStringLiteral("title")] =
            title.isEmpty() ? QStringLiteral("Chapter %1").arg(i + 1) : title;
        m_chapters.append(out);
    }

    emit chaptersChanged();
}

// ---- transport ---------------------------------------------------------

void MpvEngine::command(const QStringList &args)
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

    checked(mpv_command(m_mpv, argv.data()), args.first());
}

void MpvEngine::setOption(const QString &name, const QString &value)
{
    if (m_mpv) {
        checked(mpv_set_option_string(m_mpv, name.toUtf8().constData(),
                                      value.toUtf8().constData()),
                name);
    }
}

void MpvEngine::loadFile(const QString &file)
{
    if (!m_renderReady) {
        m_pendingFile = file;
        return;
    }
    command({QStringLiteral("loadfile"), file});
}

void MpvEngine::stop()
{
    command({QStringLiteral("stop")});
}

void MpvEngine::notifyRenderContextReady()
{
    m_renderReady = true;
    emit renderContextReady();
    if (!m_pendingFile.isEmpty()) {
        const QString file = m_pendingFile;
        m_pendingFile.clear();
        command({QStringLiteral("loadfile"), file});
    }
}

void MpvEngine::setSoftwareRendering(bool software)
{
    // The first answer is the real one: GL_RENDERER does not change under a
    // running context, and letting a later call flip it would mean the
    // workarounds and the UI could disagree about what they are running on.
    if (m_rendererKnown)
        return;
    m_rendererKnown = true;

    if (m_softwareRendering != software) {
        m_softwareRendering = software;
        emit softwareRenderingChanged();
    }

    if (software)
        applySoftwareRasterizerWorkaround();
    else
        applyHardwareDecoding();
}

void MpvEngine::applySoftwareRasterizerWorkaround()
{
    if (!m_mpv)
        return;
    // Costs nothing for 8-bit content -- the filter is a no-op when the input is
    // already yuv420p. For 10-bit it converts on the CPU, which is the price of
    // getting a correct picture out of a software rasterizer at all.
    //
    // Set through `vf add` with a label rather than by assigning `vf` wholesale,
    // so a filter the user adds later (deinterlace, crop) cannot silently
    // replace the workaround, nor be replaced by it.
    checked(mpv_set_property_string(m_mpv, "vf", "@cmp_depth:format=yuv420p"),
            QStringLiteral("vf"));
    emit logMessage(QStringLiteral(
        "software rasterizer detected: converting video to 8-bit before upload"));
}

void MpvEngine::applyHardwareDecoding()
{
    if (!m_mpv || m_hwdecForced)
        return;

    // auto-safe rather than auto: it only picks methods known to work with the
    // current VO, and falls back to software rather than producing a black
    // picture when the driver claims a decoder it cannot deliver. Whether
    // anything is available at all is mpv's problem -- under WSL there is no
    // /dev/dri render node, so this is expected to come to nothing there and to
    // pick up vaapi or nvdec on a native Linux desktop.
    checked(mpv_set_property_string(m_mpv, "hwdec", "auto-safe"),
            QStringLiteral("hwdec"));
    emit logMessage(QStringLiteral(
        "hardware GL: asking mpv for hardware decoding (hwdec=auto-safe)"));
}

void MpvEngine::togglePause()
{
    command({QStringLiteral("cycle"), QStringLiteral("pause")});
}

void MpvEngine::setPaused(bool paused)
{
    // Needed as well as togglePause() because pause is a *player* property, not
    // a per-file one: keep-open pauses at the end of a file and the next one
    // would start out paused, having been given no say in it.
    setPropertyFlag(QStringLiteral("pause"), paused, QStringLiteral("pause"));
}

namespace {

// Milliseconds, spelled out in full. QString::number(double) defaults to 'g'
// with six significant digits, which silently truncates a timestamp as it grows:
// a cue three hours in at 10799.123 s serialises as "10799.1", landing 123 ms
// off. Clicking a row is this player's headline interaction and it was missing
// the line it was asked for on anything feature-length.
QString seekArgument(double seconds)
{
    return QString::number(seconds, 'f', 3);
}

}  // namespace

void MpvEngine::seek(double seconds)
{
    // "exact" rather than mpv's default keyframe seek: a subtitle browser that
    // seeks to the nearest keyframe lands on whatever line happens to be there,
    // which is the same bug as the truncation above wearing a different hat.
    command({QStringLiteral("seek"), seekArgument(seconds),
             QStringLiteral("absolute+exact")});
}

void MpvEngine::seekRelative(double seconds)
{
    // Left on the default precision: these are the arrow keys, where
    // responsiveness under a held repeat matters more than an exact frame.
    command({QStringLiteral("seek"), seekArgument(seconds),
             QStringLiteral("relative")});
}

void MpvEngine::seekToChapter(int index)
{
    if (index < 0 || index >= m_chapters.size())
        return;
    seek(m_chapters.at(index).toMap().value(QStringLiteral("time")).toDouble());
}

void MpvEngine::setVolume(double volume)
{
    // mpv will amplify past 100, and people expect it to -- quiet films are a
    // real thing. Capped at 150 so a keyboard repeat cannot walk it somewhere
    // that clips badly.
    setPropertyDouble(QStringLiteral("volume"), qBound(0.0, volume, 150.0),
                      QStringLiteral("volume"));
}

void MpvEngine::toggleMute()
{
    command({QStringLiteral("cycle"), QStringLiteral("mute")});
}

void MpvEngine::setMuted(bool muted)
{
    setPropertyFlag(QStringLiteral("mute"), muted, QStringLiteral("mute"));
}

void MpvEngine::setSpeed(double speed)
{
    // Below ~0.25 audio filters start dropping out and above 4 it is unusable;
    // both ends are mpv's practical limits rather than hard ones.
    setPropertyDouble(QStringLiteral("speed"), qBound(0.25, speed, 4.0),
                      QStringLiteral("speed"));
}

// ---- tracks ------------------------------------------------------------

void MpvEngine::setSubtitleTrack(int id)
{
    if (!m_mpv)
        return;
    const QByteArray value =
        id < 0 ? QByteArrayLiteral("no") : QByteArray::number(id);
    checked(mpv_set_property_string(m_mpv, "sid", value.constData()),
            QStringLiteral("sid"));
}

void MpvEngine::setAudioTrack(int id)
{
    if (!m_mpv)
        return;
    const QByteArray value =
        id < 0 ? QByteArrayLiteral("no") : QByteArray::number(id);
    checked(mpv_set_property_string(m_mpv, "aid", value.constData()),
            QStringLiteral("aid"));
}

void MpvEngine::setSubtitleVisible(bool visible)
{
    setPropertyFlag(QStringLiteral("sub-visibility"), visible,
                    QStringLiteral("sub-visibility"));
}

bool MpvEngine::selectSubtitleStream(int ffIndex)
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

void MpvEngine::selectSubtitleFile(const QString &path)
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

void MpvEngine::addSubtitleFile(const QString &path)
{
    // `select` rather than `cached`: the user just picked this file, so showing
    // it is the whole point.
    command({QStringLiteral("sub-add"), path, QStringLiteral("select")});
}

bool MpvEngine::subtitleTrackMatches(int id, int ffIndex,
                                     const QString &sidecarPath) const
{
    if (sidecarPath.isEmpty())
        return MpvTrackList::subtitleIdForStream(m_tracks, ffIndex) == id;
    return MpvTrackList::subtitleIdForFile(m_tracks, sidecarPath) == id;
}

// ---- timing ------------------------------------------------------------

void MpvEngine::setSubtitleDelay(double seconds)
{
    // Wider than anyone should need, but a badly muxed broadcast capture can be
    // minutes out and refusing to express that is worse than allowing it.
    setPropertyDouble(QStringLiteral("sub-delay"), qBound(-600.0, seconds, 600.0),
                      QStringLiteral("sub-delay"));
}

void MpvEngine::adjustSubtitleDelay(double deltaSeconds)
{
    setSubtitleDelay(m_subtitleDelay + deltaSeconds);
}

void MpvEngine::setAudioDelay(double seconds)
{
    setPropertyDouble(QStringLiteral("audio-delay"), qBound(-60.0, seconds, 60.0),
                      QStringLiteral("audio-delay"));
}

// ---- A-B loop ----------------------------------------------------------

void MpvEngine::setLoopStart(double seconds)
{
    setPropertyDouble(QStringLiteral("ab-loop-a"), seconds,
                      QStringLiteral("ab-loop-a"));
}

void MpvEngine::setLoopEnd(double seconds)
{
    setPropertyDouble(QStringLiteral("ab-loop-b"), seconds,
                      QStringLiteral("ab-loop-b"));
}

void MpvEngine::setLoop(double startSeconds, double endSeconds)
{
    // A backwards pair is a slip of the hand, not a statement; swapping is what
    // the user meant and refusing would just look broken.
    double a = startSeconds;
    double b = endSeconds;
    if (b < a)
        std::swap(a, b);
    setLoopStart(qMax(0.0, a));
    setLoopEnd(b);
}

void MpvEngine::clearLoop()
{
    if (!m_mpv)
        return;
    // "no" rather than a number: these are the only two properties here whose
    // unset state is not expressible as a double.
    checked(mpv_set_property_string(m_mpv, "ab-loop-a", "no"),
            QStringLiteral("ab-loop-a"));
    checked(mpv_set_property_string(m_mpv, "ab-loop-b", "no"),
            QStringLiteral("ab-loop-b"));
    m_loopStart = -1.0;
    m_loopEnd = -1.0;
    emit loopChanged();
}

// ---- appearance --------------------------------------------------------

void MpvEngine::setSubtitleOption(const QString &name, const QString &value)
{
    if (!m_mpv)
        return;
    if (!allowedSubtitleOptions().contains(name)) {
        qWarning("refusing to set subtitle option '%s': not in the allow-list",
                 qUtf8Printable(name));
        return;
    }
    checked(mpv_set_property_string(m_mpv, name.toUtf8().constData(),
                                    value.toUtf8().constData()),
            name);
}

QString MpvEngine::subtitleOption(const QString &name) const
{
    if (!m_mpv || !allowedSubtitleOptions().contains(name))
        return {};
    char *value = mpv_get_property_string(m_mpv, name.toUtf8().constData());
    if (!value)
        return {};
    const QString out = QString::fromUtf8(value);
    mpv_free(value);
    return out;
}

void MpvEngine::setVideoAdjustment(const QString &name, int value)
{
    if (!m_mpv || !allowedVideoAdjustments().contains(name))
        return;
    int64_t v = qBound(-100, value, 100);
    checked(mpv_set_property(m_mpv, name.toUtf8().constData(), MPV_FORMAT_INT64, &v),
            name);
}

int MpvEngine::videoAdjustment(const QString &name) const
{
    if (!m_mpv || !allowedVideoAdjustments().contains(name))
        return 0;
    int64_t v = 0;
    if (mpv_get_property(m_mpv, name.toUtf8().constData(), MPV_FORMAT_INT64, &v) < 0)
        return 0;
    return int(v);
}

void MpvEngine::screenshot(bool withSubtitles)
{
    // "subtitles" burns what is on screen; "video" takes the decoded frame
    // without them, which is the one a subtitler wants for a clean reference.
    command({QStringLiteral("screenshot"),
             withSubtitles ? QStringLiteral("subtitles") : QStringLiteral("video")});
}

QString MpvEngine::localFile(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}
