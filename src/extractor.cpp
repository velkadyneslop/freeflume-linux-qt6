// FreeFlume — extraction backend implementation.
#include "extractor.h"

#include <climits>
#include <cstdlib>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace {

// Picks the largest thumbnail from yt-dlp's "thumbnails" array.
QString bestThumbnail(const QJsonObject& obj) {
    const QJsonArray thumbs = obj.value(QStringLiteral("thumbnails")).toArray();
    QString best;
    int bestWidth = -1;
    for (const QJsonValue& v : thumbs) {
        const QJsonObject t = v.toObject();
        const int w = t.value(QStringLiteral("width")).toInt(0);
        const QString url = t.value(QStringLiteral("url")).toString();
        if (!url.isEmpty() && w >= bestWidth) {
            bestWidth = w;
            best = url;
        }
    }
    // Fall back to a plain "thumbnail" field if the array was empty.
    if (best.isEmpty()) {
        best = obj.value(QStringLiteral("thumbnail")).toString();
    }
    return best;
}

// Picks the storyboard level with tile width nearest 160 px (a good preview
// size) from the video's format list.
Storyboard parseStoryboard(const QJsonObject& obj) {
    Storyboard best;
    int bestDist = INT_MAX;
    for (const QJsonValue& v : obj.value(QStringLiteral("formats")).toArray()) {
        const QJsonObject f = v.toObject();
        const bool isSb = f.value(QStringLiteral("format_note")).toString() ==
                              QLatin1String("storyboard") ||
                          f.value(QStringLiteral("format_id")).toString().startsWith(
                              QLatin1String("sb"));
        if (!isSb) {
            continue;
        }
        const int w = f.value(QStringLiteral("width")).toInt();
        const int h = f.value(QStringLiteral("height")).toInt();
        const int cols = f.value(QStringLiteral("columns")).toInt();
        const int rows = f.value(QStringLiteral("rows")).toInt();
        const QJsonArray frags = f.value(QStringLiteral("fragments")).toArray();
        if (w <= 0 || h <= 0 || cols <= 0 || rows <= 0 || frags.isEmpty()) {
            continue;
        }
        const int dist = std::abs(w - 160);
        if (dist >= bestDist) {
            continue;
        }
        bestDist = dist;
        best = Storyboard{};
        best.tileWidth = w;
        best.tileHeight = h;
        best.rows = rows;
        best.columns = cols;
        for (const QJsonValue& fv : frags) {
            const QJsonObject fo = fv.toObject();
            best.fragments.push_back(
                {fo.value(QStringLiteral("url")).toString(),
                 fo.value(QStringLiteral("duration")).toDouble()});
        }
    }
    return best;
}

VideoDetails parseDetails(const QJsonObject& obj) {
    VideoDetails d;
    d.url = obj.value(QStringLiteral("webpage_url")).toString();
    d.title = obj.value(QStringLiteral("title")).toString();
    d.channel = obj.value(QStringLiteral("channel")).toString();
    if (d.channel.isEmpty()) {
        d.channel = obj.value(QStringLiteral("uploader")).toString();
    }
    d.channelUrl = obj.value(QStringLiteral("channel_url")).toString();
    if (d.channelUrl.isEmpty()) {
        d.channelUrl = obj.value(QStringLiteral("uploader_url")).toString();
    }
    d.description = obj.value(QStringLiteral("description")).toString();
    d.uploadDate = obj.value(QStringLiteral("upload_date")).toString();
    d.durationSeconds =
        static_cast<qint64>(obj.value(QStringLiteral("duration")).toDouble(0.0));
    if (!obj.value(QStringLiteral("view_count")).isNull()) {
        d.viewCount =
            static_cast<qint64>(obj.value(QStringLiteral("view_count")).toDouble(-1.0));
    }
    if (!obj.value(QStringLiteral("like_count")).isNull()) {
        d.likeCount =
            static_cast<qint64>(obj.value(QStringLiteral("like_count")).toDouble(-1.0));
    }
    d.thumbnailUrl = bestThumbnail(obj);
    d.storyboard = parseStoryboard(obj);
    for (const QJsonValue& cv : obj.value(QStringLiteral("chapters")).toArray()) {
        const QJsonObject co = cv.toObject();
        d.chapters.push_back({co.value(QStringLiteral("start_time")).toDouble(),
                              co.value(QStringLiteral("title")).toString()});
    }
    return d;
}

