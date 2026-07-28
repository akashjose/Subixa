// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

// A golden-file check over testdata/conformance/, which is the only corpus in
// this tree committed as bytes.
//
// Why it exists. Every other fixture is muxed at test time by whatever ffmpeg is
// installed, so two platforms passing the same suite have each decoded their own
// inputs and their agreement proves nothing. This suite decodes identical bytes
// everywhere and writes what the extractor made of them into one canonical file.
// A decoder change, an FFmpeg major bump or an extractor regression then shows up
// as a diff naming the cue that moved, rather than as a pass.
//
// It deliberately asserts the APPLICATION's view -- startMs, endMs and the text
// after tag stripping and entity decoding -- rather than the raw `rect->ass`
// string. The raw contract is the decoder's and will not survive the eventual
// AVSubtitle-to-AVFrame port; what the browser puts in a row is the thing that
// must not change silently.
//
// Updating the golden is deliberate and never a side effect of a run: see
// tools/regold.sh.

#include <QtTest>

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>

#include "SubtitleExtractor.h"
#include "SubtitleTypes.h"

extern "C" {
#include <libavcodec/version.h>
}
#include <mpv/client.h>

namespace {

QString conformance(const QString &name)
{
    return QStringLiteral(SUBIXA_TESTDATA_DIR "/conformance/") + name;
}

SubtitleTrackList parse(const QString &path)
{
    SubtitleExtractor extractor;
    // The cache would let a parser regression through, and would write into the
    // developer's real cache directory besides.
    extractor.setCacheEnabled(false);
    SubtitleTrackList result;

    QObject::connect(&extractor, &SubtitleExtractor::finished, &extractor,
                     [&](int, const SubtitleTrackList &tracks) { result = tracks; });

    extractor.setCurrentRequest(1);
    extractor.extract(path, 1);
    return result;
}

// One line per track and one per cue, tab separated, stable field order. Text is
// escaped so a newline inside a cue cannot be mistaken for a record boundary --
// a cue containing \N is exactly the case trap 5 and the ASS styles table both
// touch, so it has to survive the serialisation intact.
QString escape(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    out.replace(QLatin1Char('\t'), QLatin1String("\\t"));
    out.replace(QLatin1Char('\r'), QLatin1String("\\r"));
    return out;
}

QString render(const QString &file, const SubtitleTrackList &tracks)
{
    QString out;
    QTextStream ts(&out);
    ts << "FILE\t" << file << "\ttracks=" << tracks.size() << "\n";
    for (const SubtitleTrack &t : tracks) {
        ts << "TRACK\t" << file << '\t' << t.id
           << '\t' << t.language
           << '\t' << escape(t.title)
           << '\t' << t.codecName
           << '\t' << (t.kind == SubtitleKind::Text ? "text" : "bitmap")
           << '\t' << (t.sidecar ? "sidecar" : "embedded")
           << '\t' << "lines=" << t.lines.size() << "\n";
        for (int i = 0; i < t.lines.size(); ++i) {
            const SubtitleLine &l = t.lines[i];
            ts << "CUE\t" << file << '\t' << t.id << '\t' << i
               << '\t' << l.startMs << '\t' << l.endMs
               << '\t' << escape(l.text) << "\n";
        }
    }
    return out;
}

// Every container and sidecar in the corpus, in a fixed order. Adding a file
// here without regolding is a failure, which is the intent: a new fixture has to
// have its output looked at once by a person.
const char *const kCorpus[] = {
    "basic.mkv",       // three text tracks, language tags, ASS styles table
    "entities.mkv",    // &amp; &#39; &#x27; -- decoded by us, not by ffmpeg
    "drawing.mkv",     // {\p1} vector shape beside real text
    "shifted.mkv",     // every stream at +1 h: the rebase must fire
    "noshift.mp4",     // video at 1 h, subtitles at 0: it must NOT fire
    "manytracks.mkv",  // eight tracks, which is what selects the picker over tabs
    "sidecar.mp4",     // no embedded subtitles; .srt files sit beside it
};

}  // namespace

class TstConformance : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void extractorOutputMatchesTheGolden();
    void manyTracksCrossesThePickerThreshold();
    void shiftedAndUnshiftedDisagreeDeliberately();
};

void TstConformance::initTestCase()
{
    // The corpus is committed, so a missing file means a broken checkout rather
    // than a step somebody forgot to run.
    for (const char *name : kCorpus) {
        const QString path = conformance(QLatin1String(name));
        QVERIFY2(QFileInfo::exists(path),
                 qPrintable(QStringLiteral("conformance corpus incomplete: %1").arg(path)));
    }

    // Printed rather than asserted. When this suite goes red after a dependency
    // bump, the first question is which of the three moved, and answering it
    // from the log is much cheaper than bisecting a prefix.
    qInfo("conformance: libavcodec %d.%d.%d, mpv client API 0x%lx, Qt %s",
          LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO,
          static_cast<unsigned long>(mpv_client_api_version()), qVersion());
}

