// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtGui/QColor>
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
    // The colour the browser draws rows on, handed down to every line model so a
    // cue's own colour can be kept legible against it. One binding in QML rather
    // than each model being told separately -- models are created here, and new
    // ones would otherwise miss the theme.
    Q_PROPERTY(QColor rowBackground READ rowBackground WRITE setRowBackground
                   NOTIFY rowBackgroundChanged)

public:
    explicit SubtitleManager(QObject *parent = nullptr);
    ~SubtitleManager() override;

    QVariantList tracks() const { return m_tracksView; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int progress() const { return m_progress; }
    QColor rowBackground() const { return m_rowBackground; }
    void setRowBackground(const QColor &background);

    // Parse `mediaPath` plus any sidecars next to it. Supersedes any parse
    // already running.
    Q_INVOKABLE void load(const QString &mediaPath);
    Q_INVOKABLE void clear();

    // Tests point the cue cache at a temporary directory. Call before load().
    void setCacheDirectory(const QString &directory);

    // Row model for one track, or nullptr for an out-of-range id. Owned here and
    // reused across loads, so a binding that still points at it while a new file
    // parses sees an empty model rather than a dangling pointer.
    Q_INVOKABLE SubtitleLineModel *model(int trackId) const;

    Q_INVOKABLE static QString formatTimestamp(qint64 ms);

    // Writes one track out as SubRip. Returns an empty string on success and the
    // reason otherwise, so QML can put it straight in front of the user.
    //
    // What is written is the display text -- override tags stripped, entities
    // decoded -- because the point of exporting is a file that reads the way the
    // browser does. The raw payload would be a worse copy of the original track.
    Q_INVOKABLE QString exportTrack(int trackId, const QUrl &target) const;

    // "Masters.of.the.Universe.2026.eng.srt": what the save dialog should open
    // on, built from the media file and the track's own label.
    Q_INVOKABLE QString suggestedExportName(int trackId, const QString &mediaPath) const;
    // The same name as a URL beside the media file, which is where the save
    // dialog should open. Built here so QML never has to do path arithmetic.
    Q_INVOKABLE QUrl suggestedExportUrl(int trackId, const QString &mediaPath) const;

    // For C++ consumers (the list models to come).
    const SubtitleTrackList &trackData() const { return m_tracks; }

signals:
    void tracksChanged();
    void busyChanged();
    void statusChanged();
    void progressChanged();
    void rowBackgroundChanged();
    void loaded();
    // The parse failed outright -- an unreadable or unrecognised container. The
    // status line says so too, but that is easy to miss next to a file that is
    // simply playing without subtitles.
    void failed(const QString &reason);

    // Queued across to the worker thread.
    void extractRequested(const QString &mediaPath, int requestId);

private slots:
    void onExtractFinished(int requestId, const SubtitleTrackList &tracks,
                           bool fromCache);
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
    // How long the open took, logged on completion. The gap between a parse and
    // a cache hit is the whole point of the cache, and it is invisible otherwise.
    QElapsedTimer m_elapsed;
    bool m_busy = false;
    int m_progress = 0;
    QColor m_rowBackground;
    QString m_status;
};
