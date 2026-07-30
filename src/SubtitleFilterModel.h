// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtQml/qqmlregistration.h>

class SubtitleLineModel;

// Search over one SubtitleLineModel, plus the source-to-view row mapping
// auto-follow needs.
//
// A hand-written proxy rather than a QSortFilterProxyModel, and the reason is
// measured rather than assumed. On the 200k-cue fixture, moving the pattern from
// "e" to "7" -- 200 000 rows down to 81 902 -- cost 326 ms, of which the
// predicate was 12 ms. The other 96% was QSortFilterProxyModel updating its
// mapping one contiguous run of rejected rows at a time: 14 763 runs, so 14 763
// begin/endRemoveRows pairs and 14 764 countChanged. The cost is not the signal
// traffic -- the same call takes the same time with nothing connected at all --
// it is the incremental splicing behind it. Handing the *same* filter to a proxy
// that had not built its mapping yet cost 14 ms, so rebuilding from scratch was
// 23x faster than narrowing what was already there.
//
// Hence: every filter change rescans the whole track into a QVector<int> of
// accepted source rows and publishes it as a single modelReset. The scan is the
// irreducible part, and incremental narrowing cannot shorten it -- the search
// box is debounced by 150 ms in SubtitlePanel.qml, so the event that costs
// anything is the one full scan after a burst of typing, not the keystrokes.
//
// Set `sourceModel` to switch tracks -- that is the whole cost of a tab switch,
// since no rows are copied. The search text deliberately survives the switch:
// looking for the same word across tracks is the common case.
class SubtitleFilterModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    // Declared rather than inherited, now that QAbstractProxyModel is gone. QML
    // binds it by name in Main.qml, so the name and the type have to stay put.
    Q_PROPERTY(QAbstractItemModel *sourceModel READ sourceModel WRITE setSourceModel
                   NOTIFY sourceModelChanged)
    Q_PROPERTY(QString pattern READ pattern WRITE setPattern NOTIFY patternChanged)
    // The subtitle timing offset in force, in milliseconds. Positive means the
    // subtitles have been pushed later.
    //
    // The browser has to know about it or the two halves of this player
    // disagree: with a +2 s delay, the cue stored at 00:10 is spoken at 00:12,
    // so auto-follow would highlight the wrong line and clicking a row would
    // seek two seconds off. Both directions are corrected here rather than at
    // each call site, so a resync cannot half-apply.
    Q_PROPERTY(qint64 delayMs READ delayMs WRITE setDelayMs NOTIFY delayMsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int sourceCount READ sourceCount NOTIFY countChanged)

public:
    explicit SubtitleFilterModel(QObject *parent = nullptr);

    QAbstractItemModel *sourceModel() const { return m_source; }
    void setSourceModel(QAbstractItemModel *source);

    QString pattern() const { return m_pattern; }
    void setPattern(const QString &pattern);

    qint64 delayMs() const { return m_delayMs; }
    void setDelayMs(qint64 delayMs);

    int count() const { return static_cast<int>(m_accepted.size()); }
    int sourceCount() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    // Forwarded from the source, because the delegate binds model.text,
    // model.start and friends by name and QML resolves those against *this*
    // model. With no source there is nothing to forward and the base class's
    // generic set stands in, which is what the proxy did before.
    QHash<int, QByteArray> roleNames() const override;

    // View row for the cue playing at positionMs, or -1 -- either because
    // playback is before the first cue, or because the current cue is filtered
    // out, which auto-follow treats the same way: nothing to highlight.
    // `positionMs` is playback position; the delay is applied here.
    Q_INVOKABLE int rowAt(qint64 positionMs) const;

    // Start of a view row as a *playback* position in ms, or -1 -- so seeking
    // to it lands where the line is actually spoken. Saves QML a round trip
    // through the delegate when seeking from something other than a click.
    Q_INVOKABLE qint64 startMsAt(int row) const;
    // The same value without the delay applied: what the row displays.
    Q_INVOKABLE qint64 cueStartMsAt(int row) const;

    // View row of the cue before/after `positionMs`, for previous/next-line
    // seeking. -1 when there is none in that direction.
    Q_INVOKABLE int rowAfter(qint64 positionMs) const;
    Q_INVOKABLE int rowBefore(qint64 positionMs) const;

    // End of a view row as a playback position in ms, or -1. What "loop this
    // line" needs to know where the line stops.
    Q_INVOKABLE qint64 endMsAt(int row) const;

    // The display text of the cue playing at `positionMs`, or an empty string.
    // The seek bar's hover preview calls this on every mouse move.
    Q_INVOKABLE QString textAt(qint64 positionMs) const;

    // Row accessors for QML.
    //
    // Named rather than left to `model.data(index, 260)`: QML has no way to name
    // a C++ role enum, so every call site was a magic number that silently
    // pointed somewhere else the moment a role was inserted above it. These are
    // the three the panel actually needs.
    Q_INVOKABLE QString textAtRow(int row) const;
    Q_INVOKABLE QString timestampAtRow(int row) const;

signals:
    void sourceModelChanged();
    void patternChanged();
    void countChanged();
    void delayMsChanged();

private:
    // The whole scan, source row 0 upwards. The only way the accepted set is
    // ever produced: there is no narrowing path, so m_foldSpaces flipping
    // between one pattern and the next cannot leave a stale rejection behind.
    QVector<int> scanSource() const;
    // Rescan and publish. `force` resets the view even when the accepted rows
    // come out identical, which a source swap needs: the row numbers can match
    // while the cues behind them are a different track.
    bool rescan(bool force);
    bool accepts(const QString &text) const;

    void connectSource();
    void handleSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                                 const QList<int> &roles);

    // View row showing `sourceRow`, or -1 when it is filtered out. m_accepted is
    // ascending by construction, so this is a binary search rather than the
    // reverse index a hash would cost -- auto-follow calls it once per position
    // tick, not per row.
    int viewRowFor(int sourceRow) const;

    QAbstractItemModel *m_source = nullptr;
    // m_source when it is a SubtitleLineModel, which is the only thing the app
    // ever attaches; rowAt needs its binary search over cue starts.
    SubtitleLineModel *m_lines = nullptr;
    QList<QMetaObject::Connection> m_sourceConnections;

    // Accepted source rows, ascending. The view row *is* the index into this.
    QVector<int> m_accepted;

    QString m_pattern;
    qint64 m_delayMs = 0;
    // m_pattern with U+00A0 folded to a space and case folded, so the per-row
    // scan folds the cue text only. Used when m_foldSpaces says the pattern
    // could span an ASS hard space; otherwise QString::contains() is faster.
    QString m_needle;
    bool m_foldSpaces = false;
};
