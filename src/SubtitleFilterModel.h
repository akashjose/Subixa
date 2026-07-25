#pragma once

#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

// Incremental search over one SubtitleLineModel, plus the source-to-view row
// mapping auto-follow needs.
//
// Set `sourceModel` (inherited) to switch tracks -- that is the whole cost of a
// tab switch now, since no rows are copied. The search text deliberately survives
// the switch: looking for the same word across tracks is the common case.
class SubtitleFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

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

    QString pattern() const { return m_pattern; }
    void setPattern(const QString &pattern);

    qint64 delayMs() const { return m_delayMs; }
    void setDelayMs(qint64 delayMs);

    int count() const { return rowCount(); }
    int sourceCount() const;

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
    void patternChanged();
    void countChanged();
    void delayMsChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_pattern;
    qint64 m_delayMs = 0;
    // m_pattern with U+00A0 folded to a space and case folded, so the per-row
    // scan folds the cue text only. Used when m_foldSpaces says the pattern
    // could span an ASS hard space; otherwise QString::contains() is faster.
    QString m_needle;
    bool m_foldSpaces = false;
};
