// FreeFlume — watch history page.
#pragma once

#include <QWidget>

#include "extractor.h"

class Database;
class DownloadManager;
class ThumbnailLoader;
class VideoList;

class HistoryPage : public QWidget {
    Q_OBJECT

public:
    HistoryPage(Database* db, ThumbnailLoader* thumbs, QWidget* parent = nullptr);

    void refresh();
    void setDownloadManager(DownloadManager* m);

signals:
    void playRequested(const SearchResult& item);

private:
    Database* db_;
    VideoList* list_ = nullptr;
};
