// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

// The QML layer, headless.
//
// Everything below the scene graph already had a harness (tst_subtitles); the
// wiring above it did not, and it is where the interesting mistakes live: a tab
// that no longer swaps the model, a search box no longer connected to the proxy,
// follow that stopped scrolling, view state lost when the panel moves between
// windows. Until now the only way to check any of that was a screenshot, and
// under WSLg a screenshot is the least reliable evidence available (see
// CLAUDE.md).
//
// This loads the real Main.qml -- not a mock -- with the offscreen platform. The
// scene graph never renders there, so mpv loads nothing and there is no picture,
// but the object tree, the bindings and the signal wiring are all real. Nothing
// here asserts a pixel.
//
// It writes into a temporary settings and cache location, because a test that
// quietly remembered a subtitle track in the developer's own settings would be a
// bug of exactly the kind these tests exist to catch.

#include <QtTest>

#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtGui/QGuiApplication>
#include "MpvEngine.h"
#include "SettingsService.h"

#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(SUBIXA_TESTDATA_DIR "/") + name;
}

QObject *named(QObject *root, const char *objectName)
{
    return root->findChild<QObject *>(QLatin1String(objectName));
}

}  // namespace

class TstQmlPanel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void panelShowsTheParsedTrack();
    void tabSwitchSwapsTheModel();
    void aMenuPickReachesTheBrowser();
    void skipStaysForASingleFile();
    void searchReachesTheProxy();
    void followMovesTheView();
    void followOffLeavesTheViewAlone();
    void openingAFileQueuesItsFolder();
    void aDroppedSetBecomesTheQueue();
    void remembersTheTrackPerFile();
    void resumeToggleOffIgnoresAStoredPosition();
    void subtitleToggleOffOpensOnTheDefaultTrack();
    void detachingKeepsTheViewState();
    void themeReachesBothWindows();

private:
    // Loads Main.qml and waits until the panel exists.
    bool startApp();
    // Opens a fixture through the window's own openFile(), then waits for the
    // parse to land -- the same path a drop or the dialog takes.
    bool openFixture(const QString &name);
    QObject *panel() const { return named(m_root, "subtitlePanel"); }
    // A folder of exactly two playable files, so "what plays next" has one
    // answer. The real testdata folder cannot serve: which fixture follows
    // which depends on whether the big ones were generated.
    QString queueFolderFile(const QString &name);

    QTemporaryDir m_home;
    std::unique_ptr<SettingsService> m_service;
    std::unique_ptr<MpvEngine> m_mpv;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QObject *m_root = nullptr;
};

