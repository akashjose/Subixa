// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "ShortcutRegistry.h"

#include <QtCore/QHash>
#include <QtCore/QSettings>
#include <QtGui/QKeySequence>

namespace {

constexpr auto kGroup = "hotkeys";

}  // namespace

const QVector<ShortcutRegistry::Action> &ShortcutRegistry::actions()
{
    // Order is display order: the settings page groups by `category` and keeps
    // this sequence within each group.
    //
    // `worksWhileTyping` is true only where the binding carries a modifier a
    // TextField does not claim. Space, the arrows and the bare letters all have
    // to stay off while the search box has focus.
    static const QVector<Action> table = {
        // Playback
        {"play-pause",      "Play / pause",             "Playback", "Space",        false},
        {"play-pause-alt",  "Play / pause (alternate)", "Playback", "K",            false},
        {"stop",            "Stop",                     "Playback", "Ctrl+.",       true},
        {"speed-up",        "Speed up",                 "Playback", "]",            false},
        {"speed-down",      "Slow down",                "Playback", "[",            false},
        {"speed-reset",     "Normal speed",             "Playback", "Backspace",    false},
        {"next-file",       "Next file",                "Playback", ">",            false},
        {"previous-file",   "Previous file",            "Playback", "<",            false},

        // Seeking
        {"seek-forward",    "Forward 5 seconds",        "Seeking",  "Right",        false},
        {"seek-back",       "Back 5 seconds",           "Seeking",  "Left",         false},
        {"seek-forward-1",  "Forward 1 second",         "Seeking",  "Shift+Right",  false},
        {"seek-back-1",     "Back 1 second",            "Seeking",  "Shift+Left",   false},
        {"seek-forward-10", "Forward 10 seconds",       "Seeking",  "L",            false},
        {"seek-back-10",    "Back 10 seconds",          "Seeking",  "J",            false},
        {"cue-next",        "Next subtitle line",       "Seeking",  "Ctrl+Right",   true},
        {"cue-previous",    "Previous subtitle line",   "Seeking",  "Ctrl+Left",    true},
        {"chapter-next",    "Next chapter",             "Seeking",  "PgDown",       false},
        {"chapter-previous","Previous chapter",         "Seeking",  "PgUp",         false},

        // Audio
        {"volume-up",       "Volume up",                "Audio",    "Up",           false},
        {"volume-down",     "Volume down",              "Audio",    "Down",         false},
        {"mute",            "Mute",                     "Audio",    "M",            false},
        {"audio-delay-up",  "Audio later",              "Audio",    "Ctrl+Shift+Right", true},
        {"audio-delay-down","Audio earlier",            "Audio",    "Ctrl+Shift+Left",  true},

        // Subtitles
        {"subtitle-toggle", "Show / hide subtitles",    "Subtitles", "V",           false},
        {"sub-delay-up",    "Subtitles later",          "Subtitles", "Ctrl+]",      true},
        {"sub-delay-down",  "Subtitles earlier",        "Subtitles", "Ctrl+[",      true},
        {"sub-delay-reset", "Reset subtitle timing",    "Subtitles", "Ctrl+0",      true},
        {"sub-sync-here",   "Sync subtitles to this line", "Subtitles", "Ctrl+Shift+S", true},

        // Browser
        {"panel-toggle",    "Show / hide the browser",  "Browser",  "Tab",          false},
        {"panel-detach",    "Detach / dock the browser","Browser",  "Ctrl+D",       true},
        {"search-focus",    "Search subtitles",         "Browser",  "Ctrl+F",       true},
        {"follow-toggle",   "Follow the playing line",  "Browser",  "Ctrl+G",       true},
        {"row-larger",      "Larger subtitle text",     "Browser",  "Ctrl+=",       true},
        {"row-smaller",     "Smaller subtitle text",    "Browser",  "Ctrl+-",       true},
        {"copy-cue",        "Copy this line",           "Browser",  "Ctrl+C",       true},
        {"copy-cue-timed",  "Copy this line with its timestamp", "Browser", "Ctrl+Shift+C", true},
        {"export-track",    "Export this track",        "Browser",  "Ctrl+E",       true},

        // Loop
        {"loop-set-a",      "Set loop start",           "Loop",     "A",            false},
        {"loop-set-b",      "Set loop end",             "Loop",     "B",            false},
        {"loop-clear",      "Clear the loop",           "Loop",     "Shift+A",      false},
        {"loop-this-cue",   "Loop this subtitle line",  "Loop",     "R",            false},

        // Window
        {"fullscreen",      "Fullscreen",               "Window",   "F",            false},
        {"fullscreen-alt",  "Fullscreen (alternate)",   "Window",   "F11",          true},
        {"leave-fullscreen","Leave fullscreen",         "Window",   "Esc",          false},

        // Application
        {"open-file",       "Open a file",              "Application", "Ctrl+O",    true},
        {"open-subtitle",   "Open a subtitle file",     "Application", "Ctrl+Shift+O", true},
        {"screenshot",      "Screenshot",               "Application", "Ctrl+S",    true},
        {"settings",        "Settings",                 "Application", "Ctrl+,",    true},
        {"quit",            "Quit",                     "Application", "Ctrl+Q",    true},
    };
    return table;
}

