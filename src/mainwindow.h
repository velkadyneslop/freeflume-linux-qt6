// FreeFlume — main window shell.
#pragma once

#include <QMainWindow>

#include "extractor.h"

class QListWidget;
class QStackedWidget;
class QLineEdit;
class QCompleter;
class QStringListModel;
class Extractor;
class SearchPage;
class PlayerPage;
class HistoryPage;
class SubscriptionsPage;
class PlaylistsPage;
class DownloadsPage;
class DownloadManager;
class Database;
class ThumbnailLoader;

// The top-level desktop shell: a left navigation sidebar, a top search bar,
// and a stacked content area. Styling is intentionally left to the active Qt
// style/platform theme so the app looks native on KDE, GNOME, etc.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Pre-fills the search box and runs a search (e.g. from a CLI argument).
    void search(const QString& query);

    // Opens the embedded player and plays a URL directly.
    void playUrl(const QString& url, const QString& title = {});

    // Selects a sidebar tab by row index (deep-linking / testing).
    void selectTab(int index);

    // Enters/leaves player fullscreen by hiding chrome (no widget reparenting).
    void setPlayerFullScreen(bool on);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onSearchSubmitted();
    void onNavChanged(int index);
    void onPlayRequested(const SearchResult& result);
    void onPlayQueueRequested(const QList<SearchResult>& items, int index,
                              const QString& title);

private:
    QWidget* buildSidebar();
    QWidget* buildTopBar();
    QWidget* buildPlaceholderPage(const QString& title, const QString& subtitle,
                                  const QString& iconName);

    // The player floats over the content area (never reparented), so it can
    // shrink to a mini-player while you browse other pages.
    enum PlayerState { PlayerHidden, PlayerMini, PlayerFull };
    void setPlayerState(PlayerState state);
    void layoutPlayer();
    void collapsePlayer();  // leave the full player: mini, or stop (per setting)
    void updateSidebarVisibility();  // hide the sidebar during windowed full playback

    Database* db_ = nullptr;
    ThumbnailLoader* thumbs_ = nullptr;
    Extractor* extractor_ = nullptr;
    DownloadManager* downloads_ = nullptr;
    DownloadsPage* downloadsPage_ = nullptr;
    SearchPage* searchPage_ = nullptr;
    PlayerPage* playerPage_ = nullptr;
    HistoryPage* historyPage_ = nullptr;
    SubscriptionsPage* subscriptionsPage_ = nullptr;
    PlaylistsPage* playlistsPage_ = nullptr;
    QWidget* topBar_ = nullptr;
    QWidget* content_ = nullptr;  // the right area the player floats over
    QListWidget* nav_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLineEdit* search_ = nullptr;
    QCompleter* searchCompleter_ = nullptr;
    QStringListModel* searchModel_ = nullptr;
    int lastNavIndex_ = 0;
    PlayerState playerState_ = PlayerHidden;
    bool playerFullScreen_ = false;
    bool sidebarOpen_ = false;  // user toggled the sidebar on during full playback
};
