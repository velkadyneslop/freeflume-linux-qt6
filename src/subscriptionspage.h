// FreeFlume — subscriptions page (channels + their recent videos).
#pragma once

#include <QWidget>

#include "extractor.h"

class Database;
class DownloadManager;
class Extractor;
class HoverRevealGroup;
class SubscriptionFeed;
class ThumbnailLoader;
class VideoList;
class QListWidget;

class SubscriptionsPage : public QWidget {
    Q_OBJECT

public:
    SubscriptionsPage(Database* db, ThumbnailLoader* thumbs, QWidget* parent = nullptr);

    void refresh();
    void setDownloadManager(DownloadManager* m);

signals:
    void playRequested(const SearchResult& item);
    void channelRequested(const QString& channelUrl);  // open the full channel view

private:
    void loadCurrentChannel();
    void loadFeed();
    void exportSubscriptions();
    void importSubscriptions();

    Database* db_;
    Extractor* extractor_;        // owned — dedicated to channel loading
    SubscriptionFeed* feed_ = nullptr;  // owned — What's New aggregator
    ThumbnailLoader* thumbs_ = nullptr;  // not owned — for channel avatars
    QListWidget* channels_ = nullptr;
    VideoList* videos_ = nullptr;
    HoverRevealGroup* hoverGroup_ = nullptr;  // unsubscribe buttons, shown on hover
};
