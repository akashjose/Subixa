#include "SubtitleCache.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>

#include <algorithm>

namespace {

// "CMPS". A file that does not start with this is not ours, whatever its name.
constexpr quint32 kMagic = 0x434D5053;

// Bumped whenever the layout below changes. An entry written by an older build
// is dropped rather than misread -- the whole point of the cache is that the
// cues are identical to a fresh parse.
constexpr quint32 kFormatVersion = 1;

// Pinned so a Qt upgrade cannot silently change how the primitives below are
// encoded and turn every existing entry into garbage.
constexpr auto kStreamVersion = QDataStream::Qt_6_0;

void writeLine(QDataStream &out, const SubtitleLine &line)
{
    out << line.startMs << line.endMs << line.text << line.rawText;
}

void readLine(QDataStream &in, SubtitleLine &line)
{
    in >> line.startMs >> line.endMs >> line.text >> line.rawText;
}

void writeTrack(QDataStream &out, const SubtitleTrack &track)
{
    out << qint32(track.id) << qint32(track.streamIndex) << track.language
        << track.title << track.codecName << qint32(track.kind) << track.sidecar
        << track.sourcePath << track.note;
    out << qint32(track.lines.size());
    for (const SubtitleLine &line : track.lines)
        writeLine(out, line);
}

bool readTrack(QDataStream &in, SubtitleTrack &track)
{
    qint32 id = 0;
    qint32 streamIndex = 0;
    qint32 kind = 0;
    qint32 lineCount = 0;

    in >> id >> streamIndex >> track.language >> track.title >> track.codecName
        >> kind >> track.sidecar >> track.sourcePath >> track.note;
    in >> lineCount;

    if (in.status() != QDataStream::Ok || lineCount < 0)
        return false;

    track.id = id;
    track.streamIndex = streamIndex;
    switch (kind) {
    case int(SubtitleKind::Text):
        track.kind = SubtitleKind::Text;
        break;
    case int(SubtitleKind::Bitmap):
        track.kind = SubtitleKind::Bitmap;
        break;
    default:
        track.kind = SubtitleKind::Unknown;
        break;
    }

    track.lines.resize(lineCount);
    for (qint32 i = 0; i < lineCount; ++i) {
        readLine(in, track.lines[i]);
        // Checked per line rather than once at the end: a truncated entry would
        // otherwise spend the rest of the loop appending default-constructed
        // cues to a list the caller is about to treat as a parse result.
        if (in.status() != QDataStream::Ok)
            return false;
    }
    return true;
}

}  // namespace

SubtitleCache::SubtitleCache(const QString &directory) : m_directory(directory)
{
    if (m_directory.isEmpty()) {
        m_directory =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/subtitles");
    }
}

SubtitleSourceStamp SubtitleCache::stampFor(const QString &path)
{
    SubtitleSourceStamp stamp;
    const QFileInfo info(path);
    stamp.path = info.absoluteFilePath();
    if (info.exists()) {
        stamp.size = info.size();
        stamp.modifiedMs = info.lastModified().toMSecsSinceEpoch();
    }
    return stamp;
}

QString SubtitleCache::entryPath(const QString &mediaPath) const
{
    // Same reasoning as PlaybackHistory's key: the path is hashed rather than
    // used, and one file named relatively and absolutely has to land on one
    // entry.
    const QString absolute = QFileInfo(mediaPath).absoluteFilePath();
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(absolute.toUtf8(), QCryptographicHash::Sha1).toHex());
    return m_directory + QLatin1Char('/') + hash + QStringLiteral(".cues");
}

bool SubtitleCache::load(const QString &mediaPath, const SubtitleSourceStamps &sources,
                         SubtitleTrackList *tracks) const
{
    if (!tracks || mediaPath.isEmpty())
        return false;

    QFile file(entryPath(mediaPath));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream in(&file);
    in.setVersion(kStreamVersion);

    quint32 magic = 0;
    quint32 version = 0;
    in >> magic >> version;
    if (in.status() != QDataStream::Ok || magic != kMagic || version != kFormatVersion)
        return false;

    qint32 sourceCount = 0;
    in >> sourceCount;
    if (in.status() != QDataStream::Ok || sourceCount != sources.size())
        return false;  // a sidecar appeared or went away

    for (qint32 i = 0; i < sourceCount; ++i) {
        SubtitleSourceStamp stamp;
        in >> stamp.path >> stamp.size >> stamp.modifiedMs;
        if (in.status() != QDataStream::Ok || stamp != sources.at(i))
            return false;
    }

    qint32 trackCount = 0;
    in >> trackCount;
    if (in.status() != QDataStream::Ok || trackCount < 0)
        return false;

    SubtitleTrackList parsed;
    parsed.resize(trackCount);
    for (qint32 i = 0; i < trackCount; ++i) {
        if (!readTrack(in, parsed[i]))
            return false;
    }

    *tracks = parsed;
    return true;
}

void SubtitleCache::store(const QString &mediaPath, const SubtitleSourceStamps &sources,
                          const SubtitleTrackList &tracks) const
{
    if (mediaPath.isEmpty())
        return;
    if (!QDir().mkpath(m_directory))
        return;

    // QSaveFile rather than QFile: a half-written entry that survived a crash
    // would be read back as a truncated parse, and the reader cannot tell the
    // difference between "the file ends here" and "the track ends here".
    QSaveFile file(entryPath(mediaPath));
    if (!file.open(QIODevice::WriteOnly))
        return;

    QDataStream out(&file);
    out.setVersion(kStreamVersion);

    out << kMagic << kFormatVersion;
    out << qint32(sources.size());
    for (const SubtitleSourceStamp &stamp : sources)
        out << stamp.path << stamp.size << stamp.modifiedMs;

    out << qint32(tracks.size());
    for (const SubtitleTrack &track : tracks)
        writeTrack(out, track);

    if (out.status() != QDataStream::Ok) {
        file.cancelWriting();
        return;
    }
    if (file.commit())
        prune();
}

void SubtitleCache::remove(const QString &mediaPath) const
{
    if (!mediaPath.isEmpty())
        QFile::remove(entryPath(mediaPath));
}

void SubtitleCache::prune() const
{
    QDir dir(m_directory);
    QFileInfoList entries =
        dir.entryInfoList({QStringLiteral("*.cues")}, QDir::Files | QDir::Readable);

    qint64 total = 0;
    for (const QFileInfo &info : std::as_const(entries))
        total += info.size();
    if (total <= MaxBytes)
        return;

    // Oldest first, by the time the entry was last written. Access time would be
    // the better key but is not reliably updated on the mounts this runs on.
    std::sort(entries.begin(), entries.end(),
              [](const QFileInfo &a, const QFileInfo &b) {
                  return a.lastModified() < b.lastModified();
              });

    for (const QFileInfo &info : std::as_const(entries)) {
        if (total <= MaxBytes)
            break;
        const qint64 size = info.size();
        if (QFile::remove(info.absoluteFilePath()))
            total -= size;
    }
}
