// The resume-position store, and mostly its policy.
//
// Storage is the easy half. The half worth testing is when a position is *not*
// worth keeping: resuming somebody thirty seconds into a film they finished, or
// two seconds into one they just opened, is worse behaviour than not resuming.

#include <QtTest>

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

private:
    QString settingsFile() const { return m_dir.filePath(QStringLiteral("history.ini")); }
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

QTEST_MAIN(TstPlaybackHistory)
#include "tst_playbackhistory.moc"
