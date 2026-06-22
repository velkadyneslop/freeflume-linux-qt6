// FreeFlume — new-version checker.
//
// Asks GitHub's public releases API whether a newer FreeFlume exists. Fully
// anonymous (no auth, no token), like the rest of the app. The checker holds no
// policy: the caller decides whether/when to check, honoring the user's opt-out.
#pragma once

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // Queries the latest release. Emits updateAvailable when it is newer than
    // the running build, upToDate when it is not, or checkFailed on error.
    // force=true bypasses the once-per-interval throttle (for an explicit
    // "Check now"); a non-forced call is a no-op if checked recently.
    void check(bool force = false);

    // True if dotted version 'candidate' is strictly newer than 'current'.
    // Tolerates a leading 'v' and differing component counts.
    static bool isNewer(const QString& candidate, const QString& current);

signals:
    void updateAvailable(const QString& version, const QString& url);
    void upToDate();
    void checkFailed(const QString& message);

private:
    void handleReply(QNetworkReply* reply);

    QNetworkAccessManager* net_ = nullptr;
};
