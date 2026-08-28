// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtCore/QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QSettings;

// Every keyboard action in the player, its default binding, and whatever the
// user has rebound it to.
//
// This replaces 125 lines of hardcoded Shortcut elements in Main.qml, each
// repeating the same `enabled: !root.typing` guard. That shape could express a
// fixed set of bindings and nothing else: remapping was impossible, a menu could
// not show a shortcut without the string being written out again by hand next to
// the label, and there was no way to discover that two things wanted the same
// key.
//
// One table, three consumers: the Shortcut instances themselves, the shortcut
// column in menus and tooltips, and the remapping page in Settings.
class ShortcutRegistry : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // A property rather than only an invokable, because QML has to re-read the
    // table when a binding changes. `model()` alone is a one-shot call: the
    // Shortcut repeater and the settings page both evaluated it once and then
    // never again, so a rebind reached neither until the next launch.
    Q_PROPERTY(QVariantList model READ model NOTIFY changed)

    // True while the settings page is recording a keystroke. The application's
    // own Shortcut objects are gated on this: without it, pressing Ctrl+S to
    // rebind something fires the screenshot action and the capture never sees
    // the key.
    Q_PROPERTY(bool capturing READ capturing WRITE setCapturing NOTIFY capturingChanged)

public:
    explicit ShortcutRegistry(QObject *parent = nullptr);
    // Tests pass their own ini file so they never touch real user settings.
    explicit ShortcutRegistry(const QString &settingsPath, QObject *parent = nullptr);
    ~ShortcutRegistry() override;

    struct Action
    {
        const char *id;
        const char *label;
        const char *category;
        const char *defaultSequence;
        // A handful of bindings must keep working while the search box has
        // focus -- Ctrl+F, Escape, the ones with a modifier that a TextField
        // does not claim. The rest are plain letters and would otherwise turn
        // typing "film" into fullscreen-then-mute.
        bool worksWhileTyping;
    };

    // The whole table, in display order. Static so the settings page and the
    // Shortcut repeater cannot drift apart.
    static const QVector<Action> &actions();

    // { id, label, category, sequence, defaultSequence, worksWhileTyping,
    //   isCustom } per action, for QML to repeat over.
    QVariantList model() const;

    bool capturing() const { return m_capturing; }
    void setCapturing(bool capturing);

    // The binding in force, which is the override when there is one and the
    // default otherwise. An empty string means deliberately unbound.
    Q_INVOKABLE QString sequenceFor(const QString &id) const;
    Q_INVOKABLE QString defaultSequenceFor(const QString &id) const;

    // The label of whatever already owns `sequence`, or an empty string when it
    // is free. `exceptId` is the action being edited, which cannot conflict with
    // itself.
    Q_INVOKABLE QString conflict(const QString &sequence, const QString &exceptId) const;

    // Stores an override. An empty sequence unbinds the action; a sequence equal
    // to the default clears the override rather than storing a copy of it.
    Q_INVOKABLE void setSequence(const QString &id, const QString &sequence);
    Q_INVOKABLE void resetSequence(const QString &id);
    Q_INVOKABLE void resetAll();

    // Normalises whatever a key-capture produced into the spelling QKeySequence
    // will accept and compare -- "ctrl+d" and "Ctrl+D" must not be two bindings.
    Q_INVOKABLE static QString normalise(const QString &sequence);

    // The binding a key press describes, built from the event's key and
    // modifiers rather than from its text.
    //
    // `event.text` cannot express this: with Ctrl held it is the control
    // character, so Ctrl+A arrived as "Ctrl+\x01" and QKeySequence rendered
    // that as a dangling "Ctrl+" that no keystroke can ever match; and for the
    // arrows and the function keys it is empty, so the sequence became the
    // decimal key code, which parses to nothing and silently unbound the
    // action. The key code and the modifier flags say exactly what was pressed.
    //
    // Empty for a bare modifier, or for anything that does not describe a key.
    Q_INVOKABLE static QString sequenceFromEvent(int key, int modifiers);

signals:
    void changed();
    void capturingChanged();

private:
    void load();

    // Borrowed from SettingsService when main() built one, owned otherwise.
    // Same shape as PlaybackHistory, for the same reason: QML instantiates
    // this class, so there is no caller to inject the store.
    QSettings *m_settings = nullptr;
    std::unique_ptr<QSettings> m_owned;
    QHash<QString, QString> m_overrides;
    bool m_capturing = false;
};
