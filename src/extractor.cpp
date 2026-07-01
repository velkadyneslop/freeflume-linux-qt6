// FreeFlume — extraction backend implementation.
#include <algorithm>
#include "extractor.h"

#include <climits>
#include <cstdlib>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

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

// --- InnerTube playlist paging ----------------------------------------------
//
// yt-dlp can't extract an anonymous playlist past ~200 items: YouTube stops
// emitting continuation tokens to scripted clients walking the list. But a
// playlist continuation token is just a base64'd protobuf encoding the
// playlist id and an item OFFSET — so we can craft one for any offset and jump
// straight there in a single request (the browser/NewPipe do the same; they
// merely walk the chain). No account, cookies, visitor data or PO token needed.

const char* kInnerTubeClientVersion = "2.20240620.05.00";

// Pulls the PL.../UU.../etc. id out of a playlist URL (?list=ID or /playlist).
QString extractPlaylistId(const QString& url) {
    static const QRegularExpression re(QStringLiteral("[?&]list=([A-Za-z0-9_-]+)"));
    const QRegularExpressionMatch m = re.match(url);
    return m.hasMatch() ? m.captured(1) : QString();
}

QByteArray pbVarint(quint64 n) {
    QByteArray out;
    do {
        quint8 b = n & 0x7f;
        n >>= 7;
        if (n) b |= 0x80;
        out.append(static_cast<char>(b));
    } while (n);
    return out;
}
QByteArray pbTag(int field, int wireType) {
    return pbVarint((static_cast<quint64>(field) << 3) | wireType);
}
QByteArray pbVarintField(int field, quint64 value) {
    return pbTag(field, 0) + pbVarint(value);
}
QByteArray pbStringField(int field, const QByteArray& value) {
    return pbTag(field, 2) + pbVarint(value.size()) + value;
}

// Builds the continuation token that asks for playlist items starting at
// `offset` (0-based). Verified byte-for-byte against a real token from YT.
QByteArray craftPlaylistContinuation(const QString& playlistId, int offset) {
    const QByteArray pid = playlistId.toUtf8();
    // Innermost "PT:" payload: field 1 = the offset.
    const QByteArray pt = pbVarintField(1, static_cast<quint64>(offset));
    const QByteArray ptStr =
        QByteArray("PT:") + pt.toBase64(QByteArray::Base64Encoding | QByteArray::OmitTrailingEquals);
    // Wrapper: { 1: 1, 15: "PT:..." }, base64'd with '=' URL-escaped (YT's form).
    QByteArray inner = pbVarintField(1, 1) + pbStringField(15, ptStr);
    QByteArray innerStr = inner.toBase64(QByteArray::Base64Encoding);
    innerStr.replace("=", "%3D");
    // Body: { 2: "VL"+id, 3: wrapper, 35: id }, all under field 80226972.
    const QByteArray body = pbStringField(2, QByteArray("VL") + pid) +
                            pbStringField(3, innerStr) + pbStringField(35, pid);
    const QByteArray top = pbStringField(80226972, body);
    return top.toBase64(QByteArray::Base64UrlEncoding);
}

// JSON request body for an InnerTube browse call. With a continuation token for
// a deep offset; with browseId ("VL"+id) for the first page (that response also
// carries the playlist's total length).
QByteArray innerTubeBrowseBody(const QString& playlistId, int offset) {
    QJsonObject client{{QStringLiteral("clientName"), QStringLiteral("WEB")},
                       {QStringLiteral("clientVersion"),
                        QString::fromLatin1(kInnerTubeClientVersion)},
                       {QStringLiteral("hl"), QStringLiteral("en")},
                       {QStringLiteral("gl"), QStringLiteral("US")}};
    QJsonObject body{{QStringLiteral("context"),
                      QJsonObject{{QStringLiteral("client"), client}}}};
    if (offset == 0) {
        body.insert(QStringLiteral("browseId"), QStringLiteral("VL") + playlistId);
    } else {
        body.insert(QStringLiteral("continuation"),
                    QString::fromLatin1(craftPlaylistContinuation(playlistId, offset)));
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QNetworkRequest innerTubeRequest() {
    QNetworkRequest req(QUrl(QStringLiteral(
        "https://www.youtube.com/youtubei/v1/browse?prettyPrint=false")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                                 "(KHTML, like Gecko) Chrome/126.0 Safari/537.36"));
    req.setRawHeader("Origin", "https://www.youtube.com");
    req.setRawHeader("X-Youtube-Client-Name", "1");
    req.setRawHeader("X-Youtube-Client-Version", kInnerTubeClientVersion);
    return req;
}

// "10:10" / "1:02:03" -> seconds; 0 if not a time string.
qint64 parseClockDuration(const QString& text) {
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 3) return 0;
    qint64 total = 0;
    for (const QString& p : parts) {
        bool ok = false;
        const int v = p.toInt(&ok);
        if (!ok || v < 0) return 0;
        total = total * 60 + v;
    }
    return total;
}

// Recursively finds a video-duration badge ("mm:ss") inside a lockup item.
qint64 findLockupDuration(const QJsonValue& v) {
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("thumbnailBadgeViewModel"))) {
            const QString t = o.value(QStringLiteral("thumbnailBadgeViewModel"))
                                  .toObject()
                                  .value(QStringLiteral("text"))
                                  .toString();
            const qint64 secs = parseClockDuration(t);
            if (secs > 0) return secs;
        }
        for (const QJsonValue& child : o) {
            const qint64 secs = findLockupDuration(child);
            if (secs > 0) return secs;
        }
    } else if (v.isArray()) {
        for (const QJsonValue& child : v.toArray()) {
            const qint64 secs = findLockupDuration(child);
            if (secs > 0) return secs;
        }
    }
    return 0;
}

