#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>

#include "SubtitleTypes.h"

// Identity of one file that fed a parse. Size and mtime rather than a content
// hash: hashing three gigabytes to decide whether to re-read three gigabytes
// would save nothing, and a re-encode or a re-downloaded sidecar changes both.
struct SubtitleSourceStamp
{
    QString path;
    qint64 size = 0;
    qint64 modifiedMs = 0;

    bool operator==(const SubtitleSourceStamp &other) const
    {
        return path == other.path && size == other.size
               && modifiedMs == other.modifiedMs;
    }
    bool operator!=(const SubtitleSourceStamp &other) const { return !(*this == other); }
};

using SubtitleSourceStamps = QVector<SubtitleSourceStamp>;

// On-disk store of already-parsed cues, so reopening a film does not re-walk it.
//
// Parsing is I/O bound -- subtitle packets are interleaved through the container,
// so the whole file has to be read whatever the cue count (9.4 s for a 3 GB
// feature). Nothing about the result changes between opens, so it is worth
// keeping.
//
// Used from the extractor's worker thread, never from the GUI thread: a hit
// still means reading and deserialising tens of thousands of cues.
class SubtitleCache
{
public:
    // An empty directory means the user's cache location. Tests pass their own.
    explicit SubtitleCache(const QString &directory = QString());

    // True on a hit, with `tracks` filled in. A miss leaves `tracks` untouched:
    // any stamp that disagrees, an unreadable or truncated entry, or an entry
    // written by an older format version.
    bool load(const QString &mediaPath, const SubtitleSourceStamps &sources,
              SubtitleTrackList *tracks) const;

    // Writes the entry, replacing any existing one. Failures are silent by
    // design -- a cache that cannot be written is a slow open, not an error the
    // user can act on.
    void store(const QString &mediaPath, const SubtitleSourceStamps &sources,
               const SubtitleTrackList &tracks) const;

    void remove(const QString &mediaPath) const;

    QString directory() const { return m_directory; }

    // Missing files stamp as (0, 0), which is a value no readable file has, so a
    // sidecar that disappears invalidates the entry rather than being ignored.
    static SubtitleSourceStamp stampFor(const QString &path);

    // Total bytes the cache is allowed to occupy before the oldest entries are
    // dropped. A feature's cues are a few megabytes, so this holds a large
    // library and still cannot run away.
    static constexpr qint64 MaxBytes = 512LL * 1024 * 1024;

private:
    QString entryPath(const QString &mediaPath) const;
    void prune() const;

    QString m_directory;
};
