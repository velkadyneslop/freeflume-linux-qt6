// FreeFlume — asynchronous thumbnail image loader implementation.
#include "thumbnailloader.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

ThumbnailLoader::ThumbnailLoader(QObject* parent)
    : QObject(parent), net_(new QNetworkAccessManager(this)) {}

void ThumbnailLoader::request(const QString& url) {
    if (url.isEmpty()) {
        return;
    }
    if (cache_.contains(url)) {
        const QPixmap pm = cache_.value(url);
        // Defer so callers can connect before the signal fires.
        QTimer::singleShot(0, this, [this, url, pm] { emit loaded(url, pm); });
        return;
    }
    if (inFlight_.value(url, false)) {
        return;
    }
    inFlight_[url] = true;

    // Channel avatars come back protocol-relative ("//yt3.ggpht.com/…"); add a
    // scheme so the request is valid. The cache/signal key stays the original.
    QString fetchUrl = url;
    if (fetchUrl.startsWith(QLatin1String("//"))) {
        fetchUrl.prepend(QStringLiteral("https:"));
    }

    QNetworkRequest req((QUrl(fetchUrl)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = net_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        inFlight_.remove(url);
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }
        QPixmap pm;
        if (pm.loadFromData(reply->readAll()) && !pm.isNull()) {
            cache_.insert(url, pm);
            emit loaded(url, pm);
        }
    });
}
