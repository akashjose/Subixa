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
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int sourceCount READ sourceCount NOTIFY countChanged)

public:
    explicit SubtitleFilterModel(QObject *parent = nullptr);

    QString pattern() const { return m_pattern; }
    void setPattern(const QString &pattern);

    int count() const { return rowCount(); }
    int sourceCount() const;

    // View row for the cue playing at positionMs, or -1 -- either because
    // playback is before the first cue, or because the current cue is filtered
    // out, which auto-follow treats the same way: nothing to highlight.
    Q_INVOKABLE int rowAt(qint64 positionMs) const;

    // Start of a view row in ms, or -1. Saves QML a round trip through the
    // delegate when seeking from something other than a click.
    Q_INVOKABLE qint64 startMsAt(int row) const;

signals:
    void patternChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_pattern;
};
