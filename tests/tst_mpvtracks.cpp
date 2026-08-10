// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

// What mpv itself does with track selection, verified with no window and no FBO.
//
// This is the half of the subtitle browser that a unit test over our own models
// cannot reach: whether selecting a track actually changes what mpv would burn
// over the video. It does not need a picture to answer that -- mpv exposes the
// current subtitle as text in `sub-text`, so "does the panel agree with the
// video" is a string comparison rather than a screenshot. That matters here
// because WSLg can degrade into painting stale frames while mpv keeps working
// perfectly (see CLAUDE.md), which makes visual checks the least trustworthy
// evidence available.
//
// vo=null, not QT_QPA_PLATFORM=offscreen: offscreen never creates a render
// context, so the queued loadfile is never flushed and mpv does not load the
// file at all (trap 2). vo=null has no such dependency.

#include <QtTest>

#include <QtCore/QFileInfo>

#include <mpv/client.h>

#include "FboCap.h"
#include "MpvTrackList.h"

#include <clocale>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(SUBIXA_TESTDATA_DIR "/") + name;
}

}  // namespace

class TstMpvTracks : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void subtitleTracksCarryFfIndexAndLanguage();
    void audioTrackIsSelectable();
    void selectingSidChangesWhatMpvWouldRender();
    void mpvLeavesEntitiesEncoded();

    // Pure arithmetic, no mpv handle needed.
    void fboCapLeavesOrdinaryWindowsAlone();
    void fboCapHoldsTheAreaLimit();
    void fboCapStopsAtTheVideoSize();
    void fboCapNeverUpscalesOrInverts();

    // Pure mapping, no mpv handle needed.
    void mapsStreamIndexToSid();
    void mapsSidecarPathToSid();
    void mappingIgnoresOtherTrackTypes();
    void splitsTracksByType();

    // The browser's own numbering, and the arbitration between the two.
    void opensOnTheTrackThisFileWasLeftOn();
    void aRememberedSidecarSurvivesBeingRenamed();
    void fallsBackToTheLanguageThenTheFirstTrack();
    void carriesTheKindOfTrackForwardNotOnlyTheLanguage();
    void walksThePreferenceListInOrder();
    void aFallbackTabIsNotASelection();
    void picksAnAudioTrackOrLeavesMpvAlone();
    void oneSpellingOfALanguage();
    void skipsTracksTheBrowserCannotList();
    void findsTheTabForMpvsSubtitle();
    void storesAnMpvTrackTheWayTheHistoryKeepsIt();

private:
    // Pumps mpv's event queue until `id` arrives. Every check here depends on
    // mpv having finished a seek or a load, and there is no other way to know.
    bool waitFor(mpv_event_id id, int timeoutMs = 10'000);
    QString stringProp(const char *name);
    double doubleProp(const char *name);
    // sub-text lags the seek by a frame or two even once the seek reports done.
    // 15 s rather than 5: the cue has to satisfy the bracket check below, and
    // under a full-suite run -- where this starts right after another test has
    // had mpv busy -- a decode cycle can take noticeably longer than when the
    // suite runs this case alone. The old budget turned that into a spurious
    // failure roughly one run in three.
    QString subTextAfterSeek(double seconds, int timeoutMs = 15000);
    int subTrackId(const QString &language);

    mpv_handle *m_mpv = nullptr;
};

void TstMpvTracks::initTestCase()
{
    const QString path = fixture(QStringLiteral("subs.mkv"));
    // Hard failure, not QSKIP: a skip exits 0 and would report this suite
    // green having asserted nothing. See tst_subtitles::initTestCase.
    QVERIFY2(QFileInfo::exists(path),
             "subs.mkv missing -- run ./testdata/make-fixtures.sh");

    // Its own handle, so it needs its own call. See MpvEngine's constructor.
    std::setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    QVERIFY(m_mpv);

    // No video output at all: this test is about mpv's state, not its pixels.
    QCOMPARE(mpv_set_option_string(m_mpv, "vo", "null"), 0);
    QCOMPARE(mpv_set_option_string(m_mpv, "ao", "null"), 0);
    // Paused: this suite asks "which cue is current at time T", and a position
    // that advances while it looks is the difference between a stable answer
    // and a race. Seeks still decode subtitles when paused.
    QCOMPARE(mpv_set_option_string(m_mpv, "pause", "yes"), 0);
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "hwdec", "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    // Paused: every assertion reads state at a timestamp it chose, so playback
    // must not drift past the cue under test between the seek and the read.
    mpv_set_option_string(m_mpv, "pause", "yes");

    QCOMPARE(mpv_initialize(m_mpv), 0);

    const QByteArray utf8 = path.toUtf8();
    const char *cmd[] = {"loadfile", utf8.constData(), nullptr};
    QCOMPARE(mpv_command(m_mpv, cmd), 0);
    QVERIFY2(waitFor(MPV_EVENT_FILE_LOADED), "mpv never loaded the fixture");
}

void TstMpvTracks::cleanupTestCase()
{
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

bool TstMpvTracks::waitFor(mpv_event_id id, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        mpv_event *event = mpv_wait_event(m_mpv, 0.1);
        if (!event)
            continue;
        if (event->event_id == id)
            return true;
        if (event->event_id == MPV_EVENT_END_FILE)
            return false;
    }
    return false;
}

QString TstMpvTracks::stringProp(const char *name)
{
    char *value = nullptr;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_STRING, &value) < 0 || !value)
        return QString();
    const QString result = QString::fromUtf8(value);
    mpv_free(value);
    return result;
}

double TstMpvTracks::doubleProp(const char *name)
{
    double value = 0.0;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_DOUBLE, &value) < 0)
        return -1.0;
    return value;
}

