// FreeFlume — local persistence implementation.
#include "database.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
qint64 now() {
    return QDateTime::currentSecsSinceEpoch();
}

SearchResult rowToResult(const QSqlQuery& q) {
    SearchResult r;
    r.url = q.value(QStringLiteral("url")).toString();
    r.title = q.value(QStringLiteral("title")).toString();
    r.channel = q.value(QStringLiteral("channel")).toString();
    r.thumbnailUrl = q.value(QStringLiteral("thumbnail")).toString();
    r.durationSeconds = q.value(QStringLiteral("duration")).toLongLong();
    return r;
}
}  // namespace

Database::Database(QObject* parent) : QObject(parent) {}

bool Database::open(const QString& path) {
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                    QStringLiteral("freeflume"));
    db_.setDatabaseName(path);
    if (!db_.open()) {
        return false;
    }
    QSqlQuery(db_).exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    createSchema();
    return true;
}

void Database::createSchema() {
    QSqlQuery q(db_);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS history ("
        " url TEXT PRIMARY KEY, title TEXT, channel TEXT, thumbnail TEXT,"
        " duration INTEGER, watched_at INTEGER)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS subscriptions ("
        " channel_url TEXT PRIMARY KEY, channel_name TEXT, added_at INTEGER,"
        " avatar TEXT)"));
    // Migration for databases created before the avatar column existed.
    q.exec(QStringLiteral("ALTER TABLE subscriptions ADD COLUMN avatar TEXT"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlists ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, created_at INTEGER)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlist_items ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, playlist_id INTEGER,"
        " url TEXT, title TEXT, channel TEXT, thumbnail TEXT, duration INTEGER,"
        " added_at INTEGER,"
        " FOREIGN KEY(playlist_id) REFERENCES playlists(id) ON DELETE CASCADE)"));
    // Migration: a manual ordering column for drag-to-reorder (older databases
    // ordered by added_at). Seed existing rows from their insertion order.
    q.exec(QStringLiteral("ALTER TABLE playlist_items ADD COLUMN position INTEGER"));
    q.exec(QStringLiteral("UPDATE playlist_items SET position = id WHERE position IS NULL"));
    // Watch progress (resume position + thumbnail indicators).
    q.exec(QStringLiteral("ALTER TABLE history ADD COLUMN position INTEGER DEFAULT 0"));
    q.exec(QStringLiteral("ALTER TABLE history ADD COLUMN completed INTEGER DEFAULT 0"));
    // Search history.
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS search_history ("
                          " query TEXT PRIMARY KEY, searched_at INTEGER)"));
    // Cached YT channel id (UC…) for the What's New RSS feed.
    q.exec(QStringLiteral("ALTER TABLE subscriptions ADD COLUMN channel_id TEXT"));
    // Cached upload dates (YYYYMMDD) from lazy background enrichment of rows.
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS meta_cache ("
                          " url TEXT PRIMARY KEY, upload_date TEXT)"));
    q.exec(QStringLiteral("ALTER TABLE meta_cache ADD COLUMN is_short INTEGER"));
}

QString Database::cachedUploadDate(const QString& url) const {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT upload_date FROM meta_cache WHERE url = :u"));
    q.bindValue(QStringLiteral(":u"), url);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return {};
}

void Database::cacheUploadDate(const QString& url, const QString& date) {
    QSqlQuery q(db_);
    // Upsert only the date so a cached is_short flag isn't clobbered.
    q.prepare(QStringLiteral(
        "INSERT INTO meta_cache (url, upload_date) VALUES (:u, :d)"
        " ON CONFLICT(url) DO UPDATE SET upload_date = :d2"));
    q.bindValue(QStringLiteral(":u"), url);
    q.bindValue(QStringLiteral(":d"), date);
    q.bindValue(QStringLiteral(":d2"), date);
    q.exec();
}

