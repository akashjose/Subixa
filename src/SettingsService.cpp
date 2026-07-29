// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "SettingsService.h"

namespace {

// Set by the first service constructed, cleared by its destructor. Not a
// singleton in the create-on-demand sense: main() owns the one instance on its
// stack, and anything asking before main() built it gets nullptr and falls
// back to owning a store of its own.
SettingsService *s_instance = nullptr;

}  // namespace

SettingsService::SettingsService(QObject *parent)
    : QObject(parent)
{
    if (!s_instance)
        s_instance = this;
}

SettingsService::SettingsService(const QString &settingsPath, QObject *parent)
    : QObject(parent), m_settings(settingsPath, QSettings::IniFormat)
{
    if (!s_instance)
        s_instance = this;
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
