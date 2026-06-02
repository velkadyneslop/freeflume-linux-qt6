// FreeFlume — local persistence (SQLite via Qt Sql).
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include "extractor.h"

// A video's saved playback position (seconds) and whether it's been watched.
struct WatchProgress {
    qint64 position = 0;
    qint64 duration = 0;
    bool completed = false;
};

struct Subscription {
    qint64 id = -1;
    QString channelName;
    QString channelUrl;
    QString avatarUrl;
    QString channelId;  // YT UC… id (cached, for the RSS feed)
};

struct Playlist {
    qint64 id = -1;
    QString name;
    int itemCount = 0;
};

// Wraps a single SQLite database file holding watch history, subscriptions,
// and local playlists. All access is synchronous (queries are tiny).
class Database : public QObject {
    Q_OBJECT

public:
    explicit Database(QObject* parent = nullptr);
    bool open(const QString& path);

    // ---- History ----
    void addHistory(const SearchResult& item);
    QList<SearchResult> history(int limit = 200) const;
    void clearHistory();
    void removeHistoryItem(const QString& url);

    // ---- Watch progress (resume + thumbnail indicators) ----
    void setProgress(const QString& url, qint64 position, qint64 duration, bool completed,
                     bool notify = false);
    WatchProgress progress(const QString& url) const;
    QHash<QString, WatchProgress> allProgress() const;

    // ---- Search history ----
    void addSearch(const QString& query);
    QStringList searchHistory(int limit = 30) const;
    void clearSearchHistory();

    // ---- Subscriptions ----
    void subscribe(const QString& channelName, const QString& channelUrl,
                   const QString& avatarUrl = QString());
    void unsubscribe(const QString& channelUrl);
    bool isSubscribed(const QString& channelUrl) const;
    QList<Subscription> subscriptions() const;
    void setSubscriptionChannelId(const QString& channelUrl, const QString& channelId);

    // ---- Playlists ----
    qint64 createPlaylist(const QString& name);
    QList<Playlist> playlists() const;
    void addToPlaylist(qint64 playlistId, const SearchResult& item);
    void removeFromPlaylist(qint64 playlistId, const QString& url);
    // Persist a new manual order (urls in their new positions).
    void reorderPlaylist(qint64 playlistId, const QStringList& urlsInOrder);
    QList<SearchResult> playlistItems(qint64 playlistId) const;
    void deletePlaylist(qint64 playlistId);

signals:
    void historyChanged();
    void subscriptionsChanged();
    void playlistsChanged();
    void searchHistoryChanged();

private:
    void createSchema();

    QSqlDatabase db_;
};
