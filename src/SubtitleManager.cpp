// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "SubtitleManager.h"

#include "MpvTrackList.h"
#include "SubtitleExtractor.h"
#include "SubtitleLineModel.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QTextStream>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>
#include <QtQml/QQmlEngine>

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

// "eng" -> "English", empty for a code QLocale does not know.
//
// Named apart from SubtitleManager::languageName, which publishes it to QML: a
// member of that name would shadow this one inside every member function below.
QString languageNameFor(const QString &code)
{
    if (code.isEmpty())
        return {};
    // ffmpeg writes ISO 639-2/B ("ger", "fre"); a sidecar's filename tag is
    // usually 639-1 ("en"). codeToLanguage tries every scheme it knows.
    const QLocale::Language language = QLocale::codeToLanguage(code);
    return language == QLocale::AnyLanguage ? QString()
                                            : QLocale::languageToString(language);
}

// What tells this track apart from the others in its language: the title, else
// the flavour flags.
QString qualifierFor(const SubtitleTrack &track)
{
    QString title = track.title.trimmed();
    // The extractor gives a title-less sidecar its filename. That is a source,
    // not a qualifier, and it is already in the tooltip.
    if (track.sidecar && title == QFileInfo(track.sourcePath).fileName())
        title.clear();
    // "eng" tagged "English" is the language twice over.
    if (!title.isEmpty()) {
        const QString name = languageNameFor(track.language);
        if (title.compare(track.language, Qt::CaseInsensitive) == 0
            || (!name.isEmpty() && title.compare(name, Qt::CaseInsensitive) == 0))
            title.clear();
    }
    if (!title.isEmpty())
        return title;
    if (track.hearingImpaired)
        return QStringLiteral("SDH");
    if (track.forced)
        return QStringLiteral("Forced");
    return {};
}

// Short label for a tab: the language, qualified by whatever tells this track
// apart from the others in it. Falling back to the title, then to the source
// file, and only then to a bare ordinal. Unqualified, a release carrying four
// Italian tracks printed "ITA" four times.
QString labelFor(const SubtitleTrack &track)
{
    QString base;
    if (!track.language.isEmpty() && track.language != QLatin1String("und"))
        base = track.language.toUpper();
    else if (!track.title.isEmpty())
        base = track.title;
    else if (track.sidecar)
        base = QFileInfo(track.sourcePath).fileName();
    else
        return QStringLiteral("Track %1").arg(track.id + 1);

    const QString qualifier = qualifierFor(track);
    // A label that already *is* the title has nothing left to qualify.
    if (qualifier.isEmpty() || qualifier == base)
        return base;
    return base + QLatin1Char(' ') + qualifier;
}

// SubRip wants hh:mm:ss,mmm -- same fields as the browser's timestamp with a
// comma before the milliseconds. Parsers are strict about that comma, so this
// cannot just reuse formatSubtitleTimestamp() as it stands.
QString srtTimestamp(qint64 ms)
{
    QString stamp = formatSubtitleTimestamp(ms);
    const qsizetype dot = stamp.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0)
        stamp[dot] = QLatin1Char(',');
    return stamp;
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
    connect(m_worker, &SubtitleExtractor::progress, this,
            &SubtitleManager::onExtractProgress);
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
    rebuildModels();
    emit tracksChanged();

    ++m_requestId;
    m_worker->setCurrentRequest(m_requestId);
    m_elapsed.start();
    m_progress = 0;
    emit progressChanged();
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
    rebuildModels();
    setBusy(false);
    setStatus(QString());
    emit tracksChanged();
}

void SubtitleManager::setCacheDirectory(const QString &directory)
{
    // Safe only while nothing is being parsed, which is why this is documented as
    // a before-load() call rather than exposed to QML.
    m_worker->setCacheDirectory(directory);
}

