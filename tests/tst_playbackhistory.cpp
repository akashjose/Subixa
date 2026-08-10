// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

// The resume-position store, and mostly its policy.
//
// Storage is the easy half. The half worth testing is when a position is *not*
// worth keeping: resuming somebody thirty seconds into a film they finished, or
// two seconds into one they just opened, is worse behaviour than not resuming.
//
// The other half worth testing is what reaches the *file*, and when. remember()
// is called every few seconds for hours and QSettings has no partial write, so
// the tests below watch the file itself rather than the store: Qt hands every
// QSettings on a path the same in-memory copy, and reading through a second
// PlaybackHistory would answer from that copy whether or not anything was ever
// written.
//
// Two mechanisms, and they need different instruments. flush() writing the file
// is visible with no event loop at all. The *debounce* is not: QSettings posts
// itself an update after every setValue and syncs when the event loop next
// turns, so in the running app a tick that reaches setValue reaches the disk
// whether or not this class ever calls sync(). Holding the position in members
// is what stops that, and only a test that turns the event loop can tell the
// two apart -- hence the processEvents() below.

#include <QtTest>

#include <QtCore/QCoreApplication>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>

#include "PlaybackHistory.h"

class TstPlaybackHistory : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void ignoresShortFiles();
    void ignoresTheOpeningMinute();
    void ignoresAFinishedFile();
    void remembersTheMiddle();

    void roundTripsAPosition();
    void finishingClearsAnEarlierPosition();
    void unknownFileHasNothingToResume();
    void forgetRemovesTheEntry();
    void pathsWithSlashesDoNotCollide();
    void relativeAndAbsolutePathsAgree();

    void roundTripsASubtitleSelection();
    void subtitleSelectionSurvivesFinishingTheFilm();
    void preferredLanguageFollowsTheLastChoice();
    void theModeDecidesWhatThePreferenceIs();
    void anExplicitListRoundTripsInOrder();
    void languagesAreStoredInOneSpelling();
    void audioIsRememberedTheSameWayAsSubtitles();

    void aFlushPutsThePositionInTheFile();
    void aStoredPositionCarriesItsLastUsedStamp();
    void aTickThatHasNotMovedFarNeverReachesTheFile();
    void anUnchangedPositionNeverWritesTheFile();
    void aFileNotWorthRememberingNeverWritesTheFile();
    void aFlushWritesTheOtherGroupsSharingTheFile();
    void closingDownWritesThePositionBeingHeld();
    void openingAnotherFileWritesTheOutgoingPosition();
    void forgetDropsAPositionThatWasOnlyHeld();

private:
    QString settingsFile() const { return m_dir.filePath(QStringLiteral("history.ini")); }
    // The bytes on disk, or an empty string when there is no file at all.
    QString fileText() const
    {
        QFile file(settingsFile());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        return QString::fromUtf8(file.readAll());
    }
    QTemporaryDir m_dir;
};

void TstPlaybackHistory::init()
{
    QVERIFY(m_dir.isValid());
    // Each test starts from an empty store rather than inheriting the last one's.
    QFile::remove(settingsFile());
}

void TstPlaybackHistory::ignoresShortFiles()
{
    // A 90-second clip is not something you come back to, wherever you stopped.
    QVERIFY(!PlaybackHistory::worthRemembering(60.0, 90.0));
    QVERIFY(!PlaybackHistory::worthRemembering(45.0, 119.0));
    // Nonsense input must not be stored either.
    QVERIFY(!PlaybackHistory::worthRemembering(30.0, 0.0));
    QVERIFY(!PlaybackHistory::worthRemembering(-5.0, 3600.0));
    QVERIFY(!PlaybackHistory::worthRemembering(0.0, 3600.0));
}

void TstPlaybackHistory::ignoresTheOpeningMinute()
{
    // Barely started: restarting costs the viewer nothing.
    QVERIFY(!PlaybackHistory::worthRemembering(5.0, 3600.0));
    QVERIFY(!PlaybackHistory::worthRemembering(29.9, 3600.0));
    QVERIFY(PlaybackHistory::worthRemembering(30.1, 3600.0));
}

void TstPlaybackHistory::ignoresAFinishedFile()
{
    // Inside the end margin is "finished" -- resuming there would drop the viewer
    // into the credits every time they reopened it.
    QVERIFY(!PlaybackHistory::worthRemembering(3600.0, 3600.0));
    QVERIFY(!PlaybackHistory::worthRemembering(3550.0, 3600.0));
    QVERIFY(PlaybackHistory::worthRemembering(3539.0, 3600.0));
}

