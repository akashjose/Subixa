// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
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
class PlaybackHistory : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit PlaybackHistory(QObject *parent = nullptr);
    // Tests pass their own ini file, so they never touch real user settings.
    explicit PlaybackHistory(const QString &settingsPath, QObject *parent = nullptr);
    ~PlaybackHistory() override;

    // Stores `position` for `path`, or clears the entry when the position is not
    // worth returning to. Safe to call on every tick.
    Q_INVOKABLE void remember(const QString &path, double position, double duration);

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

private:
    // Paths cannot be keys directly: QSettings reads '/' as a group separator, so
    // "/home/user/film.mkv" would silently become nested groups. The real path is
    // stored alongside the hash so the file stays readable by a human.
    static QString keyFor(const QString &path);

    std::unique_ptr<QSettings> m_settings;
};