void TstConformance::extractorOutputMatchesTheGolden()
{
    QString actual;
    for (const char *name : kCorpus)
        actual += render(QLatin1String(name), parse(conformance(QLatin1String(name))));

    // Dump mode, used only by tools/regold.sh. Writing to a path rather than
    // stdout keeps this out of QTest's own output, and means there is exactly
    // one implementation of the format instead of a second one in the script
    // that could drift from this one.
    const QByteArray dumpTo = qgetenv("SUBIXA_CONFORMANCE_DUMP");
    if (!dumpTo.isEmpty()) {
        QFile out(QString::fromLocal8Bit(dumpTo));
        QVERIFY2(out.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 qPrintable(QStringLiteral("cannot write %1").arg(QString::fromLocal8Bit(dumpTo))));
        // The binary records its own provenance. A shell script reconstructing
        // this from `pkg-config --modversion` reads whichever .pc file happens to
        // be on the path, which is not necessarily what this executable linked --
        // it reported the distribution's libavcodec 60 for a build running
        // against 62. These come from the headers this was compiled with and
        // from the library actually loaded, so they cannot disagree. Comment
        // lines are stripped before comparison, so this is provenance only.
        out.write(QStringLiteral("# built against: libavcodec %1.%2.%3, "
                                 "mpv client API %4.%5, Qt %6\n")
                      .arg(LIBAVCODEC_VERSION_MAJOR)
                      .arg(LIBAVCODEC_VERSION_MINOR)
                      .arg(LIBAVCODEC_VERSION_MICRO)
                      .arg(mpv_client_api_version() >> 16)
                      .arg(mpv_client_api_version() & 0xffff)
                      .arg(QLatin1String(qVersion()))
                      .toUtf8());
        out.write(actual.toUtf8());
        qInfo("conformance: wrote %lld bytes to %s", out.size(), dumpTo.constData());
        return;
    }

    const QString goldenPath = conformance(QStringLiteral("golden.tsv"));
    QFile golden(goldenPath);
    QVERIFY2(golden.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("cannot read %1 -- regenerate with tools/regold.sh")
                            .arg(goldenPath)));
    // The golden carries a `#` header recording when, by whom and why it was
    // last regenerated. That provenance is for the reader, not part of the
    // contract, so it is stripped before comparing.
    QString expected;
    for (const QString &line : QString::fromUtf8(golden.readAll()).split(QLatin1Char('\n'))) {
        if (!line.startsWith(QLatin1Char('#')))
            expected += line + QLatin1Char('\n');
    }
    while (expected.endsWith(QLatin1String("\n\n")))
        expected.chop(1);

    if (actual == expected)
        return;

    // Whole-file QCOMPARE on a few hundred lines is unreadable, so report the
    // first divergence and let the diff be run by hand from there.
    const QStringList a = actual.split(QLatin1Char('\n'));
    const QStringList e = expected.split(QLatin1Char('\n'));
    for (int i = 0; i < qMax(a.size(), e.size()); ++i) {
        const QString av = i < a.size() ? a[i] : QStringLiteral("<end of output>");
        const QString ev = i < e.size() ? e[i] : QStringLiteral("<end of golden>");
        if (av != ev) {
            QFAIL(qPrintable(
                QStringLiteral("conformance golden diverges at line %1\n"
                               "  golden: %2\n"
                               "  actual: %3\n"
                               "If this change is intended, review it and run "
                               "tools/regold.sh with a reason.")
                    .arg(i + 1).arg(ev, av)));
        }
    }
    QFAIL("golden differs but no differing line was found -- trailing whitespace?");
}

// SubtitlePanel chooses tabs or a track picker on `tracks.length <= 6`. Until
// this fixture existed every container in the tree had one or three tracks, so
// the picker -- which is what a 65-track film actually renders -- was never
// instantiated by any test. This does not exercise the QML; it guarantees the
// data that selects that branch exists and stays above the threshold.
void TstConformance::manyTracksCrossesThePickerThreshold()
{
    const SubtitleTrackList tracks = parse(conformance(QStringLiteral("manytracks.mkv")));
    QCOMPARE(tracks.size(), 8);
    QVERIFY2(tracks.size() > 6, "the picker threshold in SubtitlePanel.qml is 6");
    for (const SubtitleTrack &t : tracks)
        QVERIFY(!t.lines.isEmpty());
}

// Trap 6, both directions, as one assertion pair. shifted.mkv offsets every
// stream so the container start and the subtitle timeline agree and the rebase
// must fire; noshift.mp4 claims to start at 1 h while its text track is still at
// 0, where a blanket rebase would flatten every cue onto 00:00:00.
void TstConformance::shiftedAndUnshiftedDisagreeDeliberately()
{
    const SubtitleTrackList shifted = parse(conformance(QStringLiteral("shifted.mkv")));
    QVERIFY(!shifted.isEmpty());
    QVERIFY(!shifted[0].lines.isEmpty());
    QVERIFY2(shifted[0].lines.first().startMs < 60'000,
             "a wholesale offset must rebase back towards zero");

    const SubtitleTrackList kept = parse(conformance(QStringLiteral("noshift.mp4")));
    QVERIFY(!kept.isEmpty());
    QVERIFY(!kept[0].lines.isEmpty());
    QVERIFY2(kept[0].lines.first().startMs < 60'000,
             "subtitles already at zero must be left alone, not pushed to -1 h");
}

QTEST_MAIN(TstConformance)
#include "tst_conformance.moc"
