// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

// What SubtitleFilterModel emits, rather than what it answers.
//
// tst_subtitles pins the answers -- every invokable, with the filter empty and
// active. This suite pins the *traffic*, which is the half that made the class
// worth rewriting and the half a caller cannot see by asking it questions.
//
// The measurement behind it, on the 200k-cue fixture: moving the pattern from
// "e" to "7" through a QSortFilterProxyModel took 326 ms, of which the predicate
// was 12 ms. The rest was 14 763 begin/endRemoveRows pairs and 14 764
// countChanged -- one per contiguous run of rejected rows, each splicing the
// mapping again. Rebuilding the whole mapping from scratch cost 12 ms. So the
// contract is now: one modelReset per filter change, no row-level insert or
// remove ever, and nothing at all when the accepted rows do not move.
//
// Needs no fixtures and no window. The tracks here are built in memory, because
// what is under test is signal shape rather than parsing.

#include <QtTest>

#include <QtCore/QHash>
#include <QtCore/QMetaProperty>
#include <QtGui/QColor>

#include "SubtitleFilterModel.h"
#include "SubtitleLineModel.h"
#include "SubtitleTypes.h"

namespace {

// Three cues, two of them matching "alpha" with a non-matching one between: the
// gap is what forces the mapping to renumber rather than merely shorten.
QVector<SubtitleLine> threeLines()
{
    QVector<SubtitleLine> lines;
    lines.append({1000, 2000, QStringLiteral("alpha"), {}});
    lines.append({3000, 4000, QStringLiteral("beta"), {}});
    lines.append({5000, 6000, QStringLiteral("alpha again"), {}});
    return lines;
}

// A track big enough that a scattered filter result is many separate runs -- the
// shape that used to cost one begin/endRemoveRows pair each. Every tenth cue is
// the odd one out, so "needle" accepts 90% of the rows in 2000 runs, while "cue"
// accepts all of them.
QVector<SubtitleLine> scatteredLines(int count)
{
    QVector<SubtitleLine> lines;
    lines.reserve(count);
    for (int i = 0; i < count; ++i) {
        const QString text = (i % 10 == 4) ? QStringLiteral("cue %1 haystack").arg(i)
                                           : QStringLiteral("cue %1 needle").arg(i);
        lines.append({i * 100LL, i * 100LL + 90, text, {}});
    }
    return lines;
}

// SubtitleLineModel never rewrites a cue, so reaching the proxy's dataChanged
// path -- including the case where an edit changes what matches -- needs a model
// that can. The override is the whole point: setLines() would reset instead.
class EditableLineModel : public SubtitleLineModel
{
public:
    void rewriteRow(int row, const QString &text)
    {
        m_rewritten.insert(row, text);
        emit dataChanged(index(row, 0), index(row, 0), {SubtitleLineModel::TextRole});
    }

    void touchRow(int row, const QList<int> &roles)
    {
        emit dataChanged(index(row, 0), index(row, 0), roles);
    }

    void touchAll(const QList<int> &roles)
    {
        emit dataChanged(index(0, 0), index(count() - 1, 0), roles);
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == SubtitleLineModel::TextRole && m_rewritten.contains(index.row()))
            return m_rewritten.value(index.row());
        return SubtitleLineModel::data(index, role);
    }

private:
    QHash<int, QString> m_rewritten;
};

// Every signal that can move rows under a view, counted together: an assertion
// on one of them alone would pass while the model told the view something else.
struct Traffic
{
    explicit Traffic(QAbstractItemModel *model)
        : reset(model, &QAbstractItemModel::modelReset),
          aboutToReset(model, &QAbstractItemModel::modelAboutToBeReset),
          inserted(model, &QAbstractItemModel::rowsInserted),
          removed(model, &QAbstractItemModel::rowsRemoved),
          moved(model, &QAbstractItemModel::rowsMoved),
          layout(model, &QAbstractItemModel::layoutChanged),
          changed(model, &QAbstractItemModel::dataChanged)
    {
    }

