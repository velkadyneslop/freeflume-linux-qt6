// FreeFlume — asynchronous thumbnail image loader.
#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>

class QNetworkAccessManager;

// Fetches thumbnail images over the network and caches them in memory.
// Emits loaded() with the source URL so callers can match the result to the
// widget that requested it.
class ThumbnailLoader : public QObject {
    Q_OBJECT

public:
    explicit ThumbnailLoader(QObject* parent = nullptr);

    // Requests an image. If already cached, loaded() is emitted synchronously
    // on the next event-loop turn; otherwise it is fetched.
    void request(const QString& url);

    // The cached pixmap for a URL, or a null pixmap if not loaded yet.
    QPixmap cached(const QString& url) const { return cache_.value(url); }

signals:
    void loaded(const QString& url, const QPixmap& pixmap);

private:
    QNetworkAccessManager* net_;
    QHash<QString, QPixmap> cache_;
    QHash<QString, bool> inFlight_;
};