void TstQmlPanel::initTestCase()
{
    QVERIFY(m_home.isValid());
    // Hard failure, not QSKIP: a skip exits 0 and would report this suite
    // green having asserted nothing. See tst_subtitles::initTestCase.
    QVERIFY2(QFileInfo::exists(fixture(QStringLiteral("subs.mkv"))),
             "subs.mkv missing -- run ./testdata/make-fixtures.sh");

    // Before any QSettings exists. PlaybackHistory, the QML Settings type and
    // the cue cache all resolve their paths from these.
    qputenv("XDG_CONFIG_HOME", m_home.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", m_home.filePath(QStringLiteral("cache")).toUtf8());

    QCoreApplication::setApplicationName(QStringLiteral("subixa"));
    QCoreApplication::setOrganizationName(QStringLiteral("subixa"));

    // The two lines above are Unix-only, and on their own they made this suite
    // pass on Windows for the wrong reason. Qt there resolves the standard paths
    // from %APPDATA%/%LOCALAPPDATA% and ignores XDG entirely, and a
    // default-constructed QSettings -- which is what PlaybackHistory and
    // ShortcutRegistry use -- is the *registry*, which no directory redirection
    // can reach. So the suite read and wrote the developer's real profile,
    // init()'s cleanup below removed an empty temporary directory, and the
    // subtitle track remembered by the previous run reopened
    // panelShowsTheParsedTrack on tab 1 instead of tab 0. It passed once, on a
    // machine that had never run it before, and failed every time after.
    //
    // A file-backed QSettings under m_home says the same thing portably: the
    // default constructor now lands in the directory init() clears.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       m_home.filePath(QStringLiteral("config")));

    // And the cue cache, which reads QStandardPaths rather than QSettings. Test
    // mode keeps it out of the real profile on every platform; clearing it once
    // here -- not per test -- leaves the within-a-run cache hits the suite
    // already relied on intact, while matching the fresh-per-run behaviour the
    // XDG redirect gave on Linux.
    QStandardPaths::setTestModeEnabled(true);
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .removeRecursively();
}

void TstQmlPanel::init()
{
    // Every test starts from an empty store. The player now remembers which
    // subtitle track was read in a file, so without this one test's tab click
    // would decide which track the next test opens on.
    QDir(m_home.filePath(QStringLiteral("config"))).removeRecursively();
    QVERIFY(startApp());
}

void TstQmlPanel::cleanup()
{
    m_root = nullptr;
    // Reverse of construction, for the same reason main() relies on. The
    // service goes last: PlaybackHistory and ShortcutRegistry, destroyed with
    // the engine, flush into the store they borrow from it.
    m_engine.reset();
    m_mpv.reset();
    m_service.reset();
}

bool TstQmlPanel::startApp()
{
    // First, exactly as main() constructs it first: the QML-created
    // PlaybackHistory and ShortcutRegistry borrow this store, and its default
    // QSettings honours the IniFormat redirect in initTestCase.
    m_service = std::make_unique<SettingsService>();

    // Declared before the engine, and destroyed after it, exactly as main.cpp
    // does -- see MpvEngine on why that order is load-bearing. Under `offscreen`
    // no render context is ever created, so this half of it is not exercised
    // here; the ordering is mirrored anyway so the harness does not quietly
    // diverge from the thing it is testing.
    m_mpv = std::make_unique<MpvEngine>();

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->rootContext()->setContextProperty(QStringLiteral("mpvEngine"),
                                                m_mpv.get());
    // main.cpp passes the positional arguments this way; empty is "no file yet".
    m_engine->rootContext()->setContextProperty(QStringLiteral("initialFiles"),
                                                QStringList());
    m_engine->loadFromModule("Subixa", "Main");

    if (m_engine->rootObjects().isEmpty()) {
        qWarning("Main.qml did not load");
        return false;
    }
    m_root = m_engine->rootObjects().constFirst();

    // The docked panel comes from a Loader, so it appears an event loop turn
    // after the window does.
    for (int i = 0; i < 100 && !panel(); ++i)
        QTest::qWait(10);
    return panel() != nullptr;
}

bool TstQmlPanel::openFixture(const QString &name)
{
    QSignalSpy parsed(named(m_root, "subtitleManager"), SIGNAL(loaded()));
    QMetaObject::invokeMethod(m_root, "openFile", Q_ARG(QVariant, fixture(name)));
    return parsed.wait(20000);
}

void TstQmlPanel::panelShowsTheParsedTrack()
{
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));

    // Three tracks in the fixture, and the window lands on the first browsable
    // one -- which is what fills the tab bar and the list.
    QObject *tabs = named(m_root, "trackTabs");
    QVERIFY(tabs);
    QCOMPARE(tabs->property("count").toInt(), 3);
    QCOMPARE(tabs->property("currentIndex").toInt(), 0);

    QObject *list = named(m_root, "lineList");
    QVERIFY(list);
    // The English fixture track has four cues, and they reach the view through
    // the filter proxy rather than being copied.
    QCOMPARE(list->property("count").toInt(), 4);
}

void TstQmlPanel::tabSwitchSwapsTheModel()
{
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));

    QObject *tabs = named(m_root, "trackTabs");
    QObject *list = named(m_root, "lineList");
    QVERIFY(tabs && list);
    QCOMPARE(list->property("count").toInt(), 4);

    // What a click on the third tab does. The panel only reports it; the window
    // is what repoints the proxy, and this is the seam between them.
    tabs->setProperty("currentIndex", 2);
    QTRY_COMPARE(list->property("count").toInt(), 3);  // the French track
    QCOMPARE(m_root->property("currentTrack").toInt(), 2);

    tabs->setProperty("currentIndex", 1);
    QTRY_COMPARE(list->property("count").toInt(), 4);  // the Japanese ASS track
    QCOMPARE(m_root->property("currentTrack").toInt(), 1);
}

