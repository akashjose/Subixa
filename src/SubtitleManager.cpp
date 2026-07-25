#include "SubtitleManager.h"

#include "SubtitleExtractor.h"

#include <QtCore/QFileInfo>
#include <QtCore/QVariantMap>
#include <algorithm>

namespace {

QString kindName(SubtitleKind kind)
{
    switch (kind) {
    case SubtitleKind::Text:
        return QStringLiteral("text");
    case SubtitleKind::Bitmap:
        return QStringLiteral("bitmap");
    case SubtitleKind::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

// Short label for a tab: prefer the language, fall back to the title, then to
// the source file, and only then to a bare ordinal.
QString labelFor(const SubtitleTrack &track)
{
    if (!track.language.isEmpty() && track.language != QLatin1String("und"))
        return track.language.toUpper();
    if (!track.title.isEmpty())
        return track.title;
    if (track.sidecar)
        return QFileInfo(track.sourcePath).fileName();
    return QStringLiteral("Track %1").arg(track.id + 1);
}

} // namespace

SubtitleManager::SubtitleManager(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<SubtitleTrackList>("SubtitleTrackList");

    m_worker = new SubtitleExtractor;  // no parent: it is moved to another thread
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &SubtitleManager::extractRequested, m_worker,
            &SubtitleExtractor::extract);
    connect(m_worker, &SubtitleExtractor::finished, this,
            &SubtitleManager::onExtractFinished);
    connect(m_worker, &SubtitleExtractor::failed, this,
            &SubtitleManager::onExtractFailed);

    m_thread.start();
}

SubtitleManager::~SubtitleManager()
{
    // Bump the request id first so an in-flight parse drops out of its loop
    // instead of running to completion while we wait on it.
    m_worker->setCurrentRequest(-1);
    m_thread.quit();
    m_thread.wait();
}

void SubtitleManager::load(const QString &mediaPath)
{
    if (mediaPath.isEmpty()) {
        clear();
        return;
    }

    m_tracks.clear();
    m_tracksView.clear();
    emit tracksChanged();

    ++m_requestId;
    m_worker->setCurrentRequest(m_requestId);
    setStatus(QStringLiteral("parsing subtitles…"));
    setBusy(true);

    emit extractRequested(mediaPath, m_requestId);
}

void SubtitleManager::clear()
{
    // Any parse still running belongs to a file we no longer care about.
    ++m_requestId;
    m_worker->setCurrentRequest(m_requestId);

    m_tracks.clear();
    m_tracksView.clear();
    setBusy(false);
    setStatus(QString());
    emit tracksChanged();
}

void SubtitleManager::onExtractFinished(int requestId, const SubtitleTrackList &tracks)
{
    if (requestId != m_requestId)
        return;  // a newer load superseded this one

    m_tracks = tracks;
    rebuildTracksView();
    setBusy(false);

    int textTracks = 0;
    int totalLines = 0;
    for (const SubtitleTrack &t : m_tracks) {
        if (t.kind == SubtitleKind::Text)
            ++textTracks;
        totalLines += t.lines.size();
    }

    if (m_tracks.isEmpty())
        setStatus(QStringLiteral("no subtitle tracks"));
    else
        setStatus(QStringLiteral("%1 track%2, %3 line%4")
                      .arg(textTracks)
                      .arg(textTracks == 1 ? QString() : QStringLiteral("s"))
                      .arg(totalLines)
                      .arg(totalLines == 1 ? QString() : QStringLiteral("s")));

    emit tracksChanged();
    emit loaded();
}

void SubtitleManager::onExtractFailed(int requestId, const QString &reason)
{
    if (requestId != m_requestId)
        return;

    m_tracks.clear();
    m_tracksView.clear();
    setBusy(false);
    setStatus(reason);
    emit tracksChanged();
}

void SubtitleManager::rebuildTracksView()
{
    m_tracksView.clear();
    m_tracksView.reserve(m_tracks.size());

    for (const SubtitleTrack &track : std::as_const(m_tracks)) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = track.id;
        entry[QStringLiteral("label")] = labelFor(track);
        entry[QStringLiteral("language")] = track.language;
        entry[QStringLiteral("title")] = track.title;
        entry[QStringLiteral("codec")] = track.codecName;
        entry[QStringLiteral("kind")] = kindName(track.kind);
        entry[QStringLiteral("sidecar")] = track.sidecar;
        entry[QStringLiteral("source")] = QFileInfo(track.sourcePath).fileName();
        entry[QStringLiteral("streamIndex")] = track.streamIndex;
        entry[QStringLiteral("lineCount")] = track.lines.size();
        entry[QStringLiteral("browsable")] =
            track.kind == SubtitleKind::Text && !track.lines.isEmpty();
        entry[QStringLiteral("note")] = track.note;
        m_tracksView.append(entry);
    }
}

QVariantList SubtitleManager::lines(int trackId) const
{
    QVariantList rows;
    if (trackId < 0 || trackId >= m_tracks.size())
        return rows;

    const QVector<SubtitleLine> &lines = m_tracks.at(trackId).lines;
    rows.reserve(lines.size());
    for (const SubtitleLine &line : lines) {
        QVariantMap row;
        row[QStringLiteral("startMs")] = line.startMs;
        row[QStringLiteral("endMs")] = line.endMs;
        row[QStringLiteral("start")] = formatTimestamp(line.startMs);
        row[QStringLiteral("text")] = line.text;
        rows.append(row);
    }
    return rows;
}

int SubtitleManager::lineIndexAt(int trackId, qint64 positionMs) const
{
    if (trackId < 0 || trackId >= m_tracks.size())
        return -1;

    const QVector<SubtitleLine> &lines = m_tracks.at(trackId).lines;
    if (lines.isEmpty())
        return -1;

    // First cue starting after the position; the one before it is the answer.
    const auto it = std::upper_bound(lines.cbegin(), lines.cend(), positionMs,
                                     [](qint64 ms, const SubtitleLine &line) {
                                         return ms < line.startMs;
                                     });
    if (it == lines.cbegin())
        return -1;
    return static_cast<int>(std::distance(lines.cbegin(), it) - 1);
}

QString SubtitleManager::formatTimestamp(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 totalSeconds = ms / 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

void SubtitleManager::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void SubtitleManager::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
