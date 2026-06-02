// FreeFlume — SponsorBlock client (crowdsourced skippable video segments).
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

// A skippable section of a video, in seconds.
struct SponsorSegment {
    double start = 0.0;
    double end = 0.0;
    QString category;  // sponsor, selfpromo, interaction, intro, outro, …
};

// Fetches skip segments from the SponsorBlock API. Uses the privacy-preserving
// hash-prefix endpoint: only the first 4 hex chars of the video id's SHA-256 are
// sent, and the exact video is matched locally from the returned set.
class SponsorBlock : public QObject {
    Q_OBJECT

public:
    explicit SponsorBlock(QObject* parent = nullptr);

    // Requests segments for `videoId`, limited to the given category keys.
    void fetch(const QString& videoId, const QStringList& categories);

signals:
    void segmentsReady(const QString& videoId, const QList<SponsorSegment>& segments);

private:
    // One HTTP attempt; retries transient failures until attemptsLeft hits 0.
    void requestOnce(const QString& url, const QString& videoId, int attemptsLeft);

    QNetworkAccessManager* net_;
};