void TstPlaybackHistory::remembersTheMiddle()
{
    QVERIFY(PlaybackHistory::worthRemembering(1800.0, 8634.0));  // the test film
    QVERIFY(PlaybackHistory::worthRemembering(120.0, 240.0));
}

void TstPlaybackHistory::roundTripsAPosition()
{
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/example.mkv");

    QCOMPARE(history.resumeFor(film), -1.0);
    history.remember(film, 1800.0, 8634.0);
    QCOMPARE(history.resumeFor(film), 1800.0);

    // A later position replaces the earlier one.
    history.remember(film, 2400.0, 8634.0);
    QCOMPARE(history.resumeFor(film), 2400.0);

    // And it survives a new instance -- the point of the whole class.
    PlaybackHistory reopened(settingsFile());
    QCOMPARE(reopened.resumeFor(film), 2400.0);
}

void TstPlaybackHistory::finishingClearsAnEarlierPosition()
{
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/example.mkv");

    history.remember(film, 1800.0, 8634.0);
    QCOMPARE(history.resumeFor(film), 1800.0);

    // Watching to the end has to remove the entry, not leave the old midpoint
    // behind for the next open to jump to.
    history.remember(film, 8630.0, 8634.0);
    QCOMPARE(history.resumeFor(film), -1.0);
}

void TstPlaybackHistory::unknownFileHasNothingToResume()
{
    PlaybackHistory history(settingsFile());
    QCOMPARE(history.resumeFor(QStringLiteral("/media/never/opened.mkv")), -1.0);
    QCOMPARE(history.resumeFor(QString()), -1.0);
}

void TstPlaybackHistory::forgetRemovesTheEntry()
{
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/example.mkv");

    history.remember(film, 1800.0, 8634.0);
    history.forget(film);
    QCOMPARE(history.resumeFor(film), -1.0);
}

void TstPlaybackHistory::pathsWithSlashesDoNotCollide()
{
    PlaybackHistory history(settingsFile());

    // QSettings reads '/' as a group separator, so a raw path as a key would turn
    // into nested groups and two films in different directories could tread on
    // each other. Hashing the path is what avoids that.
    const QString a = QStringLiteral("/media/films/a/movie.mkv");
    const QString b = QStringLiteral("/media/films/b/movie.mkv");

    history.remember(a, 1200.0, 8000.0);
    history.remember(b, 3400.0, 8000.0);

    QCOMPARE(history.resumeFor(a), 1200.0);
    QCOMPARE(history.resumeFor(b), 3400.0);
}

void TstPlaybackHistory::relativeAndAbsolutePathsAgree()
{
    PlaybackHistory history(settingsFile());

    // argv[1] is often relative while a drop or the dialog gives an absolute
    // path. The same file opened both ways has to resume, not start over.
    const QString relative = QStringLiteral("testdata/subs.mkv");
    const QString absolute = QFileInfo(relative).absoluteFilePath();

    history.remember(relative, 300.0, 3600.0);
    QCOMPARE(history.resumeFor(absolute), 300.0);

    // And the messy spelling of the same path resolves too.
    history.remember(QStringLiteral("./testdata/../testdata/subs.mkv"), 450.0, 3600.0);
    QCOMPARE(history.resumeFor(relative), 450.0);
}

void TstPlaybackHistory::roundTripsASubtitleSelection()
{
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/example.mkv");

    QVERIFY(history.subtitleFor(film).isEmpty());

    history.rememberSubtitle(film, 7, QString(), QStringLiteral("eng"));
    QVariantMap stored = history.subtitleFor(film);
    QCOMPARE(stored.value(QStringLiteral("streamIndex")).toInt(), 7);
    QCOMPARE(stored.value(QStringLiteral("sidecarPath")).toString(), QString());
    QCOMPARE(stored.value(QStringLiteral("language")).toString(), QStringLiteral("eng"));

    // A sidecar is stored by path instead, and absolutely -- the extractor and
    // mpv can spell the same file differently.
    history.rememberSubtitle(film, -1, QStringLiteral("testdata/subs.srt"),
                             QStringLiteral("fre"));
    stored = history.subtitleFor(film);
    QCOMPARE(stored.value(QStringLiteral("sidecarPath")).toString(),
             QFileInfo(QStringLiteral("testdata/subs.srt")).absoluteFilePath());

    PlaybackHistory reopened(settingsFile());
    QCOMPARE(reopened.subtitleFor(film).value(QStringLiteral("language")).toString(),
             QStringLiteral("fre"));

    // Two films must not share a selection, for the same hashing reason as
    // positions.
    QVERIFY(history.subtitleFor(QStringLiteral("/media/films/other.mkv")).isEmpty());
}

