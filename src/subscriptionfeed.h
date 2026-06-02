// FreeFlume — "What's New": aggregates recent uploads across subscriptions.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "database.h"
#include "extractor.h"

class QNetworkAccessManager;

// Fetches each subscribed channel's YT RSS feed (resolving + caching the
// UC… id as needed), merges every video, and emits them newest-first.
class SubscriptionFeed : public QObject {
    Q_OBJECT

public:
    explicit SubscriptionFeed(Database* db, QObject* parent = nullptr);

    void refresh(const QList<Subscription>& subs);

signals:
    void ready(const QList<SearchResult>& items);  // merged, newest first

private:
    void resolveChannelId(const Subscription& sub);
    void fetchFeed(const Subscription& sub, const QString& channelId);
    void finishOne();

    Database* db_;
    QNetworkAccessManager* net_;
    QList<SearchResult> items_;
    int pending_ = 0;
    quint64 generation_ = 0;  // drops replies from a superseded refresh
};
