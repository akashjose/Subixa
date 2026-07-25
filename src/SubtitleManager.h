#pragma once

#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtCore/QVariantList>
#include <QtQml/qqmlregistration.h>

#include "SubtitleTypes.h"

class SubtitleExtractor;
class SubtitleLineModel;

// QML-facing owner of the parsed subtitle tracks. Runs a SubtitleExtractor on a
// dedicated thread and republishes its results as properties QML can bind to.
//
// Rows reach QML through one SubtitleLineModel per track (model()), which shares
// the line buffers rather than copying them. `tracks` stays a QVariantList: it is
// per-track metadata only, a handful of entries, rebuilt once per load.
class SubtitleManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // 0-100 while parsing, so the panel can say how far along it is rather than
    // just "parsing subtitles…" for nine seconds.
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)

public:
    explicit SubtitleManager(QObject *parent = nullptr);
    ~SubtitleManager() override;

    QVariantList tracks() const { return m_tracksView; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int progress() const { return m_progress; }

    // Parse `mediaPath` plus any sidecars next to it. Supersedes any parse
    // already running.
    Q_INVOKABLE void load(const QString &mediaPath);
    Q_INVOKABLE void clear();

    // Row model for one track, or nullptr for an out-of-range id. Owned here and
    // reused across loads, so a binding that still points at it while a new file
    // parses sees an empty model rather than a dangling pointer.
    Q_INVOKABLE SubtitleLineModel *model(int trackId) const;

    Q_INVOKABLE static QString formatTimestamp(qint64 ms);

    // For C++ consumers (the list models to come).
    const SubtitleTrackList &trackData() const { return m_tracks; }

signals:
    void tracksChanged();
    void busyChanged();
    void statusChanged();
    void progressChanged();
    void loaded();

    // Queued across to the worker thread.
    void extractRequested(const QString &mediaPath, int requestId);

private slots:
    void onExtractFinished(int requestId, const SubtitleTrackList &tracks);
    void onExtractFailed(int requestId, const QString &reason);
    void onExtractProgress(int requestId, int percent);

private:
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void rebuildTracksView();
    void rebuildModels();

    QThread m_thread;
    SubtitleExtractor *m_worker = nullptr;

    SubtitleTrackList m_tracks;
    QVariantList m_tracksView;  // per-track metadata, minus the rows
    // Grows to the largest track count seen and is never shrunk. Models are
    // emptied instead of deleted so QML bindings cannot outlive one.
    QVector<SubtitleLineModel *> m_models;
    int m_requestId = 0;
    bool m_busy = false;
    int m_progress = 0;
    QString m_status;
};