QString TstMpvTracks::subTextAfterSeek(double seconds, int timeoutMs)
{
    // Drain first. `waitFor(PLAYBACK_RESTART)` below would otherwise return
    // instantly on an event the *previous* test's seek left in the queue, and
    // this one would then poll while playback was still somewhere else
    // entirely. That was the flake: seeking to 4 s and reading the cue at 8.3 s,
    // because the file keeps playing while we look.
    while (mpv_event *stale = mpv_wait_event(m_mpv, 0)) {
        if (stale->event_id == MPV_EVENT_NONE)
            break;
    }

    const QByteArray pos = QByteArray::number(seconds);
    const char *cmd[] = {"seek", pos.constData(), "absolute", nullptr};
    if (mpv_command(m_mpv, cmd) < 0)
        return QString();
    waitFor(MPV_EVENT_PLAYBACK_RESTART, timeoutMs);

    // The seek is done but the subtitle for the new position may need another
    // decode cycle, so poll rather than read once and trust it.
    //
    // Non-empty is *not* enough, and accepting it was a real flake: after a
    // track switch mpv can still be reporting the cue from wherever playback
    // was before, so the first non-empty answer is sometimes the previous
    // test's line. It failed only in a full-suite run, where a preceding test
    // had left a different position and sid behind -- in isolation the state it
    // read stale happened to be empty. So the cue has to actually cover the
    // position that was asked for before it counts.
    // Re-issue the seek if the first one produces nothing for a while. Switching
    // sid while paused does not reliably make mpv decode a cue for the position
    // it is already sitting on -- with vo=null there is no redraw to force it --
    // so the poll below can otherwise read an empty sub-text until the timeout
    // and report "no subtitle" for a track that has one. A second seek always
    // shakes it loose. Seen on mpv 0.4x/libmpv 2.5 (Windows); the older 0.37 the
    // suite was written against happened not to need it, and which of the two
    // sid-switching tests loses the race moves between runs.
    QElapsedTimer sinceSeek;
    sinceSeek.start();

    QElapsedTimer timer;
    timer.start();
    QString text;
    while (timer.elapsed() < timeoutMs) {
        if (sinceSeek.elapsed() > 2000) {
            mpv_command(m_mpv, cmd);
            waitFor(MPV_EVENT_PLAYBACK_RESTART, timeoutMs);
            sinceSeek.restart();
        }

        // And the seek has to have actually landed before the cue means
        // anything -- PLAYBACK_RESTART alone does not promise that.
        if (qAbs(doubleProp("time-pos") - seconds) > 1.0) {
            text.clear();
            mpv_wait_event(m_mpv, 0.05);
            continue;
        }

        text = stringProp("sub-text");
        if (!text.isEmpty()) {
            const double start = doubleProp("sub-start");
            const double end = doubleProp("sub-end");
            // A little slack at each end: these are the decoder's own bounds and
            // a seek lands on the nearest frame rather than exactly.
            if (start <= seconds + 0.25 && end >= seconds - 0.25)
                break;
        }
        text.clear();
        mpv_wait_event(m_mpv, 0.05);
    }
    return text;
}

int TstMpvTracks::subTrackId(const QString &language)
{
    const int count = static_cast<int>(doubleProp("track-list/count"));
    for (int i = 0; i < count; ++i) {
        const QByteArray base = QByteArrayLiteral("track-list/") + QByteArray::number(i);
        if (stringProp(base + "/type") != QStringLiteral("sub"))
            continue;
        if (stringProp(base + "/lang") == language)
            return static_cast<int>(doubleProp(base + "/id"));
    }
    return -1;
}

void TstMpvTracks::subtitleTracksCarryFfIndexAndLanguage()
{
    const int count = static_cast<int>(doubleProp("track-list/count"));
    QVERIFY2(count >= 5, qPrintable(QStringLiteral("track-list/count=%1").arg(count)));

    QStringList subLanguages;
    int subs = 0;
    for (int i = 0; i < count; ++i) {
        const QByteArray base = QByteArrayLiteral("track-list/") + QByteArray::number(i);
        if (stringProp(base + "/type") != QStringLiteral("sub"))
            continue;
        ++subs;
        subLanguages << stringProp(base + "/lang");

        // ff-index is the bridge between mpv's own track numbering and the
        // ffmpeg stream index the extractor records, which is what lets a panel
        // tab name an mpv track without matching on language strings.
        const double ffIndex = doubleProp(base + "/ff-index");
        QVERIFY2(ffIndex >= 0,
                 qPrintable(QStringLiteral("track %1 has no ff-index").arg(i)));
        QVERIFY(doubleProp(base + "/id") >= 1);
    }

    QCOMPARE(subs, 3);
    QCOMPARE(subLanguages, QStringList({QStringLiteral("eng"), QStringLiteral("jpn"),
                                        QStringLiteral("fre")}));
}

void TstMpvTracks::audioTrackIsSelectable()
{
    // aid is the other half of milestone 3's first item; the fixture has exactly
    // one audio track, so the useful assertion is that it is addressable at all.
    const double aid = doubleProp("aid");
    QVERIFY2(aid >= 1, qPrintable(QStringLiteral("aid=%1").arg(aid)));

    QCOMPARE(mpv_set_property_string(m_mpv, "aid", "no"), 0);
    QCOMPARE(stringProp("aid"), QStringLiteral("no"));

    QCOMPARE(mpv_set_property_string(m_mpv, "aid", "1"), 0);
    QCOMPARE(static_cast<int>(doubleProp("aid")), 1);
}

