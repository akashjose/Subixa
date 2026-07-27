// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QAtomicInt>
#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtQml/qqmlregistration.h>

struct mpv_handle;

// Playback, as a plain QObject.
//
// This used to be one class with the video surface, which meant the mpv handle
// was owned by a scene-graph item. Three things followed from that and all of
// them were wrong:
//
//   * Teardown ran in the wrong order. Qt destroys a QQuickFramebufferObject's
//     Renderer on the render thread *after* the item's own destructor, so the
//     handle was destroyed while the render context that referenced it was
//     still alive -- the reverse of libmpv's documented requirement. Now the
//     engine is created in main() ahead of the QML engine, so it outlives every
//     item that draws from it and the ordering is correct by construction
//     rather than by care.
//   * Nothing about playback could be tested without a window, because the
//     class *was* a window's worth of machinery. Everything here runs under
//     vo=null in a test.
//   * Every new player feature -- A-B loop, sync offset, screenshots, filters --
//     had to be bolted onto a QQuickItem to reach mpv at all.
//
// The video surface is MpvVideoItem, which takes one of these as a property and
// owns only the render context and the framebuffer.
class MpvEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)

    // mpv's own view of the file's tracks: one entry per audio/video/subtitle
    // stream, keyed by mpv's ids. Distinct from SubtitleManager::tracks, which is
    // the browsable text this app parsed itself -- the two overlap but neither
    // contains the other (mpv sees bitmap subtitle tracks the browser cannot
    // list; the browser sees sidecars mpv may not have loaded).
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    // Whether there is a moving picture, as opposed to a file that merely has a
    // video *track*: cover art in a music file is one, and treating it as video
    // would mean an album pauses itself the moment the window is minimised.
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    // Selected sid/aid, or -1 for off. -1 rather than mpv's "no" so QML can
    // compare with an integer.
    Q_PROPERTY(int subtitleTrack READ subtitleTrack NOTIFY subtitleTrackChanged)
    Q_PROPERTY(int audioTrack READ audioTrack NOTIFY audioTrackChanged)

    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)

    // Seconds the subtitles are shifted by. The browser applies the same value
    // to its own timestamps, so the panel and the picture cannot disagree about
    // when a line is spoken -- which is the entire point of having both.
    Q_PROPERTY(double subtitleDelay READ subtitleDelay NOTIFY subtitleDelayChanged)
    Q_PROPERTY(double audioDelay READ audioDelay NOTIFY audioDelayChanged)
    Q_PROPERTY(bool subtitleVisible READ subtitleVisible NOTIFY subtitleVisibleChanged)

    // A-B loop bounds in seconds, or -1 when unset.
    Q_PROPERTY(double loopStart READ loopStart NOTIFY loopChanged)
    Q_PROPERTY(double loopEnd READ loopEnd NOTIFY loopChanged)
    Q_PROPERTY(bool looping READ looping NOTIFY loopChanged)

    // How far the demuxer has read ahead, for the seek bar's buffered range.
    Q_PROPERTY(double cacheEnd READ cacheEnd NOTIFY cacheEndChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)

    // True once GL_RENDERER has proved this is one of Mesa's software
    // rasterizers. The UI reads it to switch off shadows and any effect that
    // costs a render pass -- the same conditional discipline the 10-bit and
    // glFinish workarounds already use, applied one layer up.
    Q_PROPERTY(bool softwareRendering READ softwareRendering
                   NOTIFY softwareRenderingChanged)
    // What GL_RENDERER actually said, and a verdict drawn from it.
    //
    // The verdict exists because Mesa's D3D12 driver renders Qt Quick's text
    // materials in the wrong colour while every other primitive is exact -- see
    // PaintedText. It is reported rather than acted on here: the UI decides what
    // to do, and the user can override it.
    Q_PROPERTY(QString rendererName READ rendererName NOTIFY rendererNameChanged)
    Q_PROPERTY(bool glyphRenderingSuspect READ glyphRenderingSuspect
                   NOTIFY rendererNameChanged)

