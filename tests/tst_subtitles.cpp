// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

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

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>

#include <QtCore/QUrl>

#include "SubtitleCache.h"
#include "SubtitleExtractor.h"
#include "SubtitleManager.h"
#include "SubtitleStyle.h"
#include "SubtitleFilterModel.h"
#include "SubtitleLineModel.h"
#include "SubtitleTypes.h"

#include <QtCore/QBuffer>
#include <QtCore/QDataStream>
#include <QtCore/QRegularExpression>
#include <QtCore/QtEndian>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(SUBIXA_TESTDATA_DIR "/") + name;
}

// Runs the extractor synchronously. It is a plain QObject; the worker thread it
// normally lives on is SubtitleManager's business, not the parser's.
//
// The cue cache is off here: every test below is about what the parser produces,
// and a cached answer would let a parser regression through while also writing
// into the user's real cache directory. The cache has its own tests, which point
// it at a temporary directory.
SubtitleTrackList parse(const QString &path, QString *error = nullptr)
{
    SubtitleExtractor extractor;
    extractor.setCacheEnabled(false);
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

// Same, but with the cache live in `cacheDir`. Reports whether the cues came off
// disk, which is the thing worth asserting.
SubtitleTrackList parseCached(const QString &path, const QString &cacheDir,
                              bool *fromCache)
{
    SubtitleExtractor extractor;
    extractor.setCacheDirectory(cacheDir);
    SubtitleTrackList result;
    bool cached = false;

    QObject::connect(&extractor, &SubtitleExtractor::finished, &extractor,
                     [&](int, const SubtitleTrackList &tracks, bool hit) {
                         result = tracks;
                         cached = hit;
                     });

    extractor.setCurrentRequest(1);
    extractor.extract(path, 1);

    if (fromCache)
        *fromCache = cached;
    return result;
}

bool sameCues(const SubtitleTrackList &a, const SubtitleTrackList &b)
{
    if (a.size() != b.size())
        return false;
    for (int t = 0; t < a.size(); ++t) {
        if (a[t].id != b[t].id || a[t].streamIndex != b[t].streamIndex
            || a[t].language != b[t].language || a[t].title != b[t].title
            || a[t].codecName != b[t].codecName || a[t].kind != b[t].kind
            || a[t].sidecar != b[t].sidecar || a[t].sourcePath != b[t].sourcePath
            || a[t].note != b[t].note || a[t].styles != b[t].styles
            || a[t].lines.size() != b[t].lines.size()) {
            return false;
        }
        for (int i = 0; i < a[t].lines.size(); ++i) {
            const SubtitleLine &x = a[t].lines[i];
            const SubtitleLine &y = b[t].lines[i];
            // style and actor are compared for the same reason the rest of the
            // record is: this predicate is the whole definition of "the cache is
            // indistinguishable from a parse". A field added to SubtitleLine or
            // SubtitleTrack and not added here is a field the cache may drop,
            // corrupt or reorder with every suite still green.
            if (x.startMs != y.startMs || x.endMs != y.endMs || x.text != y.text
                || x.rawText != y.rawText || x.style != y.style
                || x.actor != y.actor) {
                return false;
            }
        }
    }
    return true;
}

const SubtitleTrack *trackByLanguage(const SubtitleTrackList &tracks, const QString &lang)
{
    for (const SubtitleTrack &t : tracks) {
        if (t.language == lang)
            return &t;
    }
    return nullptr;
}

// The track the proxy tests run against: two cues matching "alpha" with a
// non-matching one between them. The gap is the point -- filtering has to
// *renumber* rows, not just shorten the list, so a view row that happens to
// equal its source row proves nothing.
QVector<SubtitleLine> proxyLines()
{
    QVector<SubtitleLine> lines;
    lines.append({1000, 2000, QStringLiteral("alpha"), {}});
    lines.append({3000, 4000, QStringLiteral("beta"), {}});
    lines.append({5000, 6000, QStringLiteral("alpha again"), {}});
    return lines;
}

// SubtitleLineModel only ever emits dataChanged for the whole list at once, when
// the theme changes the row background. The proxy's remapping is per row, so
// checking it needs a model that can touch one.
class TouchableLineModel : public SubtitleLineModel
{
public:
    void touchRow(int row, const QList<int> &roles)
    {
        emit dataChanged(index(row, 0), index(row, 0), roles);
    }
};

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
    void sidecarNamesSayWhatKindOfTrackTheyAre();
    void nonAsciiFilenamesReachTheDecoder();

    void cacheReproducesTheParseExactly();
    void cacheMissesWhenTheMediaChanges();
    void cacheMissesWhenASidecarAppears();
    void corruptCacheEntryIsIgnored();
    void hostileCacheCountsAreRefused();

    void stylingRendersItalicsAndColours();
    void stylingEscapesAndBreaks();
    void stylingDropsVectorDrawings();
    void cueColoursStayLegibleOnEitherTheme();
    void memoisedContrastMatchesTheRealWalk();
    void styledRoleFollowsTheRowBackground();
    void styledAndPlainTextAgreeOnContent();

    void exportedSrtReparsesToTheSameCues();
    void exportRejectsWhatItCannotWrite();

    void indexAtFindsTheCurrentCue();
    void indexAtOnEmptyModel();

    void filterMatchesTextCaseInsensitively();
    void filterDoesNotSearchTimestamps();
    void searchFoldsHardSpaces();
    void rowAtMapsThroughTheFilter();
    void startMsAtRoundTripsThroughTheFilter();

    void cueStartMsAtIgnoresTheDelay();
    void endMsAtBracketsTheLine();
    void textAtFollowsTheSpokenCue();
    void rowAccessorsReadTheViewRow();
    void rowAfterAndRowBeforeStepThroughTheView();
    void delayMovesTimesWithoutMovingRows();
    void roleNamesComeFromTheSourceModel();
    void dataChangedRemapsThroughTheFilter();
    void filterWithoutASourceModelIsInert();
};

