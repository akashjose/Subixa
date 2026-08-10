// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

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
#include <utility>

namespace {

// "CMPS". A file that does not start with this is not ours, whatever its name.
constexpr quint32 kMagic = 0x434D5053;

// Bumped whenever the layout below changes -- and, just as importantly, whenever
// the *extractor's output* changes: an entry written before a parsing fix is
// still readable, and would quietly serve the old text forever. Version 2 is
// exactly that case: `{\p1}` vector drawings stopped being flattened into the
// cue text, so every entry written before it holds path coordinates as dialogue.
//
// Version 3 is both at once. The record grew a per-cue style and actor name and a
// per-track [V4+ Styles] table, and the extractor now produces them -- so an
// entry written before it has no styles at all, and a track styled entirely
// through its table would go on reading plain out of the cache however often it
// was reopened.
//
// Version 4 is layout alone: a track record now carries its forced and
// hearing-impaired flags, and every track in an entry written before it would
// read back as plain.
constexpr quint32 kFormatVersion = 4;

// Pinned so a Qt upgrade cannot silently change how the primitives below are
// encoded and turn every existing entry into garbage.
constexpr auto kStreamVersion = QDataStream::Qt_6_0;

// A count read from the file says how much to allocate, so it is attacker- and
// corruption-controlled: a flipped byte in a length field asks for tens of
// gigabytes in one call, which is a std::bad_alloc and a std::terminate on open.
// The entry is never invalidated by that, because the crash precedes any
// cleanup -- so the player becomes permanently unable to open that one film.
//
// Every count is therefore checked against how many bytes are actually left in
// the file before it is believed. These are the smallest encodings a record can
// have under kStreamVersion: a QString is a quint32 length and nothing else when
// empty, a qint64 is 8 bytes, a qint32 is 4, a bool is 1.
constexpr qint64 kMinLineBytes = 8 + 8 + 4 + 4 + 4 + 4;
constexpr qint64 kMinStyleBytes = 4 + 4 + 4 + 8 + 1 + 1 + 1 + 1;
constexpr qint64 kMinTrackBytes = 4 + 4 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4 + 4;

// Absolute ceilings on top of the size check, so a large *valid-length* file
// cannot ask for an allocation that is merely proportionate rather than sane.
// All three sit far above anything real: the 200k-cue fixture is the largest
// track anyone has, the 65-track film is the widest container, and a styles table
// past a few hundred rows has stopped describing a subtitle.
constexpr qint32 kMaxLinesPerTrack = 5'000'000;
constexpr qint32 kMaxTracks = 4096;
constexpr qint32 kMaxStylesPerTrack = 65'536;

// What the stream has not consumed yet. QDataStream reads straight through to
// the device rather than buffering ahead, so the device's own count is exact.
qint64 bytesLeft(QDataStream &in)
{
    const QIODevice *device = in.device();
    return device ? device->bytesAvailable() : 0;
}

// True when `count` records of at least `minBytes` each could actually fit in
// what is left of the file. A count that cannot fit is a corrupt or hostile
// entry, and the only safe response is to treat the whole thing as a miss.
bool countFits(QDataStream &in, qint32 count, qint64 minBytes, qint32 ceiling)
{
    if (count < 0 || count > ceiling)
        return false;
    return qint64(count) * minBytes <= bytesLeft(in);
}

void writeLine(QDataStream &out, const SubtitleLine &line)
{
    out << line.startMs << line.endMs << line.text << line.rawText << line.style
        << line.actor;
}

bool readLine(QDataStream &in, SubtitleLine &line)
{
    in >> line.startMs >> line.endMs >> line.text >> line.rawText >> line.style
        >> line.actor;
    return in.status() == QDataStream::Ok;
}

// The colour goes out as a plain integer with -1 for "the row declared none",
// rather than through QColor's own stream operators: those encode a spec tag and
// four 16-bit channels whose meaning is Qt's to change, and the whole point of
// pinning kStreamVersion is that nothing in an entry is Qt's to change.
void writeStyle(QDataStream &out, const QString &name, const AssStyle &style)
{
    out << name << style.fontName << qint32(style.fontSize)
        << (style.primaryColour.isValid() ? qint64(style.primaryColour.rgb())
                                          : qint64(-1))
        << style.bold << style.italic << style.underline << style.strikeOut;
}

bool readStyle(QDataStream &in, QString &name, AssStyle &style)
{
    qint32 fontSize = 0;
    qint64 colour = -1;
    in >> name >> style.fontName >> fontSize >> colour >> style.bold >> style.italic
        >> style.underline >> style.strikeOut;
    if (in.status() != QDataStream::Ok)
        return false;
    style.fontSize = fontSize;
    style.primaryColour = colour < 0 ? QColor() : QColor(QRgb(colour));
    return true;
}

void writeTrack(QDataStream &out, const SubtitleTrack &track)
{
    out << qint32(track.id) << qint32(track.streamIndex) << track.language
        << track.title << track.codecName << qint32(track.kind) << track.sidecar
        << track.sourcePath << track.note << track.forced << track.hearingImpaired;
    // Ahead of the cues rather than after them: it is track metadata, and a
    // reader that wanted only the header would otherwise have to walk 200k cues
    // to reach it. AssStyleTable is ordered, so this is byte-stable.
    out << qint32(track.styles.size());
    for (auto it = track.styles.cbegin(); it != track.styles.cend(); ++it)
        writeStyle(out, it.key(), it.value());
    out << qint32(track.lines.size());
    for (const SubtitleLine &line : track.lines)
        writeLine(out, line);
}

bool readTrack(QDataStream &in, SubtitleTrack &track)
{
    qint32 id = 0;
    qint32 streamIndex = 0;
    qint32 kind = 0;
    qint32 styleCount = 0;
    qint32 lineCount = 0;

    in >> id >> streamIndex >> track.language >> track.title >> track.codecName
        >> kind >> track.sidecar >> track.sourcePath >> track.note >> track.forced
        >> track.hearingImpaired;

    // The styles table, bounded exactly as the cue count is: it is another number
    // out of the file that decides how much to read.
    in >> styleCount;
    if (in.status() != QDataStream::Ok)
        return false;
    if (!countFits(in, styleCount, kMinStyleBytes, kMaxStylesPerTrack))
        return false;

    track.styles.clear();
    for (qint32 i = 0; i < styleCount; ++i) {
        QString name;
        AssStyle style;
        if (!readStyle(in, name, style))
            return false;
        track.styles.insert(name, style);
    }

    in >> lineCount;

    if (in.status() != QDataStream::Ok)
        return false;
    if (!countFits(in, lineCount, kMinLineBytes, kMaxLinesPerTrack))
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

    // Grown a record at a time rather than resize()d to the declared count: the
    // count has been bounded above, but reserving to it still trusts the file
    // for the whole allocation, and appending only what actually read back means
    // a truncated entry stops at the truncation instead of leaving
    // default-constructed cues behind it. reserve() keeps that from being a
    // reallocation per cue on the 200k-cue case.
    track.lines.clear();
    track.lines.reserve(lineCount);
    for (qint32 i = 0; i < lineCount; ++i) {
        SubtitleLine line;
        // Checked per line rather than once at the end: a truncated entry would
        // otherwise spend the rest of the loop appending default-constructed
        // cues to a list the caller is about to treat as a parse result.
        if (!readLine(in, line))
            return false;
        track.lines.append(std::move(line));
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
        return false;  // a sidecar appeared or went away, or the count is junk

    for (qint32 i = 0; i < sourceCount; ++i) {
        SubtitleSourceStamp stamp;
        in >> stamp.path >> stamp.size >> stamp.modifiedMs;
        if (in.status() != QDataStream::Ok || stamp != sources.at(i))
            return false;
    }

    qint32 trackCount = 0;
    in >> trackCount;
    if (in.status() != QDataStream::Ok)
        return false;
    if (!countFits(in, trackCount, kMinTrackBytes, kMaxTracks))
        return false;

    // Same shape as the cue loop: appended per track that actually read back,
    // never sized to a number the file claimed.
    SubtitleTrackList parsed;
    parsed.reserve(trackCount);
    for (qint32 i = 0; i < trackCount; ++i) {
        SubtitleTrack track;
        if (!readTrack(in, track))
            return false;
        parsed.append(std::move(track));
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
