// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QLatin1Char>
#include <QtCore/QMap>
#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtGui/QColor>

// One subtitle cue, already flattened to plain text. `text` is what the browser
// shows; `rawText` keeps the decoder's payload (ASS override tags and all) so
// styling work later does not have to re-parse the file.
//
// `style` names the [V4+ Styles] row the cue is drawn with -- most professionally
// authored ASS carries no override tags at all and says everything through the
// table -- and `actor` is the Name field beside it. Both are shared QString
// instances across the whole track: a feature names a handful of styles from tens
// of thousands of cues.
struct SubtitleLine
{
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    QString rawText;
    // Explicitly defaulted, unlike the two above, so that the aggregate
    // initialisers in the suites -- which predate these fields and have nothing
    // to say about them -- stay clean under -Wmissing-field-initializers.
    QString style = {};
    QString actor = {};
};

// One row of an ASS [V4+ Styles] table, reduced to the fields that change how a
// *list* of lines reads. Position, outline, shadow, rotation and the margins all
// say where to put a cue over the picture, which a browser has no use for.
//
// Fontname and Fontsize are kept but deliberately not rendered: the browser draws
// every row in the app's own type, and honouring a 48pt sign font would break the
// list rather than inform it. They are here because the table is the file's
// answer to "what does this style say", and a reader inspecting a track wants it.
struct AssStyle
{
    QString fontName;
    int fontSize = 0;
    QColor primaryColour;  // invalid when the row declared none we could read
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeOut = false;

    bool operator==(const AssStyle &o) const
    {
        return fontName == o.fontName && fontSize == o.fontSize
               && primaryColour == o.primaryColour && bold == o.bold
               && italic == o.italic && underline == o.underline
               && strikeOut == o.strikeOut;
    }
    bool operator!=(const AssStyle &o) const { return !(*this == o); }
};

// Ordered rather than hashed: the table is written to the cue cache, and a
// QHash's iteration order would make two runs over one file produce different
// bytes. Style tables are tens of rows, so the lookup difference is noise.
using AssStyleTable = QMap<QString, AssStyle>;

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
    // The track's [V4+ Styles] table, by style name. Empty for a format that has
    // none; every text decoder publishes a header, but only ASS and SSA carry one
    // an author wrote.
    AssStyleTable styles;
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
