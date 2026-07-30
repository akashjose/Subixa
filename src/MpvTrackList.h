// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QFileInfo>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>

// Reconciles the subtitle browser's tracks with mpv's.
//
// The two number tracks differently and neither numbering is derivable from the
// other: the extractor records an ffmpeg stream index, while mpv assigns its own
// per-type ids (sid 1, 2, 3...). mpv publishes `ff-index` per track, which is the
// only reliable bridge -- matching on language breaks immediately on a file with
// two tracks in the same language, and the 65-track film has exactly that.
//
// Both halves live here, under a name that only says "mpv" because that is the
// side the file started on: the first group below answers "which mpv track is
// this browser tab", the second answers the questions that sit entirely on the
// browser's side, and browserIndexForSubtitleId() arbitrates between the two.
// Keeping them together is the point -- when the arbitration was split between
// C++ and untyped JavaScript, the two halves disagreed about how a sidecar's
// path is spelled and the browser listed the wrong track's cues.
//
// A track is a QVariantMap either way, because that is what both sides publish
// to QML: mpv's come from MpvEngine::refreshTracks(), which fixes the key names
// used here, and the browser's from SubtitleManager::rebuildTracksView().
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

// True when mpv's subtitle track `id` is the same stream as a browser track
// described by (ffIndex, sidecarPath); an empty sidecarPath means embedded.
// The one place the embedded/sidecar split is decided, so every caller asks the
// question the same way.
inline bool matchesSubtitleId(const QVariantList &tracks, int id, int ffIndex,
                              const QString &sidecarPath)
{
    if (sidecarPath.isEmpty())
        return subtitleIdForStream(tracks, ffIndex) == id;
    return subtitleIdForFile(tracks, sidecarPath) == id;
}

// Every track of one mpv type, in mpv's own order -- the transport's audio and
// subtitle menus, which list mpv's tracks rather than the browser's.
inline QVariantList tracksOfType(const QVariantList &tracks, const char *type)
{
    QVariantList out;
    for (const QVariant &entry : tracks) {
        if (isType(entry.toMap(), type))
            out.append(entry);
    }
    return out;
}

// ---- the browser's own numbering ---------------------------------------

// A track the browser can list: parsed text with cues in it. Bitmap tracks and
// empty ones stay in the list so the tab bar can say why they cannot be read,
// which is why every scan below has to skip them rather than assume.
inline bool isBrowsable(const QVariantMap &track)
{
    return track.value(QStringLiteral("browsable")).toBool();
}

// Which tab a newly parsed file should open on, as an index into the browser's
// track list: the track last read in this very file, else the language last
// chosen anywhere, else the first browsable track. -1 when the file has nothing
// to list, which reads back as "no tab lit".
//
// `remembered` is PlaybackHistory::subtitleFor()'s map -- empty when this file
// has never had a track chosen in it -- and `preferredLanguage` its cross-file
// fallback. Both arrive as data rather than as a PlaybackHistory, so the store
// and the track list stay unaware of each other.
//
// This was ~40 lines of JavaScript, hand-optimised once because reading the
// track list inside the loop converted a QVariantList of QVariantMaps per
// iteration: roughly 4200 conversions on a 65-track film to produce one integer.
// Being here is what that optimisation was standing in for.
inline int preferredTrackIndex(const QVariantList &tracks,
                               const QVariantMap &remembered,
                               const QString &preferredLanguage)
{
    // What was being read in this file last time wins outright. An absent key
    // rather than a sentinel: the store returns an empty map for a file no track
    // has ever been chosen in, and 0 is a real stream index.
    if (remembered.contains(QStringLiteral("streamIndex"))) {
        const QString sidecarPath =
            remembered.value(QStringLiteral("sidecarPath")).toString();
        const int wantedStream = remembered.value(QStringLiteral("streamIndex")).toInt();

        for (int i = 0; i < tracks.size(); ++i) {
            const QVariantMap track = tracks.at(i).toMap();
            if (!isBrowsable(track))
                continue;
            // Sidecars are matched by path and embedded tracks by ffmpeg stream
            // index, the same split MpvEngine selects them by: neither numbering
            // follows from the other. The path comparison goes through sameFile
            // because the store holds an absolute path while the extractor may
            // hold whatever spelling the file was opened with.
            const bool sidecar = track.value(QStringLiteral("sidecar")).toBool();
            const bool match =
                sidecarPath.isEmpty()
                    ? (!sidecar
                       && track.value(QStringLiteral("streamIndex")).toInt() == wantedStream)
                    : (sidecar
                       && sameFile(track.value(QStringLiteral("sourcePath")).toString(),
                                   sidecarPath));
            if (match)
                return i;
        }
    }

    // Then the language chosen in some other film: somebody who reads English
    // SDH reads it in the next one too.
    if (!preferredLanguage.isEmpty()) {
        for (int i = 0; i < tracks.size(); ++i) {
            const QVariantMap track = tracks.at(i).toMap();
            if (isBrowsable(track)
                && track.value(QStringLiteral("language")).toString() == preferredLanguage)
                return i;
        }
    }

    for (int i = 0; i < tracks.size(); ++i) {
        if (isBrowsable(tracks.at(i).toMap()))
            return i;
    }
    return -1;
}

// The reverse: which browser tab is showing mpv's subtitle track `id`, or -1.
// -1 covers subtitles being off, mpv showing something the browser cannot list
// (a bitmap track, or a sidecar the extractor never saw), and mpv not having
// caught up with the file yet -- the extractor and mpv open it independently, so
// the tab can be chosen before mpv has a track list to match it against. All
// three mean "leave the tab where it is" to the caller.
inline int browserIndexForSubtitleId(const QVariantList &browserTracks,
                                     const QVariantList &mpvTracks, int id)
{
    if (id < 0)
        return -1;

    for (int i = 0; i < browserTracks.size(); ++i) {
        const QVariantMap track = browserTracks.at(i).toMap();
        if (!isBrowsable(track))
            continue;
        const bool sidecar = track.value(QStringLiteral("sidecar")).toBool();
        if (matchesSubtitleId(mpvTracks, id,
                              track.value(QStringLiteral("streamIndex")).toInt(),
                              sidecar ? track.value(QStringLiteral("sourcePath")).toString()
                                      : QString()))
            return i;
    }
    return -1;
}

// What to store for a track picked from the transport's subtitle menu, in the
// shape the history keeps it: { streamIndex, sidecarPath, language }. That menu
// reaches tracks the browser cannot list, so mpv's description of the track is
// the only one there is -- ff-index for an embedded stream, and the filename mpv
// loaded for a sidecar.
inline QVariantMap historyEntryForTrack(const QVariantMap &mpvTrack)
{
    const QString sidecar =
        mpvTrack.value(QStringLiteral("external")).toBool()
            ? mpvTrack.value(QStringLiteral("externalFilename")).toString()
            : QString();

    QVariantMap out;
    // No stream index for a sidecar: an external track's ff-index counts within
    // its own file, and storing it would name an unrelated embedded stream on
    // the next open -- the same collision subtitleIdForStream() skips externals
    // to avoid.
    out[QStringLiteral("streamIndex")] =
        sidecar.isEmpty() ? mpvTrack.value(QStringLiteral("ffIndex"), -1).toInt() : -1;
    out[QStringLiteral("sidecarPath")] = sidecar;
    out[QStringLiteral("language")] = mpvTrack.value(QStringLiteral("language")).toString();
    return out;
}

}  // namespace MpvTrackList