void TstQmlPanel::aMenuPickReachesTheBrowser()
{
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));

    QObject *ui = named(m_root, "panelUi");
    QObject *tabs = named(m_root, "trackTabs");
    QObject *list = named(m_root, "lineList");
    QVERIFY(ui && tabs && list);
    QCOMPARE(m_root->property("currentTrack").toInt(), 0);

    // The other direction into the browser: a track picked from the transport's
    // subtitle menu, which ends at syncPanelToSubtitleTrack() and whose only
    // write is the tab. The mpv half of that round trip cannot run here --
    // offscreen never creates a render context, so mpv loads no file and has no
    // track list to match a selection against -- so this makes the same write.
    //
    // Deliberately not through the tab bar: a tab click carries the selection
    // with it and that path always worked. This one did not, and the browser
    // went on listing the previous track's cues.
    ui->setProperty("tabIndex", 2);

    QCOMPARE(m_root->property("currentTrack").toInt(), 2);
    QTRY_COMPARE(list->property("count").toInt(), 3);       // the French track
    QTRY_COMPARE(tabs->property("currentIndex").toInt(), 2);

    ui->setProperty("tabIndex", 1);
    QCOMPARE(m_root->property("currentTrack").toInt(), 1);
    QTRY_COMPARE(list->property("count").toInt(), 4);       // the Japanese ASS track

    // -1 says nothing is being read, and it is what a file whose tracks are all
    // bitmap now opens on: no tab lit, rather than the previous film's tab still
    // lit over another film's cues. Export reads the same -1 and does nothing.
    ui->setProperty("tabIndex", -1);
    QCOMPARE(m_root->property("currentTrack").toInt(), -1);
    QTRY_COMPARE(list->property("count").toInt(), 0);
}

void TstQmlPanel::skipStaysForASingleFile()
{
    // A folder of exactly one playable file, so there is nowhere to skip to.
    // Its own folder for the same reason the queue tests have theirs.
    const QString dir = m_home.filePath(QStringLiteral("solo"));
    QDir().mkpath(dir);
    const QString only = dir + QStringLiteral("/only.mkv");
    if (!QFileInfo::exists(only))
        QFile::copy(fixture(QStringLiteral("subs.mkv")), only);

    QSignalSpy parsed(named(m_root, "subtitleManager"), SIGNAL(loaded()));
    QMetaObject::invokeMethod(m_root, "openFile", Q_ARG(QVariant, only));
    QVERIFY(parsed.wait(20000));
    QCOMPARE(named(m_root, "playlist")->property("count").toInt(), 1);

    QObject *back = named(m_root, "skipBack");
    QObject *forward = named(m_root, "skipForward");
    QObject *chip = named(m_root, "queueChip");
    QVERIFY(back && forward && chip);

    // Skip is a playback control and stays where it is at any queue length:
    // disabled says "nowhere to go", where absent says "this player is broken".
    QVERIFY(back->property("visible").toBool());
    QVERIFY(forward->property("visible").toBool());
    QVERIFY(!back->property("enabled").toBool());
    QVERIFY(!forward->property("enabled").toBool());
    // The chip is the other half of what used to be one predicate, and it must
    // not come back with it -- "1/1" is a permanent readout of nothing.
    QVERIFY(!chip->property("visible").toBool());

    // And with a queue both halves appear, which is what says the two are still
    // wired to the playlist rather than pinned on.
    const QString first = queueFolderFile(QStringLiteral("ep1.mkv"));
    queueFolderFile(QStringLiteral("ep2.mkv"));  // before the open, or it is not in the queue
    parsed.clear();
    QMetaObject::invokeMethod(m_root, "openFile", Q_ARG(QVariant, first));
    QVERIFY(parsed.wait(20000));
    QTRY_VERIFY(chip->property("visible").toBool());
    QTRY_VERIFY(forward->property("enabled").toBool());
    QVERIFY(!back->property("enabled").toBool());
}

void TstQmlPanel::searchReachesTheProxy()
{
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));

    QObject *field = named(m_root, "searchField");
    QObject *list = named(m_root, "lineList");
    QVERIFY(field && list);

    // Typing is coalesced by a 150 ms timer in the panel, so this is a QTRY.
    field->setProperty("text", QStringLiteral("albatross"));
    QTRY_COMPARE(list->property("count").toInt(), 1);

    field->setProperty("text", QStringLiteral("no such phrase"));
    QTRY_COMPARE(list->property("count").toInt(), 0);

    field->setProperty("text", QString());
    QTRY_COMPARE(list->property("count").toInt(), 4);

    // The search box also has to suppress the single-key shortcuts while it has
    // focus, or typing "film" would toggle fullscreen and mute.
    QMetaObject::invokeMethod(panel(), "focusSearch");
    QTRY_VERIFY(panel()->property("searchActive").toBool());
    QVERIFY(m_root->property("typing").toBool());
}

