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
// It writes into a temporary XDG_CONFIG_HOME and XDG_CACHE_HOME, because a test
// that quietly remembered a subtitle track in the developer's own settings would
// be a bug of exactly the kind these tests exist to catch.

#include <QtTest>

#include <QtCore/QDir>
#include <QtCore/QTemporaryDir>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(CMP_TESTDATA_DIR "/") + name;
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
    void searchReachesTheProxy();
    void followMovesTheView();
    void followOffLeavesTheViewAlone();
    void remembersTheTrackPerFile();
    void detachingKeepsTheViewState();
    void themeReachesBothWindows();

private:
    // Loads Main.qml and waits until the panel exists.
    bool startApp();
    // Opens a fixture through the window's own openFile(), then waits for the
    // parse to land -- the same path a drop or the dialog takes.
    bool openFixture(const QString &name);
    QObject *panel() const { return named(m_root, "subtitlePanel"); }

    QTemporaryDir m_home;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QObject *m_root = nullptr;
};

void TstQmlPanel::initTestCase()
{
    QVERIFY(m_home.isValid());
    if (!QFileInfo::exists(fixture(QStringLiteral("subs.mkv"))))
        QSKIP("fixtures missing -- run ./testdata/make-fixtures.sh");

    // Before any QSettings exists. PlaybackHistory, the QML Settings type and
    // the cue cache all resolve their paths from these.
    qputenv("XDG_CONFIG_HOME", m_home.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", m_home.filePath(QStringLiteral("cache")).toUtf8());

    QCoreApplication::setApplicationName(QStringLiteral("custom_media_player"));
    QCoreApplication::setOrganizationName(QStringLiteral("custom_media_player"));
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
    m_engine.reset();
}

bool TstQmlPanel::startApp()
{
    m_engine = std::make_unique<QQmlApplicationEngine>();
    // main.cpp passes argv[1] this way; an empty string is "no file yet".
    m_engine->rootContext()->setContextProperty(QStringLiteral("initialFile"),
                                                QString());
    m_engine->loadFromModule("CustomMediaPlayer", "Main");

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
        QStringLiteral("CustomMediaPlayer"), QStringLiteral("Theme"));
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
