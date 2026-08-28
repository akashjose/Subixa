// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "ShortcutRegistry.h"
#include "SettingsService.h"

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
        {"audio-cycle",     "Next audio track",         "Audio",    "Ctrl+A",       true},

        // Subtitles
        {"subtitle-toggle", "Show / hide subtitles",    "Subtitles", "V",           false},
        {"subtitle-cycle",  "Next subtitle track",      "Subtitles", "Shift+V",     false},
        {"sub-delay-up",    "Subtitles later",          "Subtitles", "Ctrl+]",      true},
        {"sub-delay-down",  "Subtitles earlier",        "Subtitles", "Ctrl+[",      true},
        // Ctrl+0 until the zoom presets took Ctrl+0..4 as a set.
        {"sub-delay-reset", "Reset subtitle timing",    "Subtitles", "Ctrl+Shift+0", true},
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
        {"leave-fullscreen","Leave fullscreen, or minimise", "Window", "Esc",       false},
        // Percentages of the video's own size: 100% is one video pixel per
        // screen pixel.
        {"zoom-half",       "Picture at 50%",           "Window",   "Ctrl+1",       true},
        {"zoom-one",        "Picture at 100%",          "Window",   "Ctrl+2",       true},
        {"zoom-one-half",   "Picture at 150%",          "Window",   "Ctrl+3",       true},
        {"zoom-double",     "Picture at 200%",          "Window",   "Ctrl+4",       true},
        {"zoom-default",    "Picture at your default size", "Window", "Ctrl+0",     true},
        {"zoom-save",       "Save this picture size as your default", "Window", "Ctrl+Shift+1", true},

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
    : QObject(parent)
{
    // Borrow the store main() built; own one only when no service exists.
    // See PlaybackHistory's default constructor -- the two must not diverge.
    if (auto *service = SettingsService::instance()) {
        m_settings = &service->store();
    } else {
        m_owned = std::make_unique<QSettings>();
        m_settings = m_owned.get();
    }
    load();
}

ShortcutRegistry::ShortcutRegistry(const QString &settingsPath, QObject *parent)
    : QObject(parent),
      m_owned(std::make_unique<QSettings>(settingsPath, QSettings::IniFormat))
{
    m_settings = m_owned.get();
    load();
}

ShortcutRegistry::~ShortcutRegistry() = default;

void ShortcutRegistry::load()
{
    m_overrides.clear();
    m_settings->beginGroup(QLatin1String(kGroup));
    const QStringList keys = m_settings->childKeys();
    for (const QString &key : keys) {
        const QString stored = m_settings->value(key).toString();
        // Empty is the deliberate unbind and must survive as it is: normalising
        // it would erase the difference between "no key" and "the default key".
        if (stored.isEmpty()) {
            m_overrides.insert(key, stored);
            continue;
        }
        // The write path normalises, so anything here that does not survive it
        // was written by an older build -- the capture field spelled the
        // sequence from the event text and stored "Ctrl+" for Ctrl+A and a bare
        // decimal key code for the arrows. Those match no keystroke, so the
        // action was silently dead and the settings row showed the garbage.
        //
        // Dropping the entry restores the default. Storing an empty string
        // would not: that reads as a deliberate unbind, so a binding the user
        // never asked to lose would stay lost.
        const QString sequence = normalise(stored);
        if (sequence.isEmpty())
            continue;
        m_overrides.insert(key, sequence);
    }
    m_settings->endGroup();
}

QString ShortcutRegistry::normalise(const QString &sequence)
{
    if (sequence.isEmpty())
        return {};
    // Round-tripping through QKeySequence is what makes "ctrl+d" and "Ctrl+D"
    // one binding rather than two that silently shadow each other.
    const QKeySequence parsed(sequence, QKeySequence::PortableText);
    if (parsed.isEmpty() || parsed.count() != 1)
        return {};

    // isEmpty() is not the check it looks like: QKeySequence accepts text it
    // cannot make a key out of and reports the failure only in the key itself.
    // "Ctrl+\x01" comes back as Key 1 and renders as a dangling "Ctrl+", and a
    // decimal key code comes back as Key_unknown and renders as nothing. Both
    // were stored as bindings, and both are unmatchable. Qt::Key_Space is the
    // lowest real key, so anything below it is not one.
    const int key = parsed[0].key();
    if (key == Qt::Key_unknown || key < Qt::Key_Space)
        return {};

    return parsed.toString(QKeySequence::PortableText);
}

QString ShortcutRegistry::sequenceFromEvent(int key, int modifiers)
{
    switch (key) {
    // A modifier on its own is not a binding.
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_unknown:
        return {};
    default:
        break;
    }
    if (key < Qt::Key_Space)
        return {};

    // Only the four modifiers a binding can carry. The keypad and group
    // switches ride along on ordinary presses and would make Ctrl+1 off the
    // numeric pad a different binding from Ctrl+1 along the top row.
    const auto wanted = Qt::KeyboardModifiers(modifiers)
                        & (Qt::ControlModifier | Qt::AltModifier
                           | Qt::ShiftModifier | Qt::MetaModifier);

    const QKeySequence sequence{QKeyCombination(wanted, Qt::Key(key))};
    return sequence.toString(QKeySequence::PortableText);
}

void ShortcutRegistry::setCapturing(bool capturing)
{
    if (m_capturing == capturing)
        return;
    m_capturing = capturing;
    emit capturingChanged();
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
