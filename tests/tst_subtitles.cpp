// Headless coverage for everything below the scene graph: the extractor, the row
// model's binary search, and the search proxy's row mapping.
//
// Why this exists: the browser was previously verified only by screenshotting a
// real window, and WSLg can degrade into painting stale or partial frames while
// the clock keeps advancing (see CLAUDE.md). Visual checks then return false
// negatives, and there was nothing else to fall back on. None of the logic here
// needs a window, a GL context, or mpv.
//
// Cue timestamps come out ~23 ms later than the .srt/.ass sources say, because
// the fixtures are muxed against testclip.mp4 and inherit its first PTS. Tests
// therefore assert timings with a tolerance and text exactly.

#include <QtTest>

#include <QtCore/QFileInfo>

#include "SubtitleExtractor.h"
#include "SubtitleFilterModel.h"
#include "SubtitleLineModel.h"
#include "SubtitleTypes.h"

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(CMP_TESTDATA_DIR "/") + name;
}

// Runs the extractor synchronously. It is a plain QObject; the worker thread it
// normally lives on is SubtitleManager's business, not the parser's.
SubtitleTrackList parse(const QString &path, QString *error = nullptr)
{
    SubtitleExtractor extractor;
    SubtitleTrackList result;
    QString failure;

    QObject::connect(&extractor, &SubtitleExtractor::finished, &extractor,
                     [&](int, const SubtitleTrackList &tracks) { result = tracks; });
    QObject::connect(&extractor, &SubtitleExtractor::failed, &extractor,
                     [&](int, const QString &reason) { failure = reason; });

    extractor.setCurrentRequest(1);
    extractor.extract(path, 1);

    if (error)
        *error = failure;
    return result;
}

const SubtitleTrack *trackByLanguage(const SubtitleTrackList &tracks, const QString &lang)
{
    for (const SubtitleTrack &t : tracks) {
        if (t.language == lang)
            return &t;
    }
    return nullptr;
}

}  // namespace

class TstSubtitles : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void embeddedTracksAreEnumerated();
    void srtTextIsDecodedAndTagsStripped();
    void assTagsAndEscapesAreResolved();
    void movTextKeepsItsFirstWords();
    void wholesaleOffsetRebasesToZero();
    void subtitlesAlreadyAtZeroAreNotRebased();
    void sidecarsAreFoundNextToTheVideo();

    void indexAtFindsTheCurrentCue();
    void indexAtOnEmptyModel();

    void filterMatchesTextCaseInsensitively();
    void filterDoesNotSearchTimestamps();
    void nonBreakingSpaceDefeatsSearch();
    void rowAtMapsThroughTheFilter();
    void startMsAtRoundTripsThroughTheFilter();
};

void TstSubtitles::initTestCase()
{
    if (!QFileInfo::exists(fixture(QStringLiteral("subs.mkv")))) {
        QSKIP("fixtures missing -- run ./testdata/make-fixtures.sh");
    }
}

void TstSubtitles::embeddedTracksAreEnumerated()
{
    const SubtitleTrackList tracks = parse(fixture(QStringLiteral("subs.mkv")));
    QCOMPARE(tracks.size(), 3);

    // Order follows the container's stream order, and ids index the list itself.
    QCOMPARE(tracks[0].language, QStringLiteral("eng"));
    QCOMPARE(tracks[1].language, QStringLiteral("jpn"));
    QCOMPARE(tracks[2].language, QStringLiteral("fre"));

    for (int i = 0; i < tracks.size(); ++i) {
        QCOMPARE(tracks[i].id, i);
        QVERIFY(!tracks[i].sidecar);
        // Classification comes from the codec descriptor's property flags, not a
        // name list (trap 8), so text codecs must land as Text.
        QCOMPARE(tracks[i].kind, SubtitleKind::Text);
        QVERIFY(tracks[i].streamIndex >= 0);
        QVERIFY(!tracks[i].lines.isEmpty());
    }

    QCOMPARE(tracks[0].lines.size(), 4);
    QCOMPARE(tracks[1].lines.size(), 4);
    QCOMPARE(tracks[2].lines.size(), 3);
}

