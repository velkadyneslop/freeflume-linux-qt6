// FreeFlume — new-version checker implementation.
#include "apppaths.h"
#include "updatecheck.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

#ifndef FREEFLUME_VERSION
#define FREEFLUME_VERSION "dev"  // set by CMake from PROJECT_VERSION
#endif

namespace {

// The GitHub repo releases are published to (see freeflume-github memory).
constexpr char kLatestApi[] =
    "https://api.github.com/repos/velkadyneslop/freeflume-linux-qt6/releases/latest";
constexpr char kReleasesPage[] =
    "https://github.com/velkadyneslop/freeflume-linux-qt6/releases/latest";

// Don't ping GitHub more than once per this window across launches.
constexpr qint64 kThrottleSecs = 20 * 60 * 60;  // 20 hours

// Split a dotted version into numeric components, ignoring a leading 'v' and
// anything non-numeric after the digits (e.g. "1.0.4-rc1" -> {1,0,4}).
QList<int> parseVersion(const QString& raw) {
    QString v = raw.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V'))) {
        v.remove(0, 1);
    }
    QList<int> out;
    for (const QString& part : v.split(QLatin1Char('.'))) {
        int i = 0;
        while (i < part.size() && part.at(i).isDigit()) {
            ++i;
        }
        out.append(part.left(i).toInt());
    }
    return out;
}

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {}

bool UpdateChecker::isNewer(const QString& candidate, const QString& current) {
    const QList<int> a = parseVersion(candidate);
    const QList<int> b = parseVersion(current);
    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const int x = i < a.size() ? a.at(i) : 0;
        const int y = i < b.size() ? b.at(i) : 0;
        if (x != y) {
            return x > y;
        }
    }
    return false;
}

void UpdateChecker::check(bool force) {
    QSettings s(apppaths::configFile(), QSettings::IniFormat);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!force) {
        const qint64 last = s.value(QStringLiteral("updates/lastCheck")).toLongLong();
        if (last > 0 && now - last < kThrottleSecs) {
            return;  // checked recently — stay quiet
        }
    }
    // Record the attempt up front so a failing check doesn't re-hammer on every
    // launch; an explicit forced check still always runs.
    s.setValue(QStringLiteral("updates/lastCheck"), now);

    if (!net_) {
        net_ = new QNetworkAccessManager(this);
    }
    QNetworkRequest req((QUrl(QString::fromLatin1(kLatestApi))));
    // GitHub requires a User-Agent; Accept pins the stable API media type.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("FreeFlume/%1").arg(QStringLiteral(FREEFLUME_VERSION)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply* reply = net_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
}

void UpdateChecker::handleReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(reply->errorString());
        return;
    }
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = obj.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        emit checkFailed(QStringLiteral("No release tag in response."));
        return;
    }
    QString url = obj.value(QStringLiteral("html_url")).toString();
    if (url.isEmpty()) {
        url = QString::fromLatin1(kReleasesPage);
    }
    if (isNewer(tag, QStringLiteral(FREEFLUME_VERSION))) {
        // Normalize the displayed version without a leading 'v'.
        QString version = tag;
        if (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V'))) {
            version.remove(0, 1);
        }
        emit updateAvailable(version, url);
    } else {
        emit upToDate();
    }
}