public:
    explicit MpvEngine(QObject *parent = nullptr);
    ~MpvEngine() override;

    // False when mpv could not be created or initialised. The player shows the
    // reason rather than aborting: mpv init genuinely fails in the field -- no
    // audio device, a sandboxed container, no usable vo -- and a core dump is
    // not something a user can act on.
    bool isValid() const { return m_mpv != nullptr; }
    QString initError() const { return m_initError; }

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    bool idle() const { return m_idle; }
    QVariantList tracks() const { return m_tracks; }
    bool hasVideo() const { return m_hasVideo; }
    int subtitleTrack() const { return m_subtitleTrack; }
    int audioTrack() const { return m_audioTrack; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    double speed() const { return m_speed; }
    double subtitleDelay() const { return m_subtitleDelay; }
    double audioDelay() const { return m_audioDelay; }
    bool subtitleVisible() const { return m_subtitleVisible; }
    double loopStart() const { return m_loopStart; }
    double loopEnd() const { return m_loopEnd; }
    bool looping() const { return m_loopStart >= 0.0 && m_loopEnd > m_loopStart; }
    double cacheEnd() const { return m_cacheEnd; }
    QVariantList chapters() const { return m_chapters; }
    bool softwareRendering() const { return m_softwareRendering; }
    QString rendererName() const { return m_rendererName; }
    bool glyphRenderingSuspect() const;
    void setRendererName(const QString &name);

    // Decoded video size, empty until mpv reports it. Read by the video item to
    // decide how large a framebuffer is worth creating.
    QSize videoSize() const { return m_videoSize; }

    // ---- transport ----------------------------------------------------
    Q_INVOKABLE void loadFile(const QString &file);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void setPaused(bool paused);
    // Absolute seeks are exact: a subtitle browser that lands on the nearest
    // keyframe shows a different line from the one that was clicked.
    Q_INVOKABLE void seek(double seconds);
    // Relative seeks are their own command rather than position+delta: mpv
    // clamps at the file's bounds, and time-pos can lag a keypress repeat.
    Q_INVOKABLE void seekRelative(double seconds);
    Q_INVOKABLE void seekToChapter(int index);

    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void setSpeed(double speed);

    // ---- tracks -------------------------------------------------------
    // -1 turns the stream off. Named rather than exposed as a WRITE on the
    // property so QML cannot bind them into a loop with mpv's own notifications.
    Q_INVOKABLE void setSubtitleTrack(int id);
    Q_INVOKABLE void setAudioTrack(int id);
    Q_INVOKABLE void setSubtitleVisible(bool visible);

    // Selects the mpv subtitle track corresponding to a browser track. Returns
    // false when mpv has no matching track *yet* -- the file may still be
    // loading, since the extractor and mpv open it independently -- so the caller
    // can retry when the track list next changes.
    Q_INVOKABLE bool selectSubtitleStream(int ffIndex);
    // Sidecars: mpv may have auto-loaded the file already, in which case it only
    // needs selecting; otherwise it has to be added first.
    Q_INVOKABLE void selectSubtitleFile(const QString &path);
    // Loads a subtitle file the user picked, and selects it.
    Q_INVOKABLE void addSubtitleFile(const QString &path);

    // True when mpv's subtitle track `id` is the same stream as a browser track
    // described by (ffIndex, sidecarPath); an empty sidecarPath means embedded.
    // Lets QML answer "which tab is mpv showing" without comparing paths itself,
    // where a relative and an absolute spelling of one file would not match.
    Q_INVOKABLE bool subtitleTrackMatches(int id, int ffIndex,
                                          const QString &sidecarPath) const;

    // ---- timing -------------------------------------------------------
    Q_INVOKABLE void setSubtitleDelay(double seconds);
    Q_INVOKABLE void adjustSubtitleDelay(double deltaSeconds);
    Q_INVOKABLE void setAudioDelay(double seconds);

    // ---- A-B loop -----------------------------------------------------
    Q_INVOKABLE void setLoopStart(double seconds);
    Q_INVOKABLE void setLoopEnd(double seconds);
    // Both at once, for "loop this line" -- the cue's own bounds with a little
    // room either side so the first syllable is not clipped.
    Q_INVOKABLE void setLoop(double startSeconds, double endSeconds);
    Q_INVOKABLE void clearLoop();

    // ---- appearance ---------------------------------------------------
    // Subtitle rendering, by mpv option name. Allow-listed rather than a general
    // property setter: the names are chosen by the settings UI, but a general
    // "set any mpv property from QML" invokable is a much larger surface than
    // this needs and mpv has properties that run commands.
    Q_INVOKABLE void setSubtitleOption(const QString &name, const QString &value);
    Q_INVOKABLE QString subtitleOption(const QString &name) const;
    // Picture adjustments: contrast, brightness, saturation, gamma, hue.
    // -100..100, mpv's own range.
    Q_INVOKABLE void setVideoAdjustment(const QString &name, int value);
    Q_INVOKABLE int videoAdjustment(const QString &name) const;

    Q_INVOKABLE void screenshot(bool withSubtitles);

    // QUrl -> local path, for a drop or a file dialog. Kept here so QML never
    // has to take a URL apart by hand.
    Q_INVOKABLE QString localFile(const QUrl &url) const;

    // ---- for the video item -------------------------------------------
    // Not for QML. The item needs the raw handle to build a render context, and
    // has to tell the engine what GL turned out to be underneath.
    mpv_handle *handle() const { return m_mpv; }
    void setSoftwareRendering(bool software);
    void notifyRenderContextReady();

    // Called from the *render* thread as contexts come and go, which is why the
    // counter is atomic. libmpv requires every mpv_render_context_free() to
    // complete before mpv_terminate_destroy(), and the destructor checks this
    // rather than assuming it: the ordering is currently guaranteed by creating
    // the engine in main() ahead of the QML engine, and that is exactly the kind
    // of invariant a later refactor breaks silently.
    void renderContextCreated() { m_liveRenderContexts.ref(); }
    void renderContextDestroyed() { m_liveRenderContexts.deref(); }

    void log(const QString &text);

    Q_INVOKABLE void command(const QStringList &args);
    void setOption(const QString &name, const QString &value);

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void idleChanged();
    void tracksChanged();
    void hasVideoChanged();
    void subtitleTrackChanged();
    void audioTrackChanged();
    void volumeChanged();
    void mutedChanged();
    void speedChanged();
    void subtitleDelayChanged();
    void audioDelayChanged();
    void subtitleVisibleChanged();
    void loopChanged();
    void cacheEndChanged();
    void chaptersChanged();
    void softwareRenderingChanged();
    void rendererNameChanged();
    void videoSizeChanged();
    void fileLoaded();
    // Playback reached the end of the file. Emitted on the edge only, and the
    // player uses it to move to the next file in the queue.
    void endOfFile();
    // A file mpv could not play: unsupported container, missing file, broken
    // stream. Worth surfacing in the window -- otherwise the picture simply
    // stays black and the only explanation is in a log nobody is reading.
    void playbackFailed(const QString &reason);
    // A command or property write mpv refused. Silent failures here were how a
    // malformed sidecar produced a tab that simply did nothing.
    void commandFailed(const QString &what, const QString &reason);
    void screenshotSaved(const QString &path);
    void logMessage(const QString &text);
    // The item may create its context before a file is asked for, or after.
    void renderContextReady();

private slots:
    void onMpvEvents();

private:
    static void onMpvWakeup(void *ctx);
    void handleMpvEvent(void *event);
    // Re-reads mpv's track-list into m_tracks. Cheap and rare: tracks change on
    // load, on sub-add, and when a selection changes.
    void refreshTracks();
    void refreshChapters();
    // Every write goes through these so a refusal is reported rather than
    // dropped. mpv returning an error is the only signal that, say, a sub-add of
    // a malformed file did nothing.
    bool checked(int rc, const QString &what);
    void setPropertyDouble(const QString &name, double value, const QString &what);
    void setPropertyFlag(const QString &name, bool value, const QString &what);
    void applyHardwareDecoding();
    void applySoftwareRasterizerWorkaround();

    mpv_handle *m_mpv = nullptr;
    QString m_initError;

    double m_position = 0.0;
    double m_duration = 0.0;
    bool m_paused = true;
    bool m_idle = true;
    QVariantList m_tracks;
    bool m_hasVideo = false;
    QVariantList m_chapters;
    int m_subtitleTrack = -1;
    int m_audioTrack = -1;
    double m_volume = 100.0;
    bool m_muted = false;
    double m_speed = 1.0;
    double m_subtitleDelay = 0.0;
    double m_audioDelay = 0.0;
    bool m_subtitleVisible = true;
    double m_loopStart = -1.0;
    double m_loopEnd = -1.0;
    double m_cacheEnd = 0.0;
    QSize m_videoSize;

    // The render context only exists once the item has been rendered at least
    // once. Loading before that makes mpv's VO fail with "No render context
    // set" and silently drop video, so early requests are queued.
    bool m_renderReady = false;
    QAtomicInt m_liveRenderContexts;
    QString m_pendingFile;
    // SUBIXA_HWDEC was set, so the automatic choice must keep its hands off.
    bool m_hwdecForced = false;
    bool m_endOfFile = false;
    bool m_softwareRendering = false;
    QString m_rendererName;
    bool m_rendererKnown = false;
};