// Turns one lockupViewModel (the modern playlist/search item) into a result.
SearchResult parseLockup(const QJsonObject& lv) {
    SearchResult r;
    r.id = lv.value(QStringLiteral("contentId")).toString();
    if (r.id.isEmpty()) return r;
    r.kind = ResultKind::Video;
    r.url = QStringLiteral("https://www.youtube.com/watch?v=") + r.id;
    r.thumbnailUrl = QStringLiteral("https://i.ytimg.com/vi/%1/hqdefault.jpg").arg(r.id);

    const QJsonObject md = lv.value(QStringLiteral("metadata"))
                               .toObject()
                               .value(QStringLiteral("lockupMetadataViewModel"))
                               .toObject();
    r.title = md.value(QStringLiteral("title")).toObject().value(QStringLiteral("content")).toString();
    const QJsonArray rows = md.value(QStringLiteral("metadata"))
                                .toObject()
                                .value(QStringLiteral("contentMetadataViewModel"))
                                .toObject()
                                .value(QStringLiteral("metadataRows"))
                                .toArray();
    if (!rows.isEmpty()) {
        const QJsonArray parts =
            rows.first().toObject().value(QStringLiteral("metadataParts")).toArray();
        if (!parts.isEmpty()) {
            r.channel = parts.first()
                            .toObject()
                            .value(QStringLiteral("text"))
                            .toObject()
                            .value(QStringLiteral("content"))
                            .toString();
        }
    }
    r.durationSeconds = findLockupDuration(lv.value(QStringLiteral("contentImage")));
    return r;
}

// Collects every lockup item (in document order) from a browse response.
void collectLockups(const QJsonValue& v, QList<SearchResult>& out) {
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("lockupViewModel"))) {
            SearchResult r = parseLockup(o.value(QStringLiteral("lockupViewModel")).toObject());
            if (!r.id.isEmpty() && !r.title.isEmpty()) {
                out.push_back(std::move(r));
            }
        }
        for (const QJsonValue& child : o) {
            collectLockups(child, out);
        }
    } else if (v.isArray()) {
        for (const QJsonValue& child : v.toArray()) {
            collectLockups(child, out);
        }
    }
}

// Finds the playlist length ("1,005 videos") in an initial browse response.
int findPlaylistTotal(const QJsonValue& v) {
    static const QRegularExpression re(QStringLiteral("^([\\d,]+) videos?$"));
    if (v.isString()) {
        const QRegularExpressionMatch m = re.match(v.toString());
        if (m.hasMatch()) {
            return m.captured(1).remove(QLatin1Char(',')).toInt();
        }
    } else if (v.isObject()) {
        for (const QJsonValue& child : v.toObject()) {
            const int n = findPlaylistTotal(child);
            if (n > 0) return n;
        }
    } else if (v.isArray()) {
        for (const QJsonValue& child : v.toArray()) {
            const int n = findPlaylistTotal(child);
            if (n > 0) return n;
        }
    }
    return 0;
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
    return (proc_ && proc_->state() != QProcess::NotRunning) || pageReply_;
}