void TstMpvTracks::selectingSidChangesWhatMpvWouldRender()
{
    const int eng = subTrackId(QStringLiteral("eng"));
    const int jpn = subTrackId(QStringLiteral("jpn"));
    QVERIFY(eng > 0);
    QVERIFY(jpn > 0);
    QVERIFY(eng != jpn);

    // 7 s falls inside the third cue of both tracks.
    QCOMPARE(mpv_set_property_string(m_mpv, "sid", QByteArray::number(eng).constData()), 0);
    QCOMPARE(static_cast<int>(doubleProp("sid")), eng);
    QCOMPARE(stringProp("current-tracks/sub/lang"), QStringLiteral("eng"));
    QCOMPARE(subTextAfterSeek(7.0), QStringLiteral("Searchable keyword: albatross."));

    // Switching the track must change the text at the same timestamp -- this is
    // the whole point of wiring the panel's tabs to sid.
    QCOMPARE(mpv_set_property_string(m_mpv, "sid", QByteArray::number(jpn).constData()), 0);
    QCOMPARE(static_cast<int>(doubleProp("sid")), jpn);
    QCOMPARE(stringProp("current-tracks/sub/lang"), QStringLiteral("jpn"));

    const QString styled = subTextAfterSeek(7.0);
    QVERIFY2(styled.contains(QStringLiteral("Italic")), qPrintable(styled));
    QVERIFY2(styled.contains(QStringLiteral("hard space")), qPrintable(styled));

    // sid=no turns subtitles off entirely, which the panel needs a way to express.
    QCOMPARE(mpv_set_property_string(m_mpv, "sid", "no"), 0);
    QCOMPARE(stringProp("sid"), QStringLiteral("no"));
}

void TstMpvTracks::mpvLeavesEntitiesEncoded()
{
    const int eng = subTrackId(QStringLiteral("eng"));
    QVERIFY(eng > 0);
    QCOMPARE(mpv_set_property_string(m_mpv, "sid", QByteArray::number(eng).constData()), 0);

    // Trap 12, pinned down: ffmpeg's SRT decoder leaves character entities
    // literal, so libass renders "&amp;" over the video while the browser panel
    // shows "&" for the same cue. Both are correct by their own rules, and any
    // future comparison between panel text and mpv's text has to expect it.
    const QString text = subTextAfterSeek(4.0);
    QVERIFY2(text.contains(QStringLiteral("&amp;")), qPrintable(text));
}

void TstMpvTracks::fboCapLeavesOrdinaryWindowsAlone()
{
    // A normal window is well under the area limit and under the video size, so
    // nothing is capped and the picture stays exactly as sharp as before.
    const QSize video(1920, 1080);
    QCOMPARE(FboCap::cappedSize(QSize(1186, 733), video), QSize(1186, 733));
    QCOMPARE(FboCap::cappedSize(QSize(1920, 1040), video), QSize(1920, 1040));
}

void TstMpvTracks::fboCapHoldsTheAreaLimit()
{
    // 4K source, so the video size never engages -- this is the term that saves a
    // 4K file, where capping to the native size would do nothing at all.
    const QSize uhd(3840, 2160);

    const QSize fullscreen = FboCap::cappedSize(QSize(2560, 1345), uhd);
    QVERIFY2(fullscreen.width() * fullscreen.height() <= FboCap::SafeArea,
             qPrintable(QStringLiteral("%1x%2").arg(fullscreen.width())
                            .arg(fullscreen.height())));

    // Aspect ratio is preserved: mpv letterboxes inside the framebuffer and Qt
    // stretches it back over the pane, so a changed aspect would distort.
    const double before = 2560.0 / 1345.0;
    const double after = double(fullscreen.width()) / double(fullscreen.height());
    QVERIFY2(qAbs(before - after) < 0.01,
             qPrintable(QStringLiteral("%1 vs %2").arg(before).arg(after)));

    // The sizes measured as corrupt in trap 10 must all come out under the limit.
    for (const QSize &pane : {QSize(2560, 1345), QSize(2220, 1345), QSize(2560, 1440)}) {
        const QSize capped = FboCap::cappedSize(pane, uhd);
        QVERIFY2(capped.width() * capped.height() <= FboCap::SafeArea,
                 qPrintable(QStringLiteral("pane %1x%2").arg(pane.width())
                                .arg(pane.height())));
    }
}

void TstMpvTracks::fboCapStopsAtTheVideoSize()
{
    // 720p in a 2560 px pane: rendering at pane size buys nothing over rendering
    // at the video's own size and letting Qt scale, so the framebuffer collapses
    // to roughly the video's width.
    const QSize capped = FboCap::cappedSize(QSize(2560, 1345), QSize(1280, 720));
    QVERIFY2(capped.width() <= 1400, qPrintable(QString::number(capped.width())));
    QVERIFY(capped.width() >= 1200);
    QVERIFY(capped.width() * capped.height() <= FboCap::SafeArea);

    // A 4:3 video in a wide pane occupies only part of that pane, so the limit has
    // to come from the width the video actually covers, not the pane's width.
    const QSize narrow = FboCap::cappedSize(QSize(2560, 1345), QSize(640, 480));
    QVERIFY2(narrow.width() < 1200, qPrintable(QString::number(narrow.width())));
}

void TstMpvTracks::fboCapNeverUpscalesOrInverts()
{
    // 4K video in a small window: the framebuffer must stay the window's size, not
    // grow to the video's.
    QCOMPARE(FboCap::cappedSize(QSize(800, 450), QSize(3840, 2160)), QSize(800, 450));

    // Before mpv reports a video size, only the area limit applies.
    QCOMPARE(FboCap::cappedSize(QSize(1000, 600), QSize()), QSize(1000, 600));
    const QSize big = FboCap::cappedSize(QSize(2560, 1440), QSize());
    QVERIFY(big.width() * big.height() <= FboCap::SafeArea);

    // Degenerate input must not produce a zero or negative framebuffer.
    QCOMPARE(FboCap::cappedSize(QSize(0, 0), QSize(1920, 1080)), QSize(0, 0));
    const QSize tiny = FboCap::cappedSize(QSize(1, 1), QSize(1920, 1080));
    QVERIFY(tiny.width() >= 1 && tiny.height() >= 1);
}