int Database::cachedIsShort(const QString& url) const {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT is_short FROM meta_cache WHERE url = :u"));
    q.bindValue(QStringLiteral(":u"), url);
    if (q.exec() && q.next() && !q.value(0).isNull()) {
        return q.value(0).toInt() ? 1 : 0;
    }
    return -1;  // no row, or the flag was never set
}

void Database::cacheIsShort(const QString& url, bool isShort) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO meta_cache (url, is_short) VALUES (:u, :s)"
        " ON CONFLICT(url) DO UPDATE SET is_short = :s2"));
    q.bindValue(QStringLiteral(":u"), url);
    q.bindValue(QStringLiteral(":s"), isShort ? 1 : 0);
    q.bindValue(QStringLiteral(":s2"), isShort ? 1 : 0);
    q.exec();
}

// ---- History ---------------------------------------------------------------

void Database::addHistory(const SearchResult& item) {
    if (item.url.isEmpty()) {
        return;
    }
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO history (url, title, channel, thumbnail, duration, watched_at)"
        " VALUES (:url, :title, :channel, :thumb, :dur, :at)"
        " ON CONFLICT(url) DO UPDATE SET title=:title2, channel=:channel2,"
        " thumbnail=:thumb2, duration=:dur2, watched_at=:at2"));
    q.bindValue(QStringLiteral(":url"), item.url);
    q.bindValue(QStringLiteral(":title"), item.title);
    q.bindValue(QStringLiteral(":channel"), item.channel);
    q.bindValue(QStringLiteral(":thumb"), item.thumbnailUrl);
    q.bindValue(QStringLiteral(":dur"), item.durationSeconds);
    q.bindValue(QStringLiteral(":at"), now());
    q.bindValue(QStringLiteral(":title2"), item.title);
    q.bindValue(QStringLiteral(":channel2"), item.channel);
    q.bindValue(QStringLiteral(":thumb2"), item.thumbnailUrl);
    q.bindValue(QStringLiteral(":dur2"), item.durationSeconds);
    q.bindValue(QStringLiteral(":at2"), now());
    q.exec();
    emit historyChanged();
}

QList<SearchResult> Database::history(int limit) const {
    QList<SearchResult> out;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT url, title, channel, thumbnail, duration FROM history"
        " ORDER BY watched_at DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), limit);
    if (q.exec()) {
        while (q.next()) {
            out.push_back(rowToResult(q));
        }
    }
    return out;
}

void Database::clearHistory() {
    QSqlQuery(db_).exec(QStringLiteral("DELETE FROM history"));
    emit historyChanged();
}

void Database::removeHistoryItem(const QString& url) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM history WHERE url = :url"));
    q.bindValue(QStringLiteral(":url"), url);
    q.exec();
    emit historyChanged();
}

void Database::setProgress(const QString& url, qint64 position, qint64 duration,
                           bool completed, bool notify) {
    if (url.isEmpty()) {
        return;
    }
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "UPDATE history SET position = :pos, completed = :done,"
        " duration = CASE WHEN :dur > 0 THEN :dur2 ELSE duration END WHERE url = :url"));
    q.bindValue(QStringLiteral(":pos"), position);
    q.bindValue(QStringLiteral(":done"), completed ? 1 : 0);
    q.bindValue(QStringLiteral(":dur"), duration);
    q.bindValue(QStringLiteral(":dur2"), duration);
    q.bindValue(QStringLiteral(":url"), url);
    q.exec();
    if (notify) {  // mid-watch saves stay quiet to avoid constant list rebuilds
        emit historyChanged();
    }
}

WatchProgress Database::progress(const QString& url) const {
    WatchProgress p;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT position, duration, completed FROM history WHERE url = :url"));
    q.bindValue(QStringLiteral(":url"), url);
    if (q.exec() && q.next()) {
        p.position = q.value(0).toLongLong();
        p.duration = q.value(1).toLongLong();
        p.completed = q.value(2).toInt() != 0;
    }
    return p;
}

