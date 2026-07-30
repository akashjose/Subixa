// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

// The settings file's owner: the version key, migration, and pruning.
//
// Everything here works the same way -- write a raw ini with a bare QSettings,
// construct a SettingsService over the path, and assert on what survives. The
// service runs migration and pruning in its constructor, so construction *is*
// the operation under test.
//
// The cases worth having are the destructive ones' complements: what must
// survive. A prune that deletes too much looks identical to one that works
// until somebody's year of positions disappears, so most assertions below are
// about entries and keys still being there.

#include <QtTest>

#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>

#include "SettingsService.h"

namespace {

// A day short of the cutoff on either side, so the tests do not flake on the
// seconds a slow run adds.
qint64 staleStamp()
{
    return QDateTime::currentSecsSinceEpoch()
           - (SettingsService::MaxEntryAgeDays + 1) * qint64(86400);
}

qint64 freshStamp()
{
    return QDateTime::currentSecsSinceEpoch()
           - (SettingsService::MaxEntryAgeDays - 1) * qint64(86400);
}

}  // namespace

class TstSettingsService : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void aFreshFileGetsTheVersionKey();
    void aLegacyEntryIsStampedRatherThanPruned();
    void anEntryOlderThanAYearIsPruned();
    void aRecentEntryStaysWithItsStamp();
    void theCapKeepsTheNewestEntries();
    void preferredLanguageSurvivesThePrune();
    void preferencesGroupsAreNeverTouched();
    void aNewerSchemaIsLeftEntirelyAlone();

private:
    QString settingsFile() const
    {
        return m_dir.filePath(QStringLiteral("settings.ini"));
    }
    // A subgroup entry the way PlaybackHistory writes one, minus lastUsed
    // unless given: hash-shaped key, position, path.
    void writeEntry(QSettings &raw, const QString &group, const QString &name,
                    qint64 lastUsed = -1)
    {
        const QString key = group + QLatin1Char('/') + name;
        raw.setValue(key + QStringLiteral("/position"), 1234.0);
        raw.setValue(key + QStringLiteral("/path"),
                     QStringLiteral("/films/") + name + QStringLiteral(".mkv"));
        if (lastUsed >= 0)
            raw.setValue(key + QStringLiteral("/lastUsed"), lastUsed);
    }
    QTemporaryDir m_dir;
};

void TstSettingsService::init()
{
    QFile::remove(settingsFile());
}

void TstSettingsService::aFreshFileGetsTheVersionKey()
{
    { SettingsService service(settingsFile()); }

    QSettings raw(settingsFile(), QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("meta/schemaVersion")).toInt(),
             SettingsService::SchemaVersion);
}

void TstSettingsService::aLegacyEntryIsStampedRatherThanPruned()
{
    {
        QSettings raw(settingsFile(), QSettings::IniFormat);
        writeEntry(raw, QStringLiteral("resume"), QStringLiteral("abc123"));
    }

    { SettingsService service(settingsFile()); }

    // The entry predates lastUsed entirely. Dropping it would turn the schema
    // upgrade into data loss; it is stamped now and ages from here.
    QSettings raw(settingsFile(), QSettings::IniFormat);
    QVERIFY(raw.contains(QStringLiteral("resume/abc123/position")));
    QVERIFY(raw.value(QStringLiteral("resume/abc123/lastUsed")).toLongLong() > 0);
    QCOMPARE(raw.value(QStringLiteral("meta/schemaVersion")).toInt(),
             SettingsService::SchemaVersion);
}

void TstSettingsService::anEntryOlderThanAYearIsPruned()
{
    {
        QSettings raw(settingsFile(), QSettings::IniFormat);
        writeEntry(raw, QStringLiteral("resume"), QStringLiteral("old"), staleStamp());
        writeEntry(raw, QStringLiteral("subtitle"), QStringLiteral("old"), staleStamp());
    }

    { SettingsService service(settingsFile()); }

    QSettings raw(settingsFile(), QSettings::IniFormat);
    QVERIFY(!raw.contains(QStringLiteral("resume/old/position")));
    QVERIFY(!raw.contains(QStringLiteral("subtitle/old/position")));
}

