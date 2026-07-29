// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtCore/QString>

// The owner of the settings file.
//
// Five writers used to open the same file independently -- PlaybackHistory,
// ShortcutRegistry, a stack QSettings in GraphicsSetup, and the two QML
// `Settings` blocks in Main.qml -- which worked, because Qt shares one
// representation of the file between every QSettings on a path, but left the
// file itself with no owner: nothing said what version its layout was, and
// nothing could safely prune it, because there was no moment when nobody else
// might be reading.
//
// This class is that owner. main() constructs it before anything else touches
// settings, so the C++ writers borrow its store and the QML groups open a file
// whose layout has already been migrated. It is deliberately *not* exposed to
// QML and holds no preferences of its own: the `Settings` blocks in Main.qml
// stay, measured as self-syncing fine on Qt 6.12. What QML must never see is a
// file mid-migration, which construction order guarantees.
//
// The QML-created PlaybackHistory and ShortcutRegistry find it through
// instance() -- they are declared declaratively in Main.qml, so constructor
// injection has no caller to inject from. A default-constructed fallback keeps
// any harness that never builds a service working, on the file Qt would have
// picked anyway.
class SettingsService : public QObject
{
    Q_OBJECT

public:
    explicit SettingsService(QObject *parent = nullptr);
    // Tests pass their own ini file, so they never touch real user settings.
    explicit SettingsService(const QString &settingsPath, QObject *parent = nullptr);
    ~SettingsService() override;

    // The service main() constructed, or nullptr when none exists -- the test
    // suites that exercise PlaybackHistory and ShortcutRegistry directly build
    // no service, and both fall back to owning their store.
    static SettingsService *instance();

    QSettings &store();

    // The layout this build writes, stored as meta/schemaVersion. Version 1 is
    // the groups as they stand -- meta, ui, subtitleStyle, resume, subtitle,
    // hotkeys, graphics -- plus a lastUsed epoch-seconds stamp on every
    // per-file entry, which is what pruning ages against. A file whose stored
    // version is *newer* than this is left entirely alone: its keys still read
    // (the format is additive), but pruning under a schema this build does not
    // know could destroy entries a newer build keys differently.
    static constexpr int SchemaVersion = 1;

    // The prune policy, applied to the per-file entries of [resume] and
    // [subtitle] only -- preferences are never pruned. A film untouched for a
    // year is not being come back to, and past the cap the oldest go first.
    // Deliberately generous: this exists to stop unbounded growth, not to
    // economise, and an entry is a few hundred bytes.
    static constexpr qint64 MaxEntryAgeDays = 365;
    static constexpr int MaxEntriesPerGroup = 500;

private:
    // Migration and pruning, run by both constructors before any borrower
    // exists -- which is the only moment it is safe to delete entries, because
    // nothing can be holding a copy it might write back.
    void initialise();
    void prune();

    QSettings m_settings;
};
