// FreeFlume — subscription feed implementation.
#include "subscriptionfeed.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

namespace {
const QByteArray kUserAgent = "Mozilla/5.0 (compatible; FreeFlume)";

QString channelIdFromUrl(const QString& url) {
    static const QRegularExpression re(QStringLiteral("/channel/(UC[A-Za-z0-9_-]{22})"));
    const auto m = re.match(url);
    return m.hasMatch() ? m.captured(1) : QString();
}

QNetworkRequest request(const QUrl& url) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    return req;
}
}  // namespace

SubscriptionFeed::SubscriptionFeed(Database* db, QObject* parent)
    : QObject(parent), db_(db), net_(new QNetworkAccessManager(this)) {}

void SubscriptionFeed::refresh(const QList<Subscription>& subs) {
    ++generation_;
    items_.clear();
    pending_ = subs.size();
    if (subs.isEmpty()) {
        emit ready({});
        return;
    }
    for (const Subscription& s : subs) {
        QString cid = s.channelId.isEmpty() ? channelIdFromUrl(s.channelUrl) : s.channelId;
        if (!cid.isEmpty()) {
            if (s.channelId.isEmpty()) {
                db_->setSubscriptionChannelId(s.channelUrl, cid);
            }
            fetchFeed(s, cid);
        } else {
            resolveChannelId(s);
        }
    }
}

void SubscriptionFeed::resolveChannelId(const Subscription& sub) {
    const quint64 gen = generation_;
    QNetworkReply* reply = net_->get(request(QUrl(sub.channelUrl)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, sub, gen] {
        reply->deleteLater();
        if (gen != generation_) {
            return;  // a newer refresh started
        }
        QString cid;
        if (reply->error() == QNetworkReply::NoError) {
            static const QRegularExpression re(
                QStringLiteral("\"(?:channelId|externalId)\":\"(UC[A-Za-z0-9_-]{22})\""));
            const auto m = re.match(QString::fromUtf8(reply->readAll()));
            if (m.hasMatch()) {
                cid = m.captured(1);
            }
        }
        if (cid.isEmpty()) {
            finishOne();  // couldn't resolve — skip this channel
            return;
        }
        db_->setSubscriptionChannelId(sub.channelUrl, cid);
        fetchFeed(sub, cid);
    });
}

void SubscriptionFeed::fetchFeed(const Subscription& sub, const QString& channelId) {
    const quint64 gen = generation_;
    const QUrl url(QStringLiteral("https://www.youtube.com/feeds/videos.xml?channel_id=%1")
                       .arg(channelId));
    QNetworkReply* reply = net_->get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, sub, gen] {
        reply->deleteLater();
        if (gen != generation_) {
            return;
        }
        if (reply->error() == QNetworkReply::NoError) {
            QXmlStreamReader xml(reply->readAll());
            bool inEntry = false;
            QString vid, title, pub, thumb;
            while (!xml.atEnd() && !xml.hasError()) {
                const auto tok = xml.readNext();
                if (tok == QXmlStreamReader::StartElement) {
                    const QStringView n = xml.name();
                    if (n == u"entry") {
                        inEntry = true;
                        vid.clear();
                        title.clear();
                        pub.clear();
                        thumb.clear();
                    } else if (inEntry && n == u"videoId") {
                        vid = xml.readElementText();
                    } else if (inEntry && n == u"title") {
                        title = xml.readElementText();
                    } else if (inEntry && n == u"published") {
                        pub = xml.readElementText();
                    } else if (inEntry && n == u"thumbnail") {
                        thumb = xml.attributes().value(QStringLiteral("url")).toString();
                    }
                } else if (tok == QXmlStreamReader::EndElement && xml.name() == u"entry") {
                    inEntry = false;
                    if (!vid.isEmpty()) {
                        SearchResult r;
                        r.id = vid;
                        r.url = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(vid);
                        r.title = title;
                        r.channel = sub.channelName;
                        r.thumbnailUrl =
                            thumb.isEmpty()
                                ? QStringLiteral("https://i.ytimg.com/vi/%1/hqdefault.jpg").arg(vid)
                                : thumb;
                        r.kind = ResultKind::Video;
                        r.published = QDateTime::fromString(pub, Qt::ISODate).toSecsSinceEpoch();
                        items_.push_back(r);
                    }
                }
            }
        }
        finishOne();
    });
}

void SubscriptionFeed::finishOne() {
    if (--pending_ > 0) {
        return;
    }
    std::sort(items_.begin(), items_.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.published > b.published;  // newest first
              });
    classifyShorts();
}

void SubscriptionFeed::classifyShorts() {
    // The RSS feed mixes Shorts into the uploads with no marker. YouTube serves
    // /shorts/<id> with 200 for a real Short and redirects (303) to /watch for a
    // normal video — so a cheap HEAD classifies it. Results are cached in the DB
    // (a video's Shorts-ness never changes), so only brand-new items are checked.
    const quint64 gen = generation_;
    classifyPending_ = 0;
    for (const SearchResult& r : items_) {
        if (db_->cachedIsShort(r.url) != -1) {
            continue;  // already known
        }
        ++classifyPending_;
        QNetworkRequest req = request(
            QUrl(QStringLiteral("https://www.youtube.com/shorts/%1").arg(r.id)));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);  // don't follow → keep the code
        req.setTransferTimeout(15000);
        const QString url = r.url;
        QNetworkReply* reply = net_->head(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url, gen] {
            reply->deleteLater();
            if (gen != generation_) {
                return;  // a newer refresh superseded this
            }
            const int code =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code == 200) {
                db_->cacheIsShort(url, true);
            } else if (code >= 300 && code < 400) {
                db_->cacheIsShort(url, false);
            }  // else transient error: leave unknown so it's retried next refresh
            if (--classifyPending_ == 0) {
                emitFiltered();
            }
        });
    }
    if (classifyPending_ == 0) {
        emitFiltered();
    }
}

void SubscriptionFeed::emitFiltered() {
    QList<SearchResult> out;
    for (const SearchResult& r : items_) {
        if (db_->cachedIsShort(r.url) != 1) {  // drop confirmed Shorts; keep unknowns
            out.push_back(r);
        }
    }
    emit ready(out);
}
