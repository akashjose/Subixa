#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QSettings;

// Remembers where playback got to, per file, so reopening a film resumes rather
// than restarting.
//
// The policy lives here rather than in QML, and is deliberately conservative:
// resuming somebody two minutes into a film they finished last week is worse
// than not resuming at all. An entry that stops being worth keeping is deleted
// rather than left to go stale, so resumeFor() can stay a plain lookup.
class PlaybackHistory : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit PlaybackHistory(QObject *parent = nullptr);
    // Tests pass their own ini file, so they never touch real user settings.
    explicit PlaybackHistory(const QString &settingsPath, QObject *parent = nullptr);
    ~PlaybackHistory() override;

    // Stores `position` for `path`, or clears the entry when the position is not
    // worth returning to. Safe to call on every tick.
    Q_INVOKABLE void remember(const QString &path, double position, double duration);

    // Seconds to resume at, or -1 when there is nothing worth resuming.
    Q_INVOKABLE double resumeFor(const QString &path) const;

    Q_INVOKABLE void forget(const QString &path);

    // Pure policy, static so it can be tested without touching storage.
    static bool worthRemembering(double position, double duration);

    // Anything shorter is a clip, not something you come back to.
    static constexpr double MinimumDuration = 120.0;
    // Below this you have barely started; restarting costs nothing.
    static constexpr double MinimumPosition = 30.0;
    // Within this of the end counts as finished -- next time should start over.
    static constexpr double EndMargin = 60.0;

private:
    // Paths cannot be keys directly: QSettings reads '/' as a group separator, so
    // "/home/akash/film.mkv" would silently become nested groups. The real path is
    // stored alongside the hash so the file stays readable by a human.
    static QString keyFor(const QString &path);

    std::unique_ptr<QSettings> m_settings;
};
