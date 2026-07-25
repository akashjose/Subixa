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

int SubtitleFilterModel::rowAt(qint64 positionMs) const
{
    const auto *lines = qobject_cast<const SubtitleLineModel *>(sourceModel());
    if (!lines)
        return -1;

    const int sourceRow = lines->indexAt(positionMs);
    if (sourceRow < 0)
        return -1;
    return mapFromSource(lines->index(sourceRow, 0)).row();
}

qint64 SubtitleFilterModel::startMsAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return -1;
    return data(index(row, 0), SubtitleLineModel::StartMsRole).toLongLong();
}