namespace {

QVariantMap makeTrack(int id, const char *type, int ffIndex, bool selected = false,
                      const QString &externalFilename = QString())
{
    QVariantMap track;
    track[QStringLiteral("id")] = id;
    track[QStringLiteral("type")] = QLatin1String(type);
    track[QStringLiteral("ffIndex")] = ffIndex;
    track[QStringLiteral("selected")] = selected;
    track[QStringLiteral("external")] = !externalFilename.isEmpty();
    track[QStringLiteral("externalFilename")] = externalFilename;
    return track;
}

}  // namespace

void TstMpvTracks::mapsStreamIndexToSid()
{
    // Shape of a real file: video and audio occupy the low ffmpeg stream indices,
    // so mpv's sid never equals the stream index and the two must not be confused.
    const QVariantList tracks = {
        makeTrack(1, "video", 0),
        makeTrack(1, "audio", 1),
        makeTrack(1, "sub", 2),
        makeTrack(2, "sub", 3),
        makeTrack(3, "sub", 4),
    };

    QCOMPARE(MpvTrackList::subtitleIdForStream(tracks, 2), 1);
    QCOMPARE(MpvTrackList::subtitleIdForStream(tracks, 3), 2);
    QCOMPARE(MpvTrackList::subtitleIdForStream(tracks, 4), 3);

    // A stream mpv does not have, and the two ways of asking for nothing.
    QCOMPARE(MpvTrackList::subtitleIdForStream(tracks, 9), -1);
    QCOMPARE(MpvTrackList::subtitleIdForStream(tracks, -1), -1);
    QCOMPARE(MpvTrackList::subtitleIdForStream({}, 2), -1);
}

void TstMpvTracks::mapsSidecarPathToSid()
{
    const QString sidecar = fixture(QStringLiteral("sidecar.srt"));
    const QVariantList tracks = {
        makeTrack(1, "sub", 2),
        makeTrack(2, "sub", 0, false, sidecar),
    };

    QCOMPARE(MpvTrackList::subtitleIdForFile(tracks, sidecar), 2);

    // The same file named a different way still has to match: mpv reports an
    // absolute path while the extractor may hold whatever argv[1] contained.
    const QString messy = QStringLiteral(SUBIXA_TESTDATA_DIR "/../testdata/sidecar.srt");
    QCOMPARE(MpvTrackList::subtitleIdForFile(tracks, messy), 2);

    QCOMPARE(MpvTrackList::subtitleIdForFile(tracks, fixture(QStringLiteral("en.srt"))), -1);
    QCOMPARE(MpvTrackList::subtitleIdForFile(tracks, QString()), -1);

    // An external track's ff-index counts within its own file, so it must never
    // be matched as though it were an embedded stream index -- here the sidecar's
    // ff-index of 0 would otherwise shadow a real stream 0.
    QCOMPARE(MpvTrackList::subtitleIdForStream(tracks, 0), -1);
}

void TstMpvTracks::mappingIgnoresOtherTrackTypes()
{
    // An audio track sharing a subtitle track's ff-index must not be returned;
    // ff-index is unique per file, but the guard costs nothing and the failure
    // would be a silently muted or wrongly switched stream.
    const QVariantList tracks = {
        makeTrack(1, "audio", 2),
        makeTrack(1, "sub", 5),
    };
    QCOMPARE(MpvTrackList::subtitleIdForStream(tracks, 2), -1);
    QCOMPARE(MpvTrackList::subtitleIdForStream(tracks, 5), 1);

    QVERIFY(!MpvTrackList::isSelected(tracks, 1));
    const QVariantList selected = {makeTrack(4, "sub", 5, true)};
    QVERIFY(MpvTrackList::isSelected(selected, 4));
    QVERIFY(!MpvTrackList::isSelected(selected, 9));
}

