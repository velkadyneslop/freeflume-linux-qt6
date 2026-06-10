// FreeFlume — lazy background metadata enrichment implementation.
#include "metaenricher.h"

#include <QDateTime>
#include <QProcess>

#include "database.h"

namespace {
constexpr qint64 kLiveTtl = 300;  // re-check a channel's live state at most every 5 min
}

MetaEnricher* MetaEnricher::instance() {
    static MetaEnricher* inst = new MetaEnricher();
    return inst;
}

MetaEnricher::MetaEnricher(QObject* parent) : QObject(parent) {}

void MetaEnricher::requestUploadDate(const QString& url) {
    if (url.isEmpty() || seenDates_.contains(url)) {
        return;
    }
    if (db_ && !db_->cachedUploadDate(url).isEmpty()) {
        seenDates_.insert(url);  // already have it
        return;
    }
    seenDates_.insert(url);
    queue_.enqueue({Kind::Date, url});
    startNext();
}

void MetaEnricher::requestLiveStatus(const QString& channelUrl) {
    if (channelUrl.isEmpty()) {
        return;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (liveCheckedAt_.value(channelUrl, 0) + kLiveTtl > now) {
        emit liveStatusReady(channelUrl, liveCache_.value(channelUrl, false));  // fresh
        return;
    }
    if (pendingLive_.contains(channelUrl)) {
        return;
    }
    pendingLive_.insert(channelUrl);
    queue_.enqueue({Kind::Live, channelUrl});
    startNext();
}

void MetaEnricher::startNext() {
    if (proc_ || queue_.isEmpty()) {
        return;  // one at a time
    }
    current_ = queue_.dequeue();
    proc_ = new QProcess(this);
    connect(proc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) { onFinished(code); });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            onFinished(-1);
        }
    });

    QStringList args{QStringLiteral("--no-warnings")};
    if (current_.kind == Kind::Date) {
        args << QStringLiteral("--no-playlist") << QStringLiteral("--print")
             << QStringLiteral("%(upload_date)s") << current_.key;
    } else {
        QString u = current_.key;  // a channel's /live resolves to its live stream
        while (u.endsWith(QLatin1Char('/'))) {
            u.chop(1);
        }
        args << QStringLiteral("--print") << QStringLiteral("%(live_status)s")
             << (u + QStringLiteral("/live"));
    }
    proc_->start(QStringLiteral("yt-dlp"), args);
}

void MetaEnricher::onFinished(int exitCode) {
    if (!proc_) {
        return;  // already handled (error + finished both fired)
    }
    const QString out =
        QString::fromUtf8(proc_->readAllStandardOutput()).trimmed().section('\n', 0, 0);
    proc_->deleteLater();
    proc_ = nullptr;

    if (current_.kind == Kind::Date) {
        // yt-dlp prints "NA" (or nothing) when a field is unknown.
        if (exitCode == 0 && out.size() == 8 && out != QLatin1String("NA")) {
            if (db_) {
                db_->cacheUploadDate(current_.key, out);
            }
            emit uploadDateReady(current_.key, out);
        }
    } else {
        const bool live = (exitCode == 0 && out == QLatin1String("is_live"));
        liveCheckedAt_[current_.key] = QDateTime::currentSecsSinceEpoch();
        liveCache_[current_.key] = live;
        pendingLive_.remove(current_.key);
        emit liveStatusReady(current_.key, live);
    }
    startNext();  // process the next queued request
}