// Strips a tab/search suffix back to the bare channel URL.
QString channelBaseUrl(const QString& url) {
    QString u = url;
    const int s = u.indexOf(QLatin1String("/search?"));
    if (s > 0) {
        u = u.left(s);
    }
    for (const char* tab : {"/videos", "/shorts", "/streams", "/playlists",
                            "/featured", "/community"}) {
        if (u.endsWith(QLatin1String(tab))) {
            u.chop(qstrlen(tab));
            break;
        }
    }
    while (u.endsWith(QLatin1Char('/'))) {
        u.chop(1);
    }
    return u;
}

// A bare channel URL is a collection of tabs (Videos/Shorts/…), which breaks
// --playlist-start/--playlist-end pagination. Target its /videos tab instead.
QString channelToVideosTab(const QString& url) {
    if (url.contains(QLatin1String("list=")) || url.contains(QLatin1String("/playlist"))) {
        return url;  // a playlist paginates fine as-is
    }
    for (const char* tab : {"/videos", "/shorts", "/streams", "/playlists",
                            "/featured", "/search", "/community"}) {
        if (url.contains(QLatin1String(tab))) {
            return url;  // already a specific tab
        }
    }
    if (url.contains(QLatin1String("/channel/")) || url.contains(QLatin1String("/@")) ||
        url.contains(QLatin1String("/c/")) || url.contains(QLatin1String("/user/"))) {
        QString u = url;
        while (u.endsWith(QLatin1Char('/'))) {
            u.chop(1);
        }
        return u + QStringLiteral("/videos");
    }
    return url;
}

ResultKind detectKind(const QString& ieKey, const QString& url) {
    // YoutubeTab entries are channels or playlists; everything else is a video.
    if (ieKey == QLatin1String("YoutubeTab") ||
        url.contains(QLatin1String("/playlist")) ||
        url.contains(QLatin1String("/channel/")) ||
        url.contains(QLatin1String("/@")) ||
        url.contains(QLatin1String("/c/")) ||
        url.contains(QLatin1String("/user/"))) {
        if (url.contains(QLatin1String("list=")) || url.contains(QLatin1String("/playlist"))) {
            return ResultKind::Playlist;
        }
        return ResultKind::Channel;
    }
    if (url.contains(QLatin1String("/shorts/"))) {
        return ResultKind::Short;
    }
    return ResultKind::Video;
}

SearchResult parseEntry(const QJsonObject& obj) {
    SearchResult r;
    r.id = obj.value(QStringLiteral("id")).toString();
    r.url = obj.value(QStringLiteral("url")).toString();
    if (r.url.isEmpty()) {
        r.url = obj.value(QStringLiteral("webpage_url")).toString();
    }
    r.kind = detectKind(obj.value(QStringLiteral("ie_key")).toString(), r.url);
    r.title = obj.value(QStringLiteral("title")).toString();
    // Channel name can live under several keys depending on the extractor.
    r.channel = obj.value(QStringLiteral("channel")).toString();
    if (r.channel.isEmpty()) {
        r.channel = obj.value(QStringLiteral("uploader")).toString();
    }
    r.durationSeconds = static_cast<qint64>(
        obj.value(QStringLiteral("duration")).toDouble(0.0));
    if (obj.contains(QStringLiteral("view_count")) &&
        !obj.value(QStringLiteral("view_count")).isNull()) {
        r.viewCount = static_cast<qint64>(
            obj.value(QStringLiteral("view_count")).toDouble(-1.0));
    }
    const QString liveStatus = obj.value(QStringLiteral("live_status")).toString();
    r.isLive = (liveStatus == QStringLiteral("is_live"));
    r.thumbnailUrl = bestThumbnail(obj);
    return r;
}

}  // namespace

Extractor::Extractor(QObject* parent) : QObject(parent) {}

bool Extractor::busy() const {
    return proc_ && proc_->state() != QProcess::NotRunning;
}