ShortcutRegistry::ShortcutRegistry(QObject *parent)
    : QObject(parent), m_settings(std::make_unique<QSettings>())
{
    load();
}

ShortcutRegistry::ShortcutRegistry(const QString &settingsPath, QObject *parent)
    : QObject(parent),
      m_settings(std::make_unique<QSettings>(settingsPath, QSettings::IniFormat))
{
    load();
}

ShortcutRegistry::~ShortcutRegistry() = default;

void ShortcutRegistry::load()
{
    m_overrides.clear();
    m_settings->beginGroup(QLatin1String(kGroup));
    const QStringList keys = m_settings->childKeys();
    for (const QString &key : keys)
        m_overrides.insert(key, m_settings->value(key).toString());
    m_settings->endGroup();
}

QString ShortcutRegistry::normalise(const QString &sequence)
{
    if (sequence.isEmpty())
        return {};
    // Round-tripping through QKeySequence is what makes "ctrl+d" and "Ctrl+D"
    // one binding rather than two that silently shadow each other.
    const QKeySequence parsed(sequence, QKeySequence::PortableText);
    if (parsed.isEmpty())
        return {};
    return parsed.toString(QKeySequence::PortableText);
}

QString ShortcutRegistry::defaultSequenceFor(const QString &id) const
{
    for (const Action &action : actions()) {
        if (QLatin1String(action.id) == id)
            return normalise(QString::fromLatin1(action.defaultSequence));
    }
    return {};
}

QString ShortcutRegistry::sequenceFor(const QString &id) const
{
    const auto it = m_overrides.constFind(id);
    if (it != m_overrides.constEnd())
        return it.value();  // may legitimately be empty: deliberately unbound
    return defaultSequenceFor(id);
}

QString ShortcutRegistry::conflict(const QString &sequence, const QString &exceptId) const
{
    const QString wanted = normalise(sequence);
    if (wanted.isEmpty())
        return {};
    for (const Action &action : actions()) {
        const QString id = QString::fromLatin1(action.id);
        if (id == exceptId)
            continue;
        if (sequenceFor(id) == wanted)
            return QString::fromLatin1(action.label);
    }
    return {};
}

void ShortcutRegistry::setSequence(const QString &id, const QString &sequence)
{
    const QString wanted = normalise(sequence);

    m_settings->beginGroup(QLatin1String(kGroup));
    if (wanted == defaultSequenceFor(id)) {
        // Storing a copy of the default would mean a later change to the
        // default silently failed to reach anyone who had ever touched the row.
        m_settings->remove(id);
        m_overrides.remove(id);
    } else {
        m_settings->setValue(id, wanted);
        m_overrides.insert(id, wanted);
    }
    m_settings->endGroup();
    m_settings->sync();

    emit changed();
}

void ShortcutRegistry::resetSequence(const QString &id)
{
    m_settings->beginGroup(QLatin1String(kGroup));
    m_settings->remove(id);
    m_settings->endGroup();
    m_settings->sync();
    m_overrides.remove(id);
    emit changed();
}

void ShortcutRegistry::resetAll()
{
    m_settings->beginGroup(QLatin1String(kGroup));
    m_settings->remove(QString());
    m_settings->endGroup();
    m_settings->sync();
    m_overrides.clear();
    emit changed();
}

QVariantList ShortcutRegistry::model() const
{
    QVariantList out;
    out.reserve(actions().size());
    for (const Action &action : actions()) {
        const QString id = QString::fromLatin1(action.id);
        QVariantMap entry;
        entry[QStringLiteral("id")] = id;
        entry[QStringLiteral("label")] = QString::fromLatin1(action.label);
        entry[QStringLiteral("category")] = QString::fromLatin1(action.category);
        entry[QStringLiteral("sequence")] = sequenceFor(id);
        entry[QStringLiteral("defaultSequence")] = defaultSequenceFor(id);
        entry[QStringLiteral("worksWhileTyping")] = action.worksWhileTyping;
        entry[QStringLiteral("isCustom")] = m_overrides.contains(id);
        out.append(entry);
    }
    return out;
}