    void clear()
    {
        reset.clear();
        aboutToReset.clear();
        inserted.clear();
        removed.clear();
        moved.clear();
        layout.clear();
        changed.clear();
    }

    QSignalSpy reset;
    QSignalSpy aboutToReset;
    QSignalSpy inserted;
    QSignalSpy removed;
    QSignalSpy moved;
    QSignalSpy layout;
    QSignalSpy changed;
};

}  // namespace

class TstSearchProxy : public QObject
{
    Q_OBJECT

private slots:
    void filterChangePublishesOneReset();
    void filterChangeNeverMovesRowsPiecemeal();
    void anUnchangedRowSetIsNotPublishedAtAll();
    void countChangedFiresOncePerFilterChange();
    void sourceReloadReappliesTheFilter();
    void switchingSourceResetsEvenWhenTheRowsLineUp();
    void spaceFoldingFlipsBothWays();
    void anEditToTheTextRescans();
    void aRestyleForwardsWithoutRescanning();
    void mappingStaysMonotonicAtScale();
    void sourceModelIsReadableAsAProperty();
    void aDestroyedSourceLeavesTheProxyEmpty();
};

// One filter change, one modelReset -- and its partner, so a view is told the
// old rows are going before it is told to re-read.
void TstSearchProxy::filterChangePublishesOneReset()
{
    SubtitleLineModel model;
    model.setLines(threeLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    Traffic traffic(&filter);
    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 2);
    QCOMPARE(traffic.reset.size(), 1);
    QCOMPARE(traffic.aboutToReset.size(), 1);

    traffic.clear();
    filter.setPattern(QString());
    QCOMPARE(filter.count(), 3);
    QCOMPARE(traffic.reset.size(), 1);
    QCOMPARE(traffic.aboutToReset.size(), 1);
}

