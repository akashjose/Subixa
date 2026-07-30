// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtQml/qqmlregistration.h>

// What plays after this file.
//
// Deliberately not a playlist *panel*: the browser is the feature this player
// exists for, and a second list competing with it would be the wrong thing to
// build. This is the smaller behaviour underneath one -- opening a file makes
// its folder the queue, so an episode is followed by the next episode, and
// dropping several files makes exactly those the queue.
//
// A QStringList rather than a QAbstractListModel: QML can use one as a model
// directly, nothing here needs per-row roles yet, and the whole point is that
// this stays small enough not to become a feature of its own.
class Playlist : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList files READ files NOTIFY filesChanged)
    Q_PROPERTY(int count READ count NOTIFY filesChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY currentChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY currentChanged)

public:
    explicit Playlist(QObject *parent = nullptr);

    QStringList files() const { return m_files; }
    int count() const { return m_files.size(); }
    int currentIndex() const { return m_currentIndex; }
    QString currentPath() const;
    bool hasNext() const { return m_currentIndex >= 0 && m_currentIndex + 1 < count(); }
    bool hasPrevious() const { return m_currentIndex > 0; }

    // Make `path`'s folder the queue, with `path` current. Files are ordered the
    // way a person would: "ep2" before "ep10", which plain string order gets
    // backwards.
    Q_INVOKABLE void openFolderOf(const QString &path);

    // Make exactly these files the queue -- a drop of several. Non-media files
    // are dropped, and the order given is kept rather than sorted: someone who
    // drops three files in a particular order meant that order.
    Q_INVOKABLE void setFiles(const QStringList &paths);

    // Turn what was dropped into things that can be played. A file passes
    // through untouched; a folder becomes the media inside it, sorted the way a
    // person sorts episodes. Folders keep the order they were given -- two
    // seasons dropped together play season one first -- but a folder's own
    // contents are sorted rather than left in whatever order the filesystem
    // hands them back, because nobody chose that order.
    //
    // Separate from setFiles so a single dropped folder can be recognised as
    // several files before the caller decides it is one file to open.
    Q_INVOKABLE static QStringList expand(const QStringList &paths);

    // Point at a file already in the queue. Does nothing if it is not there, so
    // callers can use contains() to decide whether a new queue is needed.
    Q_INVOKABLE void setCurrentPath(const QString &path);
    Q_INVOKABLE bool contains(const QString &path) const;

    // The next/previous path, or an empty string at either end. Advancing moves
    // the current index; asking at the end does not wrap, because a queue that
    // silently restarts is how you end up watching episode one twice.
    Q_INVOKABLE QString next();
    Q_INVOKABLE QString previous();

    Q_INVOKABLE void clear();

    // Whether a path looks like something this player can open. Extension-based
    // on purpose: the alternative is opening every file in a folder to find out,
    // which is exactly the cost this avoids.
    Q_INVOKABLE static bool isMediaFile(const QString &path);

    // The one list of extensions, shaped for FileDialog. Kept here so the dialog
    // and the folder scan cannot drift apart.
    Q_INVOKABLE static QStringList dialogNameFilters();

signals:
    void filesChanged();
    void currentChanged();

private:
    void replaceFiles(const QStringList &files, int index);
    void setCurrentIndex(int index);

    QStringList m_files;
    int m_currentIndex = -1;
};
