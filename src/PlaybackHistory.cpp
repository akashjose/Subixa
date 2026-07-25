#include "PlaybackHistory.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>

namespace {

constexpr auto kGroup = "resume";

}  // namespace

PlaybackHistory::PlaybackHistory(QObject *parent)
    : QObject(parent), m_settings(std::make_unique<QSettings>())
{
}

PlaybackHistory::PlaybackHistory(const QString &settingsPath, QObject *parent)
    : QObject(parent),
      m_settings(std::make_unique<QSettings>(settingsPath, QSettings::IniFormat))
{
}

PlaybackHistory::~PlaybackHistory() = default;

bool PlaybackHistory::worthRemembering(double position, double duration)
{
    if (duration <= 0.0 || position <= 0.0)
        return false;
    if (duration < MinimumDuration)
        return false;
    if (position < MinimumPosition)
        return false;
    // Finished, near enough. Remembering here would resume somebody into the
    // closing credits every time they reopened the file.
    if (position > duration - EndMargin)
        return false;
    return true;
}

QString PlaybackHistory::keyFor(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    return QString::fromLatin1(
        QCryptographicHash::hash(absolute.toUtf8(), QCryptographicHash::Sha1).toHex());
}

void PlaybackHistory::remember(const QString &path, double position, double duration)
{
    if (path.isEmpty() || !m_settings)
        return;

    const QString key = QString::fromLatin1(kGroup) + QLatin1Char('/') + keyFor(path);

    if (!worthRemembering(position, duration)) {
        // Clearing rather than leaving the old value is what makes finishing a
        // film reset it: the next open starts from the beginning.
        m_settings->remove(key);
        m_settings->sync();
        return;
    }

    m_settings->setValue(key + QStringLiteral("/position"), position);
    m_settings->setValue(key + QStringLiteral("/duration"), duration);
    // Not read back -- it is here so the settings file can be read by a person.
    m_settings->setValue(key + QStringLiteral("/path"),
                         QFileInfo(path).absoluteFilePath());
    m_settings->sync();
}

double PlaybackHistory::resumeFor(const QString &path) const
{
    if (path.isEmpty() || !m_settings)
        return -1.0;

    const QString key = QString::fromLatin1(kGroup) + QLatin1Char('/') + keyFor(path)
                        + QStringLiteral("/position");
    const QVariant stored = m_settings->value(key);
    if (!stored.isValid())
        return -1.0;

    bool ok = false;
    const double position = stored.toDouble(&ok);
    return (ok && position > 0.0) ? position : -1.0;
}

void PlaybackHistory::forget(const QString &path)
{
    if (path.isEmpty() || !m_settings)
        return;
    m_settings->remove(QString::fromLatin1(kGroup) + QLatin1Char('/') + keyFor(path));
    m_settings->sync();
}