void TstMpvTracks::splitsTracksByType()
{
    const QVariantList tracks = {
        makeTrack(1, "video", 0),
        makeTrack(1, "audio", 1),
        makeTrack(2, "audio", 2),
        makeTrack(1, "sub", 3),
    };

    const QVariantList audio = MpvTrackList::tracksOfType(tracks, "audio");
    QCOMPARE(audio.size(), 2);
    // mpv's own order, because that is the order the transport's menu lists them
    // in and a menu that reshuffles between openings is unusable.
    QCOMPARE(audio.at(0).toMap().value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(audio.at(1).toMap().value(QStringLiteral("id")).toInt(), 2);

    QCOMPARE(MpvTrackList::tracksOfType(tracks, "sub").size(), 1);
    QCOMPARE(MpvTrackList::tracksOfType(tracks, "video").size(), 1);
    QCOMPARE(MpvTrackList::tracksOfType(tracks, "audio").size(), 2);
    QVERIFY(MpvTrackList::tracksOfType(tracks, "nosuchtype").isEmpty());
    QVERIFY(MpvTrackList::tracksOfType({}, "sub").isEmpty());
}

namespace {

// One entry of SubtitleManager::tracks -- the browser's own numbering, which is
// an index into that list and has nothing to do with mpv's sid. Only the keys
// the reconciliation reads are set; the rest are labels for the tab bar.
QVariantMap makeBrowserTrack(int streamIndex, const QString &language,
                             bool browsable = true,
                             const QString &sidecarPath = QString(),
                             bool forced = false, bool hearingImpaired = false)
{
    QVariantMap track;
    track[QStringLiteral("streamIndex")] = streamIndex;
    track[QStringLiteral("language")] = language;
    track[QStringLiteral("browsable")] = browsable;
    track[QStringLiteral("sidecar")] = !sidecarPath.isEmpty();
    track[QStringLiteral("sourcePath")] = sidecarPath;
    track[QStringLiteral("forced")] = forced;
    track[QStringLiteral("hearingImpaired")] = hearingImpaired;
    return track;
}

// One entry of the cross-file preference: a kind of track, not only a language.
QVariantMap preferenceEntry(const QString &language, bool forced = false,
                            bool hearingImpaired = false,
                            bool visualImpaired = false)
{
    QVariantMap out;
    out[QStringLiteral("language")] = language;
    out[QStringLiteral("forced")] = forced;
    out[QStringLiteral("hearingImpaired")] = hearingImpaired;
    out[QStringLiteral("visualImpaired")] = visualImpaired;
    return out;
}

// What PlaybackHistory::preferredTracks() returns, for the ordinary one-entry
// case. An empty language is no preference at all rather than an entry that
// matches nothing -- the same distinction the store makes.
QVariantList preferred(const QString &language, bool forced = false,
                       bool hearingImpaired = false)
{
    if (language.isEmpty())
        return {};
    return QVariantList{preferenceEntry(language, forced, hearingImpaired)};
}

// What PlaybackHistory::subtitleFor() returns for a file a track has been chosen
// in. An empty map is a file that has never had one.
QVariantMap remembered(int streamIndex, const QString &sidecarPath,
                       const QString &language)
{
    QVariantMap out;
    out[QStringLiteral("streamIndex")] = streamIndex;
    out[QStringLiteral("sidecarPath")] = sidecarPath;
    out[QStringLiteral("language")] = language;
    return out;
}

}  // namespace

void TstMpvTracks::opensOnTheTrackThisFileWasLeftOn()
{
    const QVariantList tracks = {
        makeBrowserTrack(2, QStringLiteral("eng")),
        makeBrowserTrack(3, QStringLiteral("jpn")),
        makeBrowserTrack(4, QStringLiteral("fre")),
    };

    // The remembered track wins over both fallbacks, including a language that
    // would otherwise pick a different tab.
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, remembered(4, QString(), QStringLiteral("fre")),
                 preferred(QStringLiteral("eng"))),
             2);

    // Stream index, not position: the two coincide nowhere here, which is the
    // whole reason the browser cannot just store a tab number.
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, remembered(2, QString(), QStringLiteral("eng")), preferred(QString())),
             0);

    // Through QML the map makes a round trip via JavaScript, where every number
    // is a double. It has to mean the same thing coming back.
    QVariantMap viaJs;
    viaJs[QStringLiteral("streamIndex")] = 3.0;
    viaJs[QStringLiteral("sidecarPath")] = QString();
    viaJs[QStringLiteral("language")] = QStringLiteral("jpn");
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, viaJs, preferred(QString())), 1);

    // A stream this file no longer has -- a remux, or an entry from a file that
    // has since been replaced -- falls through rather than selecting nothing.
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, remembered(9, QString(), QStringLiteral("jpn")),
                 preferred(QStringLiteral("jpn"))),
             1);

    // A file no track has ever been chosen in: no entry at all, which is not the
    // same as an entry saying stream 0.
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, preferred(QString())), 0);
}

void TstMpvTracks::aRememberedSidecarSurvivesBeingRenamed()
{
    const QString sidecar = fixture(QStringLiteral("sidecar.srt"));
    const QVariantList tracks = {
        makeBrowserTrack(2, QStringLiteral("eng")),
        makeBrowserTrack(0, QStringLiteral("fre"), true, sidecar),
    };

    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, remembered(-1, sidecar, QStringLiteral("fre")), preferred(QString())),
             1);

    // The store holds an absolute path and the extractor may hold whatever
    // spelling the file was opened with, so the comparison cannot be a string
    // one. It was, and a sidecar opened by a relative path came back on the
    // wrong tab.
    const QString messy = QStringLiteral(SUBIXA_TESTDATA_DIR "/../testdata/sidecar.srt");
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, remembered(-1, messy, QStringLiteral("fre")), preferred(QString())),
             1);

    // The two branches must not leak into each other, and the list is ordered so
    // that a leak gives a different answer from the fallback.
    const QVariantList sidecarFirst = {
        makeBrowserTrack(0, QStringLiteral("fre"), true, sidecar),
        makeBrowserTrack(0, QStringLiteral("eng")),
    };
    // A remembered sidecar that is no longer beside the film must not select the
    // embedded track that happens to carry the stream index stored with it: an
    // external track's index counts within its own file. Falls back to the first
    // browsable track, which here is the sidecar rather than index 1.
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 sidecarFirst,
                 remembered(0, fixture(QStringLiteral("gone.srt")), QString()),
                 preferred(QString())),
             0);
    // And the reverse: a remembered embedded stream must find the embedded track
    // rather than the sidecar sitting at stream 0 of its own file.
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 sidecarFirst, remembered(0, QString(), QString()), preferred(QString())),
             1);
}

void TstMpvTracks::fallsBackToTheLanguageThenTheFirstTrack()
{
    const QVariantList tracks = {
        makeBrowserTrack(2, QStringLiteral("eng")),
        makeBrowserTrack(3, QStringLiteral("eng")),
        makeBrowserTrack(4, QStringLiteral("fre")),
    };

    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, preferred(QStringLiteral("fre"))), 2);
    // Two tracks in one language is the ordinary case, not the exotic one -- a
    // film with English and English SDH has it. Nothing here tells these two
    // apart, so the first wins.
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, preferred(QStringLiteral("eng"))), 0);
    // A language the file does not carry, and no language at all.
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, preferred(QStringLiteral("deu"))), 0);
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, preferred(QString())), 0);
}

