// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

// The ASS [V4+ Styles] table: parsing it, and what a cue drawn with it looks
// like in the browser.
//
// Why it is its own suite. Until this existed the styling the browser showed came
// only from the override tags inside a cue -- {\i1}, {\c&H00FF00&} -- and most
// professionally authored ASS carries none: the italics, the colour and the bold
// are declared once in the styles table and every Dialogue line just names a row
// of it. Such a track read plain in the browser while libass drew it styled over
// the picture, so the two halves of a subtitle reader's screen visibly disagreed
// on the one feature this product exists for.
//
// The cases below are the ones that make a plausible implementation wrong rather
// than broken -- a wrong colour, an inverted flag, a field read out of the
// neighbouring column. Each of those produces output that looks fine.

#include <QtTest>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtGui/QColor>

#include "SubtitleCache.h"
#include "SubtitleExtractor.h"
#include "SubtitleLineModel.h"
#include "SubtitleStyle.h"
#include "SubtitleTypes.h"

namespace {

// The row colour styling is resolved against, shared with tst_subtitles and
// tst_conformance: SubtitleStyle nudges a cue's colour until it is legible on the
// row it lands on, so every expectation about a colour is an expectation about
// this number too.
const QColor kRow(0x12, 0x15, 0x1c);

// A header in the shape libavcodec publishes in AVCodecContext::subtitle_header:
// the [Script Info] and [V4+ Styles] sections of the file, without the events.
QString header(const QString &format, const QStringList &styles)
{
    QString out = QStringLiteral("[Script Info]\nScriptType: v4.00+\n\n"
                                 "[V4+ Styles]\nFormat: %1\n")
                      .arg(format);
    for (const QString &style : styles)
        out += QStringLiteral("Style: %1\n").arg(style);
    return out;
}

// The canonical column order, which is what nearly every file uses -- and
// therefore exactly what an implementation that ignored the Format: line would
// still get right.
const char kCanonicalFormat[] =
    "Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
    "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, "
    "Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, "
    "Encoding";

QString conformance(const QString &name)
{
    return QStringLiteral(SUBIXA_TESTDATA_DIR "/conformance/") + name;
}

SubtitleTrackList parse(const QString &path)
{
    SubtitleExtractor extractor;
    // A cached answer would let a parser regression through, and would write into
    // the developer's real cache directory besides.
    extractor.setCacheEnabled(false);
    SubtitleTrackList result;
    QObject::connect(&extractor, &SubtitleExtractor::finished, &extractor,
                     [&](int, const SubtitleTrackList &tracks) { result = tracks; });
    extractor.setCurrentRequest(1);
    extractor.extract(path, 1);
    return result;
}

}  // namespace

class TstAssStyles : public QObject
{
    Q_OBJECT

private slots:
    void formatLineDecidesWhichColumnIsWhich();
    void coloursAreAbgrNotRgb();
    void assBooleansAreMinusOne();
    void aStyleRowNeedsAFormatLineAndAName();
    void baseStyleReachesACueWithNoOverrideTags();
    void overrideTagsBeatTheBaseStyle();
    void anUnknownStyleNameRendersPlain();
    void theTableSurvivesTheContainer();
    void strikeOutOverrideIsNotAShadowDepth();
    void aVersionTwoCacheEntryIsRefused();
};

// The Format: line says which column is which, and files do reorder it -- the
// spec has always allowed it and Aegisub's own older templates differ. An
// implementation that assumed the canonical order reads Bold out of ScaleX and
// Italic out of ScaleY: both are 100 there, both are non-zero, and the whole
// track comes out bold italic without a single error anywhere.
void TstAssStyles::formatLineDecidesWhichColumnIsWhich()
{
    const AssStyleTable reordered = SubtitleStyle::parseStyleTable(header(
        QStringLiteral("Italic, Name, PrimaryColour, Bold, Fontsize, Fontname, "
                       "Underline, StrikeOut"),
        {QStringLiteral("-1,Whisper,&H0000FFFF,0,22,Verdana,0,0")}));

    QCOMPARE(reordered.size(), 1);
    const AssStyle style = reordered.value(QStringLiteral("Whisper"));
    QVERIFY(style.italic);
    QVERIFY(!style.bold);
    QCOMPARE(style.fontSize, 22);
    QCOMPARE(style.fontName, QStringLiteral("Verdana"));
    QCOMPARE(style.primaryColour, QColor(255, 255, 0));

    // The same row read as if the order were canonical would have picked
    // "Whisper" out of Fontname and 0 out of PrimaryColour. Assert the shape it
    // did NOT take, because that is the failure this test exists for.
    QVERIFY(style.fontName != QStringLiteral("Whisper"));

    // Columns the table declares that mean nothing to a list are skipped rather
    // than mis-assigned, and a header with no styles at all is empty rather than
    // a row of defaults.
    QVERIFY(SubtitleStyle::parseStyleTable(header(QLatin1String(kCanonicalFormat), {}))
                .isEmpty());
}

