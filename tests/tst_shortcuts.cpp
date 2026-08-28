// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "ShortcutRegistry.h"

#include <QtCore/QSettings>
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
    void aKeyPressBecomesItsBinding();
    void aBareModifierIsNotABinding();
    void garbageIsNotStoredAsABinding();
    void garbageAlreadyInTheFileIsDropped();

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


// What a key press turns into, which is the whole of the rebinding UI's job.
//
// Built from the key code and the modifier flags rather than from the event's
// text, because the text cannot express it: with Ctrl held it is the control
// character, and for the arrows and the function keys it is empty. Spelling the
// sequence from it produced "Ctrl+\x01" and bare decimal key codes, neither of
// which is a binding any keystroke can match.
void TestShortcuts::aKeyPressBecomesItsBinding()
{
    // The case the old text-based spelling turned into "Ctrl+".
    QCOMPARE(ShortcutRegistry::sequenceFromEvent(Qt::Key_A, Qt::ControlModifier),
             QStringLiteral("Ctrl+A"));
    // The cases it turned into a decimal key code, which parsed to nothing and
    // silently unbound the action.
    QCOMPARE(ShortcutRegistry::sequenceFromEvent(Qt::Key_Right, Qt::NoModifier),
             QStringLiteral("Right"));
    QCOMPARE(ShortcutRegistry::sequenceFromEvent(Qt::Key_F11, Qt::NoModifier),
             QStringLiteral("F11"));
    QCOMPARE(ShortcutRegistry::sequenceFromEvent(Qt::Key_Right,
                                                 Qt::ControlModifier | Qt::ShiftModifier),
             QStringLiteral("Ctrl+Shift+Right"));
    QCOMPARE(ShortcutRegistry::sequenceFromEvent(Qt::Key_Space, Qt::NoModifier),
             QStringLiteral("Space"));

    // The keypad flag rides along on an ordinary press and must not make a
    // second binding out of the same key.
    QCOMPARE(ShortcutRegistry::sequenceFromEvent(Qt::Key_1,
                                                 Qt::ControlModifier | Qt::KeypadModifier),
             QStringLiteral("Ctrl+1"));

    // Every default in the table survives the round trip that a capture makes,
    // so nothing in it is a sequence the UI could not produce.
    for (const ShortcutRegistry::Action &action : ShortcutRegistry::actions()) {
        const QString spelled = QString::fromLatin1(action.defaultSequence);
        QVERIFY2(!ShortcutRegistry::normalise(spelled).isEmpty(),
                 qPrintable(QStringLiteral("unmatchable default: %1").arg(spelled)));
    }
}

void TestShortcuts::aBareModifierIsNotABinding()
{
    QVERIFY(ShortcutRegistry::sequenceFromEvent(Qt::Key_Control, Qt::ControlModifier).isEmpty());
    QVERIFY(ShortcutRegistry::sequenceFromEvent(Qt::Key_Shift, Qt::ShiftModifier).isEmpty());
    QVERIFY(ShortcutRegistry::sequenceFromEvent(Qt::Key_Alt, Qt::AltModifier).isEmpty());
    QVERIFY(ShortcutRegistry::sequenceFromEvent(Qt::Key_Meta, Qt::MetaModifier).isEmpty());
}

// QKeySequence::isEmpty() is not the check it looks like: it accepts text it
// cannot make a key out of and reports the failure only in the key itself. Both
// of these were stored as bindings, and neither can ever match a keystroke.
void TestShortcuts::garbageIsNotStoredAsABinding()
{
    // What Ctrl+A used to spell: a control character, rendered as a dangling
    // "Ctrl+".
    QVERIFY(ShortcutRegistry::normalise(QStringLiteral("Ctrl+") + QChar(1)).isEmpty());
    // What an arrow key used to spell: its decimal key code.
    QVERIFY(ShortcutRegistry::normalise(QStringLiteral("16777236")).isEmpty());

    // Ctrl++ ends in the same character a dangling modifier does and is a real
    // binding, so the check cannot simply be a trailing "+".
    QCOMPARE(ShortcutRegistry::normalise(QStringLiteral("Ctrl++")),
             QStringLiteral("Ctrl++"));
}

// The write path rejects an unmatchable sequence, but a release went out with
// the capture field that produced them, so the settings file on a real machine
// can already hold one. Reading has to refuse it too, or the action stays dead
// for exactly the people who hit the bug.
void TestShortcuts::garbageAlreadyInTheFileIsDropped()
{
    {
        QSettings written(iniPath(), QSettings::IniFormat);
        written.beginGroup(QStringLiteral("hotkeys"));
        // What Ctrl+A used to spell.
        written.setValue(QStringLiteral("mute"), QStringLiteral("Ctrl+") + QChar(1));
        // What an arrow key used to spell.
        written.setValue(QStringLiteral("fullscreen"), QStringLiteral("16777236"));
        // A sound override, which must not be touched.
        written.setValue(QStringLiteral("stop"), QStringLiteral("Ctrl+Q"));
        // A deliberate unbind, which must not be mistaken for garbage.
        written.setValue(QStringLiteral("play-pause"), QString());
        written.endGroup();
        written.sync();
    }

    ShortcutRegistry registry(iniPath());

    // The default comes back, rather than a binding no keystroke can match.
    QCOMPARE(registry.sequenceFor(QStringLiteral("mute")), QStringLiteral("M"));
    QCOMPARE(registry.sequenceFor(QStringLiteral("fullscreen")), QStringLiteral("F"));
    // And the row stops claiming the user chose it.
    for (const QVariant &entry : registry.model()) {
        const QVariantMap row = entry.toMap();
        const QString id = row.value(QStringLiteral("id")).toString();
        if (id == QStringLiteral("mute") || id == QStringLiteral("fullscreen"))
            QCOMPARE(row.value(QStringLiteral("isCustom")).toBool(), false);
    }

    // Neither of the two that were never garbage moves.
    QCOMPARE(registry.sequenceFor(QStringLiteral("stop")), QStringLiteral("Ctrl+Q"));
    QCOMPARE(registry.sequenceFor(QStringLiteral("play-pause")), QString());

    // A dropped entry does not shadow a later rebinding of the same action.
    registry.setSequence(QStringLiteral("mute"), QStringLiteral("Ctrl+Shift+M"));
    ShortcutRegistry reopened(iniPath());
    QCOMPARE(reopened.sequenceFor(QStringLiteral("mute")), QStringLiteral("Ctrl+Shift+M"));
}

QTEST_MAIN(TestShortcuts)
#include "tst_shortcuts.moc"