void TstMpvTracks::carriesTheKindOfTrackForwardNotOnlyTheLanguage()
{
    // The shape a release actually ships: plain and SDH in one language, tagged
    // identically apart from the flag. Found on a 37-track file where choosing
    // English SDH and letting the playlist advance put the plain English track
    // up in the next episode, every time.
    const QVariantList tracks = {
        makeBrowserTrack(7, QStringLiteral("eng")),
        makeBrowserTrack(8, QStringLiteral("eng"), true, QString(), false, true),
        makeBrowserTrack(3, QStringLiteral("ita"), true, QString(), true, false),
        makeBrowserTrack(6, QStringLiteral("ita"), true, QString(), false, true),
    };

    // The flavour picks between two tracks the language cannot separate, in
    // either direction -- this is not "prefer SDH", it is "prefer what was
    // chosen".
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, {}, preferred(QStringLiteral("eng"), false, true)),
             1);
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, {}, preferred(QStringLiteral("eng"))),
             0);
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, {}, preferred(QStringLiteral("ita"), true, false)),
             2);

    // The language is the requirement and the flavour only a preference: a file
    // with no SDH track in the language still opens on that language rather than
    // falling through to the first browsable track in another one.
    const QVariantList noSdh = {
        makeBrowserTrack(2, QStringLiteral("fre"), true, QString(), false, true),
        makeBrowserTrack(3, QStringLiteral("eng")),
        makeBrowserTrack(4, QStringLiteral("eng"), true, QString(), true, false),
    };
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 noSdh, {}, preferred(QStringLiteral("eng"), false, true)),
             1);

    // And the track remembered for this very file still outranks all of it: an
    // explicit choice here beats an inference from somewhere else.
    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, remembered(7, QString(), QStringLiteral("eng")),
                 preferred(QStringLiteral("eng"), false, true)),
             0);
}

void TstMpvTracks::walksThePreferenceListInOrder()
{
    // A dual-audio release with English SDH beside plain English: the shape both
    // halves of the preference exist for.
    const QVariantList tracks = {
        makeBrowserTrack(2, QStringLiteral("jpn")),
        makeBrowserTrack(3, QStringLiteral("eng")),
        makeBrowserTrack(4, QStringLiteral("eng"), true, QString(), false, true),
    };

    const QVariantList engSdhThenJpn = {
        preferenceEntry(QStringLiteral("eng"), false, true),
        preferenceEntry(QStringLiteral("jpn"))
    };
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, engSdhThenJpn), 2);

    // Order decides, and it decides *within* an entry too: putting English first
    // means plain English beats Japanese, because a language match is what an
    // entry is for and the flavour is only how it is refined.
    const QVariantList engSdhThenJpnNoSdh = {
        preferenceEntry(QStringLiteral("eng"), false, true),
        preferenceEntry(QStringLiteral("jpn"))
    };
    const QVariantList noSdhAvailable = {
        makeBrowserTrack(2, QStringLiteral("jpn")),
        makeBrowserTrack(3, QStringLiteral("eng")),
    };
    QCOMPARE(MpvTrackList::preferredTrackIndex(noSdhAvailable, {}, engSdhThenJpnNoSdh), 1);

    // Reversing the list reverses the answer -- otherwise the order is decoration.
    const QVariantList jpnFirst = {
        preferenceEntry(QStringLiteral("jpn")),
        preferenceEntry(QStringLiteral("eng"), false, true)
    };
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, jpnFirst), 0);

    // An entry the file cannot satisfy is skipped rather than ending the walk.
    const QVariantList missingFirst = {
        preferenceEntry(QStringLiteral("fre")),
        preferenceEntry(QStringLiteral("eng"), false, true)
    };
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, missingFirst), 2);
}

void TstMpvTracks::aFallbackTabIsNotASelection()
{
    const QVariantList tracks = {
        makeBrowserTrack(2, QStringLiteral("jpn")),
        makeBrowserTrack(3, QStringLiteral("eng")),
    };

    // The distinction the whole "follow the file" mode rests on. With no
    // preference the panel still has to light a tab -- it cannot show nothing --
    // but that tab is not a choice, and pushing it at mpv would override the
    // track the release itself flagged as default.
    const MpvTrackList::TrackChoice fallback =
        MpvTrackList::preferredTrackChoice(tracks, {}, {});
    QCOMPARE(fallback.index, 0);
    QVERIFY(!fallback.matched);

    // A preference that matches is a choice.
    const MpvTrackList::TrackChoice chosen = MpvTrackList::preferredTrackChoice(
        tracks, {}, preferred(QStringLiteral("eng")));
    QCOMPARE(chosen.index, 1);
    QVERIFY(chosen.matched);

    // So is this file's own remembered track.
    const MpvTrackList::TrackChoice remembered = MpvTrackList::preferredTrackChoice(
        tracks, ::remembered(3, QString(), QStringLiteral("eng")), {});
    QCOMPARE(remembered.index, 1);
    QVERIFY(remembered.matched);

    // A preference naming a language the file has not got is not a match, so the
    // file's default still stands rather than being overridden by the first tab.
    const MpvTrackList::TrackChoice missed = MpvTrackList::preferredTrackChoice(
        tracks, {}, preferred(QStringLiteral("fre")));
    QCOMPARE(missed.index, 0);
    QVERIFY(!missed.matched);
}

