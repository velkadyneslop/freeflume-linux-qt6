// FreeFlume — SponsorBlock client implementation.
#include "sponsorblock.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

SponsorBlock::SponsorBlock(QObject* parent)
    : QObject(parent), net_(new QNetworkAccessManager(this)) {}

void SponsorBlock::fetch(const QString& videoId, const QStringList& categories) {
    if (videoId.isEmpty() || categories.isEmpty()) {
        emit segmentsReady(videoId, {});
        return;
    }

    // Hash-prefix endpoint: send only the first 4 hex chars of SHA-256(videoId).
    const QByteArray hash =
        QCryptographicHash::hash(videoId.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString prefix = QString::fromLatin1(hash.left(4));

    QJsonArray cats;
    for (const QString& c : categories) {
        cats.append(c);
    }
    const QString catsJson =
        QString::fromUtf8(QJsonDocument(cats).toJson(QJsonDocument::Compact));

    QUrl url(QStringLiteral("https://sponsor.ajay.app/api/skipSegments/%1").arg(prefix));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("categories"), catsJson);
    query.addQueryItem(QStringLiteral("actionTypes"), QStringLiteral("[\"skip\"]"));
    url.setQuery(query);

    requestOnce(QString::fromUtf8(url.toEncoded()), videoId, /*attemptsLeft=*/3);
}

void SponsorBlock::requestOnce(const QString& url, const QString& videoId, int attemptsLeft) {
    QNetworkRequest req((QUrl(url)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FreeFlume/1.0"));

    QNetworkReply* reply = net_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, videoId, attemptsLeft] {
        reply->deleteLater();
        // The API is often briefly overloaded (503/timeout). Retry transient
        // failures with a short backoff before giving up.
        if (reply->error() != QNetworkReply::NoError) {
            if (attemptsLeft > 1) {
                QTimer::singleShot(700, this, [this, url, videoId, attemptsLeft] {
                    requestOnce(url, videoId, attemptsLeft - 1);
                });
            } else {
                emit segmentsReady(videoId, {});
            }
            return;
        }
        QList<SponsorSegment> segments;
        {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            // The endpoint returns every video sharing the hash prefix; keep ours.
            for (const QJsonValue& entry : doc.array()) {
                const QJsonObject obj = entry.toObject();
                if (obj.value(QStringLiteral("videoID")).toString() != videoId) {
                    continue;
                }
                for (const QJsonValue& sv : obj.value(QStringLiteral("segments")).toArray()) {
                    const QJsonObject so = sv.toObject();
                    if (so.value(QStringLiteral("actionType")).toString() !=
                        QLatin1String("skip")) {
                        continue;
                    }
                    const QJsonArray range = so.value(QStringLiteral("segment")).toArray();
                    if (range.size() != 2) {
                        continue;
                    }
                    SponsorSegment seg;
                    seg.start = range.at(0).toDouble();
                    seg.end = range.at(1).toDouble();
                    seg.category = so.value(QStringLiteral("category")).toString();
                    if (seg.end > seg.start) {
                        segments.push_back(seg);
                    }
                }
            }
        }
        emit segmentsReady(videoId, segments);
    });
}
