#include "Playlist.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>

#include <algorithm>

namespace {

// Video first, then the audio formats worth opening in a player built around
// subtitles. Matches the file dialog's filter, because both come from here.
const char *const kMediaExtensions[] = {
    "mkv", "mp4", "avi", "mov", "webm", "m4v", "ts", "m2ts", "mpg", "mpeg",
    "wmv", "flv", "ogv", "flac", "mp3", "opus", "m4a", "aac", "wav", "ogg",
};

// "ep2" before "ep10", which plain string order gets backwards -- runs of digits
// compare as numbers, everything else case-insensitively as text.
//
// By hand rather than with QCollator's numeric mode: that depends on ICU and on
// the runtime locale, and here (a C.UTF-8 session) it silently sorted E10 ahead
// of E2 with no warning. Nobody's episodes should reorder themselves because
// LANG changed, so the ordering is ours and deterministic.
bool lessThanNaturally(const QString &a, const QString &b)
{
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        const QChar ca = a.at(i);
        const QChar cb = b.at(j);

        if (ca.isDigit() && cb.isDigit()) {
            // Whole runs, so 2 < 10, and leading zeros do not change the value:
            // "ep02" and "ep2" are the same episode to a person.
            qsizetype ia = i;
            qsizetype ib = j;
            while (ia < a.size() && a.at(ia).isDigit())
                ++ia;
            while (ib < b.size() && b.at(ib).isDigit())
                ++ib;

            const QStringView da = QStringView(a).mid(i, ia - i);
            const QStringView db = QStringView(b).mid(j, ib - j);

            bool okA = false;
            bool okB = false;
            const qulonglong va = da.toULongLong(&okA);
            const qulonglong vb = db.toULongLong(&okB);
            if (okA && okB) {
                if (va != vb)
                    return va < vb;
            } else if (da != db) {
                // Absurdly long digit runs: fall back to comparing them as text
                // rather than pretending they are numbers.
                return da < db;
            }
            i = ia;
            j = ib;
            continue;
        }

        const QChar la = ca.toCaseFolded();
        const QChar lb = cb.toCaseFolded();
        if (la != lb)
            return la < lb;
        ++i;
        ++j;
    }
    // One is a prefix of the other, or they differ only in case; the shorter
    // comes first, and case decides the last tie so the order is total.
    if (a.size() != b.size())
        return a.size() < b.size();
    return a < b;
}

}  // namespace

Playlist::Playlist(QObject *parent) : QObject(parent) {}

bool Playlist::isMediaFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const char *ext : kMediaExtensions) {
        if (suffix == QLatin1String(ext))
            return true;
    }
    return false;
}

QStringList Playlist::dialogNameFilters()
{
    QStringList patterns;
    patterns.reserve(std::size(kMediaExtensions));
    for (const char *ext : kMediaExtensions)
        patterns << QStringLiteral("*.%1").arg(QLatin1String(ext));

    return {QStringLiteral("Media files (%1)").arg(patterns.join(QLatin1Char(' '))),
            QStringLiteral("All files (*)")};
}

QString Playlist::currentPath() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_files.size())
        return {};
    return m_files.at(m_currentIndex);
}

void Playlist::openFolderOf(const QString &path)
{
    if (path.isEmpty()) {
        clear();
        return;
    }

    const QFileInfo info(path);
    const QString absolute = info.absoluteFilePath();

    QStringList found;
    const QFileInfoList entries =
        info.absoluteDir().entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (isMediaFile(entry.fileName()))
            found << entry.absoluteFilePath();
    }

    std::sort(found.begin(), found.end(), [](const QString &a, const QString &b) {
        return lessThanNaturally(QFileInfo(a).fileName(), QFileInfo(b).fileName());
    });

    // The file being opened is always in the queue, even when the folder scan
    // did not turn it up -- an extension nobody listed, or something mpv can
    // play that this does not recognise. Refusing to queue the file that is
    // playing would be absurd.
    if (!found.contains(absolute))
        found.prepend(absolute);

    replaceFiles(found, found.indexOf(absolute));
}

void Playlist::setFiles(const QStringList &paths)
{
    QStringList kept;
    kept.reserve(paths.size());
    for (const QString &path : paths) {
        if (path.isEmpty() || !isMediaFile(path))
            continue;
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (!kept.contains(absolute))
            kept << absolute;
    }

    replaceFiles(kept, kept.isEmpty() ? -1 : 0);
}

bool Playlist::contains(const QString &path) const
{
    if (path.isEmpty())
        return false;
    return m_files.contains(QFileInfo(path).absoluteFilePath());
}

void Playlist::setCurrentPath(const QString &path)
{
    if (path.isEmpty())
        return;
    const int index = m_files.indexOf(QFileInfo(path).absoluteFilePath());
    if (index >= 0)
        setCurrentIndex(index);
}

QString Playlist::next()
{
    if (!hasNext())
        return {};
    setCurrentIndex(m_currentIndex + 1);
    return currentPath();
}

QString Playlist::previous()
{
    if (!hasPrevious())
        return {};
    setCurrentIndex(m_currentIndex - 1);
    return currentPath();
}

void Playlist::clear()
{
    if (m_files.isEmpty() && m_currentIndex < 0)
        return;
    replaceFiles({}, -1);
}

// A new queue always announces both, even when the index happens to land on the
// number it already held: index 0 of one folder is a different film from index 0
// of another, and a binding on currentPath has no other way to know.
void Playlist::replaceFiles(const QStringList &files, int index)
{
    m_files = files;
    m_currentIndex = (index >= 0 && index < m_files.size()) ? index : -1;
    emit filesChanged();
    emit currentChanged();
}

void Playlist::setCurrentIndex(int index)
{
    const int clamped = (index >= 0 && index < m_files.size()) ? index : -1;
    if (clamped == m_currentIndex)
        return;
    m_currentIndex = clamped;
    emit currentChanged();
}