void TstPlaybackHistory::subtitleSelectionSurvivesFinishingTheFilm()
{
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/example.mkv");

    history.rememberSubtitle(film, 7, QString(), QStringLiteral("eng"));
    history.remember(film, 1800.0, 8634.0);

    // Watching to the end clears the resume position by design. The track being
    // read is not part of that: the next viewing should still open on it.
    history.remember(film, 8630.0, 8634.0);
    QCOMPARE(history.resumeFor(film), -1.0);
    QCOMPARE(history.subtitleFor(film).value(QStringLiteral("streamIndex")).toInt(), 7);

    // forget() is the explicit "drop everything about this file", and does.
    history.forget(film);
    QVERIFY(history.subtitleFor(film).isEmpty());
}

void TstPlaybackHistory::preferredLanguageFollowsTheLastChoice()
{
    PlaybackHistory history(settingsFile());

    QCOMPARE(history.preferredLanguage(), QString());

    history.rememberSubtitle(QStringLiteral("/media/films/a.mkv"), 3, QString(),
                             QStringLiteral("eng"));
    QCOMPARE(history.preferredLanguage(), QStringLiteral("eng"));

    // Chosen in one film, applied to the next -- that is the whole point of it
    // being global rather than per file.
    history.rememberSubtitle(QStringLiteral("/media/films/b.mkv"), 9, QString(),
                             QStringLiteral("spa"));
    QCOMPARE(history.preferredLanguage(), QStringLiteral("spa"));

    // An untagged track says nothing about what language to prefer next time,
    // and "und" would match half a container, so neither may overwrite it.
    history.rememberSubtitle(QStringLiteral("/media/films/c.mkv"), 2, QString(),
                             QString());
    QCOMPARE(history.preferredLanguage(), QStringLiteral("spa"));
    history.rememberSubtitle(QStringLiteral("/media/films/d.mkv"), 2, QString(),
                             QStringLiteral("und"));
    QCOMPARE(history.preferredLanguage(), QStringLiteral("spa"));
}

void TstPlaybackHistory::theModeDecidesWhatThePreferenceIs()
{
    PlaybackHistory history(settingsFile());
    const QString subtitle = QStringLiteral("subtitle");

    // "learn" is the default, because it is what the player did before the
    // setting existed. An unrecognised mode -- a hand-edited file, a setting
    // from a later version -- has to read as that too rather than as silence.
    QCOMPARE(history.preferenceMode(subtitle), QStringLiteral("learn"));
    history.setPreferenceMode(subtitle, QStringLiteral("nonsense"));
    QCOMPARE(history.preferenceMode(subtitle), QStringLiteral("learn"));

    history.rememberSubtitle(QStringLiteral("/media/films/a.mkv"), 3, QString(),
                             QStringLiteral("eng"), false, true);
    QVariantList learned = history.preferredTracks(subtitle);
    QCOMPARE(learned.size(), 1);
    QCOMPARE(learned.first().toMap().value(QStringLiteral("language")).toString(),
             QStringLiteral("eng"));
    QVERIFY(learned.first().toMap().value(QStringLiteral("hearingImpaired")).toBool());

    // "file" is the absence of an opinion, and an empty list is how the caller
    // is told to leave the container's own default alone. The learned language
    // is still there underneath -- switching modes must not destroy it.
    history.setPreferenceMode(subtitle, QStringLiteral("file"));
    QVERIFY(history.preferredTracks(subtitle).isEmpty());
    QCOMPARE(history.preferredLanguage(subtitle), QStringLiteral("eng"));

    // And explicit answers from the list, not from what was last chosen.
    QVariantMap wanted;
    wanted[QStringLiteral("language")] = QStringLiteral("jpn");
    history.setPreferenceList(subtitle, QVariantList{wanted});
    history.setPreferenceMode(subtitle, QStringLiteral("explicit"));
    const QVariantList explicitList = history.preferredTracks(subtitle);
    QCOMPARE(explicitList.size(), 1);
    QCOMPARE(explicitList.first().toMap().value(QStringLiteral("language")).toString(),
             QStringLiteral("jpn"));

    // The two stream types are independent: setting one must not answer for the
    // other, or a preference for Japanese audio would arrive as one for Japanese
    // subtitles.
    QCOMPARE(history.preferenceMode(QStringLiteral("audio")), QStringLiteral("learn"));
    QVERIFY(history.preferredTracks(QStringLiteral("audio")).isEmpty());
}

