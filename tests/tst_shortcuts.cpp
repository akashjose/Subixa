// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "ShortcutRegistry.h"

#include <QtCore/QTemporaryDir>
#include <QtTest/QtTest>

// The hotkey table and its overrides.
//
// Policy, not plumbing: what these check is that a rebinding survives, that
// rebinding to the default does not leave a stale copy behind, that two actions
// cannot silently claim one key, and that spelling a sequence differently does
// not create a second binding. All of it runs against a temporary ini file --
// a test that quietly rewrote the developer's own keyboard would be the exact
// bug this suite exists to catch.
class TestShortcuts : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void defaultsAreLoaded();
    void everyActionIsWellFormed();
    void defaultsDoNotCollide();
    void overrideWinsAndPersists();
    void rebindingToTheDefaultClearsTheOverride();
    void conflictIsReported();
    void spellingDoesNotCreateASecondBinding();
    void unbindingIsDistinctFromResetting();
    void resetAllRestoresEverything();

private:
    QString iniPath() const { return m_dir.filePath(QStringLiteral("test.ini")); }
    QTemporaryDir m_dir;
};

void TestShortcuts::init()
{
    QFile::remove(iniPath());
}

void TestShortcuts::defaultsAreLoaded()
{
    ShortcutRegistry registry(iniPath());
    QCOMPARE(registry.sequenceFor(QStringLiteral("play-pause")), QStringLiteral("Space"));
    QCOMPARE(registry.sequenceFor(QStringLiteral("fullscreen")), QStringLiteral("F"));
    QCOMPARE(registry.sequenceFor(QStringLiteral("panel-detach")), QStringLiteral("Ctrl+D"));
    // An id nobody defined is empty rather than a crash or a made-up binding.
    QCOMPARE(registry.sequenceFor(QStringLiteral("no-such-action")), QString());
}

void TestShortcuts::everyActionIsWellFormed()
{
    ShortcutRegistry registry(iniPath());
    const QVariantList model = registry.model();
    QCOMPARE(model.size(), ShortcutRegistry::actions().size());

    QSet<QString> ids;
    for (const QVariant &entry : model) {
        const QVariantMap row = entry.toMap();
        const QString id = row.value(QStringLiteral("id")).toString();

        QVERIFY2(!id.isEmpty(), "every action needs an id");
        QVERIFY2(!ids.contains(id), qPrintable(QStringLiteral("duplicate id: %1").arg(id)));
        ids.insert(id);

        QVERIFY2(!row.value(QStringLiteral("label")).toString().isEmpty(),
                 qPrintable(QStringLiteral("%1 has no label").arg(id)));
        QVERIFY2(!row.value(QStringLiteral("category")).toString().isEmpty(),
                 qPrintable(QStringLiteral("%1 has no category").arg(id)));
        // A default that QKeySequence cannot parse would silently become an
        // unbound action, which is invisible until someone reaches for the key.
        QVERIFY2(!row.value(QStringLiteral("defaultSequence")).toString().isEmpty(),
                 qPrintable(QStringLiteral("%1 has an unparseable default").arg(id)));
        QCOMPARE(row.value(QStringLiteral("isCustom")).toBool(), false);
    }
}

void TestShortcuts::defaultsDoNotCollide()
{
    ShortcutRegistry registry(iniPath());

    QHash<QString, QString> seen;
    for (const ShortcutRegistry::Action &action : ShortcutRegistry::actions()) {
        const QString id = QString::fromLatin1(action.id);
        const QString sequence = registry.sequenceFor(id);
        if (sequence.isEmpty())
            continue;
        QVERIFY2(!seen.contains(sequence),
                 qPrintable(QStringLiteral("%1 and %2 both default to %3")
                                .arg(seen.value(sequence), id, sequence)));
        seen.insert(sequence, id);
    }
}

