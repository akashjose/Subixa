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

QTEST_MAIN(TstPlaybackHistory)
#include "tst_playbackhistory.moc"
