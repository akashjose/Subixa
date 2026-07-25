#pragma once

#include <QtCore/QSize>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
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

    // mpv's own view of the file's tracks: one entry per audio/video/subtitle
    // stream, keyed by mpv's ids. Distinct from SubtitleManager::tracks, which is
    // the browsable text this app parsed itself -- the two overlap but neither
    // contains the other (mpv sees bitmap subtitle tracks the browser cannot
    // list; the browser sees sidecars mpv may not have loaded).
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    // Selected sid/aid, or -1 for off. -1 rather than mpv's "no" so QML can
    // compare with an integer.
    Q_PROPERTY(int subtitleTrack READ subtitleTrack NOTIFY subtitleTrackChanged)
    Q_PROPERTY(int audioTrack READ audioTrack NOTIFY audioTrackChanged)

    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)

public:
    explicit MpvObject(QQuickItem *parent = nullptr);
    ~MpvObject() override;

    Renderer *createRenderer() const override;

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    QVariantList tracks() const { return m_tracks; }
    int subtitleTrack() const { return m_subtitleTrack; }
    int audioTrack() const { return m_audioTrack; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    double speed() const { return m_speed; }
    // Decoded video size, empty until mpv reports it. Read by the renderer to
    // decide how large a framebuffer is worth creating.
    QSize videoSize() const { return m_videoSize; }

    Q_INVOKABLE void loadFile(const QString &file);
    Q_INVOKABLE void command(const QStringList &args);
    Q_INVOKABLE void setOption(const QString &name, const QString &value);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);
    // Relative seeks are their own command rather than position+delta: mpv
    // clamps at the file's bounds, and time-pos can lag a keypress repeat.
    Q_INVOKABLE void seekRelative(double seconds);

    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void setSpeed(double speed);

    // QUrl -> local path, for a drop or a file dialog. Kept here so QML never
    // has to take a URL apart by hand.
    Q_INVOKABLE QString localFile(const QUrl &url) const;

    // -1 turns the stream off. Named rather than exposed as a WRITE on the
    // property so QML cannot bind them into a loop with mpv's own notifications.
    Q_INVOKABLE void setSubtitleTrack(int id);
    Q_INVOKABLE void setAudioTrack(int id);

    // Selects the mpv subtitle track corresponding to a browser track. Returns
    // false when mpv has no matching track *yet* -- the file may still be
    // loading, since the extractor and mpv open it independently -- so the caller
    // can retry when the track list next changes.
    Q_INVOKABLE bool selectSubtitleStream(int ffIndex);
    // Sidecars: mpv may have auto-loaded the file already, in which case it only
    // needs selecting; otherwise it has to be added first.
    Q_INVOKABLE void selectSubtitleFile(const QString &path);

    // True when mpv's subtitle track `id` is the same stream as a browser track
    // described by (ffIndex, sidecarPath); an empty sidecarPath means embedded.
    // Lets QML answer "which tab is mpv showing" without comparing paths itself,
    // where a relative and an absolute spelling of one file would not match.
    Q_INVOKABLE bool subtitleTrackMatches(int id, int ffIndex,
                                          const QString &sidecarPath) const;

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void tracksChanged();
    void subtitleTrackChanged();
    void audioTrackChanged();
    void volumeChanged();
    void mutedChanged();
    void speedChanged();
    void fileLoaded();
    void logMessage(const QString &text);

private slots:
    void doUpdate();
    void onMpvEvents();
    void onRenderContextReady();
    // Invoked from the render thread when GL_RENDERER turns out to be a software
    // rasterizer, which cannot render 10-bit planes correctly.
    void forceEightBitVideo();
    // Also from the render thread, once, with whatever driver GL actually gave us.
    void reportRenderer(const QString &renderer, const QString &version);
    // From the render thread whenever a capped framebuffer is created, so the
    // cap is visible in the log rather than being silently softer picture.
    void reportFboCap(const QSize &pane, const QSize &fbo);

private:
    static void onMpvRedraw(void *ctx);
    static void onMpvWakeup(void *ctx);
    void handleMpvEvent(void *event);
    // Re-reads mpv's track-list into m_tracks. Cheap and rare: tracks change on
    // load, on sub-add, and when a selection changes.
    void refreshTracks();

    mpv_handle *m_mpv = nullptr;
    double m_position = 0.0;
    double m_duration = 0.0;
    bool m_paused = true;
    QVariantList m_tracks;
    int m_subtitleTrack = -1;
    int m_audioTrack = -1;
    double m_volume = 100.0;
    bool m_muted = false;
    double m_speed = 1.0;
    QSize m_videoSize;

    // The render context only exists once the item has been rendered at least
    // once. Loading before that makes mpv's VO fail with "No render context
    // set" and silently drop video, so early requests are queued.
    bool m_renderReady = false;
    QString m_pendingFile;

    friend class MpvRenderer;
};