void TestShortcuts::overrideWinsAndPersists()
{
    {
        ShortcutRegistry registry(iniPath());
        QSignalSpy spy(&registry, &ShortcutRegistry::changed);
        registry.setSequence(QStringLiteral("fullscreen"), QStringLiteral("Ctrl+Shift+F"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(registry.sequenceFor(QStringLiteral("fullscreen")),
                 QStringLiteral("Ctrl+Shift+F"));
        QCOMPARE(registry.defaultSequenceFor(QStringLiteral("fullscreen")),
                 QStringLiteral("F"));
    }

    // A second registry over the same file: the override has to have reached
    // disk, not just the in-memory hash.
    ShortcutRegistry reopened(iniPath());
    QCOMPARE(reopened.sequenceFor(QStringLiteral("fullscreen")),
             QStringLiteral("Ctrl+Shift+F"));

    for (const QVariant &entry : reopened.model()) {
        const QVariantMap row = entry.toMap();
        if (row.value(QStringLiteral("id")).toString() == QLatin1String("fullscreen"))
            QCOMPARE(row.value(QStringLiteral("isCustom")).toBool(), true);
    }
}

void TestShortcuts::rebindingToTheDefaultClearsTheOverride()
{
    ShortcutRegistry registry(iniPath());
    registry.setSequence(QStringLiteral("mute"), QStringLiteral("Ctrl+M"));
    QCOMPARE(registry.sequenceFor(QStringLiteral("mute")), QStringLiteral("Ctrl+M"));

    // Back to what it always was. Storing a copy of the default would mean a
    // later change to that default silently failed to reach anyone who had once
    // touched the row -- which is a bug that only shows up a release later.
    registry.setSequence(QStringLiteral("mute"), QStringLiteral("M"));
    QCOMPARE(registry.sequenceFor(QStringLiteral("mute")), QStringLiteral("M"));

    QSettings check(iniPath(), QSettings::IniFormat);
    check.beginGroup(QStringLiteral("hotkeys"));
    QVERIFY2(!check.childKeys().contains(QStringLiteral("mute")),
             "an override equal to the default must not be stored");
}

void TestShortcuts::conflictIsReported()
{
    ShortcutRegistry registry(iniPath());

    // F is fullscreen's by default.
    QCOMPARE(registry.conflict(QStringLiteral("F"), QStringLiteral("mute")),
             QStringLiteral("Fullscreen"));
    // An action never conflicts with itself.
    QCOMPARE(registry.conflict(QStringLiteral("F"), QStringLiteral("fullscreen")), QString());
    // Something nothing has claimed.
    QCOMPARE(registry.conflict(QStringLiteral("Ctrl+Alt+Shift+Y"), QStringLiteral("mute")),
             QString());

    // A conflict follows the override, not the default.
    registry.setSequence(QStringLiteral("fullscreen"), QStringLiteral("Ctrl+Shift+F"));
    QCOMPARE(registry.conflict(QStringLiteral("F"), QStringLiteral("mute")), QString());
    QCOMPARE(registry.conflict(QStringLiteral("Ctrl+Shift+F"), QStringLiteral("mute")),
             QStringLiteral("Fullscreen"));
}

void TestShortcuts::spellingDoesNotCreateASecondBinding()
{
    ShortcutRegistry registry(iniPath());

    // Whatever a capture field produced, one binding is one binding.
    QCOMPARE(ShortcutRegistry::normalise(QStringLiteral("ctrl+d")), QStringLiteral("Ctrl+D"));
    QCOMPARE(ShortcutRegistry::normalise(QStringLiteral("CTRL+D")), QStringLiteral("Ctrl+D"));

    registry.setSequence(QStringLiteral("mute"), QStringLiteral("ctrl+shift+k"));
    QCOMPARE(registry.sequenceFor(QStringLiteral("mute")), QStringLiteral("Ctrl+Shift+K"));
    // And the stored spelling is what a conflict check compares against.
    QCOMPARE(registry.conflict(QStringLiteral("CTRL+SHIFT+K"), QStringLiteral("fullscreen")),
             QStringLiteral("Mute"));
}

void TestShortcuts::unbindingIsDistinctFromResetting()
{
    ShortcutRegistry registry(iniPath());

    // An empty sequence means "deliberately unbound", and must survive as that
    // rather than falling back to the default on the next read.
    registry.setSequence(QStringLiteral("mute"), QString());
    QCOMPARE(registry.sequenceFor(QStringLiteral("mute")), QString());

    ShortcutRegistry reopened(iniPath());
    QCOMPARE(reopened.sequenceFor(QStringLiteral("mute")), QString());

    // Resetting is the other thing, and gives the default back.
    reopened.resetSequence(QStringLiteral("mute"));
    QCOMPARE(reopened.sequenceFor(QStringLiteral("mute")), QStringLiteral("M"));
}

void TestShortcuts::resetAllRestoresEverything()
{
    ShortcutRegistry registry(iniPath());
    registry.setSequence(QStringLiteral("mute"), QStringLiteral("Ctrl+M"));
    registry.setSequence(QStringLiteral("fullscreen"), QStringLiteral("Ctrl+Shift+F"));
    registry.setSequence(QStringLiteral("play-pause"), QString());

    QSignalSpy spy(&registry, &ShortcutRegistry::changed);
    registry.resetAll();
    QCOMPARE(spy.count(), 1);

    QCOMPARE(registry.sequenceFor(QStringLiteral("mute")), QStringLiteral("M"));
    QCOMPARE(registry.sequenceFor(QStringLiteral("fullscreen")), QStringLiteral("F"));
    QCOMPARE(registry.sequenceFor(QStringLiteral("play-pause")), QStringLiteral("Space"));

    for (const QVariant &entry : registry.model())
        QCOMPARE(entry.toMap().value(QStringLiteral("isCustom")).toBool(), false);
}

QTEST_MAIN(TestShortcuts)
#include "tst_shortcuts.moc"