void TstSubtitles::srtTextIsDecodedAndTagsStripped()
{
    const SubtitleTrackList tracks = parse(fixture(QStringLiteral("subs.mkv")));
    const SubtitleTrack *eng = trackByLanguage(tracks, QStringLiteral("eng"));
    QVERIFY(eng);

    // Both source rows belong to one cue and stay one cue, newline included.
    QCOMPARE(eng->lines[0].text,
             QStringLiteral("First line of English.\nSecond row, same cue."));

    // ffmpeg turns <i> into ASS override tags, which are stripped for display,
    // but it leaves &amp; literal -- decoding entities is on our side (trap 7).
    QCOMPARE(eng->lines[1].text, QStringLiteral("Italic markup and a & entity."));
    QVERIFY(!eng->lines[1].text.contains(QStringLiteral("&amp;")));
    QVERIFY(!eng->lines[1].text.contains(QLatin1Char('{')));

    QCOMPARE(eng->lines[2].text, QStringLiteral("Searchable keyword: albatross."));
    QCOMPARE(eng->lines[3].text, QStringLiteral("Final English cue."));

    // Source says 00:00:00,500; the muxed fixture carries testclip's first PTS.
    QVERIFY2(qAbs(eng->lines[0].startMs - 500) < 100,
             qPrintable(QStringLiteral("startMs=%1").arg(eng->lines[0].startMs)));
    QVERIFY(eng->lines[0].endMs > eng->lines[0].startMs);

    // Cues must be sorted by start time -- indexAt()'s binary search depends on it.
    for (int i = 1; i < eng->lines.size(); ++i)
        QVERIFY(eng->lines[i].startMs >= eng->lines[i - 1].startMs);
}

void TstSubtitles::assTagsAndEscapesAreResolved()
{
    const SubtitleTrackList tracks = parse(fixture(QStringLiteral("subs.mkv")));
    const SubtitleTrack *ass = trackByLanguage(tracks, QStringLiteral("jpn"));
    QVERIFY(ass);

    // Every text decoder emits ASS, and the payload's text begins after the 8th
    // comma (trap 5). Splitting it wrong silently eats the first words, so these
    // assertions are really about the field layout.
    QCOMPARE(ass->lines[0].text, QStringLiteral("Styled opener with override tags."));
    QCOMPARE(ass->lines[1].text, QStringLiteral("Two lines here\nsplit by a hard break."));
    // ASS \h is a *non-breaking* space and stays one: U+00A0, not U+0020. Built
    // from an explicit codepoint because the two are indistinguishable on sight
    // in this file -- and the difference has a consequence for search, pinned
    // down by nonBreakingSpaceDefeatsSearch() below.
    const QString nbsp(QChar(0x00A0));
    QCOMPARE(ass->lines[2].text,
             QStringLiteral("Italic then normal, plus a") + nbsp
                 + QStringLiteral("hard space."));
    QCOMPARE(ass->lines[3].text, QStringLiteral("Top-positioned final cue: albatross."));

    // Raw payload is kept for styling work later, so the tags must survive there.
    QVERIFY(ass->lines[0].rawText.contains(QStringLiteral("\\pos")));
}

void TstSubtitles::movTextKeepsItsFirstWords()
{
    const QString path = fixture(QStringLiteral("movtext.mp4"));
    if (!QFileInfo::exists(path))
        QSKIP("movtext.mp4 missing -- run ./testdata/make-fixtures.sh");

    const SubtitleTrackList tracks = parse(path);
    QCOMPARE(tracks.size(), 1);
    QCOMPARE(tracks[0].kind, SubtitleKind::Text);
    QCOMPARE(tracks[0].lines.size(), 4);
    QCOMPARE(tracks[0].lines[0].text,
             QStringLiteral("First line of English.\nSecond row, same cue."));
}

void TstSubtitles::wholesaleOffsetRebasesToZero()
{
    const QString path = fixture(QStringLiteral("shifted.mkv"));
    if (!QFileInfo::exists(path))
        QSKIP("shifted.mkv missing -- run ./testdata/make-fixtures.sh");

    const SubtitleTrackList tracks = parse(path);
    QVERIFY(!tracks.isEmpty());
    QVERIFY(!tracks[0].lines.isEmpty());

    // Container starts an hour in and so do the cues. mpv rebases playback to
    // zero, so cues must follow or every seek lands 3600 s out (trap 6).
    QVERIFY2(tracks[0].lines[0].startMs < 60'000,
             qPrintable(QStringLiteral("startMs=%1").arg(tracks[0].lines[0].startMs)));
}

