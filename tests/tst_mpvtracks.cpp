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

QTEST_MAIN(TstMpvTracks)
#include "tst_mpvtracks.moc"