void TstSubtitles::initTestCase()
{
    // Hard failure, not QSKIP. A skip exits 0, so a checkout that has never run
    // make-fixtures.sh would report this suite green having asserted nothing.
    // ctest's REQUIRED_FILES catches the usual case before the binary starts;
    // this catches a fixture that vanishes after that, or a run outside ctest.
    QVERIFY2(QFileInfo::exists(fixture(QStringLiteral("subs.mkv"))),
             "subs.mkv missing -- run ./testdata/make-fixtures.sh");
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
    // down by searchFoldsHardSpaces() below.
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
    QVERIFY2(QFileInfo::exists(path),
             "movtext.mp4 missing -- run ./testdata/make-fixtures.sh");

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
    QVERIFY2(QFileInfo::exists(path),
             "shifted.mkv missing -- run ./testdata/make-fixtures.sh");

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
    QVERIFY2(QFileInfo::exists(path),
             "shifted.mp4 missing -- run ./testdata/make-fixtures.sh");

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
    QVERIFY2(QFileInfo::exists(path),
             "sidecar.mp4 missing -- run ./testdata/make-fixtures.sh");

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

void TstSubtitles::sidecarNamesSayWhatKindOfTrackTheyAre()
{
    QTemporaryDir media;
    QVERIFY(media.isValid());

    const QString video = media.filePath(QStringLiteral("film.mkv"));
    QVERIFY(QFile::copy(fixture(QStringLiteral("subs.mkv")), video));

    const auto writeSidecar = [&media](const QString &name) {
        QFile srt(media.filePath(name));
        QVERIFY(srt.open(QIODevice::WriteOnly));
        srt.write("1\n00:00:01,000 --> 00:00:03,000\nA line.\n\n");
    };
    writeSidecar(QStringLiteral("film.eng.forced.srt"));
    writeSidecar(QStringLiteral("film.eng.sdh.srt"));
    // "hi" reads two ways, and only where it sits says which: here it is the
    // language tag, so this is Hindi and not English hearing-impaired.
    writeSidecar(QStringLiteral("film.hi.srt"));
    writeSidecar(QStringLiteral("film.spa.hi.srt"));

    const SubtitleTrackList tracks = parse(video);
    const auto sidecarNamed = [&tracks](const QString &file) -> const SubtitleTrack * {
        for (const SubtitleTrack &t : tracks) {
            if (t.sidecar && QFileInfo(t.sourcePath).fileName() == file)
                return &t;
        }
        return nullptr;
    };

    const SubtitleTrack *forced = sidecarNamed(QStringLiteral("film.eng.forced.srt"));
    QVERIFY(forced);
    QVERIFY(forced->forced);
    QVERIFY(!forced->hearingImpaired);

    const SubtitleTrack *sdh = sidecarNamed(QStringLiteral("film.eng.sdh.srt"));
    QVERIFY(sdh);
    QVERIFY(sdh->hearingImpaired);
    QVERIFY(!sdh->forced);

    const SubtitleTrack *hindi = sidecarNamed(QStringLiteral("film.hi.srt"));
    QVERIFY(hindi);
    QCOMPARE(hindi->language, QStringLiteral("hi"));
    QVERIFY2(!hindi->hearingImpaired,
             "a Hindi sidecar was read as hearing-impaired");

    // The same tag one place further along, where nothing else claims it.
    const SubtitleTrack *spanishSdh = sidecarNamed(QStringLiteral("film.spa.hi.srt"));
    QVERIFY(spanishSdh);
    QCOMPARE(spanishSdh->language, QStringLiteral("spa"));
    QVERIFY(spanishSdh->hearingImpaired);
}

// The path handed to avformat_open_input has to be UTF-8 on every platform.
// QFile::encodeName() is the local 8-bit codec, which is UTF-8 on Unix but the
// ANSI codepage on Windows: this filename -- `日本語 café Привет.mkv` -- encodes
// to literal `?` there, unrecoverably, and the open then fails with "Invalid
// argument" for every file whose name the codepage cannot represent.
//
// The test exists because the bug is invisible on the development host. On Unix
// encodeName *is* toUtf8, so a revert to it passes everything here and breaks
// only Windows -- which is precisely the kind of change that gets made back.
//
// The filename below is UTF-8 in this source. Both supported compilers are gcc
// and read it as such; a toolchain that does not would need /utf-8 or escapes.
void TstSubtitles::nonAsciiFilenamesReachTheDecoder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Japanese, an accented Latin letter, and Cyrillic: the first and last are
    // unrepresentable in any single Windows codepage, the middle one encodes to
    // a different byte under CP1252 than under UTF-8.
    const QString name = QStringLiteral("日本語 café Привет.mkv");
    const QString copy = QDir(dir.path()).filePath(name);
    QVERIFY(QFile::copy(fixture(QStringLiteral("subs.mkv")), copy));

    // Qt keeps the path as UTF-16 and never round-trips it through the codepage,
    // so this passes even when the decoder cannot open the same file. That is
    // what made the original bug confusing, and why the assertion is here.
    QVERIFY(QFileInfo::exists(copy));

    QString error;
    const SubtitleTrackList tracks = parse(copy, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(tracks.size(), 3);
    for (const SubtitleTrack &t : tracks)
        QVERIFY(!t.lines.isEmpty());
}

void TstSubtitles::cacheReproducesTheParseExactly()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    const QString path = fixture(QStringLiteral("subs.mkv"));

    bool hit = true;
    const SubtitleTrackList cold = parseCached(path, cache.path(), &hit);
    QVERIFY2(!hit, "first open cannot be a hit -- the cache directory is empty");
    QCOMPARE(cold.size(), 3);

    const SubtitleTrackList warm = parseCached(path, cache.path(), &hit);
    QVERIFY2(hit, "second open should have come off disk");

    // The point of the cache is that it is indistinguishable from a parse: same
    // tracks, same metadata, same cues, same raw payloads.
    QVERIFY(sameCues(cold, warm));
    QVERIFY(sameCues(parse(path), warm));
}

void TstSubtitles::cacheMissesWhenTheMediaChanges()
{
    QTemporaryDir cache;
    QTemporaryDir media;
    QVERIFY(cache.isValid() && media.isValid());

    const QString copy = media.filePath(QStringLiteral("film.mkv"));
    QVERIFY(QFile::copy(fixture(QStringLiteral("subs.mkv")), copy));

    bool hit = true;
    parseCached(copy, cache.path(), &hit);
    QVERIFY(!hit);
    parseCached(copy, cache.path(), &hit);
    QVERIFY(hit);

    // mtime alone, with the content untouched: a re-encode that happens to land
    // on the same byte count still has to invalidate the entry.
    {
        QFile file(copy);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(120),
                                 QFileDevice::FileModificationTime));
    }
    parseCached(copy, cache.path(), &hit);
    QVERIFY2(!hit, "a changed mtime must invalidate the entry");

    // And the fresh entry is usable again.
    parseCached(copy, cache.path(), &hit);
    QVERIFY(hit);
}

void TstSubtitles::cacheMissesWhenASidecarAppears()
{
    QTemporaryDir cache;
    QTemporaryDir media;
    QVERIFY(cache.isValid() && media.isValid());

    const QString copy = media.filePath(QStringLiteral("film.mkv"));
    QVERIFY(QFile::copy(fixture(QStringLiteral("subs.mkv")), copy));

    bool hit = true;
    const SubtitleTrackList before = parseCached(copy, cache.path(), &hit);
    QVERIFY(!hit);
    QCOMPARE(before.size(), 3);
    parseCached(copy, cache.path(), &hit);
    QVERIFY(hit);

    // Dropping a subtitle file next to the video is the ordinary way a track
    // appears, and it changes nothing about the video the entry is keyed on.
    {
        QFile srt(media.filePath(QStringLiteral("film.en.srt")));
        QVERIFY(srt.open(QIODevice::WriteOnly));
        srt.write("1\n00:00:01,000 --> 00:00:03,000\nA new sidecar line.\n\n");
    }

    const SubtitleTrackList after = parseCached(copy, cache.path(), &hit);
    QVERIFY2(!hit, "a new sidecar must invalidate the entry");
    QCOMPARE(after.size(), 4);
    QCOMPARE(after[3].lines.size(), 1);
    QCOMPARE(after[3].lines[0].text, QStringLiteral("A new sidecar line."));

    parseCached(copy, cache.path(), &hit);
    QVERIFY(hit);
}