// &HAABBGGRR: alpha first, then blue, green, red -- the reverse of every other
// format. Read as RGB it swaps red and blue, which is invisible on grey text and
// wrong on every speaker who has a colour.
void TstAssStyles::coloursAreAbgrNotRgb()
{
    const AssStyleTable table = SubtitleStyle::parseStyleTable(header(
        QLatin1String("Name, PrimaryColour"),
        {
            QStringLiteral("Red,&H000000FF"),      // low byte is red
            QStringLiteral("Blue,&H00FF0000"),     // high byte is blue
            QStringLiteral("Ghost,&H8000FF00"),    // alpha is transparency, dropped
            QStringLiteral("Legacy,16777215"),     // SSA v4 wrote decimal BGR
            QStringLiteral("Broken,&Hnotacolour"), // no colour rather than black
        }));

    QCOMPARE(table.value(QStringLiteral("Red")).primaryColour, QColor(255, 0, 0));
    QCOMPARE(table.value(QStringLiteral("Blue")).primaryColour, QColor(0, 0, 255));
    QCOMPARE(table.value(QStringLiteral("Ghost")).primaryColour, QColor(0, 255, 0));
    QCOMPARE(table.value(QStringLiteral("Legacy")).primaryColour, QColor(255, 255, 255));
    QVERIFY(!table.value(QStringLiteral("Broken")).primaryColour.isValid());

    // A decimal colour must not be read as hex: "16777215" is eight valid hex
    // digits, so a parser that tried hex first would decode it to a
    // plausible-looking wrong colour instead of white.
    QVERIFY(table.value(QStringLiteral("Legacy")).primaryColour != QColor(0x15, 0x72, 0x77));
}

// ASS writes true as -1, not 1 -- it inherited the convention from VB. A parser
// that tested `== 1` reads every professionally authored italic style as upright.
void TstAssStyles::assBooleansAreMinusOne()
{
    const AssStyleTable table = SubtitleStyle::parseStyleTable(header(
        QLatin1String("Name, Bold, Italic, Underline, StrikeOut"),
        {
            QStringLiteral("Authored,-1,-1,-1,-1"),  // what Aegisub writes
            QStringLiteral("HandWritten,1,1,1,1"),   // what people type
            QStringLiteral("Off,0,0,0,0"),
            QStringLiteral("Weighted,700,0,0,0"),  // a weight, not a flag
        }));

    for (const QString &name : {QStringLiteral("Authored"), QStringLiteral("HandWritten"),
                                QStringLiteral("Weighted")}) {
        const AssStyle style = table.value(name);
        QVERIFY2(style.bold, qPrintable(name));
    }
    QVERIFY(table.value(QStringLiteral("Authored")).italic);
    QVERIFY(table.value(QStringLiteral("Authored")).underline);
    QVERIFY(table.value(QStringLiteral("Authored")).strikeOut);

    const AssStyle off = table.value(QStringLiteral("Off"));
    QVERIFY(!off.bold);
    QVERIFY(!off.italic);
    QVERIFY(!off.underline);
    QVERIFY(!off.strikeOut);

    // And -1 reaches the markup, which is the whole point of reading it.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("Aside"), kRow,
                                         table.value(QStringLiteral("Authored"))),
             QStringLiteral("<b><i><u><s>Aside</s></u></i></b>"));
}