void TstMpvTracks::picksAnAudioTrackOrLeavesMpvAlone()
{
    // mpv's numbering: aid 1 and 2 over ffmpeg streams 1 and 2, with the
    // Japanese track flagged default -- the dual-audio shape exactly.
    QVariantMap japanese = makeTrack(1, "audio", 1, true);
    japanese[QStringLiteral("language")] = QStringLiteral("ja");
    QVariantMap english = makeTrack(2, "audio", 2);
    english[QStringLiteral("language")] = QStringLiteral("en");
    QVariantMap describes = makeTrack(3, "audio", 3);
    describes[QStringLiteral("language")] = QStringLiteral("en");
    describes[QStringLiteral("visualImpaired")] = true;
    const QVariantList tracks = {makeTrack(1, "video", 0), japanese, english, describes};

    // -1 is "no opinion", which leaves the container's default playing. Not a
    // first-track fallback: there is no panel here that has to be showing
    // something, so having nothing to say is a perfectly good answer.
    QCOMPARE(MpvTrackList::preferredAudioId(tracks, {}, {}), -1);

    // The stored code is three letters and mpv's is two. This is the comparison
    // that silently did nothing before they were normalised.
    QCOMPARE(MpvTrackList::preferredAudioId(tracks, {},
                                            preferred(QStringLiteral("eng"))),
             2);
    // Audio description is the audio side's flavour, and it is a preference in
    // both directions: asking for it gets it, not asking for it avoids it.
    QVariantList wantsDescription = {
        preferenceEntry(QStringLiteral("eng"), false, false, true)
    };
    QCOMPARE(MpvTrackList::preferredAudioId(tracks, {}, wantsDescription), 3);

    // This file's remembered track wins over the preference, by ff-index.
    QVariantMap rememberedAudio;
    rememberedAudio[QStringLiteral("streamIndex")] = 1;
    QCOMPARE(MpvTrackList::preferredAudioId(tracks, rememberedAudio,
                                            preferred(QStringLiteral("eng"))),
             1);
    // A stream this file no longer has falls through to the preference rather
    // than selecting nothing.
    QVariantMap gone;
    gone[QStringLiteral("streamIndex")] = 9;
    QCOMPARE(MpvTrackList::preferredAudioId(tracks, gone,
                                            preferred(QStringLiteral("eng"))),
             2);
    // Subtitle tracks are not audio tracks, however well they match.
    QVariantMap engSub = makeTrack(1, "sub", 4);
    engSub[QStringLiteral("language")] = QStringLiteral("en");
    QCOMPARE(MpvTrackList::preferredAudioId({makeTrack(1, "video", 0), engSub}, {},
                                            preferred(QStringLiteral("eng"))),
             -1);
}

void TstMpvTracks::oneSpellingOfALanguage()
{
    // Three sources name a language and none of them agree: ffmpeg hands over
    // what the container holds, mpv publishes two-letter codes, and the settings
    // page offers a third list. An audio preference of "eng" matched no track
    // mpv called "en", so the preference simply never fired.
    QCOMPARE(MpvTrackList::canonicalLanguage(QStringLiteral("en")),
             QStringLiteral("eng"));
    QCOMPARE(MpvTrackList::canonicalLanguage(QStringLiteral("eng")),
             QStringLiteral("eng"));
    // The 639-2/B and /T split, which containers and libraries disagree on.
    QCOMPARE(MpvTrackList::canonicalLanguage(QStringLiteral("deu")),
             MpvTrackList::canonicalLanguage(QStringLiteral("ger")));
    // Region tags are a distinction this preference does not draw: mpv reports
    // "es-419" for Latin American Spanish, and it is still Spanish.
    QCOMPARE(MpvTrackList::canonicalLanguage(QStringLiteral("es-419")),
             MpvTrackList::canonicalLanguage(QStringLiteral("spa")));

    // A code nothing can place still has to match itself, or a track tagged with
    // something obscure becomes unselectable.
    QCOMPARE(MpvTrackList::canonicalLanguage(QStringLiteral("qzz")),
             QStringLiteral("qzz"));
    QVERIFY(MpvTrackList::sameLanguage(QStringLiteral("QZZ"), QStringLiteral("qzz")));
    // Empty is not a language, and must not match another empty one -- half the
    // tracks in an untagged file would answer to it.
    QVERIFY(!MpvTrackList::sameLanguage(QString(), QString()));
}

void TstMpvTracks::skipsTracksTheBrowserCannotList()
{
    // A PGS track and a text track that decoded to nothing. Both are in the list
    // so the tab bar can say why they cannot be read, and neither may be landed
    // on by any of the three passes.
    const QVariantList tracks = {
        makeBrowserTrack(2, QStringLiteral("eng"), false),
        makeBrowserTrack(3, QStringLiteral("fre"), false),
        makeBrowserTrack(4, QStringLiteral("fre")),
    };

    QCOMPARE(MpvTrackList::preferredTrackIndex(
                 tracks, remembered(2, QString(), QStringLiteral("eng")), preferred(QString())),
             2);
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, preferred(QStringLiteral("fre"))), 2);
    QCOMPARE(MpvTrackList::preferredTrackIndex(tracks, {}, preferred(QString())), 2);

    // A file whose subtitles are all bitmap opens on no tab at all, rather than
    // on the previous film's tab over another film's cues.
    const QVariantList unreadable = {
        makeBrowserTrack(2, QStringLiteral("eng"), false),
    };
    QCOMPARE(MpvTrackList::preferredTrackIndex(unreadable, {}, preferred(QStringLiteral("eng"))), -1);
    QCOMPARE(MpvTrackList::preferredTrackIndex({}, {}, preferred(QString())), -1);
}