void TstSubtitles::subtitlesAlreadyAtZeroAreNotRebased()
{
    const QString path = fixture(QStringLiteral("shifted.mp4"));
    if (!QFileInfo::exists(path))
        QSKIP("shifted.mp4 missing -- run ./testdata/make-fixtures.sh");

    const SubtitleTrackList tracks = parse(path);
    QVERIFY(!tracks.isEmpty());
    const QVector<SubtitleLine> &lines = tracks[0].lines;
    QVERIFY(lines.size() >= 3);

    // The other half of trap 6: video starts at 1 h while this subtitle stream
    // still starts at 0. Subtracting the container offset here would flatten
    // every cue onto 00:00:00, so only a track whose own first cue is at or past
    // the offset may be shifted.
    QVERIFY2(qAbs(lines[0].startMs - 500) < 100,
             qPrintable(QStringLiteral("startMs=%1").arg(lines[0].startMs)));
    QVERIFY2(lines[1].startMs > lines[0].startMs,
             "cues collapsed onto one timestamp -- offset was subtracted twice");
    QVERIFY(lines[2].startMs > lines[1].startMs);
}

void TstSubtitles::sidecarsAreFoundNextToTheVideo()
{
    const QString path = fixture(QStringLiteral("sidecar.mp4"));
    if (!QFileInfo::exists(path))
        QSKIP("sidecar.mp4 missing -- run ./testdata/make-fixtures.sh");

    const SubtitleTrackList tracks = parse(path);
    // sidecar.srt, sidecar.ass and sidecar.fr.srt all sit beside the video.
    QCOMPARE(tracks.size(), 3);
    for (const SubtitleTrack &t : tracks) {
        QVERIFY(t.sidecar);
        QVERIFY(!t.lines.isEmpty());
        QVERIFY(!t.sourcePath.isEmpty());
        QVERIFY(t.sourcePath != path);
    }

    // The language suffix in sidecar.fr.srt is where a sidecar's tag comes from.
    QVERIFY(trackByLanguage(tracks, QStringLiteral("fr"))
            || trackByLanguage(tracks, QStringLiteral("fre")));
}

void TstSubtitles::indexAtFindsTheCurrentCue()
{
    QVector<SubtitleLine> lines;
    lines.append({1000, 2000, QStringLiteral("one"), {}});
    lines.append({3000, 4000, QStringLiteral("two"), {}});
    lines.append({5000, 6000, QStringLiteral("three"), {}});

    SubtitleLineModel model;
    model.setLines(lines);
    QCOMPARE(model.count(), 3);

    QCOMPARE(model.indexAt(0), -1);       // before the first cue
    QCOMPARE(model.indexAt(999), -1);     // still before it
    QCOMPARE(model.indexAt(1000), 0);     // exactly on the start
    QCOMPARE(model.indexAt(1500), 0);     // inside
    QCOMPARE(model.indexAt(2500), 0);     // in the gap: most recently started
    QCOMPARE(model.indexAt(3000), 1);
    QCOMPARE(model.indexAt(5999), 2);
    QCOMPARE(model.indexAt(9'999'999), 2);  // past the end, still the last cue
}

void TstSubtitles::indexAtOnEmptyModel()
{
    // Models are reused across loads and emptied rather than deleted, so an
    // empty one is a normal state a QML binding can hit mid-load.
    SubtitleLineModel model;
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.indexAt(0), -1);
    QCOMPARE(model.indexAt(1234), -1);
}

void TstSubtitles::filterMatchesTextCaseInsensitively()
{
    QVector<SubtitleLine> lines;
    lines.append({1000, 2000, QStringLiteral("Searchable keyword: albatross."), {}});
    lines.append({3000, 4000, QStringLiteral("Final English cue."), {}});
    lines.append({5000, 6000, QStringLiteral("An ALBATROSS again."), {}});

    SubtitleLineModel model;
    model.setLines(lines);
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    QCOMPARE(filter.count(), 3);
    QCOMPARE(filter.sourceCount(), 3);

    filter.setPattern(QStringLiteral("albatross"));
    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.sourceCount(), 3);

    filter.setPattern(QStringLiteral("ALBA"));
    QCOMPARE(filter.count(), 2);

    filter.setPattern(QStringLiteral("english"));
    QCOMPARE(filter.count(), 1);

    filter.setPattern(QStringLiteral("nothing here"));
    QCOMPARE(filter.count(), 0);

    filter.setPattern(QString());
    QCOMPARE(filter.count(), 3);
}