void TstPlaybackHistory::anExplicitListRoundTripsInOrder()
{
    PlaybackHistory history(settingsFile());
    const QString type = QStringLiteral("subtitle");

    QVariantMap engSdh;
    engSdh[QStringLiteral("language")] = QStringLiteral("eng");
    engSdh[QStringLiteral("hearingImpaired")] = true;
    QVariantMap eng;
    eng[QStringLiteral("language")] = QStringLiteral("eng");
    QVariantMap jpnForced;
    jpnForced[QStringLiteral("language")] = QStringLiteral("jpn");
    jpnForced[QStringLiteral("forced")] = true;

    history.setPreferenceList(type, QVariantList{engSdh, eng, jpnForced});

    // Order is the whole meaning of the list, so it is the thing most worth
    // pinning: "English SDH, then English, then forced Japanese" reordered is a
    // different preference.
    const QVariantList back = history.preferenceList(type);
    QCOMPARE(back.size(), 3);
    QCOMPARE(back.at(0).toMap().value(QStringLiteral("language")).toString(),
             QStringLiteral("eng"));
    QVERIFY(back.at(0).toMap().value(QStringLiteral("hearingImpaired")).toBool());
    QVERIFY(!back.at(1).toMap().value(QStringLiteral("hearingImpaired")).toBool());
    QCOMPARE(back.at(2).toMap().value(QStringLiteral("language")).toString(),
             QStringLiteral("jpn"));
    QVERIFY(back.at(2).toMap().value(QStringLiteral("forced")).toBool());

    // Stored as readable tokens rather than as a serialised QVariant blob: this
    // file is one somebody is meant to be able to open and fix.
    history.flush();
    const QString text = fileText();
    QVERIFY2(text.contains(QStringLiteral("eng+sdh")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("jpn+forced")), qPrintable(text));

    // An entry with no language is not an entry. It would match every track or
    // none depending on where it landed, and neither is what an empty row means.
    history.setPreferenceList(type, QVariantList{QVariantMap{}});
    QVERIFY(history.preferenceList(type).isEmpty());
}

void TstPlaybackHistory::languagesAreStoredInOneSpelling()
{
    PlaybackHistory history(settingsFile());

    // mpv publishes two-letter codes and a container carries three-letter ones,
    // and both reach this store: the subtitle browser writes "eng" for the very
    // track mpv's own menu calls "en". Stored as they arrive, one preference
    // would be recorded two ways and neither would reliably match.
    history.rememberAudio(QStringLiteral("/media/films/a.mkv"), 1,
                          QStringLiteral("ja"));
    QCOMPARE(history.preferredLanguage(QStringLiteral("audio")),
             QStringLiteral("jpn"));

    QVariantMap german;
    german[QStringLiteral("language")] = QStringLiteral("de");
    history.setPreferenceList(QStringLiteral("audio"), QVariantList{german});
    // 639-2/B, which is what a container carries: "ger", not the 639-2/T "deu".
    QCOMPARE(history.preferenceList(QStringLiteral("audio"))
                 .first()
                 .toMap()
                 .value(QStringLiteral("language"))
                 .toString(),
             QStringLiteral("ger"));
}

void TstPlaybackHistory::audioIsRememberedTheSameWayAsSubtitles()
{
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/dual-audio.mkv");

    QVERIFY(history.audioFor(film).isEmpty());

    history.rememberAudio(film, 2, QStringLiteral("eng"));
    const QVariantMap entry = history.audioFor(film);
    QCOMPARE(entry.value(QStringLiteral("streamIndex")).toInt(), 2);
    QCOMPARE(entry.value(QStringLiteral("language")).toString(), QStringLiteral("eng"));

    // Its own group, so remembering an audio track says nothing about the
    // subtitle track in the same film.
    QVERIFY(history.subtitleFor(film).isEmpty());

    // And forget() takes both, or a file cleared out of the history would keep
    // answering for one of the two.
    history.rememberSubtitle(film, 3, QString(), QStringLiteral("eng"));
    history.forget(film);
    QVERIFY(history.audioFor(film).isEmpty());
    QVERIFY(history.subtitleFor(film).isEmpty());
}

