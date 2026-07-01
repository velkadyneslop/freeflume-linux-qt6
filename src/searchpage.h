// FreeFlume — search results page.
#pragma once

#include <QHash>
#include <QWidget>

#include "extractor.h"

class Database;
class DetailPane;
class DownloadManager;
class ThumbnailLoader;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QWidget;
class QToolButton;
class QAction;
class QTimer;

// Shows the state machine of a search: idle → searching → results / empty / error.
// Owns nothing about extraction beyond a (non-owning) Extractor pointer.
class SearchPage : public QWidget {
    Q_OBJECT

public:
    SearchPage(Extractor* extractor, ThumbnailLoader* thumbs, Database* db,
               QWidget* parent = nullptr);

    void setDownloadManager(DownloadManager* m) { downloads_ = m; }

public slots:
    void startSearch(const QString& query);
    void clearResults();
    // Show a channel/playlist's videos. label is an optional display name.
    void openChannel(const QString& url, const QString& label = QString());

signals:
    void statusMessage(const QString& message, int timeoutMs);
    void playRequested(const SearchResult& result);
    // Play the current playlist view as a queue, starting at the chosen item.
    void playQueueRequested(const QList<SearchResult>& items, int index,
                            const QString& title);
    // The full playlist finished loading — upgrade the player's queue to it.
    void playlistResolved(const QList<SearchResult>& items, const QString& title);

private:
    // One step of in-page navigation (a search or a channel/playlist view).
    struct NavState {
        bool isChannel = false;
        QString query;
        QString channelUrl;
        QString channelQuery;  // in-channel search text (empty = browse all)
        ChannelTab channelTab = ChannelTab::Videos;  // channel view: Videos/Streams/Shorts
        QString label;
        SearchFilters filters;
        int page = 1;
    };

    void showMessage(const QString& text);
    void populate(const QList<SearchResult>& results);
    void enqueueVisibleDates();  // lazily fetch upload dates for on-screen rows
    void onSelectionChanged();
    void fetchPage();
    void goToPage(int page);
    void updatePager(int rawCount);
    void rebuildPageNumbers(int lastPage);
    void goToState(const NavState& state, bool pushCurrent);
    void goBack();
    void searchCurrentChannel(const QString& query);
    void setChannelTab(ChannelTab tab);  // switch a channel's Videos/Streams/Shorts tab
    NavState captureCurrent() const;
    bool hasCurrentSource() const;
    bool isCurrentChannel() const;
    bool isCurrentPlaylist() const;
    void playResult(const SearchResult& r);  // queue if in a playlist, else single
    void playPlaylist(const SearchResult& playlist);  // fetch + play a whole playlist
    void showContextMenu(const SearchResult& r, const QPoint& globalPos);
    void updateContextUi();
    SearchFilters readFilters() const;
    void setFiltersUi(const SearchFilters& filters);
    void onFilterChanged();

    Extractor* extractor_;          // not owned
    Database* db_ = nullptr;        // not owned (subscriptions)
    DownloadManager* downloads_ = nullptr;  // not owned (downloads)
    ThumbnailLoader* thumbs_ = nullptr;
    DetailPane* detail_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* message_ = nullptr;
    QListWidget* list_ = nullptr;
    QHash<QString, QLabel*> metaByUrl_;        // meta labels awaiting an upload date
    QHash<QString, QLabel*> channelThumbByUrl_; // channel thumbs, for the live ring
    QTimer* dateScrollTimer_ = nullptr;   // debounces date/live enqueue on scroll

    // Pagination state. The source is either a search query or a channel/
    // playlist URL (drill-in); both paginate the same way.
    QWidget* pager_ = nullptr;
    QPushButton* prevBtn_ = nullptr;
    QPushButton* nextBtn_ = nullptr;
    QWidget* numbersHost_ = nullptr;
    QHBoxLayout* numbersLayout_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QLabel* contextLabel_ = nullptr;
    QToolButton* videosTabBtn_ = nullptr;   // channel: Videos tab
    QToolButton* streamsTabBtn_ = nullptr;  // channel: Streams tab (past + live)
    QToolButton* shortsTabBtn_ = nullptr;   // channel: Shorts tab (opt-in setting)
    QLineEdit* channelSearch_ = nullptr;
    QWidget* filterBar_ = nullptr;
    QComboBox* dateFilter_ = nullptr;
    QComboBox* typeFilter_ = nullptr;
    QComboBox* durationFilter_ = nullptr;
    QComboBox* sortFilter_ = nullptr;
    QToolButton* featuresButton_ = nullptr;
    QAction* featHd_ = nullptr;
    QAction* featFourK_ = nullptr;
    QAction* featSubs_ = nullptr;
    QAction* featLive_ = nullptr;
    QList<NavState> backStack_;
    QString currentQuery_;
    QString currentChannelUrl_;
    QString currentChannelQuery_;
    ChannelTab currentChannelTab_ = ChannelTab::Videos;
    QString currentLabel_;
    QString pendingPlaylistUrl_;    // playlist whose full list we're fetching
    QString pendingPlaylistTitle_;  // its label, captured at request time
    bool pendingPlaylistPlayAll_ = false;  // true = play the whole playlist now
    SearchFilters currentFilters_;
    bool isChannelSource_ = false;
    int currentPage_ = 1;
    int pageSize_ = 20;
    int totalItems_ = 0;    // total in source when known (0 = unknown)
    int maxKnownPage_ = 1;  // highest page we know exists (when total unknown)
    bool canPaginate_ = false;
};