void TstSubtitles::filterDoesNotSearchTimestamps()
{
    QVector<SubtitleLine> lines;
    lines.append({12'000, 13'000, QStringLiteral("no digits in this line"), {}});
    lines.append({20'000, 21'000, QStringLiteral("mentions 12 explicitly"), {}});

    SubtitleLineModel model;
    model.setLines(lines);
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    // Someone typing "12" means words, not 00:00:12.000 -- so only the line that
    // literally says 12 may match, even though both cues have 12 in a timestamp.
    filter.setPattern(QStringLiteral("12"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.data(filter.index(0, 0), SubtitleLineModel::TextRole).toString(),
             QStringLiteral("mentions 12 explicitly"));
}

void TstSubtitles::nonBreakingSpaceDefeatsSearch()
{
    const QString nbsp(QChar(0x00A0));
    QVector<SubtitleLine> lines;
    lines.append({1000, 2000,
                  QStringLiteral("plus a") + nbsp + QStringLiteral("hard space."), {}});

    SubtitleLineModel model;
    model.setLines(lines);
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    // Known gap, recorded rather than fixed: the cue holds U+00A0 where the user
    // typed U+0020, so a phrase spanning an ASS \h finds nothing. It hides well
    // because searching either side of the hard space works fine, and the panel
    // renders the two identically. Folding U+00A0 to a space in
    // filterAcceptsRow() would close it; when that happens this test flips to
    // an unexpected pass and says so.
    filter.setPattern(QStringLiteral("a hard space"));
    QEXPECT_FAIL("", "search does not fold U+00A0 to a plain space", Continue);
    QCOMPARE(filter.count(), 1);

    filter.setPattern(QStringLiteral("hard space"));
    QCOMPARE(filter.count(), 1);
}

void TstSubtitles::rowAtMapsThroughTheFilter()
{
    QVector<SubtitleLine> lines;
    lines.append({1000, 2000, QStringLiteral("alpha"), {}});
    lines.append({3000, 4000, QStringLiteral("beta"), {}});
    lines.append({5000, 6000, QStringLiteral("alpha again"), {}});

    SubtitleLineModel model;
    model.setLines(lines);
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    // Unfiltered, view rows are source rows.
    QCOMPARE(filter.rowAt(0), -1);
    QCOMPARE(filter.rowAt(1500), 0);
    QCOMPARE(filter.rowAt(3500), 1);
    QCOMPARE(filter.rowAt(5500), 2);

    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 2);

    // Source row 0 is view row 0; source row 2 collapses to view row 1.
    QCOMPARE(filter.rowAt(1500), 0);
    QCOMPARE(filter.rowAt(5500), 1);

    // The cue playing now is filtered out: -1, because there is nothing to
    // highlight. Auto-follow treats that the same as "before the first cue"
    // rather than scrolling somewhere arbitrary.
    QCOMPARE(filter.rowAt(3500), -1);
}

void TstSubtitles::startMsAtRoundTripsThroughTheFilter()
{
    QVector<SubtitleLine> lines;
    lines.append({1000, 2000, QStringLiteral("alpha"), {}});
    lines.append({3000, 4000, QStringLiteral("beta"), {}});
    lines.append({5000, 6000, QStringLiteral("alpha again"), {}});

    SubtitleLineModel model;
    model.setLines(lines);
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    QCOMPARE(filter.startMsAt(0), 1000);
    QCOMPARE(filter.startMsAt(2), 5000);
    QCOMPARE(filter.startMsAt(-1), -1);
    QCOMPARE(filter.startMsAt(99), -1);

    filter.setPattern(QStringLiteral("alpha"));
    // View row 1 is now the cue that started at 5000, which is what a click on
    // the second visible row has to seek to.
    QCOMPARE(filter.startMsAt(1), 5000);

    // rowAt() and startMsAt() must agree, since auto-follow uses the first to
    // scroll and click-to-seek uses the second.
    const int row = filter.rowAt(5500);
    QCOMPARE(filter.startMsAt(row), 5000);
}

QTEST_MAIN(TstSubtitles)
#include "tst_subtitles.moc"
