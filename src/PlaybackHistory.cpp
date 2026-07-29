// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "PlaybackHistory.h"
#include "SettingsService.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>

namespace {

constexpr auto kGroup = "resume";
// Separate from kGroup on purpose: finishing a film clears its resume entry, and
// that must not also forget which track was being read.
constexpr auto kSubtitleGroup = "subtitle";
constexpr auto kPreferredLanguageKey = "subtitle/preferredLanguage";

}  // namespace

PlaybackHistory::PlaybackHistory(QObject *parent)
    : QObject(parent)
{
    // Borrow the store main() built, so this class reads a file that has
    // already been migrated and pruned. The fallback is what the service
    // would have opened anyway; it keeps a harness that builds no service --
    // and the QML tooling instantiating types speculatively -- working.
    if (auto *service = SettingsService::instance()) {
        m_settings = &service->store();
    } else {
        m_owned = std::make_unique<QSettings>();
        m_settings = m_owned.get();
    }
    startFlushTimer();
}

PlaybackHistory::PlaybackHistory(const QString &settingsPath, QObject *parent)
    : QObject(parent),
      m_owned(std::make_unique<QSettings>(settingsPath, QSettings::IniFormat))
{
    m_settings = m_owned.get();
    startFlushTimer();
}

PlaybackHistory::~PlaybackHistory()
{
    // The last stretch of playback is held rather than written, and a clean exit
    // is exactly when it has to reach the file.
    flush();
}

void PlaybackHistory::startFlushTimer()
{
    // The app's one deliberate periodic write, and deliberately not the resume
    // tick. The QML `Settings` groups (ui, subtitleStyle) share this file, and
    // before this they had no flush anyone here owned -- they went out on
    // whatever sync() the resume tick happened to do. This is now their flush,
    // whatever remember() does.
    //
    // Measured, so it is not oversold: they do not actually depend on it. On Qt
    // 6.12 a QML `Settings` group reaches disk about half a second after a
    // property changes with nobody calling sync() at all -- QQmlSettings has its
    // own write timer, and QSettings posts itself an update after every setValue
    // and syncs when the event loop next turns. So this is one owned path to
    // disk in place of two borrowed ones, not a fix for a live bug.
    //
    // Costing nothing when nothing changed is what makes a 30-second period
    // reasonable: sync() on a file with no pending values is a stat, not a
    // rewrite -- it does not even recreate the file if it has been deleted -- so
    // an idle player writes nothing at all.
    m_flushTimer.setInterval(FlushIntervalMs);
    // Nothing here needs the wakeup to be punctual, and a coarse timer lets the
    // kernel group it with others.
    m_flushTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_flushTimer, &QTimer::timeout, this, &PlaybackHistory::flush);
    m_flushTimer.start();
}

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

    if (key != m_pendingKey) {
        // Another file. Where the outgoing one got to is final now, and this is
        // the last moment it can be saved -- the fields below are about to be
        // overwritten.
        flush();
        m_pendingKey = key;
        m_pendingPath = path;
        m_storedPosition = -1.0;
    }

    if (!worthRemembering(position, duration)) {
        resetPending();
        // Clearing rather than leaving the old value is what makes finishing a
        // film reset it: the next open starts from the beginning. Once, though.
        // The tick behind this keeps arriving for as long as the file is open,
        // and this saves the lookup and the sync() on every one of them. Only
        // that: QSettings does not dirty the file for the removal of a key it
        // never had, so the guard is a cost, not a correctness, decision.
        if (m_settings->contains(key + QStringLiteral("/position"))) {
            m_settings->remove(key);
            flush();
        }
        return;
    }

    m_pendingPosition = position;
    m_pendingDuration = duration;
    m_hasPending = true;

    // Held until it has something new to say. Five seconds of progress is not
    // worth an entry; PositionWriteStep of it is. Held means held in these
    // members: handing it to QSettings is already the write, because QSettings
    // syncs itself the next time the event loop turns.
    if (m_storedPosition < 0.0
        || qAbs(position - m_storedPosition) >= PositionWriteStep) {
        storePending();
    }
}

