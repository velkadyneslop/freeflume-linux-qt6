// FreeFlume — extraction backend.
//
// Wraps yt-dlp as an asynchronous subprocess. yt-dlp replaces NewPipe's
// JVM-only extractor and supports YouTube, Rumble, BitChute, and many more.
#pragma once

#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>

// What a search result points to.
enum class ResultKind { Video, Short, Channel, Playlist };

// YouTube search filters (encoded into the sp= URL parameter). 0 = "any".
struct SearchFilters {
    int uploadDate = 0;  // 1 hour, 2 today, 3 week, 4 month, 5 year
    int type = 0;        // 1 video, 2 channel, 3 playlist, 4 movie
    int duration = 0;    // 1 short (<4m), 2 long (>20m), 3 medium (4-20m)
    int sort = 0;        // 1 rating, 2 upload date, 3 view count (0 = relevance)
    bool hd = false;
    bool fourK = false;
    bool subtitles = false;
    bool live = false;
    bool any() const {
        return uploadDate || type || duration || sort || hd || fourK || subtitles || live;
    }
};

// One search result / video reference (lightweight, from a flat extraction).
struct SearchResult {
    QString id;
    QString url;
    QString title;
    QString channel;
    qint64 durationSeconds = 0;  // 0 when unknown (e.g. live)
    qint64 viewCount = -1;       // -1 when unknown
    QString thumbnailUrl;        // best available thumbnail
    bool isLive = false;
    ResultKind kind = ResultKind::Video;
    qint64 published = 0;        // upload time (unix secs); 0 = unknown (feed only)
};

// A YouTube storyboard: a set of sprite images, each a rows×columns grid of
// little thumbnails at a fixed time interval (used for seek-bar previews).
struct StoryboardFragment {
    QString url;
    double duration = 0.0;  // seconds covered by this sprite image
};
struct Storyboard {
    int tileWidth = 0;
    int tileHeight = 0;
    int rows = 0;
    int columns = 0;
    QList<StoryboardFragment> fragments;
    bool valid() const {
        return tileWidth > 0 && tileHeight > 0 && rows > 0 && columns > 0 &&
               !fragments.isEmpty();
    }
};

// A video chapter (section with a start time and title).
struct Chapter {
    double startSeconds = 0.0;
    QString title;
};

// Full metadata for a single video (from a non-flat extraction).
struct VideoDetails {
    QString url;
    QString title;
    QString channel;
    QString channelUrl;
    QString description;
    QString uploadDate;  // YYYYMMDD as reported by yt-dlp
    qint64 durationSeconds = 0;
    qint64 viewCount = -1;
    qint64 likeCount = -1;
    QString thumbnailUrl;
    Storyboard storyboard;
    QList<Chapter> chapters;
};

// Runs extraction commands without blocking the UI thread.
class Extractor : public QObject {
    Q_OBJECT

public:
    explicit Extractor(QObject* parent = nullptr);

    // Kicks off a search for one page of results (1-based page, pageSize per
    // page). When richResults is true, the full YouTube results page is used so
    // that channels and playlists are returned too (not just videos).
    void search(const QString& query, int page = 1, int pageSize = 20,
                bool richResults = false, const SearchFilters& filters = {});

    // Loads one page of a channel/playlist URL. Emits the same
    // searchStarted/searchFinished/searchFailed signals as search().
    void fetchChannel(const QString& channelUrl, int page = 1, int pageSize = 50);

    // Searches within a single channel (YouTube's per-channel search). Emits the
    // same signals as search(). channelUrl may include a tab; it is stripped.
    void searchInChannel(const QString& channelUrl, const QString& query, int page = 1,
                         int pageSize = 20);

    // Fetches full metadata for one video URL. Independent of search().
    void fetchDetails(const QString& url);

    // Fetches the ENTIRE flat list of a playlist/channel URL (no pagination),
    // for building a play queue. Runs on its own process so it doesn't disturb
    // an in-progress search/page; emits playlistItemsReady on success.
    void fetchPlaylistItems(const QString& url);

    bool busy() const;
    void cancel();

signals:
    void searchStarted(const QString& query);
    void searchFinished(const QList<SearchResult>& results);
    void searchFailed(const QString& message);
    // Total item count for the current source when known (e.g. a playlist's
    // length), or 0 if unknown. Emitted just before searchFinished.
    void searchTotalKnown(int total);

    void detailsStarted(const QString& url);
    void detailsFinished(const VideoDetails& details);
    void detailsFailed(const QString& message);

    // The full ordered items of the playlist/channel requested via
    // fetchPlaylistItems(), echoing back the source url.
    void playlistItemsReady(const QList<SearchResult>& items, const QString& url);

private:
    void runFlat(const QString& target, const QString& query, int start, int end);
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void handleError(QProcess::ProcessError error);
    void handleDetailsFinished(int exitCode, QProcess::ExitStatus status);
    void handlePlaylistFinished(int exitCode, QProcess::ExitStatus status);

    QProcess* proc_ = nullptr;
    QProcess* detailsProc_ = nullptr;
    QProcess* playlistProc_ = nullptr;
    QString query_;
    QString detailsUrl_;
    QString playlistUrl_;
};

Q_DECLARE_METATYPE(VideoDetails)

Q_DECLARE_METATYPE(SearchResult)