void Extractor::cancel() {
    if (proc_) {
        proc_->disconnect(this);
        proc_->kill();
        proc_->deleteLater();
        proc_ = nullptr;
    }
    if (pageReply_) {
        pageReply_->disconnect(this);
        pageReply_->abort();
        pageReply_->deleteLater();
        pageReply_ = nullptr;
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
        runFlat({url}, query, start, end);
    } else {
        // ytsearchN fetches N results; slice the page from those.
        runFlat({QStringLiteral("ytsearch%1:%2").arg(end).arg(query)}, query, start, end);
    }
}

void Extractor::fetchChannel(const QString& channelUrl, int page, int pageSize, bool streamsTab) {
    const int start = (page - 1) * pageSize + 1;
    const int end = page * pageSize;
    if (streamsTab) {
        // Streams view: the whole /streams tab (live + upcoming + past), newest-first
        // as YouTube returns it. No pin/drop — past streams are the point here.
        runFlat({channelBaseUrl(channelUrl) + QStringLiteral("/streams")}, channelUrl,
                start, end);
        return;
    }
    const bool channelLike = channelUrl.contains(QLatin1String("/channel/")) ||
                             channelUrl.contains(QLatin1String("/@")) ||
                             channelUrl.contains(QLatin1String("/c/")) ||
                             channelUrl.contains(QLatin1String("/user/"));
    const bool isPlaylist = channelUrl.contains(QLatin1String("list=")) ||
                            channelUrl.contains(QLatin1String("/playlist"));
    // Playlists go through InnerTube directly: yt-dlp can't reach anonymous
    // playlist items past ~200, but a crafted continuation token can jump to
    // any page. (Channels still extract fine via yt-dlp.)
    if (isPlaylist) {
        fetchPlaylistPage(channelUrl, page, pageSize);
        return;
    }
    // Page 1 of an actual channel also pulls its /streams tab so a currently-live
    // (or upcoming) stream can be shown — the /videos tab omits streams entirely.
    // handleFinished then keeps the uploads + only the live/upcoming stream entries
    // (no duration), DROPS finished past streams, and floats the live ones to the
    // top. Net order = live/upcoming → uploads (clean, like YouTube's Videos tab).
    const bool withStreams = (page == 1 && channelLike && !isPlaylist);
    QStringList targets;
    targets << channelToVideosTab(channelUrl);
    if (withStreams) {
        targets << channelBaseUrl(channelUrl) + QStringLiteral("/streams");
    }
    runFlat(targets, channelUrl, start, end, withStreams);
}

void Extractor::searchInChannel(const QString& channelUrl, const QString& query, int page,
                                int pageSize) {
    const int start = (page - 1) * pageSize + 1;
    const int end = page * pageSize;
    const QString url = QStringLiteral("%1/search?query=%2")
                            .arg(channelBaseUrl(channelUrl),
                                 QString::fromUtf8(QUrl::toPercentEncoding(query)));
    runFlat({url}, channelUrl, start, end);
}

void Extractor::fetchPlaylistPage(const QString& playlistUrl, int page, int pageSize) {
    cancel();
    query_ = playlistUrl;
    pinLiveFirst_ = false;
    emit searchStarted(playlistUrl);

    const QString plid = extractPlaylistId(playlistUrl);
    if (plid.isEmpty()) {
        emit searchFailed(QStringLiteral("Could not read the playlist id from the URL."));
        return;
    }
    const int offset = (page - 1) * pageSize;
    if (!net_) {
        net_ = new QNetworkAccessManager(this);
    }
    pageReply_ = net_->post(innerTubeRequest(), innerTubeBrowseBody(plid, offset));
    // The first request (offset 0) also carries the playlist's total length.
    connect(pageReply_, &QNetworkReply::finished, this,
            [this, pageSize, wantTotal = (offset == 0)]() {
                handlePlaylistPageReply(pageReply_, pageSize, wantTotal);
            });
}

