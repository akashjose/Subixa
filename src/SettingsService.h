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

private:
    QSettings m_settings;
};