void Extractor::cancel() {
    if (proc_) {
        proc_->disconnect(this);
        proc_->kill();
        proc_->deleteLater();
        proc_ = nullptr;
    }
}

namespace {
// Encodes the filters into YT's sp= protobuf (base64). Layout:
//   field 1 (varint)        = sort
//   field 2 (sub-message)   = { 1: uploadDate, 2: type, 3: duration }
QByteArray buildSpParam(const SearchFilters& f) {
    auto addVarint = [](QByteArray& b, char tag, int value) {
        b.append(tag);
        b.append(static_cast<char>(value));  // all values < 128 → single byte
    };
    QByteArray inner;
    if (f.uploadDate) addVarint(inner, 0x08, f.uploadDate);  // sub field 1
    if (f.type) addVarint(inner, 0x10, f.type);              // sub field 2
    if (f.duration) addVarint(inner, 0x18, f.duration);      // sub field 3
    if (f.hd) addVarint(inner, 0x20, 1);                     // field 4: HD
    if (f.subtitles) addVarint(inner, 0x28, 1);              // field 5: subtitles/CC
    if (f.live) addVarint(inner, 0x40, 1);                   // field 8: live
    if (f.fourK) addVarint(inner, 0x70, 1);                  // field 14: 4K

    QByteArray msg;
    if (f.sort) addVarint(msg, 0x08, f.sort);  // top-level field 1
    if (!inner.isEmpty()) {
        msg.append('\x12');                              // field 2, length-delimited
        msg.append(static_cast<char>(inner.size()));
        msg.append(inner);
    }
    return msg.toBase64();
}
}  // namespace

void Extractor::search(const QString& query, int page, int pageSize, bool richResults,
                       const SearchFilters& filters) {
    const int start = (page - 1) * pageSize + 1;
    const int end = page * pageSize;
    // Filters only work on the full results page (ytsearch ignores sp=).
    if (richResults || filters.any()) {
        QString url = QStringLiteral("https://www.youtube.com/results?search_query=%1")
                          .arg(QString::fromUtf8(QUrl::toPercentEncoding(query)));
        if (filters.any()) {
            url += QStringLiteral("&sp=%1").arg(
                QString::fromUtf8(QUrl::toPercentEncoding(QString::fromLatin1(buildSpParam(filters)))));
        }
        runFlat(url, query, start, end);
    } else {
        // ytsearchN fetches N results; slice the page from those.
        runFlat(QStringLiteral("ytsearch%1:%2").arg(end).arg(query), query, start, end);
    }
}

void Extractor::fetchChannel(const QString& channelUrl, int page, int pageSize) {
    const int start = (page - 1) * pageSize + 1;
    const int end = page * pageSize;
    runFlat(channelToVideosTab(channelUrl), channelUrl, start, end);
}

void Extractor::searchInChannel(const QString& channelUrl, const QString& query, int page,
                                int pageSize) {
    const int start = (page - 1) * pageSize + 1;
    const int end = page * pageSize;
    const QString url = QStringLiteral("%1/search?query=%2")
                            .arg(channelBaseUrl(channelUrl),
                                 QString::fromUtf8(QUrl::toPercentEncoding(query)));
    runFlat(url, channelUrl, start, end);
}