void TstSubtitles::corruptCacheEntryIsIgnored()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    const QString path = fixture(QStringLiteral("subs.mkv"));

    bool hit = true;
    parseCached(path, cache.path(), &hit);
    QVERIFY(!hit);

    // Truncate the entry in place. A half-written file is what a crash mid-store
    // would leave behind, and reading one back as a short track list would be
    // worse than any reparse -- the browser would simply be missing cues.
    const QFileInfoList entries = QDir(cache.path())
                                      .entryInfoList({QStringLiteral("*.cues")},
                                                     QDir::Files);
    QCOMPARE(entries.size(), 1);
    {
        QFile entry(entries.first().absoluteFilePath());
        QVERIFY(entry.open(QIODevice::ReadWrite));
        QVERIFY(entry.resize(entry.size() / 2));
    }

    const SubtitleTrackList recovered = parseCached(path, cache.path(), &hit);
    QVERIFY2(!hit, "a truncated entry must not be served");
    QVERIFY(sameCues(parse(path), recovered));

    // A garbled header is the other shape of the same problem.
    {
        QFile entry(entries.first().absoluteFilePath());
        QVERIFY(entry.open(QIODevice::ReadWrite));
        QVERIFY(entry.write("junk") == 4);
    }
    parseCached(path, cache.path(), &hit);
    QVERIFY(!hit);
}

void TstSubtitles::stylingRendersItalicsAndColours()
{
    const QColor dark(QStringLiteral("#12121a"));

    // What ffmpeg hands over for an italicised SRT line, and what libass would
    // draw over the picture. The browser used to show it flat.
    QCOMPARE(SubtitleStyle::toStyledText(
                 QStringLiteral("{\\i1}Whispered.{\\i0} Then not."), dark),
             QStringLiteral("<i>Whispered.</i> Then not."));

    // Bold as a flag and as a weight; 500 and up reads as bold.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("{\\b1}Shout{\\b0}."), dark),
             QStringLiteral("<b>Shout</b>."));
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("{\\b700}Shout{\\b400}."), dark),
             QStringLiteral("<b>Shout</b>."));

    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("{\\u1}Titled{\\u0}"), dark),
             QStringLiteral("<u>Titled</u>"));

    // ASS colours are &HBBGGRR&: this is pure red, not pure blue, and getting
    // that backwards is the classic way to render every speaker wrong.
    QCOMPARE(SubtitleStyle::parseAssColour(QStringLiteral("&H0000FF&")),
             QColor(255, 0, 0));
    QCOMPARE(SubtitleStyle::parseAssColour(QStringLiteral("&HFF0000&")),
             QColor(0, 0, 255));
    // With an alpha byte, which is transparency and is dropped.
    QCOMPARE(SubtitleStyle::parseAssColour(QStringLiteral("&H8000FF00&")),
             QColor(0, 255, 0));
    QVERIFY(!SubtitleStyle::parseAssColour(QStringLiteral("&Hzzz&")).isValid());

    const QString coloured =
        SubtitleStyle::toStyledText(QStringLiteral("{\\c&H0000FF&}Red speaker"), dark);
    QVERIFY2(coloured.contains(QStringLiteral("<font color=\"#ff0000\">")),
             qPrintable(coloured));
    QVERIFY(coloured.endsWith(QStringLiteral("</font>")));

    // \r drops back to the line's own style, closing whatever was open.
    QCOMPARE(SubtitleStyle::toStyledText(
                 QStringLiteral("{\\i1}Aside{\\r} and back."), dark),
             QStringLiteral("<i>Aside</i> and back."));

    // Tags that place or animate a cue say nothing about how it reads.
    QCOMPARE(SubtitleStyle::toStyledText(
                 QStringLiteral("{\\pos(320,410)\\fad(200,200)}Plain."), dark),
             QStringLiteral("Plain."));
}

void TstSubtitles::stylingEscapesAndBreaks()
{
    const QColor dark(QStringLiteral("#12121a"));

    // The markup is generated, so anything in the cue that looks like markup has
    // to stop being markup -- a subtitle saying <i> means the characters.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("5 < 6 & <i> stays"), dark),
             QStringLiteral("5 &lt; 6 &amp; &lt;i&gt; stays"));

    // Both ASS break escapes, and the real newline the extractor uses to join a
    // cue's rects, all read as breaks in a list.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("One\\NTwo\\nThree\nFour"), dark),
             QStringLiteral("One<br>Two<br>Three<br>Four"));

    // \h is a hard space and stays one.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("a\\hb"), dark),
             QStringLiteral("a&nbsp;b"));

    // A break at either end is noise in a row.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("\\NHello\\N"), dark),
             QStringLiteral("Hello"));
}

void TstSubtitles::stylingDropsVectorDrawings()
{
    const QColor dark(QStringLiteral("#12121a"));

    // {\p1} switches to drawing mode: what follows is a path -- a shape, a
    // censor bar, a karaoke box -- and rendering it as dialogue puts
    // "m 0 0 l 100 0" in the browser.
    QCOMPARE(SubtitleStyle::toStyledText(
                 QStringLiteral("{\\p1}m 0 0 l 100 0 l 100 50{\\p0}"), dark),
             QString());
    QCOMPARE(SubtitleStyle::toStyledText(
                 QStringLiteral("Look{\\p1}m 0 0 l 9 9{\\p0} there"), dark),
             QStringLiteral("Look there"));

    // And the same for the plain text the browser searches and shows when
    // styling is off -- which is a change to the *extractor's* output, and so
    // the reason the cue cache's format version was bumped. End to end, through
    // a real .ass file, because that is where the coordinates come from.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString assPath = dir.filePath(QStringLiteral("drawings.ass"));
    {
        QFile file(assPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            "[Script Info]\nScriptType: v4.00+\n\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
            "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
            "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
            "MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,"
            "0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n\n"
            "[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
            "Effect, Text\n"
            "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,Normal line.\n"
            "Dialogue: 0,0:00:04.00,0:00:06.00,Default,,0,0,0,,"
            "{\\p1}m 0 0 l 100 0 l 100 50{\\p0}\n"
            "Dialogue: 0,0:00:07.00,0:00:09.00,Default,,0,0,0,,"
            "Look{\\p1}m 0 0 l 9 9{\\p0} there\n");
    }

    const SubtitleTrackList tracks = parse(assPath);
    QCOMPARE(tracks.size(), 1);
    // The drawing-only cue decodes to nothing and drops out entirely, the way
    // any other blank cue does; the mixed one keeps its words.
    QCOMPARE(tracks[0].lines.size(), 2);
    QCOMPARE(tracks[0].lines[0].text, QStringLiteral("Normal line."));
    QCOMPARE(tracks[0].lines[1].text, QStringLiteral("Look there"));
}

void TstSubtitles::cueColoursStayLegibleOnEitherTheme()
{
    const QColor darkPanel(QStringLiteral("#12121a"));
    const QColor lightPanel(QStringLiteral("#f6f7f9"));

    // The guarantee is a contrast ratio, so that is what these assert rather
    // than a lightness number that happens to satisfy it today.
    auto legible = [](const QColor &c, const QColor &bg) {
        return SubtitleStyle::contrastRatio(c, bg) >= 4.5;
    };

    // White dialogue is the common case, and on a light panel it would be
    // invisible. On the dark panel nothing is wrong with it, so it must come
    // back untouched -- adjusting a colour that reads perfectly well would be
    // the player second-guessing the subtitler for no reason.
    QCOMPARE(SubtitleStyle::readableOn(Qt::white, darkPanel), QColor(Qt::white));
    QVERIFY(!legible(Qt::white, lightPanel));
    QVERIFY(legible(SubtitleStyle::readableOn(Qt::white, lightPanel), lightPanel));

    // Pure red on a nearly black row is one of the more legible things on it,
    // and an earlier rule that compared plain brightness called it unreadable.
    QCOMPARE(SubtitleStyle::readableOn(QColor(255, 0, 0), darkPanel), QColor(255, 0, 0));

    // A coloured speaker keeps their hue -- that is the part carrying meaning --
    // while moving far enough from the background to be read.
    const QColor yellowOnLight =
        SubtitleStyle::readableOn(QColor(255, 255, 0), lightPanel);
    QVERIFY(legible(yellowOnLight, lightPanel));
    QVERIFY2(qAbs(yellowOnLight.hue() - 60) < 12, qPrintable(yellowOnLight.name()));

    // Dark cue text on the dark panel gets the same treatment the other way.
    const QColor navyOnDark = SubtitleStyle::readableOn(QColor(10, 10, 40), darkPanel);
    QVERIFY(legible(navyOnDark, darkPanel));
    QVERIFY2(navyOnDark.lightness() > 100, qPrintable(navyOnDark.name()));
}

