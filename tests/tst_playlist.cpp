// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

// What plays next.
//
// All of this is file-system reasoning, so none of it needs mpv or a window. The
// part worth testing is the ordering: a folder of episodes is named ep1..ep10,
// and plain string order puts ep10 second. The rest is boundaries -- the last
// file must not wrap round to the first, and a queue built from a drop must keep
// the order it was dropped in rather than inventing one.

#include <QtTest>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>

#include "Playlist.h"

namespace {

void touch(const QString &path)
{
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write("x");
}

QStringList names(const QStringList &paths)
{
    QStringList out;
    for (const QString &path : paths)
        out << QFileInfo(path).fileName();
    return out;
}

}  // namespace

class TstPlaylist : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void recognisesMediaByExtension();
    void folderBecomesTheQueue();
    void episodesSortLikeAPersonWouldSortThem();
    void paddedAndUnpaddedNumbersAgree();
    void nonMediaFilesAreLeftOut();
    void theOpenedFileIsAlwaysQueuedEvenIfUnrecognised();
    void nextAndPreviousStopAtTheEnds();
    void aDropKeepsTheOrderItWasDroppedIn();
    void reopeningAQueuedFileKeepsTheQueue();
    void dialogFiltersAndScanAgree();

private:
    QString path(const QString &name) const { return m_dir.filePath(name); }
    QTemporaryDir m_dir;
};

void TstPlaylist::init()
{
    QVERIFY(m_dir.isValid());
    // Each test builds the folder it needs.
    const QDir dir(m_dir.path());
    for (const QString &name : dir.entryList(QDir::Files))
        QFile::remove(dir.filePath(name));
}

void TstPlaylist::recognisesMediaByExtension()
{
    QVERIFY(Playlist::isMediaFile(QStringLiteral("/films/a.mkv")));
    QVERIFY(Playlist::isMediaFile(QStringLiteral("/films/a.MP4")));  // case-blind
    QVERIFY(Playlist::isMediaFile(QStringLiteral("/music/a.flac")));
    QVERIFY(!Playlist::isMediaFile(QStringLiteral("/films/a.srt")));
    QVERIFY(!Playlist::isMediaFile(QStringLiteral("/films/a.nfo")));
    QVERIFY(!Playlist::isMediaFile(QStringLiteral("/films/a")));
}

void TstPlaylist::folderBecomesTheQueue()
{
    touch(path(QStringLiteral("a.mkv")));
    touch(path(QStringLiteral("b.mkv")));
    touch(path(QStringLiteral("c.mkv")));

    Playlist playlist;
    playlist.openFolderOf(path(QStringLiteral("b.mkv")));

    // Opening one file queues its neighbours, which is the whole point: nobody
    // should have to build a playlist to watch the next episode.
    QCOMPARE(playlist.count(), 3);
    QCOMPARE(playlist.currentIndex(), 1);
    QCOMPARE(QFileInfo(playlist.currentPath()).fileName(), QStringLiteral("b.mkv"));
    QVERIFY(playlist.hasNext());
    QVERIFY(playlist.hasPrevious());
}

void TstPlaylist::episodesSortLikeAPersonWouldSortThem()
{
    for (int i : {1, 2, 3, 9, 10, 11, 20})
        touch(path(QStringLiteral("Show.S01E%1.mkv").arg(i)));

    Playlist playlist;
    playlist.openFolderOf(path(QStringLiteral("Show.S01E1.mkv")));

    // Plain string order gives E1, E10, E11, E2... which is how a folder of
    // episodes ends up playing in the wrong order all evening.
    QCOMPARE(names(playlist.files()),
             (QStringList{QStringLiteral("Show.S01E1.mkv"), QStringLiteral("Show.S01E2.mkv"),
                          QStringLiteral("Show.S01E3.mkv"), QStringLiteral("Show.S01E9.mkv"),
                          QStringLiteral("Show.S01E10.mkv"), QStringLiteral("Show.S01E11.mkv"),
                          QStringLiteral("Show.S01E20.mkv")}));
}

void TstPlaylist::paddedAndUnpaddedNumbersAgree()
{
    for (const char *name : {"ep2.mkv", "ep02.mp4", "ep10.mkv", "EP1.mkv", "ep 3.mkv"})
        touch(path(QLatin1String(name)));

    Playlist playlist;
    playlist.openFolderOf(path(QStringLiteral("ep2.mkv")));

    // Zero padding is not a value and case is not an ordering: what matters is
    // that the episode numbers come out ascending however they were typed, and
    // that the two spellings of episode 2 stay adjacent rather than one of them
    // landing after episode 10.
    const QStringList order = names(playlist.files());
    QCOMPARE(order.size(), 5);
    const int one = order.indexOf(QStringLiteral("EP1.mkv"));
    const int two = order.indexOf(QStringLiteral("ep2.mkv"));
    const int twoPadded = order.indexOf(QStringLiteral("ep02.mp4"));
    const int ten = order.indexOf(QStringLiteral("ep10.mkv"));

    QVERIFY(one < two && one < twoPadded);
    QVERIFY(two < ten && twoPadded < ten);
    // Adjacent, in either order: "02" and "2" are the same number, so what
    // separates them is the extension, and which of .mkv and .mp4 wins is not
    // something this should have an opinion about.
    QCOMPARE(qAbs(two - twoPadded), 1);

    // "ep 3.mkv" sorts ahead of all of them, and that is right rather than a
    // quirk: a space precedes digits here as it does in any file manager. The
    // point of including it is that the space does not stop the numbers in the
    // *other* names being read as numbers.
    QCOMPARE(order.first(), QStringLiteral("ep 3.mkv"));
}