void PlaybackHistory::storePending()
{
    if (!m_hasPending || !m_settings)
        return;
    m_hasPending = false;

    // A position that says what the entry already says is not worth storing, and
    // QSettings will not notice on its own: setValue marks the file dirty whether
    // or not the value changed, and the next sync then serialises all of it.
    if (qFuzzyCompare(m_pendingPosition, m_storedPosition))
        return;

    m_settings->setValue(m_pendingKey + QStringLiteral("/position"), m_pendingPosition);
    m_settings->setValue(m_pendingKey + QStringLiteral("/duration"), m_pendingDuration);
    // Not read back -- it is here so the settings file can be read by a person.
    m_settings->setValue(m_pendingKey + QStringLiteral("/path"),
                         QFileInfo(m_pendingPath).absoluteFilePath());
    // What SettingsService ages the entry by. Strictly inside this branch: a
    // tick that says nothing new must keep saying nothing to the file, and the
    // write-cadence tests watch the file to hold it to that.
    m_settings->setValue(m_pendingKey + QStringLiteral("/lastUsed"),
                         QDateTime::currentSecsSinceEpoch());
    m_storedPosition = m_pendingPosition;
}

void PlaybackHistory::resetPending()
{
    m_hasPending = false;
    m_storedPosition = -1.0;
}

void PlaybackHistory::flush()
{
    if (!m_settings)
        return;
    storePending();
    m_settings->sync();
}

double PlaybackHistory::resumeFor(const QString &path) const
{
    if (path.isEmpty() || !m_settings)
        return -1.0;

    const QString key = QString::fromLatin1(kGroup) + QLatin1Char('/') + keyFor(path);
    // A position that is still held is what this file is at, whether or not it
    // has reached disk. Reading around it would mean reopening the file that is
    // playing resumed at whatever the last write happened to catch.
    if (m_hasPending && key == m_pendingKey)
        return m_pendingPosition;

    const QVariant stored = m_settings->value(key + QStringLiteral("/position"));
    if (!stored.isValid())
        return -1.0;

    bool ok = false;
    const double position = stored.toDouble(&ok);
    return (ok && position > 0.0) ? position : -1.0;
}

void PlaybackHistory::rememberSubtitle(const QString &path, int streamIndex,
                                       const QString &sidecarPath,
                                       const QString &language)
{
    if (path.isEmpty() || !m_settings)
        return;

    const QString key =
        QString::fromLatin1(kSubtitleGroup) + QLatin1Char('/') + keyFor(path);

    m_settings->setValue(key + QStringLiteral("/streamIndex"), streamIndex);
    m_settings->setValue(key + QStringLiteral("/sidecarPath"),
                         sidecarPath.isEmpty()
                             ? QString()
                             : QFileInfo(sidecarPath).absoluteFilePath());
    m_settings->setValue(key + QStringLiteral("/language"), language);
    m_settings->setValue(key + QStringLiteral("/path"),
                         QFileInfo(path).absoluteFilePath());
    // What SettingsService ages the entry by.
    m_settings->setValue(key + QStringLiteral("/lastUsed"),
                         QDateTime::currentSecsSinceEpoch());

    // The fallback for files with no entry of their own. Only a real language
    // tag is worth keeping -- "und" would match half a container.
    if (!language.isEmpty() && language != QLatin1String("und"))
        m_settings->setValue(QString::fromLatin1(kPreferredLanguageKey), language);

    // Written through: this follows a deliberate choice of track, which happens
    // a handful of times in a session rather than every few seconds.
    flush();
}

QVariantMap PlaybackHistory::subtitleFor(const QString &path) const
{
    QVariantMap out;
    if (path.isEmpty() || !m_settings)
        return out;

    const QString key =
        QString::fromLatin1(kSubtitleGroup) + QLatin1Char('/') + keyFor(path);
    const QVariant streamIndex = m_settings->value(key + QStringLiteral("/streamIndex"));
    if (!streamIndex.isValid())
        return out;

    out[QStringLiteral("streamIndex")] = streamIndex.toInt();
    out[QStringLiteral("sidecarPath")] =
        m_settings->value(key + QStringLiteral("/sidecarPath")).toString();
    out[QStringLiteral("language")] =
        m_settings->value(key + QStringLiteral("/language")).toString();
    return out;
}

QString PlaybackHistory::preferredLanguage() const
{
    if (!m_settings)
        return {};
    return m_settings->value(QString::fromLatin1(kPreferredLanguageKey)).toString();
}

void PlaybackHistory::forget(const QString &path)
{
    if (path.isEmpty() || !m_settings)
        return;
    const QString key = QString::fromLatin1(kGroup) + QLatin1Char('/') + keyFor(path);
    // A held position for this file would be written back by the next flush,
    // quietly undoing the forget.
    if (key == m_pendingKey)
        resetPending();
    m_settings->remove(key);
    m_settings->remove(QString::fromLatin1(kSubtitleGroup) + QLatin1Char('/')
                       + keyFor(path));
    flush();
}