void TstSubtitles::memoisedContrastMatchesTheRealWalk()
{
    // readableOn() is memoised because it is computed per visible row and the
    // walk is expensive. A cache that answered differently from the thing it
    // caches would break the one guarantee the function makes -- 4.5:1 against
    // the row -- in a way that only shows up on the second cue of a colour.
    const QColor darkRow(0x12, 0x15, 0x1c);
    const QColor lightRow(0xff, 0xff, 0xff);

    int checked = 0;
    for (const QColor &background : {darkRow, lightRow}) {
        for (int r = 0; r <= 255; r += 51) {
            for (int g = 0; g <= 255; g += 51) {
                for (int b = 0; b <= 255; b += 51) {
                    const QColor cue(r, g, b);
                    const QColor expected =
                        SubtitleStyle::readableOnUncached(cue, background);
                    // Twice: the first call fills the table, the second reads it.
                    QCOMPARE(SubtitleStyle::readableOn(cue, background), expected);
                    QCOMPARE(SubtitleStyle::readableOn(cue, background), expected);
                    QVERIFY2(SubtitleStyle::contrastRatio(expected, background)
                                 >= 4.4,
                             qPrintable(QStringLiteral("%1 on %2 came back at %3:1")
                                            .arg(cue.name(), background.name())
                                            .arg(SubtitleStyle::contrastRatio(
                                                expected, background))));
                    ++checked;
                }
            }
        }
    }
    QCOMPARE(checked, 2 * 6 * 6 * 6);

    // An invalid colour must pass straight through rather than being cached as
    // something.
    QVERIFY(!SubtitleStyle::readableOn(QColor(), darkRow).isValid());
}

void TstSubtitles::styledRoleFollowsTheRowBackground()
{
    QVector<SubtitleLine> lines;
    SubtitleLine line;
    line.startMs = 1000;
    line.endMs = 2000;
    line.text = QStringLiteral("White words");
    line.rawText = QStringLiteral("{\\c&HFFFFFF&}White words");
    lines.append(line);

    SubtitleLineModel model;
    model.setLines(lines);
    model.setBackground(QColor(QStringLiteral("#12121a")));

    const QModelIndex index = model.index(0, 0);
    const QString onDark =
        model.data(index, SubtitleLineModel::StyledTextRole).toString();
    QVERIFY2(onDark.contains(QStringLiteral("#ffffff")), qPrintable(onDark));

    // The theme changes under the model, and the styling has to follow: the
    // alternative is white on white the moment somebody picks the light theme.
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.setBackground(QColor(QStringLiteral("#f6f7f9")));
    QCOMPARE(changed.size(), 1);

    const QString onLight =
        model.data(index, SubtitleLineModel::StyledTextRole).toString();
    QVERIFY2(!onLight.contains(QStringLiteral("#ffffff")), qPrintable(onLight));

    // The plain text is untouched by any of it -- search must not depend on the
    // theme.
    QCOMPARE(model.data(index, SubtitleLineModel::TextRole).toString(),
             QStringLiteral("White words"));
}

void TstSubtitles::exportedSrtReparsesToTheSameCues()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SubtitleManager manager;
    manager.setCacheDirectory(dir.filePath(QStringLiteral("cache")));
    QSignalSpy loaded(&manager, &SubtitleManager::loaded);
    manager.load(fixture(QStringLiteral("subs.mkv")));
    QVERIFY2(loaded.wait(20000), "the extractor never reported a result");

    const SubtitleTrack &source = manager.trackData().at(0);
    const QString out = dir.filePath(QStringLiteral("export.srt"));
    QCOMPARE(manager.exportTrack(0, QUrl::fromLocalFile(out)), QString());

    // The real check is a round trip: what we write has to come back through our
    // own parser as the cues we started with. An .srt that only looks right is
    // the failure mode here -- a missing comma in the timestamp is enough to
    // make every player reject it silently.
    const SubtitleTrackList back = parse(out);
    QCOMPARE(back.size(), 1);
    QCOMPARE(back[0].lines.size(), source.lines.size());
    for (int i = 0; i < source.lines.size(); ++i) {
        QCOMPARE(back[0].lines[i].text, source.lines[i].text);
        QCOMPARE(back[0].lines[i].startMs, source.lines[i].startMs);
        QCOMPARE(back[0].lines[i].endMs, source.lines[i].endMs);
    }

    // And it is a plain SubRip file, not just something ffmpeg tolerates.
    QFile written(out);
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(written.readAll());
    QVERIFY(text.startsWith(QStringLiteral("1\n")));
    QVERIFY(text.contains(QStringLiteral(" --> ")));
    QVERIFY2(!text.contains(QRegularExpression(QStringLiteral(R"(\d\d:\d\d:\d\d\.\d)"))),
             "milliseconds must be comma-separated in SubRip, not dotted");

    // The suggested name carries the language, which is what tells two exports
    // of one film apart.
    QVERIFY(manager.suggestedExportName(0, QStringLiteral("/films/Movie.2026.mkv"))
                .startsWith(QStringLiteral("Movie.2026.eng")));
}

void TstSubtitles::exportRejectsWhatItCannotWrite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SubtitleManager manager;
    manager.setCacheDirectory(dir.filePath(QStringLiteral("cache")));

    // Nothing loaded: every id is out of range, and saying so beats writing an
    // empty file the user then tries to load.
    QVERIFY(!manager.exportTrack(0, QUrl::fromLocalFile(dir.filePath(
                                        QStringLiteral("none.srt")))).isEmpty());

    QSignalSpy loaded(&manager, &SubtitleManager::loaded);
    manager.load(fixture(QStringLiteral("subs.mkv")));
    QVERIFY(loaded.wait(20000));

    QVERIFY(!manager.exportTrack(99, QUrl::fromLocalFile(dir.filePath(
                                         QStringLiteral("none.srt")))).isEmpty());
    // An undirectable destination has to come back as a message, not a crash or
    // a silent no-op.
    QVERIFY(!manager
                 .exportTrack(0, QUrl::fromLocalFile(dir.filePath(
                                     QStringLiteral("no/such/dir/out.srt"))))
                 .isEmpty());
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