QHash<QString, WatchProgress> Database::allProgress() const {
    QHash<QString, WatchProgress> out;
    QSqlQuery q(db_);
    if (q.exec(QStringLiteral("SELECT url, position, duration, completed FROM history"
                              " WHERE position > 0 OR completed != 0"))) {
        while (q.next()) {
            WatchProgress p;
            p.position = q.value(1).toLongLong();
            p.duration = q.value(2).toLongLong();
            p.completed = q.value(3).toInt() != 0;
            out.insert(q.value(0).toString(), p);
        }
    }
    return out;
}

// ---- Search history --------------------------------------------------------

void Database::addSearch(const QString& query) {
    const QString q2 = query.trimmed();
    if (q2.isEmpty()) {
        return;
    }
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO search_history (query, searched_at) VALUES (:q, :at)"
        " ON CONFLICT(query) DO UPDATE SET searched_at = :at2"));
    q.bindValue(QStringLiteral(":q"), q2);
    q.bindValue(QStringLiteral(":at"), now());
    q.bindValue(QStringLiteral(":at2"), now());
    q.exec();
}

QStringList Database::searchHistory(int limit) const {
    QStringList out;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT query FROM search_history ORDER BY searched_at DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), limit);
    if (q.exec()) {
        while (q.next()) {
            out << q.value(0).toString();
        }
    }
    return out;
}

void Database::clearSearchHistory() {
    QSqlQuery(db_).exec(QStringLiteral("DELETE FROM search_history"));
    emit searchHistoryChanged();
}

// ---- Subscriptions ---------------------------------------------------------

void Database::subscribe(const QString& channelName, const QString& channelUrl,
                         const QString& avatarUrl) {
    if (channelUrl.isEmpty()) {
        return;
    }
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO subscriptions (channel_url, channel_name, added_at, avatar)"
        " VALUES (:url, :name, :at, :avatar)"
        " ON CONFLICT(channel_url) DO UPDATE SET channel_name=:name2,"
        " avatar=COALESCE(NULLIF(:avatar2,''), avatar)"));
    q.bindValue(QStringLiteral(":url"), channelUrl);
    q.bindValue(QStringLiteral(":name"), channelName);
    q.bindValue(QStringLiteral(":at"), now());
    q.bindValue(QStringLiteral(":avatar"), avatarUrl);
    q.bindValue(QStringLiteral(":name2"), channelName);
    q.bindValue(QStringLiteral(":avatar2"), avatarUrl);
    q.exec();
    emit subscriptionsChanged();
}

void Database::unsubscribe(const QString& channelUrl) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM subscriptions WHERE channel_url = :url"));
    q.bindValue(QStringLiteral(":url"), channelUrl);
    q.exec();
    emit subscriptionsChanged();
}

bool Database::isSubscribed(const QString& channelUrl) const {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM subscriptions WHERE channel_url = :url LIMIT 1"));
    q.bindValue(QStringLiteral(":url"), channelUrl);
    return q.exec() && q.next();
}

QList<Subscription> Database::subscriptions() const {
    QList<Subscription> out;
    QSqlQuery q(db_);
    if (q.exec(QStringLiteral(
            "SELECT channel_url, channel_name, avatar, channel_id FROM subscriptions"
            " ORDER BY channel_name COLLATE NOCASE"))) {
        while (q.next()) {
            Subscription s;
            s.channelUrl = q.value(0).toString();
            s.channelName = q.value(1).toString();
            s.avatarUrl = q.value(2).toString();
            s.channelId = q.value(3).toString();
            out.push_back(s);
        }
    }
    return out;
}

void Database::setSubscriptionChannelId(const QString& channelUrl, const QString& channelId) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "UPDATE subscriptions SET channel_id = :cid WHERE channel_url = :url"));
    q.bindValue(QStringLiteral(":cid"), channelId);
    q.bindValue(QStringLiteral(":url"), channelUrl);
    q.exec();
}