void TstSettingsService::aRecentEntryStaysWithItsStamp()
{
    const qint64 stamp = freshStamp();
    {
        QSettings raw(settingsFile(), QSettings::IniFormat);
        writeEntry(raw, QStringLiteral("resume"), QStringLiteral("recent"), stamp);
    }

    { SettingsService service(settingsFile()); }

    // Still there, and the stamp was not rewritten -- pruning records nothing,
    // or an entry would refresh itself by surviving.
    QSettings raw(settingsFile(), QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("resume/recent/lastUsed")).toLongLong(), stamp);
}

void TstSettingsService::theCapKeepsTheNewestEntries()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    {
        QSettings raw(settingsFile(), QSettings::IniFormat);
        // Cap + 10 entries, oldest first: entry0 is the oldest.
        for (int i = 0; i < SettingsService::MaxEntriesPerGroup + 10; ++i) {
            writeEntry(raw, QStringLiteral("resume"),
                       QStringLiteral("entry%1").arg(i),
                       now - (SettingsService::MaxEntriesPerGroup + 10 - i) * 60);
        }
    }

    { SettingsService service(settingsFile()); }

    QSettings raw(settingsFile(), QSettings::IniFormat);
    raw.beginGroup(QStringLiteral("resume"));
    QCOMPARE(raw.childGroups().size(), SettingsService::MaxEntriesPerGroup);
    // The ten oldest went; the newest stayed.
    QVERIFY(!raw.contains(QStringLiteral("entry9/position")));
    QVERIFY(raw.contains(QStringLiteral("entry10/position")));
}

void TstSettingsService::preferredLanguageSurvivesThePrune()
{
    {
        QSettings raw(settingsFile(), QSettings::IniFormat);
        // The plain key beside the hashed subgroups. A prune walking childKeys
        // instead of childGroups deletes it silently -- this is the case that
        // fails if that ever happens.
        raw.setValue(QStringLiteral("subtitle/preferredLanguage"),
                     QStringLiteral("spa"));
        writeEntry(raw, QStringLiteral("subtitle"), QStringLiteral("old"),
                   staleStamp());
    }

    { SettingsService service(settingsFile()); }

    QSettings raw(settingsFile(), QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("subtitle/preferredLanguage")).toString(),
             QStringLiteral("spa"));
    QVERIFY(!raw.contains(QStringLiteral("subtitle/old/position")));
}

void TstSettingsService::preferencesGroupsAreNeverTouched()
{
    {
        QSettings raw(settingsFile(), QSettings::IniFormat);
        raw.setValue(QStringLiteral("ui/volume"), 80);
        raw.setValue(QStringLiteral("subtitleStyle/fontSize"), 55);
        raw.setValue(QStringLiteral("hotkeys/play-pause"), QStringLiteral("P"));
        raw.setValue(QStringLiteral("graphics/probe-x"), true);
        // A group this build has never heard of -- a newer build's, or another
        // tool's. Pruning iterates what it knows, so this survives untouched.
        raw.setValue(QStringLiteral("future/whatever/lastUsed"), qint64(1));
    }

    { SettingsService service(settingsFile()); }

    QSettings raw(settingsFile(), QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("ui/volume")).toInt(), 80);
    QCOMPARE(raw.value(QStringLiteral("subtitleStyle/fontSize")).toInt(), 55);
    QCOMPARE(raw.value(QStringLiteral("hotkeys/play-pause")).toString(),
             QStringLiteral("P"));
    QCOMPARE(raw.value(QStringLiteral("graphics/probe-x")).toBool(), true);
    QCOMPARE(raw.value(QStringLiteral("future/whatever/lastUsed")).toLongLong(),
             qint64(1));
}

void TstSettingsService::aNewerSchemaIsLeftEntirelyAlone()
{
    {
        QSettings raw(settingsFile(), QSettings::IniFormat);
        raw.setValue(QStringLiteral("meta/schemaVersion"),
                     SettingsService::SchemaVersion + 1);
        // Prunable on every rule this build knows -- and it must survive,
        // because a newer schema may key entries in ways this build would
        // misread as stale.
        writeEntry(raw, QStringLiteral("resume"), QStringLiteral("old"),
                   staleStamp());
    }

    { SettingsService service(settingsFile()); }

    QSettings raw(settingsFile(), QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("meta/schemaVersion")).toInt(),
             SettingsService::SchemaVersion + 1);
    QVERIFY(raw.contains(QStringLiteral("resume/old/position")));
}

QTEST_GUILESS_MAIN(TstSettingsService)
#include "tst_settingsservice.moc"
