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

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>

#include <memory>

#include "Playlist.h"

namespace {

void touch(const QString &path)
{
    QFile file(path);
    // Qt 6.12 marks open() nodiscard. A fixture that cannot be created is not a
    // test failure to be reported later -- every case after it would be
    // meaningless -- so it stops here.
    if (!file.open(QIODevice::WriteOnly))
        qFatal("cannot create test fixture %s", qPrintable(path));
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
    void aDroppedFolderBecomesItsContents();
    void severalDroppedFoldersPlayInTheOrderDropped();
    void aDroppedFolderIsWalkedToTheBottom();
    void aFolderWithNothingPlayableExpandsToNothing();
    void qmlCanCallExpand();
    void reopeningAQueuedFileKeepsTheQueue();
    void dialogFiltersAndScanAgree();

private:
    QString path(const QString &name) const { return m_dir.filePath(name); }
    // A file at `name`, creating whatever folders lead to it.
    QString touchIn(const QString &name) const
    {
        const QString full = path(name);
        QDir().mkpath(QFileInfo(full).absolutePath());
        touch(full);
        return full;
    }
    QTemporaryDir m_dir;
};

void TstPlaylist::init()
{
    QVERIFY(m_dir.isValid());
    // Each test builds the folder it needs -- subdirectories included, now that
    // a drop can be a folder and the tests make some.
    const QDir dir(m_dir.path());
    for (const QString &name : dir.entryList(QDir::Files))
        QFile::remove(dir.filePath(name));
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        QDir(dir.filePath(name)).removeRecursively();
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

void TstPlaylist::aDroppedFolderBecomesItsContents()
{
    touchIn(QStringLiteral("Season 1/ep10.mkv"));
    touchIn(QStringLiteral("Season 1/ep2.mkv"));
    touchIn(QStringLiteral("Season 1/ep1.mkv"));
    touchIn(QStringLiteral("Season 1/ep1.srt"));
    touchIn(QStringLiteral("Season 1/readme.nfo"));

    // Sorted, not left in whatever order the filesystem returns: nobody chose
    // that order, whereas the numbering in the names is a choice. The sidecar
    // and the notes are not things to play.
    QCOMPARE(names(Playlist::expand({path(QStringLiteral("Season 1"))})),
             (QStringList{QStringLiteral("ep1.mkv"), QStringLiteral("ep2.mkv"),
                          QStringLiteral("ep10.mkv")}));
}

void TstPlaylist::severalDroppedFoldersPlayInTheOrderDropped()
{
    touchIn(QStringLiteral("Season 2/ep1.mkv"));
    touchIn(QStringLiteral("Season 1/ep1.mkv"));
    const QString loose = touchIn(QStringLiteral("extra.mkv"));

    // Season two first, because that is the order they were dropped in -- the
    // sorting is inside each folder, never across them. A file dropped
    // alongside folders keeps its place in the sequence.
    const QStringList expanded = Playlist::expand(
        {path(QStringLiteral("Season 2")), loose, path(QStringLiteral("Season 1"))});
    QCOMPARE(expanded.size(), 3);
    QCOMPARE(QDir(m_dir.path()).relativeFilePath(expanded.at(0)),
             QStringLiteral("Season 2/ep1.mkv"));
    QCOMPARE(QFileInfo(expanded.at(1)).fileName(), QStringLiteral("extra.mkv"));
    QCOMPARE(QDir(m_dir.path()).relativeFilePath(expanded.at(2)),
             QStringLiteral("Season 1/ep1.mkv"));

    // And the queue that comes out of it is exactly that, first file current.
    Playlist playlist;
    playlist.setFiles(expanded);
    QCOMPARE(playlist.count(), 3);
    QCOMPARE(playlist.currentIndex(), 0);
}

void TstPlaylist::aDroppedFolderIsWalkedToTheBottom()
{
    touchIn(QStringLiteral("Show/Season 1/ep1.mkv"));
    touchIn(QStringLiteral("Show/Season 1/Extras/blooper.mkv"));
    touchIn(QStringLiteral("Show/Season 2/ep1.mkv"));
    touchIn(QStringLiteral("Show/trailer.mkv"));

    // One level would miss every episode here, which is how these folders are
    // actually laid out. Files before subfolders at each level, so the trailer
    // sitting at the top comes before the seasons, and a season's own episodes
    // come before its Extras.
    QCOMPARE(names(Playlist::expand({path(QStringLiteral("Show"))})),
             (QStringList{QStringLiteral("trailer.mkv"), QStringLiteral("ep1.mkv"),
                          QStringLiteral("blooper.mkv"), QStringLiteral("ep1.mkv")}));
}

void TstPlaylist::aFolderWithNothingPlayableExpandsToNothing()
{
    touchIn(QStringLiteral("Subs/film.srt"));
    QDir().mkpath(path(QStringLiteral("Empty")));

    // Nothing, rather than the folder itself: handing a directory to mpv as if
    // it were a film is the failure this replaced. The window turns an empty
    // expansion into a notice, which is the only way a drop can say so.
    QVERIFY(Playlist::expand({path(QStringLiteral("Subs")),
                              path(QStringLiteral("Empty"))}).isEmpty());

    // A plain file is not filtered here though -- it has to reach the caller so
    // that dropping one thing mpv cannot open reports mpv's error rather than
    // looking like a drop that missed the window.
    const QString notes = touchIn(QStringLiteral("notes.txt"));
    QCOMPARE(Playlist::expand({notes}), QStringList{notes});
}

void TstPlaylist::qmlCanCallExpand()
{
    touchIn(QStringLiteral("Season 1/ep2.mkv"));
    touchIn(QStringLiteral("Season 1/ep1.mkv"));

    // expand() is static, and the drop handler reaches it through the Playlist
    // *instance* in Main.qml. A static Q_INVOKABLE not being callable that way
    // would fail at runtime, inside a drop, with nothing said at build time --
    // so the call path the window actually uses is the thing tested here, not
    // just the C++ function behind it.
    QQmlEngine engine;
    Playlist playlist;
    engine.rootContext()->setContextProperty(QStringLiteral("playlist"), &playlist);

    QQmlComponent component(&engine);
    component.setData("import QtQml\nQtObject { function go(p) { return playlist.expand(p) } }",
                      QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY(object != nullptr);

    QVariant returned;
    QVERIFY(QMetaObject::invokeMethod(
        object.get(), "go", Q_RETURN_ARG(QVariant, returned),
        Q_ARG(QVariant, QVariant(QStringList{path(QStringLiteral("Season 1"))}))));

    QCOMPARE(names(returned.toStringList()),
             (QStringList{QStringLiteral("ep1.mkv"), QStringLiteral("ep2.mkv")}));
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