// ---- Playlists -------------------------------------------------------------

qint64 Database::createPlaylist(const QString& name) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO playlists (name, created_at) VALUES (:name, :at)"));
    q.bindValue(QStringLiteral(":name"), name);
    q.bindValue(QStringLiteral(":at"), now());
    if (!q.exec()) {
        return -1;
    }
    emit playlistsChanged();
    return q.lastInsertId().toLongLong();
}

QList<Playlist> Database::playlists() const {
    QList<Playlist> out;
    QSqlQuery q(db_);
    if (q.exec(QStringLiteral(
            "SELECT p.id, p.name, COUNT(i.id) FROM playlists p"
            " LEFT JOIN playlist_items i ON i.playlist_id = p.id"
            " GROUP BY p.id ORDER BY p.created_at"))) {
        while (q.next()) {
            Playlist p;
            p.id = q.value(0).toLongLong();
            p.name = q.value(1).toString();
            p.itemCount = q.value(2).toInt();
            out.push_back(p);
        }
    }
    return out;
}

void Database::addToPlaylist(qint64 playlistId, const SearchResult& item) {
    // New items go to the end of the manual order.
    qint64 nextPos = 0;
    {
        QSqlQuery m(db_);
        m.prepare(QStringLiteral("SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_items"
                                 " WHERE playlist_id = :pid"));
        m.bindValue(QStringLiteral(":pid"), playlistId);
        if (m.exec() && m.next()) {
            nextPos = m.value(0).toLongLong();
        }
    }
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO playlist_items"
        " (playlist_id, url, title, channel, thumbnail, duration, added_at, position)"
        " VALUES (:pid, :url, :title, :channel, :thumb, :dur, :at, :pos)"));
    q.bindValue(QStringLiteral(":pid"), playlistId);
    q.bindValue(QStringLiteral(":url"), item.url);
    q.bindValue(QStringLiteral(":title"), item.title);
    q.bindValue(QStringLiteral(":channel"), item.channel);
    q.bindValue(QStringLiteral(":thumb"), item.thumbnailUrl);
    q.bindValue(QStringLiteral(":dur"), item.durationSeconds);
    q.bindValue(QStringLiteral(":at"), now());
    q.bindValue(QStringLiteral(":pos"), nextPos);
    q.exec();
    emit playlistsChanged();
}

void Database::reorderPlaylist(qint64 playlistId, const QStringList& urlsInOrder) {
    db_.transaction();
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "UPDATE playlist_items SET position = :pos WHERE playlist_id = :pid AND url = :url"));
    for (int i = 0; i < urlsInOrder.size(); ++i) {
        q.bindValue(QStringLiteral(":pos"), i);
        q.bindValue(QStringLiteral(":pid"), playlistId);
        q.bindValue(QStringLiteral(":url"), urlsInOrder.at(i));
        q.exec();
    }
    db_.commit();
    emit playlistsChanged();
}

void Database::removeFromPlaylist(qint64 playlistId, const QString& url) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "DELETE FROM playlist_items WHERE playlist_id = :pid AND url = :url"));
    q.bindValue(QStringLiteral(":pid"), playlistId);
    q.bindValue(QStringLiteral(":url"), url);
    q.exec();
    emit playlistsChanged();
}

QList<SearchResult> Database::playlistItems(qint64 playlistId) const {
    QList<SearchResult> out;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT url, title, channel, thumbnail, duration FROM playlist_items"
        " WHERE playlist_id = :pid ORDER BY position ASC, id ASC"));
    q.bindValue(QStringLiteral(":pid"), playlistId);
    if (q.exec()) {
        while (q.next()) {
            out.push_back(rowToResult(q));
        }
    }
    return out;
}

void Database::deletePlaylist(qint64 playlistId) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM playlists WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), playlistId);
    q.exec();
    emit playlistsChanged();
}
