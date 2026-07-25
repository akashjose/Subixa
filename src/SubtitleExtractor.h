#pragma once

#include <QtCore/QAtomicInt>
#include <QtCore/QObject>

#include "SubtitleCache.h"
#include "SubtitleTypes.h"

// Demuxes and decodes subtitle streams with libavformat/libavcodec. Lives on a
// worker thread: a feature-length ASS track is tens of thousands of cues and
// parsing it on the GUI thread stutters playback.
//
// Subtitle text cannot come from mpv -- mpv only exposes the line currently on
// screen, which cannot produce a browsable list. So the streams get demuxed a
// second time here, independently of playback.
class SubtitleExtractor : public QObject
{
    Q_OBJECT

public:
    explicit SubtitleExtractor(QObject *parent = nullptr);

    // Called from the GUI thread. The parse loop polls this and abandons work
    // whose id has been superseded, so opening a second file mid-parse does not
    // have to wait for the first to finish.
    void setCurrentRequest(int id) { m_currentRequest.storeRelaxed(id); }

    // Tests point this at a temporary directory so they never read or write the
    // user's real cache. Must be called before the thread starts extracting.
    void setCacheDirectory(const QString &directory) { m_cache = SubtitleCache(directory); }
    // Off means every extract() is a real parse -- which is what the tests for
    // the parser itself want, and how to time a cold open.
    void setCacheEnabled(bool enabled) { m_cacheEnabled = enabled; }

public slots:
    void extract(const QString &mediaPath, int requestId);

signals:
    // `fromCache` is true when the cues came off disk rather than out of the
    // container, which is worth saying: a 3 GB feature is the difference between
    // nine seconds and none.
    void finished(int requestId, const SubtitleTrackList &tracks, bool fromCache);
    void failed(int requestId, const QString &reason);
    // 0-100 while a container is being walked. Emitted only on change, so a
    // three-gigabyte file sends about a hundred of these rather than one per
    // packet.
    void progress(int requestId, int percent);

private:
    bool cancelled(int requestId) const
    {
        return m_currentRequest.loadRelaxed() != requestId;
    }

    // Appends every subtitle track found in `path` to `out`. A non-empty
    // `videoBase` means `path` is a sidecar sitting next to a video with that
    // base name, which is where a sidecar's language tag comes from.
    bool readContainer(const QString &path, const QString &videoBase,
                       SubtitleTrackList &out, int requestId, QString *error);

    QAtomicInt m_currentRequest{0};
    SubtitleCache m_cache;
    bool m_cacheEnabled = true;
};