void TstPlaybackHistory::aFlushPutsThePositionInTheFile()
{
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/example.mkv");

    // Nothing in remember() writes the file, so this is the first position and it
    // is still not on disk.
    history.remember(film, 1800.0, 8634.0);
    QVERIFY2(!fileText().contains(QStringLiteral("position=1800")),
             "remember() wrote the file on its own");

    // flush() is what the timer, the destructor and closing down all call.
    history.flush();
    QVERIFY(fileText().contains(QStringLiteral("position=1800")));

    // One tick later. Five seconds of progress is not worth storing, let alone
    // serialising every entry in the file for -- so the file keeps the old value
    // until something flushes...
    history.remember(film, 1805.0, 8634.0);
    QVERIFY(fileText().contains(QStringLiteral("position=1800")));
    QVERIFY(!fileText().contains(QStringLiteral("position=1805")));
    // ...while the store still answers with the newer one, because reopening the
    // file that is playing must not resume at whatever the last write caught.
    QCOMPARE(history.resumeFor(film), 1805.0);

    history.flush();
    QVERIFY(fileText().contains(QStringLiteral("position=1805")));
    QVERIFY(!fileText().contains(QStringLiteral("position=1800")));

    // And a fresh store reads it back, which is the point of writing at all.
    PlaybackHistory reopened(settingsFile());
    QCOMPARE(reopened.resumeFor(film), 1805.0);
}

void TstPlaybackHistory::aStoredPositionCarriesItsLastUsedStamp()
{
    PlaybackHistory history(settingsFile());
    history.remember(QStringLiteral("/media/films/example.mkv"), 1800.0, 8634.0);
    history.flush();

    // The stamp is what SettingsService ages the entry by, and it rides the
    // same write as the position -- strictly inside the changed-position
    // branch, or the never-writes cases above stop holding.
    QVERIFY(fileText().contains(QStringLiteral("lastUsed=")));

    // Same for a subtitle choice.
    history.rememberSubtitle(QStringLiteral("/media/films/example.mkv"), 2,
                             QString(), QStringLiteral("spa"));
    QSettings raw(settingsFile(), QSettings::IniFormat);
    raw.beginGroup(QStringLiteral("subtitle"));
    const QStringList entries = raw.childGroups();
    QCOMPARE(entries.size(), 1);
    QVERIFY(raw.value(entries.first() + QStringLiteral("/lastUsed")).toLongLong() > 0);
}

void TstPlaybackHistory::aTickThatHasNotMovedFarNeverReachesTheFile()
{
    // The test above shows flush() writing the file, which it would do just as
    // well if remember() still handed every tick to QSettings. This one is about
    // the thing that actually cuts the rewrites in the running app: a tick that
    // reaches setValue reaches the disk at the next turn of the event loop,
    // because QSettings syncs itself, so the position has to be held short of
    // QSettings entirely rather than merely short of sync().
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/example.mkv");

    // The first position for a file is always worth storing -- a short session
    // has to leave something behind -- and here is Qt writing it unprompted.
    history.remember(film, 1800.0, 8634.0);
    QCoreApplication::processEvents();
    QVERIFY(fileText().contains(QStringLiteral("position=1800")));

    // Now the five-second tick, five times over, with the event loop turning
    // between each the way it does under a running player. This is the ~1440
    // full rewrites of a feature-length film, and none of them may happen.
    for (double at = 1805.0; at <= 1825.0; at += 5.0) {
        history.remember(film, at, 8634.0);
        QCoreApplication::processEvents();
        QVERIFY2(fileText().contains(QStringLiteral("position=1800")),
                 "a five-second tick rewrote the whole settings file");
    }

    // A tick a whole PositionWriteStep on is worth an entry, and goes out.
    history.remember(film, 1830.0, 8634.0);
    QCoreApplication::processEvents();
    QVERIFY(fileText().contains(QStringLiteral("position=1830")));
}

void TstPlaybackHistory::anUnchangedPositionNeverWritesTheFile()
{
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/example.mkv");

    history.remember(film, 1800.0, 8634.0);
    history.flush();
    QVERIFY(QFile::exists(settingsFile()));

    // Removing the file behind the store is what makes a rewrite visible: there
    // is no partial write in QSettings, so anything at all reaching it brings the
    // whole file back.
    QVERIFY(QFile::remove(settingsFile()));

    // A player sitting still, ticking. QSettings does not compare before it
    // stores, so without a check of our own each of these would have serialised
    // every entry in the file again.
    for (int i = 0; i < 20; ++i) {
        history.remember(film, 1800.0, 8634.0);
        history.flush();
    }
    QVERIFY2(!QFile::exists(settingsFile()),
             "an unchanged position rewrote the whole settings file");

    // A position that really moved still writes, so the check above is not
    // passing because writing stopped working.
    history.remember(film, 1830.0, 8634.0);
    history.flush();
    QVERIFY(fileText().contains(QStringLiteral("position=1830")));
}