void Extractor::runFlat(const QString& target, const QString& label, int start, int end) {
    cancel();
    query_ = label;

    proc_ = new QProcess(this);
    connect(proc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &Extractor::handleFinished);
    connect(proc_, &QProcess::errorOccurred, this, &Extractor::handleError);

    const QStringList args = {
        QStringLiteral("--flat-playlist"),
        QStringLiteral("--dump-json"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--ignore-errors"),
        QStringLiteral("--playlist-start"),
        QString::number(start),
        QStringLiteral("--playlist-end"),
        QString::number(end),
        target,
    };

    emit searchStarted(label);
    proc_->start(QStringLiteral("yt-dlp"), args);
}

void Extractor::fetchDetails(const QString& url) {
    if (detailsProc_) {
        detailsProc_->disconnect(this);
        detailsProc_->kill();
        detailsProc_->deleteLater();
    }
    detailsUrl_ = url;
    detailsProc_ = new QProcess(this);
    connect(detailsProc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &Extractor::handleDetailsFinished);

    const QStringList args = {
        QStringLiteral("--dump-single-json"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--no-playlist"),
        // Some videos are unavailable to the default client but work via android.
        QStringLiteral("--extractor-args"),
        QStringLiteral("youtube:player_client=default,android"),
        url,
    };
    emit detailsStarted(url);
    detailsProc_->start(QStringLiteral("yt-dlp"), args);
}

void Extractor::handleDetailsFinished(int exitCode, QProcess::ExitStatus status) {
    if (!detailsProc_) {
        return;
    }
    const QByteArray out = detailsProc_->readAllStandardOutput();
    detailsProc_->deleteLater();
    detailsProc_ = nullptr;

    if (status == QProcess::CrashExit || exitCode != 0) {
        emit detailsFailed(QStringLiteral("Could not load video details."));
        return;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit detailsFailed(QStringLiteral("Could not parse video details."));
        return;
    }
    emit detailsFinished(parseDetails(doc.object()));
}

void Extractor::handleError(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        emit searchFailed(QStringLiteral(
            "Could not launch yt-dlp. Is it installed and on your PATH?"));
    }
}

void Extractor::fetchPlaylistItems(const QString& url) {
    if (playlistProc_) {
        playlistProc_->disconnect(this);
        playlistProc_->kill();
        playlistProc_->deleteLater();
    }
    playlistUrl_ = url;
    playlistProc_ = new QProcess(this);
    connect(playlistProc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &Extractor::handlePlaylistFinished);

    // No --playlist-start/--playlist-end: fetch the whole thing.
    const QStringList args = {
        QStringLiteral("--flat-playlist"),
        QStringLiteral("--dump-json"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--ignore-errors"),
        channelToVideosTab(url),
    };
    playlistProc_->start(QStringLiteral("yt-dlp"), args);
}

void Extractor::handlePlaylistFinished(int exitCode, QProcess::ExitStatus status) {
    if (!playlistProc_) {
        return;
    }
    const QByteArray out = playlistProc_->readAllStandardOutput();
    const QString url = playlistUrl_;
    playlistProc_->deleteLater();
    playlistProc_ = nullptr;

    if (status == QProcess::CrashExit) {
        return;  // best-effort; the page's current-page queue still works
    }
    Q_UNUSED(exitCode);

    QList<SearchResult> results;
    for (const QByteArray& line : out.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        SearchResult r = parseEntry(doc.object());
        if (!r.title.isEmpty() && !r.url.isEmpty() &&
            (r.kind == ResultKind::Video || r.kind == ResultKind::Short)) {
            results.push_back(std::move(r));
        }
    }

    if (!results.isEmpty()) {
        emit playlistItemsReady(results, url);
    }
}

void Extractor::handleFinished(int exitCode, QProcess::ExitStatus status) {
    if (!proc_) {
        return;
    }
    const QByteArray out = proc_->readAllStandardOutput();
    const QByteArray err = proc_->readAllStandardError();
    proc_->deleteLater();
    proc_ = nullptr;

    if (status == QProcess::CrashExit) {
        emit searchFailed(QStringLiteral("yt-dlp crashed during the search."));
        return;
    }

    QList<SearchResult> results;
    int total = 0;  // playlist length, when reported
    // yt-dlp emits one JSON object per line (NDJSON) with --dump-json.
    for (const QByteArray& line : out.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        const QJsonObject obj = doc.object();
        const int pc = obj.value(QStringLiteral("playlist_count")).toInt(0);
        if (pc > total) {
            total = pc;
        }
        SearchResult r = parseEntry(obj);
        if (!r.title.isEmpty() && !r.url.isEmpty()) {
            results.push_back(std::move(r));
        }
    }

    if (results.isEmpty()) {
        if (exitCode != 0) {
            QString msg = QString::fromUtf8(err).trimmed();
            if (msg.isEmpty()) {
                msg = QStringLiteral("yt-dlp exited with code %1.").arg(exitCode);
            }
            emit searchFailed(msg);
        } else {
            emit searchTotalKnown(total);
            emit searchFinished(results);  // genuinely no matches
        }
        return;
    }

    emit searchTotalKnown(total);
    emit searchFinished(results);
}