void TstQmlPanel::followMovesTheView()
{
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));

    QObject *list = named(m_root, "lineList");
    QVERIFY(list);

    // currentRow is what the window computes from the playing position; the
    // panel's job is to move the view to it. Playback itself cannot run here --
    // offscreen never creates the render context -- so the row is set directly,
    // which is the same value syncFollow() would produce.
    m_root->setProperty("currentRow", 2);
    QTRY_COMPARE(list->property("currentIndex").toInt(), 2);

    m_root->setProperty("currentRow", 0);
    QTRY_COMPARE(list->property("currentIndex").toInt(), 0);
}

void TstQmlPanel::followOffLeavesTheViewAlone()
{
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));

    QObject *list = named(m_root, "lineList");
    QObject *ui = named(m_root, "panelUi");
    QVERIFY(list && ui);

    m_root->setProperty("currentRow", 1);
    QTRY_COMPARE(list->property("currentIndex").toInt(), 1);

    // Dragging the list turns follow off, and after that the view must stay
    // where the reader put it however far playback moves on.
    ui->setProperty("following", false);
    m_root->setProperty("currentRow", 3);
    QTest::qWait(100);
    QCOMPARE(list->property("currentIndex").toInt(), 1);

    // Turning it back on jumps to the line playing now rather than waiting for
    // the next cue boundary.
    ui->setProperty("following", true);
    m_root->setProperty("currentRow", 3);
    QTRY_COMPARE(list->property("currentIndex").toInt(), 3);
}

QString TstQmlPanel::queueFolderFile(const QString &name)
{
    const QString dir = m_home.filePath(QStringLiteral("queue"));
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + name;
    if (!QFileInfo::exists(path))
        QFile::copy(fixture(QStringLiteral("subs.mkv")), path);
    return path;
}

void TstQmlPanel::openingAFileQueuesItsFolder()
{
    const QString first = queueFolderFile(QStringLiteral("ep1.mkv"));
    const QString second = queueFolderFile(QStringLiteral("ep2.mkv"));

    QSignalSpy parsed(named(m_root, "subtitleManager"), SIGNAL(loaded()));
    QMetaObject::invokeMethod(m_root, "openFile", Q_ARG(QVariant, first));
    QVERIFY(parsed.wait(20000));

    QObject *playlist = named(m_root, "playlist");
    QVERIFY(playlist);
    // Nobody built a playlist; opening one file is what queued the folder.
    QCOMPARE(playlist->property("count").toInt(), 2);
    QCOMPARE(playlist->property("currentPath").toString(), first);
    QVERIFY(playlist->property("hasNext").toBool());
    QVERIFY(!playlist->property("hasPrevious").toBool());

    // The path a finished file takes. Playback cannot actually reach the end
    // here -- offscreen never renders, so mpv never plays -- which is why this
    // drives the same function the end-of-file handler calls.
    parsed.clear();
    QMetaObject::invokeMethod(m_root, "playNext");
    QVERIFY(parsed.wait(20000));
    QCOMPARE(m_root->property("currentFile").toString(), second);
    QCOMPARE(playlist->property("currentPath").toString(), second);
    QVERIFY(!playlist->property("hasNext").toBool());

    // Advancing must not rebuild the queue from the new file's folder -- it is
    // the same folder here, but a queue that reshuffles itself on every advance
    // would lose a dropped set at the first boundary.
    QCOMPARE(playlist->property("count").toInt(), 2);

    parsed.clear();
    QMetaObject::invokeMethod(m_root, "playPrevious");
    QVERIFY(parsed.wait(20000));
    QCOMPARE(m_root->property("currentFile").toString(), first);
}

