// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "SettingsService.h"

#include <QtCore/QDateTime>
#include <QtCore/QList>
#include <QtCore/QStringList>

#include <algorithm>
#include <utility>

namespace {

// Set by the first service constructed, cleared by its destructor. Not a
// singleton in the create-on-demand sense: main() owns the one instance on its
// stack, and anything asking before main() built it gets nullptr and falls
// back to owning a store of its own.
SettingsService *s_instance = nullptr;

constexpr auto kSchemaKey = "meta/schemaVersion";

// The groups holding one subgroup per file, which is what can grow without
// bound. Everything else in the file is a preference: a fixed set of keys that
// pruning must never touch.
constexpr const char *kPrunedGroups[] = {"resume", "subtitle", "audio"};

}  // namespace

SettingsService::SettingsService(QObject *parent)
    : QObject(parent)
{
    if (!s_instance)
        s_instance = this;
    initialise();
}

SettingsService::SettingsService(const QString &settingsPath, QObject *parent)
    : QObject(parent), m_settings(settingsPath, QSettings::IniFormat)
{
    if (!s_instance)
        s_instance = this;
    initialise();
}

SettingsService::~SettingsService()
{
    // Everything borrowed from this store has flushed by now: main() declares
    // the service before MpvEngine and the QML engine, so destruction reaches
    // it last, after ~PlaybackHistory and ~ShortcutRegistry have run.
    m_settings.sync();
    if (s_instance == this)
        s_instance = nullptr;
}

SettingsService *SettingsService::instance()
{
    return s_instance;
}

QSettings &SettingsService::store()
{
    return m_settings;
}

void SettingsService::initialise()
{
    const int stored = m_settings.value(QLatin1String(kSchemaKey), 0).toInt();
    if (stored > SchemaVersion) {
        // Written by a newer build. Read it, change what the user changes, but
        // do not migrate, prune, or wind the version key backwards -- see the
        // header. Downgrades are rare enough that a warning is proportionate.
        qWarning("settings: file has schema %d, newer than this build's %d; "
                 "leaving its layout alone", stored, SchemaVersion);
        return;
    }

    // Version 0 is every file from before the key existed. Its entries lack
    // lastUsed, and prune() below stamps them rather than dropping them --
    // punishing an upgrade by forgetting a year of positions is exactly the
    // wrong first impression for a version key to make. So the v0 -> v1
    // migration *is* that stamp pass, and all that is left here is the key.
    // Only written when it moves: setValue dirties the file whether or not the
    // value changed, and the next sync would then rewrite all of it.
    if (stored < SchemaVersion)
        m_settings.setValue(QLatin1String(kSchemaKey), SchemaVersion);

    prune();
    m_settings.sync();
}

void SettingsService::prune()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 oldestKept = now - MaxEntryAgeDays * qint64(86400);

    for (const char *groupName : kPrunedGroups) {
        m_settings.beginGroup(QLatin1String(groupName));

        // childGroups, not childKeys: subtitle/preferredLanguage is a plain
        // key sitting beside the hashed subgroups, and walking keys instead of
        // groups would silently delete it. Pinned by the test suite.
        const QStringList entries = m_settings.childGroups();
        QList<std::pair<qint64, QString>> kept;
        kept.reserve(entries.size());

        for (const QString &entry : entries) {
            bool ok = false;
            qint64 when =
                m_settings.value(entry + QStringLiteral("/lastUsed")).toLongLong(&ok);
            if (!ok || when <= 0) {
                // No stamp: the entry predates the schema, or the value is
                // garbage. Stamp it now and age it from here -- conservative,
                // because dropping it would turn a schema upgrade into data
                // loss.
                when = now;
                m_settings.setValue(entry + QStringLiteral("/lastUsed"), when);
            }
            if (when < oldestKept) {
                m_settings.remove(entry);
                continue;
            }
            kept.emplace_back(when, entry);
        }

        // Age alone does not bound the file -- a heavy year of viewing is
        // still thousands of entries -- so past the cap, the oldest go.
        if (kept.size() > MaxEntriesPerGroup) {
            std::sort(kept.begin(), kept.end());
            const auto excess = kept.size() - MaxEntriesPerGroup;
            for (qsizetype i = 0; i < excess; ++i)
                m_settings.remove(kept.at(i).second);
        }

        m_settings.endGroup();
    }
}