void TstMpvTracks::findsTheTabForMpvsSubtitle()
{
    const QString sidecar = fixture(QStringLiteral("sidecar.srt"));
    // mpv's numbering: sid 1..3 over ffmpeg streams 2..4, plus a sidecar it
    // auto-loaded. Nothing about it follows from the browser's list below.
    const QVariantList mpvTracks = {
        makeTrack(1, "video", 0),
        makeTrack(1, "audio", 1),
        makeTrack(1, "sub", 2),
        makeTrack(2, "sub", 3),
        makeTrack(3, "sub", 4),
        makeTrack(4, "sub", 0, false, sidecar),
    };
    const QVariantList browserTracks = {
        makeBrowserTrack(2, QStringLiteral("eng")),
        makeBrowserTrack(4, QStringLiteral("fre")),
        makeBrowserTrack(0, QStringLiteral("fre"), true, sidecar),
    };

    QCOMPARE(MpvTrackList::browserIndexForSubtitleId(browserTracks, mpvTracks, 1), 0);
    QCOMPARE(MpvTrackList::browserIndexForSubtitleId(browserTracks, mpvTracks, 3), 1);
    QCOMPARE(MpvTrackList::browserIndexForSubtitleId(browserTracks, mpvTracks, 4), 2);

    // sid 2 is stream 3, which the browser has no tab for -- a bitmap track, say.
    // The tab stays where it is rather than moving to something arbitrary.
    QCOMPARE(MpvTrackList::browserIndexForSubtitleId(browserTracks, mpvTracks, 2), -1);
    // Subtitles off.
    QCOMPARE(MpvTrackList::browserIndexForSubtitleId(browserTracks, mpvTracks, -1), -1);
    // mpv has not caught up with the file yet: it and the extractor open it
    // independently, so the browser can have tabs before mpv has a track list.
    QCOMPARE(MpvTrackList::browserIndexForSubtitleId(browserTracks, {}, 1), -1);

    // The sidecar again, spelled the other way round in the browser's list.
    const QVariantList messyBrowser = {
        makeBrowserTrack(0, QStringLiteral("fre"), true,
                         QStringLiteral(SUBIXA_TESTDATA_DIR "/../testdata/sidecar.srt")),
    };
    QCOMPARE(MpvTrackList::browserIndexForSubtitleId(messyBrowser, mpvTracks, 4), 0);

    // A tab the browser cannot list is never the answer, even when its stream
    // index would match.
    const QVariantList unreadable = {
        makeBrowserTrack(2, QStringLiteral("eng"), false),
    };
    QCOMPARE(MpvTrackList::browserIndexForSubtitleId(unreadable, mpvTracks, 1), -1);
}

void TstMpvTracks::storesAnMpvTrackTheWayTheHistoryKeepsIt()
{
    QVariantMap embedded = makeTrack(2, "sub", 3);
    embedded[QStringLiteral("language")] = QStringLiteral("jpn");
    embedded[QStringLiteral("hearingImpaired")] = true;
    const QVariantMap forEmbedded = MpvTrackList::historyEntryForTrack(embedded);
    // ff-index, not sid: sid is mpv's own numbering and means nothing to the
    // extractor, which is what has to find this track again next time.
    QCOMPARE(forEmbedded.value(QStringLiteral("streamIndex")).toInt(), 3);
    QVERIFY(forEmbedded.value(QStringLiteral("sidecarPath")).toString().isEmpty());
    QCOMPARE(forEmbedded.value(QStringLiteral("language")).toString(),
             QStringLiteral("jpn"));
    // The flavour comes through too: a choice made in the transport menu has to
    // carry the same thing forward as one made on a tab, or which control was
    // used would decide what the next file opens on.
    QVERIFY(forEmbedded.value(QStringLiteral("hearingImpaired")).toBool());
    QVERIFY(!forEmbedded.value(QStringLiteral("forced")).toBool());

    const QString sidecar = fixture(QStringLiteral("sidecar.srt"));
    QVariantMap external = makeTrack(4, "sub", 0, false, sidecar);
    external[QStringLiteral("language")] = QStringLiteral("fre");
    const QVariantMap forExternal = MpvTrackList::historyEntryForTrack(external);
    // -1 rather than the external track's ff-index of 0, which counts within the
    // sidecar's own file and would name an unrelated embedded stream on reopen.
    QCOMPARE(forExternal.value(QStringLiteral("streamIndex")).toInt(), -1);
    QCOMPARE(forExternal.value(QStringLiteral("sidecarPath")).toString(), sidecar);
    QCOMPARE(forExternal.value(QStringLiteral("language")).toString(),
             QStringLiteral("fre"));

    // A track with no language tag and no ff-index at all -- mpv reports both as
    // absent often enough. It must produce a storable entry rather than nothing.
    QVariantMap bare;
    bare[QStringLiteral("id")] = 1;
    bare[QStringLiteral("type")] = QStringLiteral("sub");
    const QVariantMap forBare = MpvTrackList::historyEntryForTrack(bare);
    QCOMPARE(forBare.value(QStringLiteral("streamIndex")).toInt(), -1);
    QVERIFY(forBare.value(QStringLiteral("sidecarPath")).toString().isEmpty());
    QVERIFY(forBare.value(QStringLiteral("language")).toString().isEmpty());

    // And what the two halves have to agree on: what is stored for a track has
    // to find that same track again. Round-trip the sidecar through the store's
    // shape and back into the browser's numbering.
    const QVariantList browserTracks = {
        makeBrowserTrack(3, QStringLiteral("jpn")),
        makeBrowserTrack(0, QStringLiteral("fre"), true, sidecar),
    };
    QCOMPARE(MpvTrackList::preferredTrackIndex(browserTracks, forExternal, preferred(QString())), 1);
    QCOMPARE(MpvTrackList::preferredTrackIndex(browserTracks, forEmbedded, preferred(QString())), 0);
}

QTEST_MAIN(TstMpvTracks)
#include "tst_mpvtracks.moc"