void SubtitleManager::onExtractFinished(int requestId, const SubtitleTrackList &tracks,
                                        bool fromCache)
{
    if (requestId != m_requestId)
        return;  // a newer load superseded this one

    m_tracks = tracks;
    rebuildTracksView();
    rebuildModels();
    setBusy(false);

    int textTracks = 0;
    int totalLines = 0;
    for (const SubtitleTrack &t : m_tracks) {
        if (t.kind == SubtitleKind::Text)
            ++textTracks;
        totalLines += t.lines.size();
    }

    if (m_tracks.isEmpty()) {
        setStatus(QStringLiteral("no subtitle tracks"));
    } else {
        // "cached" is worth saying: without it a hit is indistinguishable from
        // a parse that was suspiciously quick.
        setStatus(QStringLiteral("%1 track%2, %3 line%4%5")
                      .arg(textTracks)
                      .arg(textTracks == 1 ? QString() : QStringLiteral("s"))
                      .arg(totalLines)
                      .arg(totalLines == 1 ? QString() : QStringLiteral("s"))
                      .arg(fromCache ? QStringLiteral(" · cached") : QString()));
    }

    qInfo().noquote() << "subtitles:" << m_status << "in" << m_elapsed.elapsed() << "ms"
                      << (fromCache ? "(cache hit)" : "(parsed)");

    emit tracksChanged();
    emit loaded();
}

void SubtitleManager::onExtractProgress(int requestId, int percent)
{
    // A superseded parse keeps running until its demux loop notices; its
    // progress must not be shown as the current file's.
    if (requestId != m_requestId || m_progress == percent)
        return;
    m_progress = percent;
    emit progressChanged();
}

void SubtitleManager::onExtractFailed(int requestId, const QString &reason)
{
    if (requestId != m_requestId)
        return;

    m_tracks.clear();
    m_tracksView.clear();
    rebuildModels();
    setBusy(false);
    setStatus(reason);
    emit tracksChanged();
    emit failed(reason);
}

void SubtitleManager::rebuildTracksView()
{
    m_tracksView.clear();
    m_tracksView.reserve(m_tracks.size());

    // Two tracks can be genuinely indistinguishable -- one language, no title,
    // no flags -- and a strip of identical tabs is no better than the bare
    // language it replaced. Only the repeats take an ordinal.
    QStringList labels;
    labels.reserve(m_tracks.size());
    QHash<QString, int> occurrences;
    for (const SubtitleTrack &track : std::as_const(m_tracks)) {
        labels.append(labelFor(track));
        ++occurrences[labels.constLast()];
    }
    QHash<QString, int> numbered;
    for (qsizetype i = 0; i < labels.size(); ++i) {
        const QString base = labels.at(i);
        if (occurrences.value(base) < 2)
            continue;
        // The first keeps the bare label, so no file grows a pointless "1".
        if (++numbered[base] > 1)
            labels[i] = base + QLatin1Char(' ') + QString::number(numbered.value(base));
    }

    qsizetype index = 0;
    for (const SubtitleTrack &track : std::as_const(m_tracks)) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = track.id;
        entry[QStringLiteral("label")] = labels.at(index++);
        entry[QStringLiteral("language")] = track.language;
        entry[QStringLiteral("title")] = track.title;
        entry[QStringLiteral("codec")] = track.codecName;
        entry[QStringLiteral("kind")] = kindName(track.kind);
        entry[QStringLiteral("sidecar")] = track.sidecar;
        entry[QStringLiteral("forced")] = track.forced;
        entry[QStringLiteral("hearingImpaired")] = track.hearingImpaired;
        entry[QStringLiteral("source")] = QFileInfo(track.sourcePath).fileName();
        // Full path as well as the display name: selecting a sidecar in mpv means
        // matching or adding it by path, since mpv numbers external tracks
        // independently of anything the extractor sees.
        entry[QStringLiteral("sourcePath")] = track.sourcePath;
        entry[QStringLiteral("streamIndex")] = track.streamIndex;
        entry[QStringLiteral("lineCount")] = track.lines.size();
        entry[QStringLiteral("browsable")] =
            track.kind == SubtitleKind::Text && !track.lines.isEmpty();
        entry[QStringLiteral("note")] = track.note;
        m_tracksView.append(entry);
    }
}

void SubtitleManager::setRowBackground(const QColor &background)
{
    if (m_rowBackground == background)
        return;
    m_rowBackground = background;
    for (SubtitleLineModel *model : std::as_const(m_models))
        model->setBackground(m_rowBackground);
    emit rowBackgroundChanged();
}

void SubtitleManager::rebuildModels()
{
    while (m_models.size() < m_tracks.size()) {
        auto *model = new SubtitleLineModel(this);
        // Before it has rows, so the first paint is already themed.
        model->setBackground(m_rowBackground);
        m_models.append(model);
    }

    // Surplus models from a previous, larger file are emptied rather than
    // deleted -- a QML binding may still hold one for an instant after the
    // track list changes.
    for (int i = 0; i < m_models.size(); ++i) {
        const bool live = i < m_tracks.size();
        // Before the lines: setStyles() on a populated model emits dataChanged
        // for every row, which the reset from setLines() would only repeat.
        m_models[i]->setStyles(live ? m_tracks.at(i).styles : AssStyleTable());
        m_models[i]->setLines(live ? m_tracks.at(i).lines : QVector<SubtitleLine>());
    }
}

