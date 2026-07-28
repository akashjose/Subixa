// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "SubtitleLineModel.h"

#include "SubtitleStyle.h"

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
    case StyleNameRole:
        return line.style;
    case ActorRole:
        return line.actor;
    case StyledTextRole:
        // Built here rather than at parse time: it is derived from rawText, only
        // visible rows ever ask for it, and storing a third string per cue would
        // cost megabytes on a feature-length track for something the reader sees
        // twenty rows of.
        //
        // A cue naming a style the table does not have -- a renamed style, a
        // sidecar muxed against the wrong header -- resolves to a default-built
        // AssStyle, which says nothing, so it renders exactly as it did before
        // the table existed rather than failing.
        return SubtitleStyle::toStyledText(line.rawText, m_background,
                                           m_styles.value(line.style));
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
        {StyledTextRole, "styled"},
        {StyleNameRole, "styleName"},
        {ActorRole, "actor"},
    };
}

void SubtitleLineModel::setStyles(const AssStyleTable &styles)
{
    if (m_styles == styles)
        return;
    m_styles = styles;
    if (m_lines.isEmpty())
        return;
    // Same shape as setBackground(): only the styled role is derived from the
    // table, and every row's is.
    emit dataChanged(index(0, 0), index(count() - 1, 0), {StyledTextRole});
}

void SubtitleLineModel::setBackground(const QColor &background)
{
    if (m_background == background)
        return;
    m_background = background;
    if (m_lines.isEmpty())
        return;
    // Only the styled role changes, and every row's does: a theme switch has to
    // repaint the list without touching the cues themselves.
    emit dataChanged(index(0, 0), index(count() - 1, 0), {StyledTextRole});
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
