// FreeFlume — subscriptions page (channels + their recent videos).
#pragma once

#include <QHash>
#include <QWidget>

#include "extractor.h"

class Database;
class DownloadManager;
class Extractor;
class HoverRevealGroup;
class SubscriptionFeed;
class ThumbnailLoader;
class VideoList;
class QLabel;
class QListWidget;
class QTimer;

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
    void enqueueVisibleLive();  // check live status for on-screen channel avatars

    Database* db_;
    Extractor* extractor_;        // owned — dedicated to channel loading
    SubscriptionFeed* feed_ = nullptr;  // owned — What's New aggregator
    ThumbnailLoader* thumbs_ = nullptr;  // not owned — for channel avatars
    QListWidget* channels_ = nullptr;
    VideoList* videos_ = nullptr;
    HoverRevealGroup* hoverGroup_ = nullptr;  // unsubscribe buttons, shown on hover
    QHash<QString, QLabel*> avatarByChannel_;  // channel avatars, for the live ring
    QTimer* liveScrollTimer_ = nullptr;        // debounces live checks on scroll
};
