#include "SubtitleLineModel.h"

#include <algorithm>

SubtitleLineModel::SubtitleLineModel(QObject *parent) : QAbstractListModel(parent) {}

void SubtitleLineModel::setLines(const QVector<SubtitleLine> &lines)
{
    beginResetModel();
    m_lines = lines;  // implicitly shared: no deep copy
    endResetModel();
    emit countChanged();
}

int SubtitleLineModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;  // flat list
    return count();
}

QVariant SubtitleLineModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_lines.size())
        return {};

    const SubtitleLine &line = m_lines.at(index.row());
    switch (role) {
    case StartMsRole:
        return line.startMs;
    case EndMsRole:
        return line.endMs;
    case StartTextRole:
        return formatSubtitleTimestamp(line.startMs);
    case TextRole:
    case Qt::DisplayRole:
        return line.text;
    case RawTextRole:
        return line.rawText;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> SubtitleLineModel::roleNames() const
{
    return {
        {StartMsRole, "startMs"},
        {EndMsRole, "endMs"},
        {StartTextRole, "start"},
        {TextRole, "text"},
        {RawTextRole, "rawText"},
    };
}

int SubtitleLineModel::indexAt(qint64 positionMs) const
{
    if (m_lines.isEmpty())
        return -1;

    // First cue starting after the position; the one before it is the answer.
    const auto it = std::upper_bound(m_lines.cbegin(), m_lines.cend(), positionMs,
                                     [](qint64 ms, const SubtitleLine &line) {
                                         return ms < line.startMs;
                                     });
    if (it == m_lines.cbegin())
        return -1;
    return static_cast<int>(std::distance(m_lines.cbegin(), it) - 1);
}
