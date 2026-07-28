// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QSettings;

// Per-file playback state: where playback got to, and which subtitle track was
// being read. Both are things a player should remember about a film rather than
// about the application.
//
// The resume policy lives here rather than in QML, and is deliberately
// conservative: resuming somebody two minutes into a film they finished last
// week is worse than not resuming at all. An entry that stops being worth
// keeping is deleted rather than left to go stale, so resumeFor() can stay a
// plain lookup.
//
// The subtitle selection is kept in its own group, not beside the position:
// finishing a film clears the resume entry, and it would be perverse for that to
// also forget that this household reads the Latin American Spanish track.
//
// remember() is called every few seconds for the whole length of a film, so what
// it costs matters: QSettings has no partial write, and every sync serialises the
// entire settings file. So remember() holds the position in members and hands it
// to QSettings only when it has moved a PositionWriteStep. That gate, not the
// flush timer, is what cuts the writes: QSettings posts itself an update after
// every setValue and syncs when the event loop next turns, so under a running
// player anything that reaches setValue reaches the disk. Staying short of
// QSettings is the only way to not write.
//
// flush() is the one thing here that *asks* for the file, on a slow timer, on
// destruction, and whenever a caller wants it current. It is also the app's
// mid-session flush for everything else in the file: Qt gives every QSettings on
// a path one shared representation, so the QML `Settings` groups in Main.qml
// (ui, subtitleStyle) go out with it. Those have a path to disk of their own and
// are not relying on this -- see startFlushTimer() -- but they were riding on
// the resume tick's sync() before, which is not something the next change to
// remember() should have to know about.
class PlaybackHistory : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit PlaybackHistory(QObject *parent = nullptr);
    // Tests pass their own ini file, so they never touch real user settings.
    explicit PlaybackHistory(const QString &settingsPath, QObject *parent = nullptr);
    ~PlaybackHistory() override;

    // Records `position` for `path`, or clears the entry when the position is not
    // worth returning to. Safe to call on every tick, and cheap: a tick that says
    // nothing new does nothing at all, and one that has not moved a
    // PositionWriteStep is held in memory rather than handed to QSettings, which
    // is what keeps it off the disk. Clearing an entry a film has just finished
    // does write, but only the once.
    Q_INVOKABLE void remember(const QString &path, double position, double duration);

    // Writes the held position and everything else pending to the settings file.
    // Called on a timer while the player runs, and on destruction; a caller that
    // needs the file current now -- closing down, or about to be killed -- can
    // call it directly.
    //
    // This is also what puts the QML `Settings` groups on disk mid-session: they
    // share the file, and Qt shares one representation of it between every
    // QSettings on the path.
    Q_INVOKABLE void flush();

    // Seconds to resume at, or -1 when there is nothing worth resuming.
    Q_INVOKABLE double resumeFor(const QString &path) const;

    Q_INVOKABLE void forget(const QString &path);

    // Which subtitle track was being read in `path`. Embedded tracks are stored
    // by ffmpeg stream index and sidecars by absolute path, for the same reason
    // MpvObject selects them that way: mpv's own numbering follows from neither.
    Q_INVOKABLE void rememberSubtitle(const QString &path, int streamIndex,
                                      const QString &sidecarPath,
                                      const QString &language);
    // { streamIndex, sidecarPath, language }, or an empty map when this file has
    // never had a track chosen in it.
    Q_INVOKABLE QVariantMap subtitleFor(const QString &path) const;

    // The language last chosen anywhere, used to pick a track in a file that has
    // no entry of its own -- somebody who reads English SDH reads it in the next
    // film too. Empty when nothing has been chosen yet.
    Q_INVOKABLE QString preferredLanguage() const;

    // Pure policy, static so it can be tested without touching storage.
    static bool worthRemembering(double position, double duration);

    // Anything shorter is a clip, not something you come back to.
    static constexpr double MinimumDuration = 120.0;
    // Below this you have barely started; restarting costs nothing.
    static constexpr double MinimumPosition = 30.0;
    // Within this of the end counts as finished -- next time should start over.
    static constexpr double EndMargin = 60.0;

    // How far playback must move before the position is stored again. The tick
    // that calls remember() is every five seconds and runs for hours, so this is
    // what turns roughly 1400 stored positions for a feature-length film into
    // about 240 -- at the cost of a resume that can be this much early after a
    // crash, which is a few seconds of film nobody notices.
    static constexpr double PositionWriteStep = 30.0;

    // How often flush() asks for the file while the player runs. Long, because
    // it is a backstop rather than the write path: what a kill -9 costs is
    // bounded by PositionWriteStep for the position, and by QSettings' own
    // event-loop sync for everything else in the file.
    static constexpr int FlushIntervalMs = 30000;

private:
    // Paths cannot be keys directly: QSettings reads '/' as a group separator, so
    // "/home/user/film.mkv" would silently become nested groups. The real path is
    // stored alongside the hash so the file stays readable by a human.
    static QString keyFor(const QString &path);

    void startFlushTimer();
    // Hands the held position to QSettings, unless it says the same thing as the
    // last one that went in. Does not sync -- but under a running event loop
    // QSettings will, shortly, on its own, so calling this *is* committing to a
    // write of the whole file. That is why remember() rations it.
    void storePending();
    // Drops the held position, for the paths that are clearing the entry on
    // purpose: without it the next flush would write the old value straight back.
    void resetPending();

    std::unique_ptr<QSettings> m_settings;
    QTimer m_flushTimer;

    // The resume key remember() was last called for, and what it was told. Most
    // ticks are held here rather than stored, so resumeFor() has to answer from
    // them.
    QString m_pendingKey;
    QString m_pendingPath;
    double m_pendingPosition = -1.0;
    double m_pendingDuration = -1.0;
    bool m_hasPending = false;
    // The position last given to QSettings for m_pendingKey, or -1 when this run
    // has given it none. What the next tick is measured against.
    double m_storedPosition = -1.0;
};