void TstSubtitles::searchFoldsHardSpaces()
{
    const QString nbsp(QChar(0x00A0));
    QVector<SubtitleLine> lines;
    lines.append({1000, 2000,
                  QStringLiteral("plus a") + nbsp + QStringLiteral("hard space."), {}});

    SubtitleLineModel model;
    model.setLines(lines);
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    // The cue holds U+00A0 where the user types U+0020, so a phrase spanning an
    // ASS \h has to match anyway. This hid well before it was fixed: searching
    // either side of the hard space works, and the panel renders both alike.
    filter.setPattern(QStringLiteral("a hard space"));
    QCOMPARE(filter.count(), 1);

    filter.setPattern(QStringLiteral("hard space"));
    QCOMPARE(filter.count(), 1);

    // A hard space pasted out of the panel has to match too, which is why the
    // pattern is folded rather than only the cue text.
    filter.setPattern(QStringLiteral("a") + nbsp + QStringLiteral("hard"));
    QCOMPARE(filter.count(), 1);

    // Case folding still applies on the folded path.
    filter.setPattern(QStringLiteral("A HARD SPACE"));
    QCOMPARE(filter.count(), 1);

    // And the folded path must not match across a gap that is not there.
    filter.setPattern(QStringLiteral("plus  a"));
    QCOMPARE(filter.count(), 0);
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


void TstSubtitles::hostileCacheCountsAreRefused()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    const QString path = fixture(QStringLiteral("subs.mkv"));

    bool hit = true;
    const SubtitleTrackList real = parseCached(path, cache.path(), &hit);
    QVERIFY(!hit);
    QCOMPARE(real.size(), 3);

    const QFileInfoList entries = QDir(cache.path())
                                      .entryInfoList({QStringLiteral("*.cues")},
                                                     QDir::Files);
    QCOMPARE(entries.size(), 1);
    const QString entryPath = entries.first().absoluteFilePath();

    QByteArray original;
    {
        QFile source(entryPath);
        QVERIFY(source.open(QIODevice::ReadOnly));
        original = source.readAll();
    }
    QVERIFY(original.size() > 64);

    // Walk the header exactly as the loader does, to find where the three count
    // fields actually sit. Reproducing the layout here is deliberate: these are
    // the only fields that decide an allocation, and a format change that moved
    // them should fail loudly rather than leave the test poisoning padding.
    // Version 3 adding the [V4+ Styles] table is exactly that -- it put a third
    // count between the track header and the cues, and this walk went red.
    // Version 4 did it again on a smaller scale: two flavour flags at the end of
    // the track header moved every count after them by two bytes.
    qint64 trackCountAt = -1;
    qint64 styleCountAt = -1;
    qint64 lineCountAt = -1;
    {
        QBuffer buffer(&original);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        QDataStream in(&buffer);
        in.setVersion(QDataStream::Qt_6_0);

        quint32 magic = 0;
        quint32 version = 0;
        qint32 sourceCount = 0;
        in >> magic >> version >> sourceCount;
        QCOMPARE(in.status(), QDataStream::Ok);
        for (qint32 i = 0; i < sourceCount; ++i) {
            QString stampPath;
            qint64 size = 0;
            qint64 modified = 0;
            in >> stampPath >> size >> modified;
        }
        QCOMPARE(in.status(), QDataStream::Ok);

        trackCountAt = buffer.pos();
        qint32 trackCount = 0;
        in >> trackCount;
        QCOMPARE(trackCount, qint32(real.size()));

        // The first track's fields, up to its cue count.
        qint32 id = 0, streamIndex = 0, kind = 0;
        QString language, title, codecName, sourcePath, note;
        bool sidecar = false, forced = false, hearingImpaired = false;
        in >> id >> streamIndex >> language >> title >> codecName >> kind
            >> sidecar >> sourcePath >> note >> forced >> hearingImpaired;
        QCOMPARE(in.status(), QDataStream::Ok);

        styleCountAt = buffer.pos();
        qint32 styleCount = 0;
        in >> styleCount;
        QCOMPARE(styleCount, qint32(real.at(0).styles.size()));
        for (qint32 i = 0; i < styleCount; ++i) {
            QString name, fontName;
            qint32 fontSize = 0;
            qint64 colour = 0;
            bool bold = false, italic = false, underline = false, strikeOut = false;
            in >> name >> fontName >> fontSize >> colour >> bold >> italic
                >> underline >> strikeOut;
        }
        QCOMPARE(in.status(), QDataStream::Ok);

        lineCountAt = buffer.pos();
        qint32 lineCount = 0;
        in >> lineCount;
        QCOMPARE(lineCount, qint32(real.at(0).lines.size()));
    }
    QVERIFY(trackCountAt > 0);
    QVERIFY(styleCountAt > trackCountAt);
    QVERIFY(lineCountAt > styleCountAt);

    // A count field is corruption- and attacker-controlled: it says how much to
    // allocate before anything has been read. Unbounded, a flipped byte here
    // asked for tens of gigabytes in one call -- and the entry was never
    // invalidated, because nothing reached any cleanup, so the player became
    // permanently unable to open that one film.
    //
    // Be clear about what this test does and does not prove. It pins the
    // *behaviour*: a poisoned count is a miss, and the real cues still come
    // back. It does not observe the allocation, and it cannot -- checked by
    // removing the bound and re-running, where every case below still passes,
    // because the read loop then fails on the next record and reports the same
    // miss one enormous reserve() later. The bound is what stops that reserve;
    // the assertion here is the guarantee around it, not the mechanism.
    const QVector<quint32> poisons = {
        quint32(0x7FFFFFFF),  // INT_MAX
        quint32(0x40000000),  // a billion
        quint32(0x00100000),  // a million: plausible, still far past the file
        quint32(0xFFFFFFFF),  // reads back as -1
    };

    for (const qint64 offset : {trackCountAt, styleCountAt, lineCountAt}) {
        for (const quint32 poison : poisons) {
            QByteArray damaged = original;
            qToBigEndian(poison, damaged.data() + offset);

            {
                QFile out(entryPath);
                QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
                QCOMPARE(out.write(damaged), qint64(damaged.size()));
            }

            bool damagedHit = true;
            const SubtitleTrackList recovered =
                parseCached(path, cache.path(), &damagedHit);
            QVERIFY2(!damagedHit,
                     qPrintable(QStringLiteral("count at %1 poisoned with 0x%2 "
                                               "was served from cache")
                                    .arg(offset)
                                    .arg(poison, 0, 16)));
            // A miss still has to produce the real cues.
            QCOMPARE(recovered.size(), real.size());
        }
    }

    // And a count that is merely *slightly* too large -- the case a bounds check
    // written against a fixed ceiling rather than the file size would let past.
    {
        QByteArray damaged = original;
        qToBigEndian(quint32(real.at(0).lines.size() + 10000),
                     damaged.data() + lineCountAt);
        QFile out(entryPath);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(out.write(damaged), qint64(damaged.size()));
        out.close();

        bool damagedHit = true;
        parseCached(path, cache.path(), &damagedHit);
        QVERIFY2(!damagedHit, "a cue count larger than the file can hold was served");
    }
}