// The point of the rewrite. A scattered match used to be one begin/endRemoveRows
// pair per contiguous run of rejected rows -- 2000 of them on this track, 14 763
// on the 200k fixture -- each splicing the mapping vector again.
void TstSearchProxy::filterChangeNeverMovesRowsPiecemeal()
{
    SubtitleLineModel model;
    model.setLines(scatteredLines(20'000));
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);
    QCOMPARE(filter.count(), 20'000);

    Traffic traffic(&filter);
    filter.setPattern(QStringLiteral("needle"));
    QCOMPARE(filter.count(), 18'000);
    QCOMPARE(traffic.reset.size(), 1);
    QCOMPARE(traffic.inserted.size(), 0);
    QCOMPARE(traffic.removed.size(), 0);
    QCOMPARE(traffic.moved.size(), 0);
    QCOMPARE(traffic.layout.size(), 0);

    // Widening is the same shape, not a special case.
    traffic.clear();
    filter.setPattern(QStringLiteral("cue"));
    QCOMPARE(filter.count(), 20'000);
    QCOMPARE(traffic.reset.size(), 1);
    QCOMPARE(traffic.inserted.size(), 0);
    QCOMPARE(traffic.removed.size(), 0);

    // And the rows the view would paint are the ones that matched, renumbered.
    filter.setPattern(QStringLiteral("needle"));
    QCOMPARE(filter.textAtRow(0), QStringLiteral("cue 0 needle"));
    QCOMPARE(filter.textAtRow(4), QStringLiteral("cue 5 needle"));  // 4 was the gap
    QCOMPARE(filter.textAtRow(17'999), QStringLiteral("cue 19999 needle"));
    QCOMPARE(filter.textAtRow(18'000), QString());
}

// Typing another letter of a word every cue contains changes the pattern without
// changing the list. Resetting there would throw a reader who is mid-track back
// to row 0 for nothing, so it is deliberately silent.
void TstSearchProxy::anUnchangedRowSetIsNotPublishedAtAll()
{
    SubtitleLineModel model;
    model.setLines(scatteredLines(2000));
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    Traffic traffic(&filter);
    QSignalSpy patterns(&filter, &SubtitleFilterModel::patternChanged);

    // Every cue begins "cue ", so the accepted set is still all of them.
    filter.setPattern(QStringLiteral("cue"));
    QCOMPARE(filter.count(), 2000);
    QCOMPARE(traffic.reset.size(), 0);
    QCOMPARE(traffic.aboutToReset.size(), 0);
    // The pattern itself still changed, and the panel binds the match chip and
    // the empty state to it.
    QCOMPARE(patterns.size(), 1);
    QCOMPARE(filter.pattern(), QStringLiteral("cue"));

    // Narrowing for real does reset.
    filter.setPattern(QStringLiteral("needle"));
    QCOMPARE(filter.count(), 1800);
    QCOMPARE(traffic.reset.size(), 1);

    // Two different patterns accepting the same rows: silent again.
    traffic.clear();
    filter.setPattern(QStringLiteral("needl"));
    QCOMPARE(filter.count(), 1800);
    QCOMPARE(traffic.reset.size(), 0);

    // Setting the same pattern again is not a change at all.
    patterns.clear();
    filter.setPattern(QStringLiteral("needl"));
    QCOMPARE(patterns.size(), 0);
    QCOMPARE(traffic.reset.size(), 0);
}

// countChanged drives the "37 / 2000" chip and the empty state. It used to fire
// once per removed run, so one keystroke recomputed the chip 14 764 times.
void TstSearchProxy::countChangedFiresOncePerFilterChange()
{
    SubtitleLineModel model;
    model.setLines(scatteredLines(2000));
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    QSignalSpy counted(&filter, &SubtitleFilterModel::countChanged);
    filter.setPattern(QStringLiteral("needle"));
    QCOMPARE(filter.count(), 1800);
    QCOMPARE(counted.size(), 1);

    counted.clear();
    filter.setPattern(QStringLiteral("haystack"));
    QCOMPARE(filter.count(), 200);
    QCOMPARE(counted.size(), 1);

    // And not at all when the rows did not move.
    counted.clear();
    filter.setPattern(QStringLiteral("haystac"));
    QCOMPARE(filter.count(), 200);
    QCOMPARE(counted.size(), 0);
}

// SubtitleManager reuses its line models across files rather than deleting them,
// so opening a second file is setLines() on the model the proxy is already
// attached to. The filter has to be re-applied to the new cues -- keeping the
// old accepted rows would index a track that no longer exists.
void TstSearchProxy::sourceReloadReappliesTheFilter()
{
    SubtitleLineModel model;
    model.setLines(threeLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);
    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 2);

    Traffic traffic(&filter);
    QVector<SubtitleLine> next;
    next.append({500, 900, QStringLiteral("alpha once more"), {}});
    next.append({1500, 1900, QStringLiteral("gamma"), {}});
    next.append({2500, 2900, QStringLiteral("delta"), {}});
    next.append({3500, 3900, QStringLiteral("ALPHA shouting"), {}});
    model.setLines(next);

    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.sourceCount(), 4);
    QCOMPARE(filter.textAtRow(0), QStringLiteral("alpha once more"));
    QCOMPARE(filter.textAtRow(1), QStringLiteral("ALPHA shouting"));
    QCOMPARE(filter.startMsAt(1), 3500);
    QCOMPARE(traffic.reset.size(), 1);
    QCOMPARE(traffic.aboutToReset.size(), 1);
    QCOMPARE(traffic.removed.size(), 0);

    // A track with nothing matching empties the view rather than keeping stale
    // rows -- the panel shows its "no lines contain ..." state off count alone.
    model.setLines({{0, 100, QStringLiteral("nothing of the sort"), {}}});
    QCOMPARE(filter.count(), 0);
    QCOMPARE(filter.sourceCount(), 1);
    QCOMPARE(filter.rowAt(50), -1);
}

// A tab click repoints the proxy at another track. The accepted row *numbers*
// can be identical across two tracks while the cues behind them differ, so the
// "nothing moved, stay quiet" shortcut must not apply to a source swap.
void TstSearchProxy::switchingSourceResetsEvenWhenTheRowsLineUp()
{
    SubtitleLineModel english;
    english.setLines(threeLines());

    QVector<SubtitleLine> french;
    french.append({1000, 2000, QStringLiteral("alpha en francais"), {}});
    french.append({3000, 4000, QStringLiteral("beta"), {}});
    french.append({5000, 6000, QStringLiteral("alpha encore"), {}});
    SubtitleLineModel other;
    other.setLines(french);

    SubtitleFilterModel filter;
    filter.setSourceModel(&english);
    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 2);

    Traffic traffic(&filter);
    QSignalSpy sources(&filter, &SubtitleFilterModel::sourceModelChanged);
    filter.setSourceModel(&other);

    // Same two source rows accepted, entirely different text behind them.
    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.textAtRow(1), QStringLiteral("alpha encore"));
    QCOMPARE(traffic.reset.size(), 1);
    QCOMPARE(sources.size(), 1);

    // Setting the same source again is not a change.
    traffic.clear();
    sources.clear();
    filter.setSourceModel(&other);
    QCOMPARE(traffic.reset.size(), 0);
    QCOMPARE(sources.size(), 0);

    // The old track has to be let go of, or a theme change on a track nobody is
    // looking at would repaint the rows of the one they are.
    traffic.clear();
    english.setLines(threeLines());
    english.setBackground(QColor(QStringLiteral("#101418")));
    QCOMPARE(traffic.reset.size(), 0);
    QCOMPARE(traffic.changed.size(), 0);
    QCOMPARE(filter.textAtRow(0), QStringLiteral("alpha en francais"));
}

// A pattern containing a space searches through ASS hard spaces and takes a
// different code path from one that does not. Crossing that line in either
// direction has to rescan the whole track: a scan that narrowed the previous
// result would carry forward rows the other path rejected, and the wrong answer
// is a missing search hit -- silent, and indistinguishable from the film not
// saying the words.
void TstSearchProxy::spaceFoldingFlipsBothWays()
{
    const QString nbsp(QChar(0x00A0));
    QVector<SubtitleLine> lines;
    lines.append({0, 100, QStringLiteral("a") + nbsp + QStringLiteral("hard space"), {}});
    lines.append({200, 300, QStringLiteral("a plain space"), {}});
    lines.append({400, 500, QStringLiteral("hardly anything"), {}});

    SubtitleLineModel model;
    model.setLines(lines);
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);

    // No space: the plain contains() path, and every row has an "a".
    filter.setPattern(QStringLiteral("a"));
    QCOMPARE(filter.count(), 3);

    // Adding a space switches to the folding path.
    filter.setPattern(QStringLiteral("a hard"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.textAtRow(0),
             QStringLiteral("a") + nbsp + QStringLiteral("hard space"));

    // The direction that would actually break. "hard space" on the folding path
    // accepts row 0 only; dropping back to a pattern with no space has to
    // reconsider rows 1 and 2 from the source rather than from the survivors.
    filter.setPattern(QStringLiteral("hard space"));
    QCOMPARE(filter.count(), 1);
    filter.setPattern(QStringLiteral("hard"));
    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.textAtRow(1), QStringLiteral("hardly anything"));

    // And a pattern differing only in which kind of space it holds reaches the
    // same row, whichever order the two arrive in.
    filter.setPattern(QStringLiteral("a") + nbsp + QStringLiteral("hard"));
    QCOMPARE(filter.count(), 1);
    filter.setPattern(QStringLiteral("a plain"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.textAtRow(0), QStringLiteral("a plain space"));
}

// Nothing rewrites a cue today. If something ever does, the search has to
// notice: a row whose text no longer contains the term must leave the list, and
// a search result that lies is the one failure this browser cannot survive.
void TstSearchProxy::anEditToTheTextRescans()
{
    EditableLineModel model;
    model.setLines(threeLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);
    filter.setPattern(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 2);

    Traffic traffic(&filter);

    // A visible row loses the term: it leaves, and the whole list is republished
    // because the rows below it have renumbered.
    model.rewriteRow(0, QStringLiteral("gamma"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(traffic.reset.size(), 1);
    QCOMPARE(filter.textAtRow(0), QStringLiteral("alpha again"));

    // A hidden row gains it, and arrives above the one already showing.
    traffic.clear();
    model.rewriteRow(1, QStringLiteral("alpha, belatedly"));
    QCOMPARE(filter.count(), 2);
    QCOMPARE(traffic.reset.size(), 1);
    QCOMPARE(filter.textAtRow(0), QStringLiteral("alpha, belatedly"));
    QCOMPARE(filter.cueStartMsAt(0), 3000);

    // An edit that does not change what matches is a plain forward, remapped to
    // the view row -- not a reset, which would scroll the list for a repaint.
    traffic.clear();
    model.rewriteRow(2, QStringLiteral("alpha again, reworded"));
    QCOMPARE(traffic.reset.size(), 0);
    QCOMPARE(traffic.changed.size(), 1);
    QCOMPARE(traffic.changed.at(0).at(0).value<QModelIndex>().row(), 1);

    // An unqualified dataChanged means "every role", which includes the text, so
    // it is rescanned rather than trusted.
    traffic.clear();
    model.touchAll({});
    QCOMPARE(traffic.reset.size(), 0);   // nothing actually moved
    QCOMPARE(traffic.changed.size(), 1);
    QCOMPARE(filter.count(), 2);
}

// The one dataChanged the app really emits: a theme change restyles every row.
// It names StyledTextRole, so it cannot alter what matches and must not cost a
// rescan -- on a 200k track that would be a full pass for a colour change.
void TstSearchProxy::aRestyleForwardsWithoutRescanning()
{
    EditableLineModel model;
    model.setLines(threeLines());
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);
    filter.setPattern(QStringLiteral("alpha"));

    Traffic traffic(&filter);
    model.setBackground(QColor(QStringLiteral("#f6f7f9")));

    // Source rows 0 and 2 survive the filter and are adjacent in the view, so
    // the whole restyle is one signal covering view rows 0..1.
    QCOMPARE(traffic.reset.size(), 0);
    QCOMPARE(traffic.changed.size(), 1);
    QCOMPARE(traffic.changed.at(0).at(0).value<QModelIndex>().row(), 0);
    QCOMPARE(traffic.changed.at(0).at(1).value<QModelIndex>().row(), 1);
    QCOMPARE(traffic.changed.at(0).at(2).value<QList<int>>(),
             QList<int>{SubtitleLineModel::StyledTextRole});

    // A row the filter hides produces nothing: the view has no index for it, and
    // forwarding the source's number would repaint whichever row happened to be
    // sitting there.
    traffic.clear();
    model.touchRow(1, {SubtitleLineModel::StyledTextRole});
    QCOMPARE(traffic.changed.size(), 0);
}

// The mapping is a sorted vector searched by bisection rather than a reverse
// index, which is only correct while it stays ascending. Auto-follow asks it for
// a source row on every position tick, so an off-by-one here highlights the
// wrong line for the length of the film.
void TstSearchProxy::mappingStaysMonotonicAtScale()
{
    SubtitleLineModel model;
    model.setLines(scatteredLines(20'000));
    SubtitleFilterModel filter;
    filter.setSourceModel(&model);
    filter.setPattern(QStringLiteral("needle"));
    QCOMPARE(filter.count(), 18'000);

    // Walk the whole view and check every row against the source row it claims,
    // by the timestamp the source stored for it.
    for (int row = 0; row < filter.count(); ++row) {
        const int within = row % 9;
        const int sourceRow = (row / 9) * 10 + within + (within >= 4 ? 1 : 0);
        QCOMPARE(filter.cueStartMsAt(row), sourceRow * 100LL);
    }

    // And back the other way, through the position lookup: a cue that survived
    // maps to its view row, one that did not maps to -1.
    QCOMPARE(filter.rowAt(0), 0);          // source 0 -> view 0
    QCOMPARE(filter.rowAt(300), 3);        // source 3 -> view 3
    QCOMPARE(filter.rowAt(400), -1);       // source 4 was filtered out
    QCOMPARE(filter.rowAt(500), 4);        // source 5 -> view 4, renumbered
    QCOMPARE(filter.rowAt(1'999'900), 17'999);

    // The delay shifts which cue is playing without touching the mapping.
    filter.setDelayMs(500);
    QCOMPARE(filter.rowAt(1000), 4);
    QCOMPARE(filter.startMsAt(4), 1000);
    QCOMPARE(filter.cueStartMsAt(4), 500);
}

// Main.qml binds `sourceModel:` on the proxy. It used to come from
// QAbstractProxyModel and is now declared here, so it has to be present under
// that exact name, take a QAbstractItemModel*, and notify -- a binding that
// silently stopped updating would strand the panel on the track it opened with.
void TstSearchProxy::sourceModelIsReadableAsAProperty()
{
    SubtitleLineModel model;
    model.setLines(threeLines());
    SubtitleFilterModel filter;

    const QMetaObject *meta = filter.metaObject();
    const int index = meta->indexOfProperty("sourceModel");
    QVERIFY(index >= 0);
    const QMetaProperty property = meta->property(index);
    QVERIFY(property.isWritable());
    QVERIFY(property.hasNotifySignal());
    QCOMPARE(QByteArray(property.typeName()), QByteArrayLiteral("QAbstractItemModel*"));

    QSignalSpy sources(&filter, &SubtitleFilterModel::sourceModelChanged);
    QVERIFY(filter.setProperty("sourceModel",
                               QVariant::fromValue<QAbstractItemModel *>(&model)));
    QCOMPARE(sources.size(), 1);
    QCOMPARE(filter.sourceModel(), &model);
    QCOMPARE(filter.count(), 3);

    // Closing a file assigns null, which QML writes as a default-constructed
    // pointer rather than by calling setSourceModel(nullptr) itself.
    QVERIFY(filter.setProperty("sourceModel",
                               QVariant::fromValue<QAbstractItemModel *>(nullptr)));
    QCOMPARE(filter.sourceModel(), nullptr);
    QCOMPARE(filter.count(), 0);
    QCOMPARE(sources.size(), 2);
}

// The manager parents its models to itself and empties them rather than deleting
// them, so this should not happen -- but the proxy holds a raw pointer it reads
// on every position tick, and the alternative to noticing is reading freed
// memory.
void TstSearchProxy::aDestroyedSourceLeavesTheProxyEmpty()
{
    SubtitleFilterModel filter;
    {
        SubtitleLineModel model;
        model.setLines(threeLines());
        filter.setSourceModel(&model);
        filter.setPattern(QStringLiteral("alpha"));
        QCOMPARE(filter.count(), 2);
    }

    QCOMPARE(filter.sourceModel(), nullptr);
    QCOMPARE(filter.count(), 0);
    QCOMPARE(filter.sourceCount(), 0);
    QCOMPARE(filter.rowAt(1500), -1);
    QCOMPARE(filter.textAtRow(0), QString());
    // The search text survives, exactly as it survives a track switch.
    QCOMPARE(filter.pattern(), QStringLiteral("alpha"));
}

QTEST_MAIN(TstSearchProxy)
#include "tst_searchproxy.moc"