void Extractor::handlePlaylistPageReply(QNetworkReply* reply, int pageSize, bool wantTotal) {
    if (reply != pageReply_) {
        return;  // a stale reply we already abandoned
    }
    pageReply_ = nullptr;
    const QNetworkReply::NetworkError netErr = reply->error();
    const QByteArray out = reply->readAll();
    reply->deleteLater();

    if (netErr != QNetworkReply::NoError) {
        emit searchFailed(QStringLiteral("Could not reach YouTube to load the playlist."));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(out);
    if (doc.isNull()) {
        emit searchFailed(QStringLiteral("Could not parse the playlist response."));
        return;
    }

    QList<SearchResult> all;
    collectLockups(QJsonValue(doc.object()), all);
    // A browse response holds up to ~100 items from the offset; keep this page's.
    QList<SearchResult> pageItems = all.mid(0, pageSize);

    if (wantTotal) {
        emit searchTotalKnown(findPlaylistTotal(QJsonValue(doc.object())));
    }
    emit searchFinished(pageItems);
}

void Extractor::requestPlaylistChunk(const QString& playlistId, int offset) {
    if (!net_) {
        net_ = new QNetworkAccessManager(this);
    }
    chunkReply_ = net_->post(innerTubeRequest(), innerTubeBrowseBody(playlistId, offset));
    connect(chunkReply_, &QNetworkReply::finished, this,
            [this, playlistId, offset]() {
                handlePlaylistChunkReply(chunkReply_, playlistId, offset);
            });
}

void Extractor::handlePlaylistChunkReply(QNetworkReply* reply, const QString& playlistId,
                                         int offset) {
    if (reply != chunkReply_) {
        return;
    }
    chunkReply_ = nullptr;
    const bool ok = reply->error() == QNetworkReply::NoError;
    const QByteArray out = reply->readAll();
    reply->deleteLater();

    int got = 0;
    if (ok) {
        const QJsonDocument doc = QJsonDocument::fromJson(out);
        QList<SearchResult> chunk;
        collectLockups(QJsonValue(doc.object()), chunk);
        got = chunk.size();
        playlistAccum_ += chunk;
    }
    // A full chunk is 100 items; a short (or empty) one means the end. Cap the
    // walk so a malformed response can't loop forever.
    if (ok && got >= 100 && offset < 100000) {
        requestPlaylistChunk(playlistId, offset + got);
        return;
    }
    emit playlistItemsReady(playlistAccum_, playlistUrl_);
}

void Extractor::runFlat(const QStringList& targets, const QString& label, int start, int end,
                        bool pinLiveFirst) {
    cancel();
    query_ = label;
    pinLiveFirst_ = pinLiveFirst;

    proc_ = new QProcess(this);
    connect(proc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &Extractor::handleFinished);
    connect(proc_, &QProcess::errorOccurred, this, &Extractor::handleError);

    QStringList args = {
        QStringLiteral("--flat-playlist"),
        QStringLiteral("--dump-json"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--ignore-errors"),
        QStringLiteral("--playlist-start"),
        QString::number(start),
        QStringLiteral("--playlist-end"),
        QString::number(end),
    };
    args += targets;  // one URL (search/single tab) or several (channel tabs), in order

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

void Extractor::fetchAudioTracks(const QString& url) {
    if (audioProc_) {
        audioProc_->disconnect(this);
        audioProc_->kill();
        audioProc_->deleteLater();
    }
    audioUrl_ = url;
    audioProc_ = new QProcess(this);
    connect(audioProc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &Extractor::handleAudioTracksFinished);

    const QStringList args = {
        QStringLiteral("--dump-single-json"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--no-playlist"),
        // web_embedded exposes the alternate-language audio the default client
        // hides. We only read the language list here, so nsig/URLs don't matter.
        QStringLiteral("--extractor-args"),
        QStringLiteral("youtube:player_client=web_embedded"),
        url,
    };
    audioProc_->start(QStringLiteral("yt-dlp"), args);
}

void Extractor::handleAudioTracksFinished(int exitCode, QProcess::ExitStatus status) {
    if (!audioProc_) {
        return;
    }
    const QByteArray out = audioProc_->readAllStandardOutput();
    const QString url = audioUrl_;
    audioProc_->deleteLater();
    audioProc_ = nullptr;

    QList<AudioTrackInfo> tracks;
    if (status == QProcess::NormalExit && exitCode == 0) {
        const QJsonObject root = QJsonDocument::fromJson(out).object();
        QSet<QString> seen;
        for (const QJsonValue& v : root.value(QStringLiteral("formats")).toArray()) {
            const QJsonObject f = v.toObject();
            // Audio-only formats carry the per-language tracks.
            if (f.value(QStringLiteral("acodec")).toString() == QLatin1String("none") ||
                f.value(QStringLiteral("vcodec")).toString() != QLatin1String("none")) {
                continue;
            }
            const QString code = f.value(QStringLiteral("language")).toString();
            if (code.isEmpty() || seen.contains(code)) {
                continue;
            }
            seen.insert(code);
            const QString note = f.value(QStringLiteral("format_note")).toString();

            // The original vs. dubbed vs. auto-dubbed distinction isn't in
            // format_note — it lives in the stream URL's "xtags" (acont=original /
            // dubbed / dubbed-auto). language_preference==10 also flags the original.
            QString acont;
            const QUrl mediaUrl(f.value(QStringLiteral("url")).toString());
            const QString xtags = QUrlQuery(mediaUrl.query()).queryItemValue(
                QStringLiteral("xtags"), QUrl::FullyDecoded);
            for (const QString& part : xtags.split(QLatin1Char(':'))) {
                if (part.startsWith(QLatin1String("acont="))) {
                    acont = part.mid(6);
                }
            }

            AudioTrackInfo t;
            t.code = code;
            // Clean language name: drop the ", quality" tail and any " (region)" /
            // " original (default)" qualifier, e.g. "English (US) original
            // (default), low" -> "English", "French (FR), low" -> "French".
            t.name = note.section(QLatin1Char(','), 0, 0)
                         .section(QStringLiteral(" ("), 0, 0)
                         .trimmed();
            t.isDefault = acont == QLatin1String("original") ||
                          f.value(QStringLiteral("language_preference")).toInt() == 10 ||
                          note.contains(QLatin1String("default"), Qt::CaseInsensitive);
            t.autoDub = acont == QLatin1String("dubbed-auto");
            tracks.push_back(t);
        }
        // Original(s) first, then the rest alphabetical by language name.
        std::sort(tracks.begin(), tracks.end(),
                  [](const AudioTrackInfo& a, const AudioTrackInfo& b) {
                      if (a.isDefault != b.isDefault) {
                          return a.isDefault;  // default sorts to the top
                      }
                      return a.name.localeAwareCompare(b.name) < 0;
                  });
    }
    emit audioTracksReady(tracks, url);
}

void Extractor::handleError(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        emit searchFailed(QStringLiteral(
            "Could not launch yt-dlp. Is it installed and on your PATH?"));
    }
}

void Extractor::fetchPlaylistItems(const QString& url) {
    playlistUrl_ = url;

    // Playlists: walk the whole thing via InnerTube (100 items per request) so
    // the play queue isn't capped at yt-dlp's ~200-item anonymous ceiling.
    const QString plid = extractPlaylistId(url);
    if (!plid.isEmpty()) {
        if (chunkReply_) {
            chunkReply_->disconnect(this);
            chunkReply_->abort();
            chunkReply_->deleteLater();
            chunkReply_ = nullptr;
        }
        playlistAccum_.clear();
        requestPlaylistChunk(plid, 0);
        return;
    }

    if (playlistProc_) {
        playlistProc_->disconnect(this);
        playlistProc_->kill();
        playlistProc_->deleteLater();
    }
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
        // A channel's page 1 also pulls its /streams tab (to surface live/upcoming
        // streams). Those entries belong to the streams tab, not the videos list —
        // and crucially their playlist_count is the *stream* count, not the
        // channel's. Letting that count become the "total" wrongly collapses the
        // page count to 1 and hides the pager. So ignore playlist_count from any
        // /streams entry; the /videos tab reports no count, leaving channels on the
        // rawCount heuristic in the UI (the intended behaviour for open-ended tabs).
        const QString src = obj.value(QStringLiteral("playlist_webpage_url")).toString();
        const QString orig = obj.value(QStringLiteral("original_url")).toString();
        const bool fromStreams = src.endsWith(QLatin1String("/streams")) ||
                                 orig.endsWith(QLatin1String("/streams"));
        if (!fromStreams) {
            const int pc = obj.value(QStringLiteral("playlist_count")).toInt(0);
            if (pc > total) {
                total = pc;
            }
        }
        SearchResult r = parseEntry(obj);
        if (r.title.isEmpty() || r.url.isEmpty()) {
            continue;
        }
        if (pinLiveFirst_ && r.durationSeconds > 0 && fromStreams) {
            // Channel view: drop finished PAST streams. A past stream has a duration
            // and comes from /streams; uploads also have a duration but come from
            // /videos (kept). Live/upcoming streams have no duration and are kept +
            // floated to the top below.
            continue;
        }
        results.push_back(std::move(r));
    }

    if (pinLiveFirst_) {
        // Float the currently-live/upcoming streams (no duration) above the uploads.
        std::stable_partition(results.begin(), results.end(),
                              [](const SearchResult& r) { return r.durationSeconds <= 0; });
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