void TstQmlPanel::aDroppedSetBecomesTheQueue()
{
    const QString a = queueFolderFile(QStringLiteral("ep1.mkv"));
    const QString b = queueFolderFile(QStringLiteral("ep2.mkv"));
    queueFolderFile(QStringLiteral("ep3.mkv"));  // in the folder, not in the drop

    QObject *playlist = named(m_root, "playlist");
    QVERIFY(playlist);
    // Hoisted: a braced list inside Q_ARG would split on its own comma.
    const QStringList dropped{b, a};
    QVERIFY(QMetaObject::invokeMethod(playlist, "setFiles",
                                      Q_ARG(QStringList, dropped)));

    QSignalSpy parsed(named(m_root, "subtitleManager"), SIGNAL(loaded()));
    QMetaObject::invokeMethod(m_root, "openFile",
                              Q_ARG(QVariant, playlist->property("currentPath")));
    QVERIFY(parsed.wait(20000));

    // Exactly what was dropped, in that order -- the third file in the folder is
    // not in the queue, and opening the first of the dropped pair must not
    // replace the queue with the folder.
    QCOMPARE(playlist->property("count").toInt(), 2);
    QCOMPARE(playlist->property("currentPath").toString(), b);
    QCOMPARE(playlist->property("currentIndex").toInt(), 0);

    parsed.clear();
    QMetaObject::invokeMethod(m_root, "playNext");
    QVERIFY(parsed.wait(20000));
    QCOMPARE(m_root->property("currentFile").toString(), a);
    QCOMPARE(playlist->property("count").toInt(), 2);
}

void TstQmlPanel::remembersTheTrackPerFile()
{
    // Stand in for "the reader picked the French track last time". Writing it
    // through PlaybackHistory rather than by clicking is deliberate: what is
    // under test is the *restore* path a fresh open takes, which is where a
    // film with 65 tracks either opens on the one being read or does not.
    QObject *history = named(m_root, "playbackHistory");
    QVERIFY(history);

    QVERIFY(openFixture(QStringLiteral("subs.mkv")));
    const QString path = QFileInfo(fixture(QStringLiteral("subs.mkv")))
                             .absoluteFilePath();
    // Track 2 of the fixture is the French one: three cues, stream index 2.
    // Typed Q_ARGs, not QVariant ones: this is a C++ Q_INVOKABLE, and the
    // metacall matches on exact types rather than converting the way a call
    // from QML would.
    QVERIFY(QMetaObject::invokeMethod(history, "rememberSubtitle",
                                      Q_ARG(QString, path), Q_ARG(int, 4),
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QString, QStringLiteral("fre"))));

    // Reopen exactly as a second launch would.
    cleanup();
    QVERIFY(startApp());
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));

    QCOMPARE(m_root->property("currentTrack").toInt(), 2);
    QCOMPARE(named(m_root, "lineList")->property("count").toInt(), 3);
    // And the tab has to follow the content. A panel showing one track with
    // another tab lit is worse than not remembering at all.
    QTRY_COMPARE(named(m_root, "trackTabs")->property("currentIndex").toInt(), 2);
}

void TstQmlPanel::resumeToggleOffIgnoresAStoredPosition()
{
    QObject *history = named(m_root, "playbackHistory");
    QVERIFY(history);

    // An hour into a feature-length film: a position the policy keeps.
    const QString path = QFileInfo(fixture(QStringLiteral("subs.mkv")))
                             .absoluteFilePath();
    QVERIFY(QMetaObject::invokeMethod(history, "remember", Q_ARG(QString, path),
                                      Q_ARG(double, 3600.0),
                                      Q_ARG(double, 8634.0)));
    QVERIFY(QMetaObject::invokeMethod(history, "flush"));

    // Second launch, with resuming switched off.
    cleanup();
    QVERIFY(startApp());
    QObject *prefs = m_root->property("prefsStore").value<QObject *>();
    QVERIFY(prefs);
    QVERIFY(prefs->setProperty("resumeWhereLeftOff", false));

    // pendingResume is set in openFile() and, under offscreen, never consumed
    // -- no render context means mpv loads nothing (trap 2) -- which is
    // exactly what leaves it observable here.
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));
    QCOMPARE(m_root->property("pendingResume").toDouble(), -1.0);

    // Third launch, switched back on: the position was kept the whole time.
    // The toggle gates the restore, not the memory.
    cleanup();
    QVERIFY(startApp());
    prefs = m_root->property("prefsStore").value<QObject *>();
    QVERIFY(prefs);
    QVERIFY(prefs->setProperty("resumeWhereLeftOff", true));

    QVERIFY(openFixture(QStringLiteral("subs.mkv")));
    QCOMPARE(m_root->property("pendingResume").toDouble(), 3600.0);
}