void TstAssStyles::aStyleRowNeedsAFormatLineAndAName()
{
    // A Style: row ahead of any Format: line has no declared column order.
    // Guessing the canonical one would italicise a track at random, so the row is
    // dropped and the cue reads plain -- what it did before the table was read at
    // all.
    const QString noFormat =
        QStringLiteral("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                       "Style: Orphan,Arial,20,&H0000FFFF,0,-1\n");
    QVERIFY(SubtitleStyle::parseStyleTable(noFormat).isEmpty());

    // Rows outside the styles section are not styles, whatever they are called.
    const QString elsewhere =
        QStringLiteral("[Script Info]\nStyle: NotAStyle,Arial\n\n[Events]\n"
                       "Format: Layer, Start, End, Style, Name, Text\n"
                       "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,Hello\n");
    QVERIFY(SubtitleStyle::parseStyleTable(elsewhere).isEmpty());

    // An unnamed style cannot be referred to by any Dialogue line.
    QVERIFY(SubtitleStyle::parseStyleTable(
                header(QLatin1String("Name, Italic"), {QStringLiteral(",-1")}))
                .isEmpty());

    // SSA's section is spelled "[V4 Styles]" and its rows are the same shape.
    const QString ssa =
        QStringLiteral("[V4 Styles]\nFormat: Name, Italic\nStyle: Old,-1\n");
    QVERIFY(SubtitleStyle::parseStyleTable(ssa).value(QStringLiteral("Old")).italic);
}

// The defect this whole lane is about: a cue with no override tags at all, drawn
// with a style that says italic and coloured.
void TstAssStyles::baseStyleReachesACueWithNoOverrideTags()
{
    const AssStyleTable table = SubtitleStyle::parseStyleTable(header(
        QLatin1String(kCanonicalFormat),
        {QStringLiteral("Sign,Arial,28,&H0000A5FF,&H000000FF,&H00000000,&H00000000,"
                        "-1,-1,0,0,100,100,0,0,1,2,0,2,10,10,10,1")}));

    const QString styled = SubtitleStyle::toStyledText(
        QStringLiteral("Closed for the season"), kRow, table.value(QStringLiteral("Sign")));
    QCOMPARE(styled,
             QStringLiteral("<font color=\"#ffa500\"><b><i>Closed for the "
                            "season</i></b></font>"));

    // With no style at all -- which is what every cue got before this existed --
    // the same payload is plain. That difference is the bug report.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("Closed for the season"), kRow),
             QStringLiteral("Closed for the season"));

    // ASS's own default primary colour is opaque white, and every ffmpeg text
    // decoder synthesises a header declaring it -- SRT has no styles table of its
    // own. Taken literally that would put an explicit colour on every row of
    // every SRT track, in a colour nobody chose, so it is left to the theme.
    AssStyle plainWhite;
    plainWhite.primaryColour = QColor(255, 255, 255);
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("Ordinary"), kRow, plainWhite),
             QStringLiteral("Ordinary"));
}

// ASS's own precedence: what a cue says about itself beats what its style says.
void TstAssStyles::overrideTagsBeatTheBaseStyle()
{
    AssStyle base;
    base.italic = true;
    base.primaryColour = QColor(255, 255, 0);  // yellow

    // The tag turns the style's italic off for a run, and back on after. The
    // colour is closed and reopened around each run because the emitter closes
    // every open tag whenever anything changes rather than tracking which ones
    // it could have kept -- longer markup for the same rendering, and the same
    // shape it has always produced for override-only cues.
    QCOMPARE(SubtitleStyle::toStyledText(
                 QStringLiteral("Leaning{\\i0} upright{\\i1} leaning"), kRow, base),
             QStringLiteral("<font color=\"#ffff00\"><i>Leaning</i></font>"
                            "<font color=\"#ffff00\"> upright</font>"
                            "<font color=\"#ffff00\"><i> leaning</i></font>"));

    // And the style's colour for a run, without disturbing its italic.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("{\\c&H0000FF&}Red now"), kRow, base),
             QStringLiteral("<font color=\"#ff0000\"><i>Red now</i></font>"));

    // \r drops back to the style rather than to nothing, which is the direction
    // that only means something once a style exists.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("{\\b1}Loud{\\r} normal"), kRow, base),
             QStringLiteral("<font color=\"#ffff00\"><b><i>Loud</i></b></font>"
                            "<font color=\"#ffff00\"><i> normal</i></font>"));

    // A bare \c is the same reset for the colour alone. \clip, which starts the
    // same way and is far commoner, must not be read as one.
    QCOMPARE(SubtitleStyle::toStyledText(
                 QStringLiteral("{\\c&H00FF00&}Green{\\c} back"), kRow, base),
             QStringLiteral("<font color=\"#00ff00\"><i>Green</i></font>"
                            "<font color=\"#ffff00\"><i> back</i></font>"));
    QCOMPARE(SubtitleStyle::toStyledText(
                 QStringLiteral("{\\c&H00FF00&\\clip(0,0,9,9)}Clipped"), kRow, base),
             QStringLiteral("<font color=\"#00ff00\"><i>Clipped</i></font>"));
}