SubtitleLineModel *SubtitleManager::model(int trackId) const
{
    if (trackId < 0 || trackId >= m_models.size())
        return nullptr;

    SubtitleLineModel *model = m_models.at(trackId);
    // Without this the engine takes JavaScript ownership of a model returned
    // from an invokable and can collect it out from under us.
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    return model;
}

QVariantMap SubtitleManager::preferredTrackChoice(const QVariantMap &remembered,
                                                  const QVariantList &preferences) const
{
    const MpvTrackList::TrackChoice choice =
        MpvTrackList::preferredTrackChoice(m_tracksView, remembered, preferences);
    QVariantMap out;
    out[QStringLiteral("index")] = choice.index;
    out[QStringLiteral("matched")] = choice.matched;
    return out;
}

QString SubtitleManager::languageName(const QString &code)
{
    return languageNameFor(code);
}

QString SubtitleManager::formatTimestamp(qint64 ms)
{
    return formatSubtitleTimestamp(ms);
}

QString SubtitleManager::exportTrack(int trackId, const QUrl &target) const
{
    if (trackId < 0 || trackId >= m_tracks.size())
        return QStringLiteral("no such track");

    const SubtitleTrack &track = m_tracks.at(trackId);
    if (track.lines.isEmpty())
        return QStringLiteral("that track has no lines to export");

    const QString path = target.isLocalFile() ? target.toLocalFile() : target.toString();
    if (path.isEmpty())
        return QStringLiteral("no destination");

    // QSaveFile: an export interrupted halfway would otherwise leave a
    // half-written .srt sitting next to the film, where it looks like a real one.
    // Deliberately *not* QIODevice::Text. That flag translates every "\n" below
    // into "\r\n" on Windows and leaves it alone everywhere else, so the same
    // track exported on two machines would come out byte-different -- the same
    // objection as the explicit encoding below. Every SubRip reader accepts LF.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("cannot write %1: %2")
            .arg(QFileInfo(path).fileName(), file.errorString());

    QTextStream out(&file);
    // Explicit, not the locale's: a subtitle file that decodes differently on
    // the next machine is a broken subtitle file.
    out.setEncoding(QStringConverter::Utf8);

    for (int i = 0; i < track.lines.size(); ++i) {
        const SubtitleLine &line = track.lines.at(i);
        out << (i + 1) << "\n"
            << srtTimestamp(line.startMs) << " --> " << srtTimestamp(line.endMs) << "\n"
            << line.text << "\n\n";
    }

    out.flush();
    if (out.status() != QTextStream::Ok || !file.commit())
        return QStringLiteral("could not write %1").arg(QFileInfo(path).fileName());

    return QString();
}

QString SubtitleManager::suggestedExportName(int trackId, const QString &mediaPath) const
{
    const QString base = mediaPath.isEmpty()
                             ? QStringLiteral("subtitles")
                             : QFileInfo(mediaPath).completeBaseName();
    if (trackId < 0 || trackId >= m_tracks.size())
        return base + QStringLiteral(".srt");

    const SubtitleTrack &track = m_tracks.at(trackId);
    QString tag = track.language;
    if (tag.isEmpty() || tag == QLatin1String("und"))
        tag = QStringLiteral("track%1").arg(track.id + 1);
    // A title like "Latin American" distinguishes two tracks of one language,
    // which is exactly the case where an export needs telling apart.
    if (!track.title.isEmpty() && !track.sidecar) {
        QString title = track.title;
        title.replace(QRegularExpression(QStringLiteral("[^\\w]+")), QStringLiteral("-"));
        title = title.trimmed();
        if (!title.isEmpty())
            tag += QLatin1Char('.') + title;
    }
    return QStringLiteral("%1.%2.srt").arg(base, tag);
}

QUrl SubtitleManager::suggestedExportUrl(int trackId, const QString &mediaPath) const
{
    const QString name = suggestedExportName(trackId, mediaPath);
    const QDir dir = mediaPath.isEmpty() ? QDir::current()
                                         : QFileInfo(mediaPath).absoluteDir();
    return QUrl::fromLocalFile(dir.absoluteFilePath(name));
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
