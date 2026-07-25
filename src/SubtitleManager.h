#pragma once

#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtCore/QVariantList>
#include <QtQml/qqmlregistration.h>

#include "SubtitleTypes.h"

class SubtitleExtractor;

// QML-facing owner of the parsed subtitle tracks. Runs a SubtitleExtractor on a
// dedicated thread and republishes its results as properties QML can bind to.
//
// Milestone 2 replaces the QVariantList accessors here with a proper
// QAbstractListModel per track; the parsed data behind them (trackData()) is
// already in the shape that model needs.
class SubtitleManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit SubtitleManager(QObject *parent = nullptr);
    ~SubtitleManager() override;

    QVariantList tracks() const { return m_tracksView; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    // Parse `mediaPath` plus any sidecars next to it. Supersedes any parse
    // already running.
    Q_INVOKABLE void load(const QString &mediaPath);
    Q_INVOKABLE void clear();

    // Rows of one track as {startMs, endMs, start, text}. Temporary: a real
    // model lands with the browser UI.
    Q_INVOKABLE QVariantList lines(int trackId) const;

    // Index of the cue covering `positionMs`, or the one most recently started
    // if none covers it; -1 before the first cue. Binary search -- this gets
    // called on every position tick once auto-follow exists.
    Q_INVOKABLE int lineIndexAt(int trackId, qint64 positionMs) const;

    Q_INVOKABLE static QString formatTimestamp(qint64 ms);

    // For C++ consumers (the list models to come).
    const SubtitleTrackList &trackData() const { return m_tracks; }

signals:
    void tracksChanged();
    void busyChanged();
    void statusChanged();
    void loaded();

    // Queued across to the worker thread.
    void extractRequested(const QString &mediaPath, int requestId);

private slots:
    void onExtractFinished(int requestId, const SubtitleTrackList &tracks);
    void onExtractFailed(int requestId, const QString &reason);

private:
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void rebuildTracksView();

    QThread m_thread;
    SubtitleExtractor *m_worker = nullptr;

    SubtitleTrackList m_tracks;
    QVariantList m_tracksView;  // per-track metadata, minus the rows
    int m_requestId = 0;
    bool m_busy = false;
    QString m_status;
};
