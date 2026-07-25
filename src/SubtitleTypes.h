#pragma once

#include <QtCore/QLatin1Char>
#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QVector>

// One subtitle cue, already flattened to plain text. `text` is what the browser
// shows; `rawText` keeps the decoder's payload (ASS override tags and all) so
// styling work later does not have to re-parse the file.
struct SubtitleLine
{
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    QString rawText;
};

enum class SubtitleKind {
    Text,    // decodes to characters -- browsable
    Bitmap,  // PGS/VOBSUB/DVD: pictures, no text. Would need OCR; out of scope.
    Unknown
};

struct SubtitleTrack
{
    int id = -1;           // index into SubtitleManager's track list
    int streamIndex = -1;  // ffmpeg stream index within sourcePath
    QString language;
    QString title;
    QString codecName;
    SubtitleKind kind = SubtitleKind::Unknown;
    bool sidecar = false;  // came from a file next to the video, not from it
    QString sourcePath;
    QString note;  // why a track has no lines, when that needs explaining
    QVector<SubtitleLine> lines;
};

using SubtitleTrackList = QVector<SubtitleTrack>;

Q_DECLARE_METATYPE(SubtitleTrackList)

// hh:mm:ss.mmm. Lives here rather than on SubtitleManager so the list model can
// format a row without depending on the manager.
inline QString formatSubtitleTimestamp(qint64 ms)
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
