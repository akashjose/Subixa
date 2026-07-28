// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "SubtitleFilterModel.h"

#include "SubtitleLineModel.h"

#include <algorithm>
#include <numeric>

namespace {

// ASS \h is a non-breaking space and the extractor keeps it as one, which is
// correct for display -- but nobody types U+00A0 into a search box, so without
// folding it a phrase spanning a hard space matches nothing. It hides well: the
// panel renders both identically and searching either side of the gap works.
QChar foldSpace(QChar c)
{
    return c == QChar(QChar::Nbsp) ? QLatin1Char(' ') : c;
}

// Case-insensitive substring search treating U+00A0 in the haystack as a plain
// space. `needle` arrives already space-folded and case-folded from setPattern,
// so the inner loop folds one side only.
bool containsFoldingSpaces(const QString &haystack, const QString &needle)
{
    const int n = haystack.size();
    const int m = needle.size();
    if (m > n)
        return false;

    for (int i = 0; i <= n - m; ++i) {
        int j = 0;
        while (j < m && foldSpace(haystack.at(i + j)).toCaseFolded() == needle.at(j))
            ++j;
        if (j == m)
            return true;
    }
    return false;
}

}  // namespace

SubtitleFilterModel::SubtitleFilterModel(QObject *parent) : QAbstractListModel(parent)
{
    // Every membership change this model publishes is a reset -- there is no
    // row-level insert or remove path -- so one connection covers filtering,
    // switching tracks and the source reloading its cues.
    connect(this, &QAbstractItemModel::modelReset, this,
            &SubtitleFilterModel::countChanged);
}

void SubtitleFilterModel::setSourceModel(QAbstractItemModel *source)
{
    if (source == m_source)
        return;

    for (const QMetaObject::Connection &c : std::as_const(m_sourceConnections))
        disconnect(c);
    m_sourceConnections.clear();

    // The scan happens inside the reset, so that by the time the view is told to
    // re-read everything the pointer and the mapping already describe the same
    // track. roleNames() moves with the source too, and only a reset makes QML
    // ask for them again.
    beginResetModel();
    m_source = source;
    m_lines = qobject_cast<SubtitleLineModel *>(source);
    m_accepted = scanSource();
    endResetModel();

    connectSource();
    emit sourceModelChanged();
}

void SubtitleFilterModel::connectSource()
{
    if (!m_source)
        return;

    // A source reset is bracketed rather than answered afterwards: between the
    // two the cues are being replaced under us, and m_accepted indexes rows that
    // are on their way out. SubtitleManager reuses its line models across files
    // rather than deleting them, so this is the ordinary path for opening a
    // second file, not an edge case.
    m_sourceConnections = {
        connect(m_source, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
            beginResetModel();
            m_accepted.clear();
        }),
        connect(m_source, &QAbstractItemModel::modelReset, this, [this] {
            m_accepted = scanSource();
            endResetModel();
        }),
        connect(m_source, &QAbstractItemModel::dataChanged, this,
                &SubtitleFilterModel::handleSourceDataChanged),
        // Nothing in this app inserts or removes single cues -- a track arrives
        // whole via setLines() -- so these rescan rather than trying to patch
        // the mapping. Coarse on purpose: an incremental path that is never
        // exercised is an incremental path that is wrong.
        connect(m_source, &QAbstractItemModel::rowsInserted, this,
                [this] { rescan(true); }),
        connect(m_source, &QAbstractItemModel::rowsRemoved, this,
                [this] { rescan(true); }),
        connect(m_source, &QAbstractItemModel::rowsMoved, this, [this] { rescan(true); }),
        connect(m_source, &QAbstractItemModel::layoutChanged, this,
                [this] { rescan(true); }),
        // The manager parents its models to itself and empties them rather than
        // deleting them, so this should never fire; it is here because the
        // alternative to noticing is reading freed memory on the next tick.
        connect(m_source, &QObject::destroyed, this, [this] { setSourceModel(nullptr); }),
    };
}