void TstPlaybackHistory::aFileNotWorthRememberingNeverWritesTheFile()
{
    PlaybackHistory history(settingsFile());
    const QString clip = QStringLiteral("/media/clips/ninety-seconds.mkv");

    // Nothing about a 90-second clip is ever worth an entry, and the ticks arrive
    // all the same -- clearing an entry that was never there, over and over, for
    // as long as the clip is open. None of that may reach the file.
    for (int i = 0; i < 20; ++i) {
        history.remember(clip, 60.0, 90.0);
        history.flush();
    }
    QVERIFY2(!QFile::exists(settingsFile()),
             "a file with nothing worth remembering still wrote the settings file");
}

void TstPlaybackHistory::aFlushWritesTheOtherGroupsSharingTheFile()
{
    PlaybackHistory history(settingsFile());

    // Standing in for the QML `Settings` groups in Main.qml: separate QSettings
    // on the same file, with no flush of their own that this code owns. Qt gives
    // every QSettings on a path one shared representation, which is what makes
    // flush() theirs as much as ours. They used to go out on whatever sync() the
    // resume tick happened to do, so this is the test that stops the next change
    // to remember() quietly taking their mid-session flush with it.
    QSettings preferences(settingsFile(), QSettings::IniFormat);
    preferences.setValue(QStringLiteral("ui/panelWidth"), 360);
    QVERIFY(!fileText().contains(QStringLiteral("panelWidth")));

    history.flush();
    QVERIFY(fileText().contains(QStringLiteral("panelWidth=360")));
}

void TstPlaybackHistory::closingDownWritesThePositionBeingHeld()
{
    const QString film = QStringLiteral("/media/films/example.mkv");
    {
        PlaybackHistory history(settingsFile());
        history.remember(film, 1800.0, 8634.0);
        history.flush();

        // Held rather than stored -- five seconds is not worth an entry. Which
        // is fine right up until the session ends here.
        history.remember(film, 1805.0, 8634.0);
        QVERIFY(fileText().contains(QStringLiteral("position=1800")));
    }

    // Destruction is the application quitting. Without a flush there the
    // debounce would quietly cost the viewer the last stretch of every session,
    // and nothing else in the class would notice: ~QSettings syncs, but it can
    // only write what was handed to it.
    QVERIFY2(fileText().contains(QStringLiteral("position=1805")),
             "the position being held at exit never reached the file");
}

void TstPlaybackHistory::openingAnotherFileWritesTheOutgoingPosition()
{
    PlaybackHistory history(settingsFile());
    const QString first = QStringLiteral("/media/films/first.mkv");
    const QString second = QStringLiteral("/media/films/second.mkv");

    history.remember(first, 1800.0, 8634.0);
    history.remember(first, 1805.0, 8634.0);  // held, not stored

    // The next episode. There is one held position and it is about to describe a
    // different file, so this is the last moment the outgoing one can be saved.
    history.remember(second, 600.0, 3600.0);

    QCOMPARE(history.resumeFor(first), 1805.0);
    QVERIFY2(fileText().contains(QStringLiteral("position=1805")),
             "switching files threw away where the outgoing one got to");
}

void TstPlaybackHistory::forgetDropsAPositionThatWasOnlyHeld()
{
    PlaybackHistory history(settingsFile());
    const QString film = QStringLiteral("/media/films/example.mkv");

    history.remember(film, 1800.0, 8634.0);
    history.remember(film, 1805.0, 8634.0);  // held, not stored

    // Forgetting a file that is still open is the case the hold makes awkward:
    // the held position would otherwise be written back by the very next flush,
    // undoing the forget a few seconds after it happened.
    history.forget(film);
    QCOMPARE(history.resumeFor(film), -1.0);

    history.flush();
    QCOMPARE(history.resumeFor(film), -1.0);
    QVERIFY2(!fileText().contains(QStringLiteral("position=1805")),
             "a held position came back after forget()");
}

QTEST_MAIN(TstPlaybackHistory)
#include "tst_playbackhistory.moc"