void TstQmlPanel::subtitleToggleOffOpensOnTheDefaultTrack()
{
    QObject *history = named(m_root, "playbackHistory");
    QVERIFY(history);

    QVERIFY(openFixture(QStringLiteral("subs.mkv")));
    const QString path = QFileInfo(fixture(QStringLiteral("subs.mkv")))
                             .absoluteFilePath();
    // The French track, stream index 4 -- but with no language recorded, so
    // the preferred-language fallback stays empty and cannot answer for the
    // per-file entry. That separation is what makes the toggle observable:
    // with a language stored, the fallback would reopen the same track and
    // off would look identical to on.
    QVERIFY(QMetaObject::invokeMethod(history, "rememberSubtitle",
                                      Q_ARG(QString, path), Q_ARG(int, 4),
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QString, QString())));

    // Second launch, with the per-file memory switched off: the panel opens on
    // the first browsable track, exactly as it would for a file never seen.
    cleanup();
    QVERIFY(startApp());
    QObject *prefs = m_root->property("prefsStore").value<QObject *>();
    QVERIFY(prefs);
    QVERIFY(prefs->setProperty("rememberSubtitleTrack", false));

    QVERIFY(openFixture(QStringLiteral("subs.mkv")));
    QCOMPARE(m_root->property("currentTrack").toInt(), 0);
    QTRY_COMPARE(named(m_root, "trackTabs")->property("currentIndex").toInt(), 0);

    // Third launch, switched back on: the entry survived the off period.
    cleanup();
    QVERIFY(startApp());
    prefs = m_root->property("prefsStore").value<QObject *>();
    QVERIFY(prefs);
    QVERIFY(prefs->setProperty("rememberSubtitleTrack", true));

    QVERIFY(openFixture(QStringLiteral("subs.mkv")));
    QCOMPARE(m_root->property("currentTrack").toInt(), 2);
    QTRY_COMPARE(named(m_root, "trackTabs")->property("currentIndex").toInt(), 2);
}

void TstQmlPanel::detachingKeepsTheViewState()
{
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));

    QObject *tabs = named(m_root, "trackTabs");
    QVERIFY(tabs);
    tabs->setProperty("currentIndex", 1);
    named(m_root, "searchField")->setProperty("text", QStringLiteral("albatross"));
    QTRY_COMPARE(named(m_root, "lineList")->property("count").toInt(), 1);

    // The panel is one component with two homes, and moving it destroys and
    // rebuilds the item -- which is why search text, tab and follow live in the
    // window rather than in the panel. This is the test that says so.
    m_root->setProperty("panelDetached", true);
    QTRY_VERIFY(m_root->property("panelDetached").toBool());
    QTRY_VERIFY(panel() != nullptr);

    QObject *rebuiltTabs = named(m_root, "trackTabs");
    QObject *rebuiltField = named(m_root, "searchField");
    QVERIFY(rebuiltTabs && rebuiltField);
    QCOMPARE(rebuiltTabs->property("currentIndex").toInt(), 1);
    QCOMPARE(rebuiltField->property("text").toString(), QStringLiteral("albatross"));
    QTRY_COMPARE(named(m_root, "lineList")->property("count").toInt(), 1);
    // The selected track rides on the tab now, so a bar that wrote one of its
    // own resets back on the way through would move the track being read as well
    // as the tab lit -- trap 13 with a larger blast radius than it had.
    QCOMPARE(m_root->property("currentTrack").toInt(), 1);

    // And back again.
    m_root->setProperty("panelDetached", false);
    QTRY_VERIFY(panel() != nullptr);
    QCOMPARE(named(m_root, "trackTabs")->property("currentIndex").toInt(), 1);
    QCOMPARE(named(m_root, "searchField")->property("text").toString(),
             QStringLiteral("albatross"));
}

void TstQmlPanel::themeReachesBothWindows()
{
    QVERIFY(openFixture(QStringLiteral("subs.mkv")));

    const QColor darkPanel = panel()->property("color").value<QColor>();
    QVERIFY(darkPanel.lightness() < 128);

    QObject *theme = m_engine->singletonInstance<QObject *>(
        QStringLiteral("Subixa"), QStringLiteral("Theme"));
    QVERIFY(theme);
    theme->setProperty("dark", false);

    // A singleton rather than a property threaded through, so this has to reach
    // a panel nobody told about the change -- including one in the other window.
    QTRY_VERIFY(panel()->property("color").value<QColor>().lightness() > 128);

    m_root->setProperty("panelDetached", true);
    QTRY_VERIFY(panel() != nullptr);
    QVERIFY(panel()->property("color").value<QColor>().lightness() > 128);

    theme->setProperty("dark", true);
    QTRY_COMPARE(panel()->property("color").value<QColor>(), darkPanel);
}

QTEST_MAIN(TstQmlPanel)
#include "tst_qmlpanel.moc"
