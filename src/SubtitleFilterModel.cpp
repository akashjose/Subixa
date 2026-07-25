#include "SubtitleFilterModel.h"

#include "SubtitleLineModel.h"

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
    return sourceModel()
        ->data(index, SubtitleLineModel::TextRole)
        .toString()
        .contains(m_pattern, Qt::CaseInsensitive);
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
