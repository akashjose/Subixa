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
// The track selections are kept in their own groups, not beside the position:
// finishing a film clears the resume entry, and that must not also forget which
// track was being read.
//
// remember() is called every few seconds for the whole length of a film, and
// QSettings has no partial write -- every sync serialises the entire file. So
// remember() holds the position in members and hands it to QSettings only once
// it has moved a PositionWriteStep. That gate, not the flush timer, is what cuts
// the writes: QSettings posts itself an update after every setValue and syncs on
// the next turn of the event loop, so anything reaching setValue reaches disk.
//
// flush() is the one thing here that *asks* for the file. It is also the app's
// mid-session flush for everything else in it: Qt gives every QSettings on a
// path one shared representation, so the QML `Settings` groups in Main.qml go
// out with it.
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
    // MpvEngine selects them that way: mpv's own numbering follows from neither.
    //
    // `forced` and `hearingImpaired` are what the *next* file is matched on.
    Q_INVOKABLE void rememberSubtitle(const QString &path, int streamIndex,
                                      const QString &sidecarPath,
                                      const QString &language,
                                      bool forced = false,
                                      bool hearingImpaired = false);
    // { streamIndex, sidecarPath, language }, or an empty map when this file has
    // never had a track chosen in it.
    Q_INVOKABLE QVariantMap subtitleFor(const QString &path) const;

    // The same, for the audio track: no sidecars, so `streamIndex` is mpv's
    // ff-index. `visualImpaired` is the audio-description flag.
    Q_INVOKABLE void rememberAudio(const QString &path, int streamIndex,
                                   const QString &language,
                                   bool visualImpaired = false);
    // { streamIndex, language }, or empty when this file has never had an audio
    // track chosen in it.
    Q_INVOKABLE QVariantMap audioFor(const QString &path) const;

    // ---- cross-file preference -----------------------------------------
    // `type` is "subtitle" or "audio" throughout.

    // "file", "learn" or "explicit". Anything unrecognised reads as "learn",
    // which is what the player did before this setting existed.
    Q_INVOKABLE QString preferenceMode(const QString &type) const;
    Q_INVOKABLE void setPreferenceMode(const QString &type, const QString &mode);

    // The explicit list, in order, each entry
    // { language, forced, hearingImpaired, visualImpaired }. Kept whatever the
    // mode is: switching to "follow the file" for one film should not throw
    // away a list somebody built.
    Q_INVOKABLE QVariantList preferenceList(const QString &type) const;
    Q_INVOKABLE void setPreferenceList(const QString &type, const QVariantList &entries);

    // What the selection walks, in order: empty in "file" mode, the one learned
    // entry in "learn" mode, the explicit list in "explicit" mode. Empty means
    // the file's own default is left alone -- not the same as a list that
    // matches nothing, and the caller has to tell them apart.
    Q_INVOKABLE QVariantList preferredTracks(const QString &type) const;

    // The language last chosen anywhere, whatever the mode.
    Q_INVOKABLE QString preferredLanguage(const QString &type = QStringLiteral("subtitle")) const;

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
    // "subtitle" unless the caller said "audio": one misspelling from QML must
    // not silently write a third group nothing ever reads.
    static QString groupFor(const QString &type);
    static QString entryKey(const QString &type, const QString &path);
    void rememberPreferredFlavour(const QString &type, const QString &language,
                                  bool forced, bool hearingImpaired,
                                  bool visualImpaired);

    void startFlushTimer();
    // Hands the held position to QSettings, unless it says the same thing as the
    // last one that went in. Does not sync -- but under a running event loop
    // QSettings will, shortly, on its own, so calling this *is* committing to a
    // write of the whole file. That is why remember() rations it.
    void storePending();
    // Drops the held position, for the paths that are clearing the entry on
    // purpose: without it the next flush would write the old value straight back.
    void resetPending();

    // Borrowed from SettingsService when main() built one, owned otherwise --
    // the QML engine creates this class declaratively, so there is no caller
    // to hand the store in through the constructor. m_owned is only the
    // fallback's storage; every use goes through m_settings.
    QSettings *m_settings = nullptr;
    std::unique_ptr<QSettings> m_owned;
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