void TstPlaylist::nonMediaFilesAreLeftOut()
{
    touch(path(QStringLiteral("film.mkv")));
    touch(path(QStringLiteral("film.srt")));       // its own subtitles
    touch(path(QStringLiteral("film.nfo")));
    touch(path(QStringLiteral("poster.jpg")));
    touch(path(QStringLiteral("other.mp4")));

    Playlist playlist;
    playlist.openFolderOf(path(QStringLiteral("film.mkv")));

    // A sidecar is not a thing to play next -- it belongs to the film that is
    // playing now.
    QCOMPARE(names(playlist.files()),
             (QStringList{QStringLiteral("film.mkv"), QStringLiteral("other.mp4")}));
}

void TstPlaylist::theOpenedFileIsAlwaysQueuedEvenIfUnrecognised()
{
    touch(path(QStringLiteral("film.mkv")));
    touch(path(QStringLiteral("odd.rmvb")));  // playable by mpv, unlisted here

    Playlist playlist;
    playlist.openFolderOf(path(QStringLiteral("odd.rmvb")));

    // Refusing to queue the file that is playing would be absurd, however the
    // extension list feels about it.
    QVERIFY(playlist.count() >= 1);
    QCOMPARE(QFileInfo(playlist.currentPath()).fileName(), QStringLiteral("odd.rmvb"));
    QVERIFY(playlist.contains(path(QStringLiteral("odd.rmvb"))));
}

void TstPlaylist::nextAndPreviousStopAtTheEnds()
{
    touch(path(QStringLiteral("a.mkv")));
    touch(path(QStringLiteral("b.mkv")));

    Playlist playlist;
    playlist.openFolderOf(path(QStringLiteral("a.mkv")));

    QVERIFY(!playlist.hasPrevious());
    QCOMPARE(playlist.previous(), QString());
    QCOMPARE(playlist.currentIndex(), 0);

    QCOMPARE(QFileInfo(playlist.next()).fileName(), QStringLiteral("b.mkv"));
    QCOMPARE(playlist.currentIndex(), 1);

    // The end is the end: a queue that silently restarts is how you watch
    // episode one twice.
    QVERIFY(!playlist.hasNext());
    QCOMPARE(playlist.next(), QString());
    QCOMPARE(playlist.currentIndex(), 1);

    QCOMPARE(QFileInfo(playlist.previous()).fileName(), QStringLiteral("a.mkv"));
}

void TstPlaylist::aDropKeepsTheOrderItWasDroppedIn()
{
    const QStringList dropped{path(QStringLiteral("c.mkv")), path(QStringLiteral("a.mkv")),
                              path(QStringLiteral("notes.txt")),
                              path(QStringLiteral("b.mkv")),
                              path(QStringLiteral("c.mkv"))};
    for (const QString &p : dropped)
        touch(p);

    Playlist playlist;
    playlist.setFiles(dropped);

    // Order as dropped, the non-media file gone, and the repeat collapsed --
    // sorting here would override a choice the person just made by hand.
    QCOMPARE(names(playlist.files()),
             (QStringList{QStringLiteral("c.mkv"), QStringLiteral("a.mkv"),
                          QStringLiteral("b.mkv")}));
    QCOMPARE(playlist.currentIndex(), 0);
    QCOMPARE(QFileInfo(playlist.currentPath()).fileName(), QStringLiteral("c.mkv"));
}

void TstPlaylist::reopeningAQueuedFileKeepsTheQueue()
{
    touch(path(QStringLiteral("a.mkv")));
    touch(path(QStringLiteral("b.mkv")));
    touch(path(QStringLiteral("c.mkv")));

    Playlist playlist;
    playlist.setFiles({path(QStringLiteral("c.mkv")), path(QStringLiteral("a.mkv"))});
    QCOMPARE(playlist.count(), 2);

    // The window asks this before deciding whether to rebuild the queue: a file
    // already in it is an advance, not a new folder. Without that, playing to
    // the end of a dropped pair would silently swallow the pair and queue the
    // whole folder instead.
    QVERIFY(playlist.contains(path(QStringLiteral("a.mkv"))));
    playlist.setCurrentPath(path(QStringLiteral("a.mkv")));
    QCOMPARE(playlist.currentIndex(), 1);
    QCOMPARE(playlist.count(), 2);

    QVERIFY(!playlist.contains(path(QStringLiteral("b.mkv"))));
    // A relative spelling of a queued file is the same file.
    const QString relative = QDir::current().relativeFilePath(path(QStringLiteral("c.mkv")));
    QVERIFY(playlist.contains(relative));
}

void TstPlaylist::dialogFiltersAndScanAgree()
{
    // One list, two users: the open dialog and the folder scan. They drifted
    // apart the moment there were two of them, so there is one.
    const QStringList filters = Playlist::dialogNameFilters();
    QCOMPARE(filters.size(), 2);
    QVERIFY(filters.first().contains(QStringLiteral("*.mkv")));
    QVERIFY(filters.first().contains(QStringLiteral("*.flac")));
    QVERIFY(!filters.first().contains(QStringLiteral("*.srt")));
    QVERIFY(filters.last().contains(QStringLiteral("(*)")));
}

QTEST_MAIN(TstPlaylist)
#include "tst_playlist.moc"