void SubtitleFilterModel::setPattern(const QString &pattern)
{
    if (m_pattern == pattern)
        return;
    m_pattern = pattern;

    // Only a pattern containing a space can span a hard space, so single-word
    // searches -- the overwhelming common case -- keep the optimised
    // QString::contains() path and pay nothing for the folding.
    m_needle.clear();
    m_needle.reserve(pattern.size());
    m_foldSpaces = false;
    for (const QChar c : pattern) {
        const QChar folded = foldSpace(c);
        if (folded == QLatin1Char(' '))
            m_foldSpaces = true;  // also catches a hard space pasted from the panel
        m_needle.append(folded.toCaseFolded());
    }

    rescan(false);
    emit patternChanged();
}

bool SubtitleFilterModel::rescan(bool force)
{
    QVector<int> accepted = scanSource();
    // A pattern that changes without changing what matches -- typing a second
    // letter of a word every cue contains -- must not disturb the list, or the
    // reader loses their place in it for nothing.
    if (!force && accepted == m_accepted)
        return false;

    beginResetModel();
    m_accepted = std::move(accepted);
    endResetModel();
    return true;
}

QVector<int> SubtitleFilterModel::scanSource() const
{
    QVector<int> accepted;
    const int rows = m_source ? m_source->rowCount() : 0;
    if (rows <= 0)
        return accepted;

    if (m_pattern.isEmpty()) {
        // The common case, and worth not asking the source 200 000 questions to
        // arrive at "all of them".
        accepted.resize(rows);
        std::iota(accepted.begin(), accepted.end(), 0);
        return accepted;
    }

    // No reserve(): the hit rate of a search is anywhere between one row and all
    // of them, and a guess that is wrong either wastes megabytes or reallocates
    // anyway. Growth is amortised.
    for (int row = 0; row < rows; ++row) {
        const QString text =
            m_source->data(m_source->index(row, 0), SubtitleLineModel::TextRole).toString();
        if (accepts(text))
            accepted.append(row);
    }
    return accepted;
}

bool SubtitleFilterModel::accepts(const QString &text) const
{
    if (!m_foldSpaces)
        return text.contains(m_pattern, Qt::CaseInsensitive);
    return containsFoldingSpaces(text, m_needle);
}

int SubtitleFilterModel::viewRowFor(int sourceRow) const
{
    const auto it = std::lower_bound(m_accepted.cbegin(), m_accepted.cend(), sourceRow);
    if (it == m_accepted.cend() || *it != sourceRow)
        return -1;
    return static_cast<int>(std::distance(m_accepted.cbegin(), it));
}

void SubtitleFilterModel::handleSourceDataChanged(const QModelIndex &topLeft,
                                                  const QModelIndex &bottomRight,
                                                  const QList<int> &roles)
{
    if (topLeft.parent().isValid())
        return;  // flat list; nothing else can reach the view

    // A change to the cue text is a change to what matches. Nothing in the app
    // rewrites a cue today -- SubtitleLineModel's only dataChanged is the theme
    // restyling every row, which names StyledTextRole -- but an edit arriving
    // here without a rescan would leave the search listing lines that no longer
    // contain the term, and that failure is silent.
    if (roles.isEmpty() || roles.contains(SubtitleLineModel::TextRole)) {
        if (rescan(false))
            return;  // the reset already told the view to re-read everything
    }

    // Rows accepted out of a contiguous source range are contiguous in the view,
    // because the mapping is monotonic -- so one signal covers the range however
    // many rows the filter dropped out of the middle of it. Forwarding the
    // source's own numbers instead would repaint the wrong rows, and with only
    // the styled role changing the wrong rows would still look right.
    const auto first =
        std::lower_bound(m_accepted.cbegin(), m_accepted.cend(), topLeft.row());
    const auto last =
        std::upper_bound(m_accepted.cbegin(), m_accepted.cend(), bottomRight.row());
    if (first == last)
        return;  // nothing in the changed range is showing

    const int firstView = static_cast<int>(std::distance(m_accepted.cbegin(), first));
    const int lastView = static_cast<int>(std::distance(m_accepted.cbegin(), last)) - 1;
    emit dataChanged(index(firstView, 0), index(lastView, 0), roles);
}

int SubtitleFilterModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;  // flat list
    return count();
}

QVariant SubtitleFilterModel::data(const QModelIndex &index, int role) const
{
    if (!m_source || index.row() < 0 || index.row() >= m_accepted.size())
        return {};
    return m_source->data(m_source->index(m_accepted.at(index.row()), 0), role);
}

QHash<int, QByteArray> SubtitleFilterModel::roleNames() const
{
    return m_source ? m_source->roleNames() : QAbstractListModel::roleNames();
}

int SubtitleFilterModel::sourceCount() const
{
    return m_source ? m_source->rowCount() : 0;
}

void SubtitleFilterModel::setDelayMs(qint64 delayMs)
{
    if (m_delayMs == delayMs)
        return;
    m_delayMs = delayMs;
    // No filter or row change: the delay moves where cues *are in time*, not
    // which of them are showing. Auto-follow recomputes from the next position
    // tick, which is at most a frame away.
    emit delayMsChanged();
}

int SubtitleFilterModel::rowAt(qint64 positionMs) const
{
    if (!m_lines)
        return -1;

    // A cue stored at t is spoken at t + delay, so the cue playing now is the
    // one stored at (now - delay).
    const int sourceRow = m_lines->indexAt(positionMs - m_delayMs);
    if (sourceRow < 0)
        return -1;
    return viewRowFor(sourceRow);
}

int SubtitleFilterModel::rowAfter(qint64 positionMs) const
{
    const int rows = count();
    if (rows == 0)
        return -1;
    const int here = rowAt(positionMs);
    // -1 means playback is before the first visible cue, so the next line is
    // the first one rather than "none".
    const int next = here < 0 ? 0 : here + 1;
    return next < rows ? next : -1;
}

int SubtitleFilterModel::rowBefore(qint64 positionMs) const
{
    const int here = rowAt(positionMs);
    if (here < 0)
        return -1;
    // Someone a little way into a line means "take me back to its start", the
    // way a track-back button does on a music player; only near the start does
    // it mean the previous line.
    constexpr qint64 kRestartWindowMs = 1200;
    if (positionMs - startMsAt(here) > kRestartWindowMs)
        return here;
    return here > 0 ? here - 1 : -1;
}

QString SubtitleFilterModel::textAt(qint64 positionMs) const
{
    const int row = rowAt(positionMs);
    if (row < 0)
        return {};
    return data(index(row, 0), SubtitleLineModel::TextRole).toString();
}

qint64 SubtitleFilterModel::cueStartMsAt(int row) const
{
    if (row < 0 || row >= count())
        return -1;
    return data(index(row, 0), SubtitleLineModel::StartMsRole).toLongLong();
}

qint64 SubtitleFilterModel::startMsAt(int row) const
{
    const qint64 cueStart = cueStartMsAt(row);
    if (cueStart < 0)
        return -1;
    // Playback position, not the stored one: seeking here must land where the
    // line is actually spoken.
    return cueStart + m_delayMs;
}

qint64 SubtitleFilterModel::endMsAt(int row) const
{
    if (row < 0 || row >= count())
        return -1;
    const qint64 cueEnd =
        data(index(row, 0), SubtitleLineModel::EndMsRole).toLongLong();
    if (cueEnd <= 0)
        return -1;
    return cueEnd + m_delayMs;
}

QString SubtitleFilterModel::textAtRow(int row) const
{
    if (row < 0 || row >= count())
        return {};
    return data(index(row, 0), SubtitleLineModel::TextRole).toString();
}

QString SubtitleFilterModel::timestampAtRow(int row) const
{
    if (row < 0 || row >= count())
        return {};
    return data(index(row, 0), SubtitleLineModel::StartTextRole).toString();
}