void TstSubtitles::styledAndPlainTextAgreeOnContent()
{
    // The invariant the panel rests on: turning styling on changes how a row is
    // *rendered* and nothing about what it says. Search matches the plain text,
    // so the moment the two disagree the browser is showing lines it will not
    // find.
    //
    // They did disagree. Entities were decoded on the way to `text` but not on
    // the way to `styled`, and the styling pass then escaped the surviving
    // ampersand -- so with styling on (the default) a cue reading "a & entity"
    // rendered as "a &amp; entity" while search still matched "a & entity".
    const QColor background(0x12, 0x15, 0x1c);

    // StyledText markup back to the characters it renders as. Only the subset
    // SubtitleStyle emits.
    const auto render = [](QString markup) {
        static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
        markup.replace(QStringLiteral("<br>"), QStringLiteral("\n"));
        markup.remove(tag);
        markup.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        markup.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        markup.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        markup.replace(QStringLiteral("&nbsp;"), QString(QChar(0x00A0)));
        // Last, or it would undo the escaping of the others.
        markup.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        return markup;
    };

    int checked = 0;
    for (const QString &name : {QStringLiteral("subs.mkv"),
                                QStringLiteral("sidecar.mp4"),
                                QStringLiteral("movtext.mp4")}) {
        const QString path = fixture(name);
        if (!QFileInfo::exists(path))
            continue;

        for (const SubtitleTrack &track : parse(path)) {
            for (const SubtitleLine &line : track.lines) {
                const QString styled =
                    SubtitleStyle::toStyledText(line.rawText, background);
                QCOMPARE(render(styled), line.text);
                ++checked;
            }
        }
    }
    QVERIFY2(checked > 0, "no cues were compared");

    // And the specific case that was broken, pinned by hand so a change to the
    // fixtures cannot quietly remove the coverage.
    const QString styled = SubtitleStyle::toStyledText(
        QStringLiteral("Fish &amp; chips &lt;3"), background);
    QCOMPARE(render(styled), QStringLiteral("Fish & chips <3"));
    QVERIFY2(styled.contains(QStringLiteral("&amp;")),
             qPrintable(styled));   // renders as one ampersand
    QVERIFY2(!styled.contains(QStringLiteral("&amp;amp;")),
             qPrintable(styled));   // would render as "&amp;"
}

// The rest of SubtitleFilterModel's surface, pinned as it behaves today.
//
// These are characterization tests, not specifications. The class is a
// QSortFilterProxyModel and is slated to become a purpose-built proxy instead;
// the row mapping, the delay arithmetic and the signal forwarding would all keep
// compiling through that swap and could each go quietly wrong, which is the one
// failure mode a subtitle browser cannot survive -- a click seeking to the wrong
// line looks like a timing bug in the file. So every invokable is exercised with
// the filter both empty and active, since renumbering is where the mapping shows.
//
// Where the current behaviour looks questionable it is still recorded, with the
// oddity named in the comment rather than asserted away.

void TstSubtitles::cueStartMsAtIgnoresTheDelay()
{
    SubtitleLineModel model;
    model.setLines(proxyLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    QCOMPARE(filter.cueStartMsAt(0), 1000);
    QCOMPARE(filter.cueStartMsAt(2), 5000);
    QCOMPARE(filter.cueStartMsAt(-1), -1);
    QCOMPARE(filter.cueStartMsAt(3), -1);

    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.cueStartMsAt(1), 5000);
    // Row 2 existed a moment ago and does not now: the panel keeps view rows
    // across a keystroke, so an accessor being asked for a row that has just
    // been filtered away is ordinary, not a bug in the caller.
    QCOMPARE(filter.cueStartMsAt(2), -1);

    // The pair either side of the delay: cueStartMsAt is what the row *shows*,
    // startMsAt is where a click on it *seeks*. Anything that collapses the two
    // makes a resynced file display the wrong timestamps or seek to the wrong
    // place, depending on which one wins.
    filter.setDelayMs(2000);
    QCOMPARE(filter.cueStartMsAt(1), 5000);
    QCOMPARE(filter.startMsAt(1), 7000);
    QCOMPARE(filter.cueStartMsAt(9), -1);
}

