#include "SubtitleFilterModel.h"

#include "SubtitleLineModel.h"

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

SubtitleFilterModel::SubtitleFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    // The proxy's own signals cover every way the row count moves: filtering
    // (rows inserted/removed) and switching tracks (model reset).
    connect(this, &QAbstractItemModel::rowsInserted, this,
            &SubtitleFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this,
            &SubtitleFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this,
            &SubtitleFilterModel::countChanged);
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

    // Rows only, not sort order: nothing here sorts, and invalidate() would
    // redo that work on every keystroke.
    invalidateRowsFilter();
    emit patternChanged();
    emit countChanged();
}

int SubtitleFilterModel::sourceCount() const
{
    return sourceModel() ? sourceModel()->rowCount() : 0;
}

bool SubtitleFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_pattern.isEmpty())
        return true;  // the common case: skip the per-row string work entirely

    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    // Text only. Someone typing "12" is looking for words, not a timestamp.
    const QString text =
        sourceModel()->data(index, SubtitleLineModel::TextRole).toString();

    if (!m_foldSpaces)
        return text.contains(m_pattern, Qt::CaseInsensitive);
    return containsFoldingSpaces(text, m_needle);
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
    const auto *lines = qobject_cast<const SubtitleLineModel *>(sourceModel());
    if (!lines)
        return -1;

    // A cue stored at t is spoken at t + delay, so the cue playing now is the
    // one stored at (now - delay).
    const int sourceRow = lines->indexAt(positionMs - m_delayMs);
    if (sourceRow < 0)
        return -1;
    return mapFromSource(lines->index(sourceRow, 0)).row();
}

int SubtitleFilterModel::rowAfter(qint64 positionMs) const
{
    const int count = rowCount();
    if (count == 0)
        return -1;
    const int here = rowAt(positionMs);
    // -1 means playback is before the first visible cue, so the next line is
    // the first one rather than "none".
    const int next = here < 0 ? 0 : here + 1;
    return next < count ? next : -1;
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
    if (row < 0 || row >= rowCount())
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
    if (row < 0 || row >= rowCount())
        return -1;
    const qint64 cueEnd =
        data(index(row, 0), SubtitleLineModel::EndMsRole).toLongLong();
    if (cueEnd <= 0)
        return -1;
    return cueEnd + m_delayMs;
}

QString SubtitleFilterModel::textAtRow(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    return data(index(row, 0), SubtitleLineModel::TextRole).toString();
}

QString SubtitleFilterModel::timestampAtRow(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    return data(index(row, 0), SubtitleLineModel::StartTextRole).toString();
}
