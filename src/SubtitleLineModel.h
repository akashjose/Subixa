// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QAbstractListModel>
#include <QtGui/QColor>
#include <QtQml/qqmlregistration.h>

#include "SubtitleTypes.h"

// Rows of a single subtitle track.
//
// The line buffer is implicitly shared with the track list SubtitleManager holds,
// so handing a track to a model is a refcount bump, not a copy -- this is what
// replaces milestone 1's QVariantList snapshot (~600 ms on the GUI thread for
// 200k cues). Nothing here mutates m_lines after setLines(), so it never detaches.
//
// Instantiated by SubtitleManager, never by QML: QML only ever receives one
// through SubtitleManager::model().
class SubtitleLineModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        StartMsRole = Qt::UserRole + 1,
        EndMsRole,
        StartTextRole,  // startMs as hh:mm:ss.mmm, formatted per visible row
        TextRole,       // display text: override tags stripped, entities decoded
        RawTextRole,    // decoder payload, exactly as the decoder gave it
        StyledTextRole, // that payload as markup: italics, bold, speaker colours
    };
    Q_ENUM(Role)

    explicit SubtitleLineModel(QObject *parent = nullptr);

    void setLines(const QVector<SubtitleLine> &lines);

    // The colour rows are drawn on, so a cue's own colour can be kept legible
    // against it -- subtitle colours are chosen to sit over a picture, and white
    // dialogue on a light theme would otherwise be invisible. Set by the manager
    // from the theme; styling is computed per visible row, never stored.
    void setBackground(const QColor &background);

    int count() const { return static_cast<int>(m_lines.size()); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Row of the cue covering positionMs, or of the one most recently started if
    // none covers it; -1 before the first cue. Auto-follow calls this on every
    // position tick, so it stays a binary search -- the extractor sorts cues by
    // start time, which is what makes that valid.
    Q_INVOKABLE int indexAt(qint64 positionMs) const;

signals:
    void countChanged();

private:
    QVector<SubtitleLine> m_lines;
    QColor m_background;
};
