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

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(CMP_TESTDATA_DIR "/") + name;
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
            || a[t].note != b[t].note || a[t].lines.size() != b[t].lines.size()) {
            return false;
        }
        for (int i = 0; i < a[t].lines.size(); ++i) {
            const SubtitleLine &x = a[t].lines[i];
            const SubtitleLine &y = b[t].lines[i];
            if (x.startMs != y.startMs || x.endMs != y.endMs || x.text != y.text
                || x.rawText != y.rawText) {
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

    void cacheReproducesTheParseExactly();
    void cacheMissesWhenTheMediaChanges();
    void cacheMissesWhenASidecarAppears();
    void corruptCacheEntryIsIgnored();

    void stylingRendersItalicsAndColours();
    void stylingEscapesAndBreaks();
    void stylingDropsVectorDrawings();
    void cueColoursStayLegibleOnEitherTheme();
    void styledRoleFollowsTheRowBackground();

    void exportedSrtReparsesToTheSameCues();
    void exportRejectsWhatItCannotWrite();

    void indexAtFindsTheCurrentCue();
    void indexAtOnEmptyModel();

    void filterMatchesTextCaseInsensitively();
    void filterDoesNotSearchTimestamps();
    void searchFoldsHardSpaces();
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

QTEST_MAIN(TstSubtitles)
#include "tst_subtitles.moc"