// A Dialogue line naming a style the table does not declare. It happens -- a
// renamed style, a sidecar muxed against the wrong header -- and the cue must
// read plain rather than disappear or take some other row's styling.
void TstAssStyles::anUnknownStyleNameRendersPlain()
{
    const AssStyleTable table = SubtitleStyle::parseStyleTable(
        header(QLatin1String("Name, Italic, PrimaryColour"),
               {QStringLiteral("Known,-1,&H000000FF")}));

    SubtitleLineModel model;
    model.setStyles(table);
    model.setLines({
        SubtitleLine{0, 1000, QStringLiteral("Known line"), QStringLiteral("Known line"),
                     QStringLiteral("Known"), QString()},
        SubtitleLine{1000, 2000, QStringLiteral("Stray line"),
                     QStringLiteral("Stray line"), QStringLiteral("NoSuchStyle"),
                     QString()},
        SubtitleLine{2000, 3000, QStringLiteral("Nameless"), QStringLiteral("Nameless"),
                     QString(), QString()},
    });
    model.setBackground(kRow);

    auto styled = [&model](int row) {
        return model.data(model.index(row, 0), SubtitleLineModel::StyledTextRole)
            .toString();
    };

    QCOMPARE(styled(0), QStringLiteral("<font color=\"#ff0000\"><i>Known line</i></font>"));
    QCOMPARE(styled(1), QStringLiteral("Stray line"));
    QCOMPARE(styled(2), QStringLiteral("Nameless"));

    // The name is readable in its own right: a reader filtering a track by
    // speaker needs it, and it is what the styled role resolved against.
    QCOMPARE(model.data(model.index(1, 0), SubtitleLineModel::StyleNameRole).toString(),
             QStringLiteral("NoSuchStyle"));
}

// End to end, through the committed fixture: the header has to survive
// avcodec_open2, the style name has to survive the Dialogue split, and the two
// have to meet again in the model.
void TstAssStyles::theTableSurvivesTheContainer()
{
    const QString path = conformance(QStringLiteral("styletable.mkv"));
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    const SubtitleTrackList tracks = parse(path);
    QCOMPARE(tracks.size(), 1);
    const SubtitleTrack &track = tracks.first();

    QCOMPARE(track.styles.size(), 3);
    QVERIFY(track.styles.contains(QStringLiteral("Default")));
    QCOMPARE(track.styles.value(QStringLiteral("Sign")).fontSize, 28);
    QVERIFY(track.styles.value(QStringLiteral("Sign")).bold);
    QCOMPARE(track.styles.value(QStringLiteral("Thought")).primaryColour,
             QColor(0, 255, 255));

    QCOMPARE(track.lines.size(), 4);
    QCOMPARE(track.lines.at(0).style, QStringLiteral("Default"));
    QCOMPARE(track.lines.at(1).style, QStringLiteral("Sign"));
    QCOMPARE(track.lines.at(3).style, QStringLiteral("Missing"));

    // The actor -- the ASS Name field -- is the column beside Style, and reading
    // one off by a column would swap them silently.
    QCOMPARE(track.lines.at(0).actor, QStringLiteral("Narrator"));
    QCOMPARE(track.lines.at(1).actor, QString());
    QCOMPARE(track.lines.at(2).actor, QStringLiteral("Kaori"));

    // Every cue's own text is free of markup: this fixture says everything
    // through the table, which is what makes it evidence.
    for (const SubtitleLine &line : track.lines) {
        QVERIFY2(!line.rawText.contains(QLatin1Char('{')),
                 qPrintable(QStringLiteral("fixture stopped being tag-free: %1")
                                .arg(line.rawText)));
    }

    SubtitleLineModel model;
    model.setStyles(track.styles);
    model.setLines(track.lines);
    model.setBackground(kRow);

    auto styled = [&model](int row) {
        return model.data(model.index(row, 0), SubtitleLineModel::StyledTextRole)
            .toString();
    };
    QCOMPARE(styled(0), QStringLiteral("Plain by the table"));
    QCOMPARE(styled(1),
             QStringLiteral("<font color=\"#ffa500\"><b><i>Bold italic orange by the "
                            "table</i></b></font>"));
    QCOMPARE(styled(2),
             QStringLiteral("<font color=\"#00ffff\"><i><u>Italic underlined cyan by "
                            "the table</u></i></font>"));
    QCOMPARE(styled(3), QStringLiteral("Named a style the table does not have"));

    // Styling changes how a row is drawn and nothing about what it says: search
    // matches the plain text, so the moment the two disagree the browser shows
    // lines it cannot find.
    for (const SubtitleLine &line : track.lines)
        QVERIFY(!line.text.isEmpty());
}

