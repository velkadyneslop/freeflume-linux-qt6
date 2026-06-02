// FreeFlume — local playlists page.
#pragma once

#include <QWidget>

#include "extractor.h"

class Database;
class DownloadManager;
class ThumbnailLoader;
class VideoList;
class QListWidget;

class PlaylistsPage : public QWidget {
    Q_OBJECT

public:
    PlaylistsPage(Database* db, ThumbnailLoader* thumbs, QWidget* parent = nullptr);

    void refresh();
    void setDownloadManager(DownloadManager* m);

signals:
    void playRequested(const SearchResult& item);
    // Play the whole playlist as a queue, starting at the chosen item.
    void playQueueRequested(const QList<SearchResult>& items, int index,
                            const QString& title);

private:
    void loadCurrentPlaylist();
    qint64 currentPlaylistId() const;

    Database* db_;
    QListWidget* playlists_ = nullptr;
    VideoList* items_ = nullptr;
    QList<SearchResult> currentItems_;  // videos of the selected playlist
    QString currentName_;               // its name (for the Up Next header)
};