void TstSubtitles::endMsAtBracketsTheLine()
{
    SubtitleLineModel model;
    model.setLines(proxyLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    QCOMPARE(filter.endMsAt(0), 2000);
    QCOMPARE(filter.endMsAt(2), 6000);
    QCOMPARE(filter.endMsAt(-1), -1);
    QCOMPARE(filter.endMsAt(3), -1);

    filter.setPattern(QStringLiteral("alpha"));
    // Loop-this-line reads both ends of one row, so they have to describe the
    // same cue after renumbering -- 5000..6000, not 5000..2000.
    QCOMPARE(filter.startMsAt(1), 5000);
    QCOMPARE(filter.endMsAt(1), 6000);
    QCOMPARE(filter.endMsAt(2), -1);

    // The delay moves the end exactly as it moves the start, or a loop set on a
    // resynced file would run short by the offset.
    filter.setDelayMs(500);
    QCOMPARE(filter.endMsAt(1), 6500);
    filter.setDelayMs(-500);
    QCOMPARE(filter.endMsAt(1), 5500);

    // A stored end of 0 reads as "no end". The guard is on the value rather than
    // on the row, so endMsAt cannot tell a zero-length cue at the head of the
    // file from a cue whose end was never parsed -- both come back -1 and the
    // loop button does nothing. Recorded as it stands: harmless in practice
    // because nothing produces a cue that ends at 0, and the alternative is
    // looping over a zero-length range.
    QVector<SubtitleLine> degenerate;
    degenerate.append({0, 0, QStringLiteral("no end"), {}});
    SubtitleLineModel edge;
    edge.setLines(degenerate);
    SubtitleFilterModel edgeFilter;
    edgeFilter.setSourceModel(&edge);
    QCOMPARE(edgeFilter.count(), 1);
    QCOMPARE(edgeFilter.cueStartMsAt(0), 0);
    QCOMPARE(edgeFilter.endMsAt(0), -1);
}

void TstSubtitles::textAtFollowsTheSpokenCue()
{
    SubtitleLineModel model;
    model.setLines(proxyLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    QCOMPARE(filter.textAt(0), QString());
    QCOMPARE(filter.textAt(999), QString());
    QCOMPARE(filter.textAt(1000), QStringLiteral("alpha"));
    QCOMPARE(filter.textAt(1500), QStringLiteral("alpha"));
    QCOMPARE(filter.textAt(3500), QStringLiteral("beta"));

    // In the gap after a cue ends, and past the last cue, the preview keeps
    // showing the line that started most recently: textAt inherits indexAt's
    // rule and never consults endMs. Hovering a silent stretch therefore shows
    // the previous line rather than nothing. Pinned rather than corrected --
    // "the last thing said" is defensible for a hover preview, and auto-follow
    // depends on the same rule to keep a highlight during a pause.
    QCOMPARE(filter.textAt(2500), QStringLiteral("alpha"));
    QCOMPARE(filter.textAt(9'999'999), QStringLiteral("alpha again"));

    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.textAt(1500), QStringLiteral("alpha"));
    QCOMPARE(filter.textAt(5500), QStringLiteral("alpha again"));
    // The cue playing now is filtered out, so the preview goes blank rather than
    // quoting a line the list is not showing.
    QCOMPARE(filter.textAt(3500), QString());

    filter.setPattern(QString());
    filter.setDelayMs(500);
    // With +500 the cue stored at 1000 is not spoken until 1500.
    QCOMPARE(filter.textAt(1200), QString());
    QCOMPARE(filter.textAt(1600), QStringLiteral("alpha"));
}

void TstSubtitles::rowAccessorsReadTheViewRow()
{
    SubtitleLineModel model;
    model.setLines(proxyLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    QCOMPARE(filter.textAtRow(0), QStringLiteral("alpha"));
    QCOMPARE(filter.textAtRow(2), QStringLiteral("alpha again"));
    QCOMPARE(filter.timestampAtRow(0), QStringLiteral("00:00:01.000"));
    QCOMPARE(filter.timestampAtRow(2), QStringLiteral("00:00:05.000"));

    // Out of range on either side is an empty string, not the nearest row: these
    // feed a copy action and a status line, where a wrong answer is silent.
    QCOMPARE(filter.textAtRow(-1), QString());
    QCOMPARE(filter.textAtRow(3), QString());
    QCOMPARE(filter.timestampAtRow(-1), QString());
    QCOMPARE(filter.timestampAtRow(3), QString());

    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.textAtRow(1), QStringLiteral("alpha again"));
    QCOMPARE(filter.timestampAtRow(1), QStringLiteral("00:00:05.000"));
    QCOMPARE(filter.textAtRow(2), QString());
    QCOMPARE(filter.timestampAtRow(2), QString());

    // The timestamp is the stored one. It comes from the line model, which knows
    // nothing about the delay, so a resynced row goes on displaying where the cue
    // sits in the file while startMsAt seeks to where it is heard. The two
    // disagreeing is deliberate; a row that renumbered its own timestamps would
    // no longer match the subtitle file the reader is editing.
    filter.setDelayMs(2000);
    QCOMPARE(filter.timestampAtRow(1), QStringLiteral("00:00:05.000"));
    QCOMPARE(filter.startMsAt(1), 7000);

    // An empty track is a normal state: models are emptied rather than deleted
    // between loads, and QML asks for row 0 as soon as it is rebound.
    SubtitleLineModel empty;
    SubtitleFilterModel emptyFilter;
    emptyFilter.setSourceModel(&empty);
    QCOMPARE(emptyFilter.count(), 0);
    QCOMPARE(emptyFilter.textAtRow(0), QString());
    QCOMPARE(emptyFilter.timestampAtRow(0), QString());
}

void TstSubtitles::rowAfterAndRowBeforeStepThroughTheView()
{
    SubtitleLineModel model;
    model.setLines(proxyLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    // Before the first cue, "next line" means the first one rather than nothing.
    QCOMPARE(filter.rowAfter(0), 0);
    QCOMPARE(filter.rowAfter(1500), 1);
    QCOMPARE(filter.rowAfter(3500), 2);
    // Past the last cue there is nowhere to go, and the caller must not seek.
    QCOMPARE(filter.rowAfter(5500), -1);
    QCOMPARE(filter.rowAfter(9'999'999), -1);

    // rowBefore is a track-back button, not a plain decrement: a little way into
    // a line it restarts that line, and only near the start does it step back.
    QCOMPARE(filter.rowBefore(0), -1);       // nothing has been said yet
    QCOMPARE(filter.rowBefore(1100), -1);    // just inside the first line
    QCOMPARE(filter.rowBefore(2500), 0);     // further in: restart it
    QCOMPARE(filter.rowBefore(3100), 0);     // just inside the second: step back
    QCOMPARE(filter.rowBefore(5000), 1);     // exactly on the third's start
    QCOMPARE(filter.rowBefore(6300), 2);     // well into it: restart

    // The 1200 ms window itself, from both sides. Exactly on the boundary steps
    // back, because the comparison is strict.
    QCOMPARE(filter.rowBefore(2200), -1);
    QCOMPARE(filter.rowBefore(2201), 0);

    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.rowAfter(0), 0);
    QCOMPARE(filter.rowAfter(1500), 1);
    QCOMPARE(filter.rowAfter(5500), -1);
    QCOMPARE(filter.rowBefore(5100), 0);
    QCOMPARE(filter.rowBefore(6500), 1);

    // Playback sitting on a cue the filter hides. rowAt says -1 there, which
    // rowAfter cannot tell apart from "before the first cue" -- so next-line
    // jumps to the top of the list, which is *backwards* from where playback is,
    // and previous-line does nothing at all. Both are pinned as they stand: the
    // shape of the fix is a decision about the search UI, not about this class.
    QCOMPARE(filter.rowAt(3500), -1);
    QCOMPARE(filter.rowAfter(3500), 0);
    QCOMPARE(filter.rowBefore(3500), -1);

    // A search matching nothing leaves no row to step to in either direction.
    filter.setPattern(QStringLiteral("nothing here"));
    QCOMPARE(filter.count(), 0);
    QCOMPARE(filter.rowAfter(0), -1);
    QCOMPARE(filter.rowAfter(3500), -1);
    QCOMPARE(filter.rowBefore(3500), -1);

    // With a delay in force the window is still measured in playback time, so
    // the answers match the undelayed ones taken at the same point in the line.
    filter.setPattern(QString());
    filter.setDelayMs(1000);
    QCOMPARE(filter.rowBefore(2100), -1);    // 1100 into the file, as above
    QCOMPARE(filter.rowBefore(3500), 0);     // 2500, as above
    QCOMPARE(filter.rowAfter(2500), 1);

    // And an empty track answers nothing rather than row 0.
    SubtitleLineModel empty;
    SubtitleFilterModel emptyFilter;
    emptyFilter.setSourceModel(&empty);
    QCOMPARE(emptyFilter.rowAfter(0), -1);
    QCOMPARE(emptyFilter.rowBefore(0), -1);
}

void TstSubtitles::delayMovesTimesWithoutMovingRows()
{
    SubtitleLineModel model;
    model.setLines(proxyLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);
    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 2);

    QSignalSpy delayed(&filter, &SubtitleFilterModel::delayMsChanged);
    QSignalSpy counted(&filter, &SubtitleFilterModel::countChanged);
    QSignalSpy removed(&filter, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&filter, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(&filter, &QAbstractItemModel::modelReset);

    // Resyncing must not disturb the list. It moves where cues *are in time*,
    // not which of them match, and a reset here would throw away the scroll
    // position and the selection every time the user nudged the offset.
    filter.setDelayMs(2000);
    QCOMPARE(delayed.size(), 1);
    QCOMPARE(filter.count(), 2);
    QCOMPARE(counted.size(), 0);
    QCOMPARE(removed.size(), 0);
    QCOMPARE(inserted.size(), 0);
    QCOMPARE(reset.size(), 0);
    QCOMPARE(filter.pattern(), QStringLiteral("alpha"));

    // Setting the same value again is not a change.
    filter.setDelayMs(2000);
    QCOMPARE(delayed.size(), 1);

    // Everything that speaks playback time shifts; everything that speaks stored
    // time does not.
    QCOMPARE(filter.rowAt(1500), -1);
    QCOMPARE(filter.rowAt(3500), 0);
    QCOMPARE(filter.rowAt(7500), 1);
    QCOMPARE(filter.startMsAt(0), 3000);
    QCOMPARE(filter.endMsAt(0), 4000);
    QCOMPARE(filter.textAt(3500), QStringLiteral("alpha"));
    QCOMPARE(filter.cueStartMsAt(0), 1000);
    QCOMPARE(filter.timestampAtRow(0), QStringLiteral("00:00:01.000"));
    QCOMPARE(filter.textAtRow(0), QStringLiteral("alpha"));

    // A negative delay pulls cues earlier, and nothing clamps the result at
    // zero: subtitles pushed far enough back report a playback position before
    // the start of the file.
    filter.setDelayMs(-500);
    QCOMPARE(filter.startMsAt(0), 500);
    QCOMPARE(filter.rowAt(600), 0);
    filter.setDelayMs(-2000);
    QCOMPARE(filter.startMsAt(0), -1000);

    // Which collides with the sentinel: at exactly -1001 the first row's start
    // *is* -1, the value the same call uses for "no such row". A caller testing
    // `>= 0` before seeking silently declines to seek to that line. It takes an
    // offset larger than the first cue's own timestamp to reach, so no real file
    // has hit it -- recorded because the collision is invisible at the call site.
    filter.setDelayMs(-1001);
    QCOMPARE(filter.startMsAt(0), -1);
    QCOMPARE(filter.startMsAt(99), -1);
    QCOMPARE(filter.cueStartMsAt(0), 1000);
    QCOMPARE(filter.count(), 2);
}

void TstSubtitles::roleNamesComeFromTheSourceModel()
{
    SubtitleLineModel model;
    model.setLines(proxyLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    // The delegate binds model.text, model.start, model.startMs, model.endMs and
    // model.styled by name. QML resolves those against the *proxy's* roleNames,
    // so a proxy that stopped forwarding the source's names would leave every row
    // blank with no warning anywhere -- the delegate would simply be reading
    // undefined properties.
    QCOMPARE(filter.roleNames(), model.roleNames());
    const QHash<int, QByteArray> names = filter.roleNames();
    QCOMPARE(names.value(SubtitleLineModel::StartMsRole), QByteArrayLiteral("startMs"));
    QCOMPARE(names.value(SubtitleLineModel::EndMsRole), QByteArrayLiteral("endMs"));
    QCOMPARE(names.value(SubtitleLineModel::StartTextRole), QByteArrayLiteral("start"));
    QCOMPARE(names.value(SubtitleLineModel::TextRole), QByteArrayLiteral("text"));
    QCOMPARE(names.value(SubtitleLineModel::RawTextRole), QByteArrayLiteral("rawText"));
    QCOMPARE(names.value(SubtitleLineModel::StyledTextRole), QByteArrayLiteral("styled"));

    // A filter being active changes nothing about the names, and neither does
    // switching tracks -- which is a source model swap, the whole cost of a tab
    // click.
    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.roleNames(), model.roleNames());

    SubtitleLineModel other;
    other.setLines(proxyLines());
    filter.setSourceModel(&other);
    QCOMPARE(filter.roleNames(), other.roleNames());

    // And the roles read back through the proxy, which is the other half of the
    // same contract: the name has to reach the right column of data.
    const QModelIndex row = filter.index(0, 0);
    QCOMPARE(filter.data(row, SubtitleLineModel::TextRole).toString(),
             QStringLiteral("alpha"));
    QCOMPARE(filter.data(row, SubtitleLineModel::StartMsRole).toLongLong(), 1000);
    QCOMPARE(filter.data(row, SubtitleLineModel::StartTextRole).toString(),
             QStringLiteral("00:00:01.000"));
}

void TstSubtitles::dataChangedRemapsThroughTheFilter()
{
    TouchableLineModel model;
    model.setLines(proxyLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    QSignalSpy changed(&filter, &QAbstractItemModel::dataChanged);

    // Unfiltered the row numbers pass straight through, roles and all.
    model.touchRow(1, {SubtitleLineModel::StyledTextRole});
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.at(0).at(1).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.at(0).at(2).value<QList<int>>(),
             QList<int>{SubtitleLineModel::StyledTextRole});

    filter.setPattern(QStringLiteral("alpha"));
    changed.clear();

    // Source row 2 is view row 1, and the view row is what the list is painting.
    // A proxy that forwarded the source's number would repaint the wrong row --
    // and with only the styled role changing, the wrong row would look right.
    model.touchRow(2, {SubtitleLineModel::StyledTextRole});
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.at(0).at(1).value<QModelIndex>().row(), 1);

    // A hidden row produces nothing at all.
    changed.clear();
    model.touchRow(1, {SubtitleLineModel::StyledTextRole});
    QCOMPARE(changed.size(), 0);

    // The way the app actually emits it: a theme change restyles every row at
    // once, and the two surviving rows are contiguous in the view even though
    // they are not in the source.
    changed.clear();
    model.setBackground(QColor(QStringLiteral("#f6f7f9")));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 0);
    QCOMPARE(changed.at(0).at(1).value<QModelIndex>().row(), 1);

    // None of it disturbs the filter: the cue text did not change, so no row
    // enters or leaves the view.
    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.textAtRow(1), QStringLiteral("alpha again"));
}

