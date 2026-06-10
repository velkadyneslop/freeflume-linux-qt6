// FreeFlume — lazy background metadata enrichment.
//
// The fast (flat) yt-dlp listings used for result rows carry no upload date, and
// give no live-status for channels, so we fetch those on demand — only for rows
// the user actually sees, one yt-dlp at a time, and cache the results. A single
// shared instance serializes every request so we never burst the network (which
// trips YT's rate limiting). Upload dates are cached in the DB (immutable); live
// status is cached in memory with a short TTL (it changes).
#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>

class Database;
class QProcess;

class MetaEnricher : public QObject {
    Q_OBJECT

public:
    static MetaEnricher* instance();
    void setDatabase(Database* db) { db_ = db; }

    // Enqueue a background fetch of a video's upload date (no-op if cached/queued).
    void requestUploadDate(const QString& url);
    // Resolve whether a channel is currently live. Re-emits a fresh cached result
    // immediately; otherwise enqueues a check.
    void requestLiveStatus(const QString& channelUrl);

signals:
    void uploadDateReady(const QString& url, const QString& date);   // YYYYMMDD
    void liveStatusReady(const QString& channelUrl, bool live);

private:
    explicit MetaEnricher(QObject* parent = nullptr);
    enum class Kind { Date, Live };
    struct Job {
        Kind kind;
        QString key;
    };
    void startNext();
    void onFinished(int exitCode);

    Database* db_ = nullptr;
    QProcess* proc_ = nullptr;
    Job current_{Kind::Date, {}};
    QQueue<Job> queue_;
    QSet<QString> seenDates_;                 // queued/fetched dates (dedupe)
    QSet<QString> pendingLive_;               // in-flight live checks (dedupe)
    QHash<QString, qint64> liveCheckedAt_;    // channelUrl -> unix secs of last check
    QHash<QString, bool> liveCache_;          // channelUrl -> last-known live state
};
