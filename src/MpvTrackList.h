#pragma once

#include <QtCore/QFileInfo>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>

// Maps the subtitle browser's tracks onto mpv's.
//
// The two number tracks differently and neither numbering is derivable from the
// other: the extractor records an ffmpeg stream index, while mpv assigns its own
// per-type ids (sid 1, 2, 3...). mpv publishes `ff-index` per track, which is the
// only reliable bridge -- matching on language breaks immediately on a file with
// two tracks in the same language, and the 65-track film has exactly that.
//
// Kept header-only and free of mpv types so it can be tested without a window, a
// GL context, or an mpv handle. MpvObject::loadFile() is queued until the render
// context exists (trap 2), so a test that drove MpvObject directly would never
// get a track list to map at all.
namespace MpvTrackList {

inline bool isType(const QVariantMap &track, const char *type)
{
    return track.value(QStringLiteral("type")).toString() == QLatin1String(type);
}

// Two paths name the same sidecar even when one is relative and the other is
// not, so compare resolved paths rather than strings.
inline bool sameFile(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
}

// mpv's sid for the embedded track carrying ffmpeg stream `ffIndex`, or -1.
inline int subtitleIdForStream(const QVariantList &tracks, int ffIndex)
{
    if (ffIndex < 0)
        return -1;
    for (const QVariant &entry : tracks) {
        const QVariantMap track = entry.toMap();
        if (!isType(track, "sub"))
            continue;
        // An external track's ff-index refers to its own file, so it would
        // collide with an embedded stream index for no good reason.
        if (track.value(QStringLiteral("external")).toBool())
            continue;
        if (track.value(QStringLiteral("ffIndex")).toInt() == ffIndex)
            return track.value(QStringLiteral("id")).toInt();
    }
    return -1;
}

// mpv's sid for an already-loaded sidecar file, or -1 if mpv does not have it.
// mpv auto-loads some sidecars itself (--sub-auto), so a file may be present
// without us ever having added it.
inline int subtitleIdForFile(const QVariantList &tracks, const QString &path)
{
    for (const QVariant &entry : tracks) {
        const QVariantMap track = entry.toMap();
        if (!isType(track, "sub"))
            continue;
        if (sameFile(track.value(QStringLiteral("externalFilename")).toString(), path))
            return track.value(QStringLiteral("id")).toInt();
    }
    return -1;
}

inline bool isSelected(const QVariantList &tracks, int id)
{
    for (const QVariant &entry : tracks) {
        const QVariantMap track = entry.toMap();
        if (track.value(QStringLiteral("id")).toInt() == id)
            return track.value(QStringLiteral("selected")).toBool();
    }
    return false;
}

}  // namespace MpvTrackList