void TstSubtitles::filterWithoutASourceModelIsInert()
{
    // The state the panel is in before a file is opened, and again after one
    // with no subtitle tracks. QML binds to the proxy either way and calls
    // straight into it, so every accessor has to answer rather than crash.
    SubtitleFilterModel filter;
    QCOMPARE(filter.count(), 0);
    QCOMPARE(filter.sourceCount(), 0);
    QCOMPARE(filter.rowAt(0), -1);
    QCOMPARE(filter.rowAt(5000), -1);
    QCOMPARE(filter.rowAfter(0), -1);
    QCOMPARE(filter.rowBefore(0), -1);
    QCOMPARE(filter.startMsAt(0), -1);
    QCOMPARE(filter.cueStartMsAt(0), -1);
    QCOMPARE(filter.endMsAt(0), -1);
    QCOMPARE(filter.textAt(0), QString());
    QCOMPARE(filter.textAtRow(0), QString());
    QCOMPARE(filter.timestampAtRow(0), QString());

    // With nothing to forward, roleNames falls back to the base model's generic
    // set -- "display", "decoration" and friends, none of which a subtitle row
    // binds. A view attached this early sees undefined properties until a track
    // arrives and the reset makes QML read the names again. Recorded because it
    // is the one place the panel's role names are legitimately absent, and a
    // replacement proxy that answered with the subtitle names here instead would
    // be an improvement rather than a regression.
    QVERIFY(!filter.roleNames().values().contains(QByteArrayLiteral("text")));
    QCOMPARE(filter.roleNames().value(Qt::DisplayRole), QByteArrayLiteral("display"));

    // Typing into the search box with nothing loaded is harmless, and the text
    // is kept: it survives a source change by design.
    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 0);
    QCOMPARE(filter.pattern(), QStringLiteral("alpha"));
    filter.setDelayMs(2000);
    QCOMPARE(filter.startMsAt(0), -1);

    // A track arrives with both still in force.
    SubtitleLineModel model;
    model.setLines(proxyLines());
    filter.setSourceModel(&model);
    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.sourceCount(), 3);
    QCOMPARE(filter.startMsAt(0), 3000);
    QCOMPARE(filter.textAtRow(1), QStringLiteral("alpha again"));

    // And goes away again -- closing the file, or opening one with no subtitles.
    // The proxy has to empty rather than keep answering from a model that may be
    // about to be deleted.
    filter.setSourceModel(nullptr);
    QCOMPARE(filter.count(), 0);
    QCOMPARE(filter.sourceCount(), 0);
    QCOMPARE(filter.rowAt(3500), -1);
    QCOMPARE(filter.rowAfter(0), -1);
    QCOMPARE(filter.textAtRow(0), QString());
    QCOMPARE(filter.timestampAtRow(0), QString());
    QCOMPARE(filter.endMsAt(0), -1);
    QVERIFY(!filter.roleNames().values().contains(QByteArrayLiteral("text")));
}

QTEST_MAIN(TstSubtitles)
#include "tst_subtitles.moc"