// \s1 arrived with the styles table, because strike-out only became expressible
// once a style could declare it. The branch has to tell itself apart from \shad,
// which starts with the same letter and is far commoner -- and nothing else in
// the corpus carries either, so without this the tag can be deleted outright and
// every other suite stays green.
void TstAssStyles::strikeOutOverrideIsNotAShadowDepth()
{
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("{\\s1}gone"), kRow),
             QStringLiteral("<s>gone</s>"));

    // \s0 turns off what the style declared, like every other flag override.
    AssStyle struck;
    struck.strikeOut = true;
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("{\\s0}kept"), kRow, struck),
             QStringLiteral("kept"));

    // \shad4 is a shadow depth. Read as a strike-out it would put a line through
    // every cue of any file that sets one, which is most of them.
    QCOMPARE(SubtitleStyle::toStyledText(QStringLiteral("{\\shad4}plain"), kRow),
             QStringLiteral("plain"));
}

// Trap 14: an extractor change without a cache version bump serves the old text
// forever. Version 3 is that bump, and the mechanism is one integer -- so it is
// worth an assertion that a version-2 entry is *refused* rather than read with
// the new layout, which would take a per-cue style out of the next cue's
// timestamp.
void TstAssStyles::aVersionTwoCacheEntryIsRefused()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString media = dir.path() + QStringLiteral("/fake.mkv");
    {
        QFile f(media);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write("x"), qint64(1));
    }

    SubtitleTrack track;
    track.id = 0;
    track.kind = SubtitleKind::Text;
    track.codecName = QStringLiteral("ass");
    AssStyle sign;
    sign.italic = true;
    sign.primaryColour = QColor(255, 165, 0);
    track.styles.insert(QStringLiteral("Sign"), sign);
    track.lines.append(SubtitleLine{0, 1000, QStringLiteral("hi"),
                                    QStringLiteral("hi"), QStringLiteral("Sign"),
                                    QStringLiteral("Kaori")});

    const SubtitleCache cache(dir.path());
    const SubtitleSourceStamps stamps{SubtitleCache::stampFor(media)};
    cache.store(media, stamps, SubtitleTrackList{track});

    // It round-trips first, styles and all -- otherwise the refusal below would
    // pass for the wrong reason.
    SubtitleTrackList warm;
    QVERIFY(cache.load(media, stamps, &warm));
    QCOMPARE(warm.size(), 1);
    QCOMPARE(warm.at(0).styles, track.styles);
    QCOMPARE(warm.at(0).lines.at(0).style, QStringLiteral("Sign"));
    QCOMPARE(warm.at(0).lines.at(0).actor, QStringLiteral("Kaori"));

    const QFileInfoList entries =
        QDir(dir.path()).entryInfoList({QStringLiteral("*.cues")}, QDir::Files);
    QCOMPARE(entries.size(), 1);
    const QString entry = entries.first().absoluteFilePath();

    QByteArray bytes;
    {
        QFile f(entry);
        QVERIFY(f.open(QIODevice::ReadOnly));
        bytes = f.readAll();
    }
    // magic is a big-endian quint32, then the version: byte 7 is its low octet.
    QVERIFY(bytes.size() > 8);
    QCOMPARE(quint8(bytes.at(7)), quint8(3));
    bytes[7] = char(2);
    {
        QFile f(entry);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(f.write(bytes), qint64(bytes.size()));
    }

    SubtitleTrackList stale;
    QVERIFY2(!cache.load(media, stamps, &stale),
             "a version-2 entry was served: the format version does not gate reads, "
             "so every cue written before the styles table would be read with the "
             "new layout");
}

QTEST_MAIN(TstAssStyles)
#include "tst_assstyles.moc"
