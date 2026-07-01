// FreeFlume — main window shell implementation.
#include "apppaths.h"
#include "mainwindow.h"

#include <QCompleter>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QSignalBlocker>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QDir>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QShortcut>
#include <QStringListModel>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QWidget>

#include "database.h"
#include "metaenricher.h"
#include "downloadmanager.h"
#include "downloadspage.h"
#include "sharemenu.h"
#include "extractor.h"
#include "historypage.h"
#include "playerpage.h"
#include "playlistspage.h"
#include "searchpage.h"
#include "settingspage.h"
#include "subscriptionspage.h"
#include "thumbnailloader.h"
#include "updatecheck.h"

#ifndef FREEFLUME_VERSION
#define FREEFLUME_VERSION "dev"  // set by CMake from PROJECT_VERSION
#endif

namespace {

struct NavItem {
    const char* label;
    const char* icon;      // preferred theme icon name
    const char* fallback;  // used if the preferred name isn't in the theme
};

// Sidebar destinations — a cohesive monochrome icon set, with a fallback name
// for desktops whose icon theme uses different names.
constexpr NavItem kNavItems[] = {
    {"Search", "edit-find", "system-search"},
    {"Subscriptions", "feed-subscribe", "rss"},
    {"History", "view-history", "document-open-recent"},
    {"Playlists", "view-media-playlist", "media-playlist-normal"},
    {"Downloads", "download", "folder-download"},
    {"Settings", "configure", "preferences-system"},
};

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    qRegisterMetaType<SearchResult>();

    setWindowTitle(QStringLiteral("FreeFlume"));
    // Window icon is inherited from QApplication::windowIcon() (set in main).
    resize(1100, 720);

    // Persistence: one SQLite file under the user's data dir.
    const QString dataDir = apppaths::dataDir();
    db_ = new Database(this);
    db_->open(dataDir + QStringLiteral("/freeflume.db"));
    MetaEnricher::instance()->setDatabase(db_);  // shared lazy row-date fetcher

    thumbs_ = new ThumbnailLoader(this);
    extractor_ = new Extractor(this);
    downloads_ = new DownloadManager(this);

    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildSidebar());

    // Right side: top bar stacked above the content pages.
    auto* right = new QWidget(central);
    auto* rightCol = new QVBoxLayout(right);
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->setSpacing(0);
    topBar_ = buildTopBar();
    rightCol->addWidget(topBar_);

    pages_ = new QStackedWidget(right);

    searchPage_ = new SearchPage(extractor_, thumbs_, db_, right);
    connect(searchPage_, &SearchPage::playRequested, this, &MainWindow::onPlayRequested);
    connect(searchPage_, &SearchPage::playQueueRequested, this,
            &MainWindow::onPlayQueueRequested);
    connect(searchPage_, &SearchPage::playlistResolved, this,
            [this](const QList<SearchResult>& items, const QString& title) {
                playerPage_->extendQueue(items, title);
            });
    connect(searchPage_, &SearchPage::statusMessage, this,
            [this](const QString& msg, int timeout) {
                statusBar()->showMessage(msg, timeout);
            });
    pages_->addWidget(searchPage_);  // index 0 — Search

    subscriptionsPage_ = new SubscriptionsPage(db_, thumbs_, right);
    connect(subscriptionsPage_, &SubscriptionsPage::playRequested,
            this, &MainWindow::onPlayRequested);
    connect(subscriptionsPage_, &SubscriptionsPage::channelRequested, this,
            [this](const QString& url) {
                lastNavIndex_ = 0;
                nav_->setCurrentRow(0);
                pages_->setCurrentWidget(searchPage_);
                searchPage_->openChannel(url);
            });
    pages_->addWidget(subscriptionsPage_);  // index 1 — Subscriptions

    historyPage_ = new HistoryPage(db_, thumbs_, right);
    connect(historyPage_, &HistoryPage::playRequested, this, &MainWindow::onPlayRequested);
    pages_->addWidget(historyPage_);  // index 2 — History

    playlistsPage_ = new PlaylistsPage(db_, thumbs_, right);
    connect(playlistsPage_, &PlaylistsPage::playRequested, this, &MainWindow::onPlayRequested);
    connect(playlistsPage_, &PlaylistsPage::playQueueRequested, this,
            &MainWindow::onPlayQueueRequested);
    pages_->addWidget(playlistsPage_);  // index 3 — Playlists

    downloadsPage_ = new DownloadsPage(downloads_, right);
    pages_->addWidget(downloadsPage_);  // index 4 — Downloads

    pages_->addWidget(new SettingsPage(db_, right));  // index 5 — Settings

    // Let every surface offer a "Download" entry.
    searchPage_->setDownloadManager(downloads_);
    subscriptionsPage_->setDownloadManager(downloads_);
    historyPage_->setDownloadManager(downloads_);
    playlistsPage_->setDownloadManager(downloads_);

    // Player floats over the content area (not in the page stack) so it can
    // shrink to a mini-player while you browse. Reparented now, before the GL
    // context exists, and only ever resized/moved afterwards (never reparented).
    content_ = right;
    content_->installEventFilter(this);  // re-layout the floating player on resize
    playerPage_ = new PlayerPage(right);
    playerPage_->setDatabase(db_);
    playerPage_->setDownloadManager(downloads_);
    playerPage_->hide();
    connect(playerPage_, &PlayerPage::backRequested, this,
            [this] { collapsePlayer(); });                    // ← mini, or stop
    connect(playerPage_, &PlayerPage::expandRequested, this,
            [this] { setPlayerState(PlayerFull); });
    connect(playerPage_, &PlayerPage::closeRequested, this, [this] {
        playerPage_->stop();
        setPlayerState(PlayerHidden);
    });
    connect(playerPage_, &PlayerPage::fullscreenToggleRequested, this,
            [this] { setPlayerFullScreen(!playerFullScreen_); });
    connect(playerPage_, &PlayerPage::toggleSidebarRequested, this, [this] {
        sidebarOpen_ = !sidebarOpen_;
        updateSidebarVisibility();
    });
    connect(playerPage_, &PlayerPage::nowPlaying, this, [this](const SearchResult& item) {
        playbackEnded_ = false;  // a fresh video is playing again
        statusBar()->showMessage(QStringLiteral("Playing: %1").arg(item.title), 5000);
        if (QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("history/rememberWatch"), true).toBool()) {
            db_->addHistory(item);
        }
    });
    connect(playerPage_, &PlayerPage::playbackFinished, this,
            [this] { playbackEnded_ = true; });
    connect(playerPage_, &PlayerPage::channelRequested, this, [this](const QString& urlRef) {
        const QString url = urlRef;
        setPlayerFullScreen(false);
        setPlayerState(PlayerMini);  // keep playing in the corner
        nav_->setCurrentRow(0);      // highlight Search in the sidebar
        searchPage_->openChannel(url);
    });

    // The search bar belongs to the Search page only; hide it elsewhere.
    connect(pages_, &QStackedWidget::currentChanged, this, [this] {
        topBar_->setVisible(pages_->currentWidget() == searchPage_);
    });

    rightCol->addWidget(pages_, /*stretch=*/1);

    // Update bar sits at the very top of the content column, above the search
    // bar; hidden until a newer release is found.
    rightCol->insertWidget(0, buildUpdateBanner());

    root->addWidget(right, /*stretch=*/1);
    setCentralWidget(central);

    statusBar()->showMessage(QStringLiteral("Ready · yt-dlp + mpv backends"));

    // Look for a newer FreeFlume shortly after startup (never blocks first
    // paint; obeys the opt-out and the once-per-20h throttle).
    updateChecker_ = new UpdateChecker(this);
    connect(updateChecker_, &UpdateChecker::updateAvailable, this,
            [this](const QString& version, const QString& url) {
                updateUrl_ = url;
                updateBannerText_->setText(
                    tr("FreeFlume %1 is available (you have %2).")
                        .arg(version, QStringLiteral(FREEFLUME_VERSION)));
                updateBanner_->setVisible(true);
            });
    QTimer::singleShot(4000, this, [this] { maybeCheckForUpdates(); });

    // Ctrl+K → jump to Search and focus the search box.
    auto* focusSearch = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    connect(focusSearch, &QShortcut::activated, this, [this] {
        selectTab(0);
        search_->setFocus();
        search_->selectAll();
    });

    nav_->setCurrentRow(0);
}

QWidget* MainWindow::buildUpdateBanner() {
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("UpdateBanner"));
    bar->setFrameShape(QFrame::StyledPanel);
    bar->setVisible(false);  // shown only when a newer release is found
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 5, 6, 5);
    row->setSpacing(8);

    auto* icon = new QLabel(bar);
    icon->setPixmap(QIcon::fromTheme(QStringLiteral("software-update-available"),
                                     QIcon::fromTheme(QStringLiteral("system-software-update")))
                        .pixmap(16, 16));
    row->addWidget(icon);

    updateBannerText_ = new QLabel(bar);
    updateBannerText_->setTextFormat(Qt::PlainText);
    row->addWidget(updateBannerText_);
    row->addStretch(1);

    auto* view = new QPushButton(tr("View release"), bar);
    connect(view, &QPushButton::clicked, this, [this] {
        if (!updateUrl_.isEmpty()) {
            QDesktopServices::openUrl(QUrl(updateUrl_));
        }
    });
    row->addWidget(view);

    auto* close = new QToolButton(bar);
    close->setIcon(QIcon::fromTheme(QStringLiteral("window-close"),
                                    QIcon::fromTheme(QStringLiteral("dialog-close"))));
    close->setAutoRaise(true);
    close->setToolTip(tr("Dismiss"));
    connect(close, &QToolButton::clicked, this, [this] { updateBanner_->setVisible(false); });
    row->addWidget(close);

    updateBanner_ = bar;
    return bar;
}

void MainWindow::maybeCheckForUpdates() {
    const bool notify = QSettings(apppaths::configFile(), QSettings::IniFormat)
                            .value(QStringLiteral("updates/notify"), false)
                            .toBool();
    if (notify && updateChecker_) {
        updateChecker_->check(/*force=*/false);
    }
}

QWidget* MainWindow::buildSidebar() {
    nav_ = new QListWidget(this);
    nav_->setObjectName(QStringLiteral("Sidebar"));
    nav_->setFrameShape(QFrame::NoFrame);
    nav_->setIconSize(QSize(20, 20));
    nav_->setUniformItemSizes(true);
    nav_->setSpacing(2);

    for (const auto& item : kNavItems) {
        const QIcon icon = QIcon::fromTheme(QString::fromUtf8(item.icon),
                                            QIcon::fromTheme(QString::fromUtf8(item.fallback)));
        auto* row = new QListWidgetItem(icon, QString::fromUtf8(item.label), nav_);
        row->setSizeHint(QSize(0, 40));
    }
    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::onNavChanged);

    // Sidebar = the nav list, with an active-download indicator pinned below it.
    sidebar_ = new QWidget(this);
    sidebar_->setObjectName(QStringLiteral("Sidebar"));
    sidebar_->setFixedWidth(210);
    auto* col = new QVBoxLayout(sidebar_);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(nav_, 1);

    dlStatus_ = new QWidget(sidebar_);
    dlStatus_->setCursor(Qt::PointingHandCursor);
    dlStatus_->setToolTip(tr("Show downloads"));
    dlStatus_->installEventFilter(this);  // click → open the Downloads page
    auto* dcol = new QVBoxLayout(dlStatus_);
    dcol->setContentsMargins(12, 6, 12, 10);
    dcol->setSpacing(3);
    dlStatusLabel_ = new QLabel(dlStatus_);
    dlStatusLabel_->setEnabled(false);  // muted caption
    dlStatusBar_ = new QProgressBar(dlStatus_);
    dlStatusBar_->setRange(0, 100);
    dlStatusBar_->setTextVisible(false);
    dlStatusBar_->setFixedHeight(6);
    dcol->addWidget(dlStatusLabel_);
    dcol->addWidget(dlStatusBar_);
    dlStatus_->setVisible(false);
    col->addWidget(dlStatus_);

    connect(downloads_, &DownloadManager::changed, this, &MainWindow::updateDownloadStatus);
    updateDownloadStatus();
    return sidebar_;
}

void MainWindow::updateDownloadStatus() {
    if (!dlStatus_ || !downloads_) {
        return;
    }
    const Download* active = nullptr;
    int queued = 0;
    for (const Download& d : downloads_->downloads()) {
        if (d.status == Download::Running && !active) {
            active = &d;
        } else if (d.status == Download::Queued) {
            ++queued;
        }
    }
    if (!active) {
        dlStatus_->setVisible(false);
        return;
    }
    dlStatusBar_->setValue(active->percent);
    QString txt = active->merging ? tr("Merging… %1%").arg(active->percent)
                                  : tr("Downloading %1%").arg(active->percent);
    if (queued > 0) {
        txt += tr("  ·  %n queued", "", queued);
    }
    dlStatusLabel_->setText(txt);
    dlStatus_->setVisible(true);
}

QWidget* MainWindow::buildTopBar() {
    auto* bar = new QWidget(this);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(12, 10, 12, 10);

    search_ = new QLineEdit(bar);
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText(QStringLiteral("Search videos, channels…  (Ctrl+K)"));
    search_->addAction(QIcon::fromTheme(QStringLiteral("system-search")),
                       QLineEdit::LeadingPosition);

    // Past searches as an autocomplete dropdown.
    searchModel_ = new QStringListModel(db_->searchHistory(), this);
    searchCompleter_ = new QCompleter(searchModel_, this);
    searchCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
    searchCompleter_->setCompletionMode(QCompleter::PopupCompletion);
    searchCompleter_->setFilterMode(Qt::MatchContains);
    search_->setCompleter(searchCompleter_);
    search_->installEventFilter(this);  // pop recent searches on focus/click
    connect(db_, &Database::searchHistoryChanged, this,
            [this] { searchModel_->setStringList(db_->searchHistory()); });

    // Optional live YouTube search suggestions (opt-in, default off).
    suggestNam_ = new QNetworkAccessManager(this);
    suggestTimer_ = new QTimer(this);
    suggestTimer_->setSingleShot(true);
    suggestTimer_->setInterval(160);  // debounce keystrokes
    connect(suggestTimer_, &QTimer::timeout, this, &MainWindow::fetchSuggestions);
    connect(search_, &QLineEdit::textEdited, this, [this](const QString& text) {
        const QSettings s(apppaths::configFile(), QSettings::IniFormat);
        if (s.value(QStringLiteral("search/suggestions"), false).toBool() &&
            !text.trimmed().isEmpty()) {
            suggestTimer_->start();
        }
    });

    connect(search_, &QLineEdit::returnPressed, this, &MainWindow::onSearchSubmitted);
    // Clearing the box (e.g. the clear button) wipes the results + suggestions.
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.isEmpty()) {
            suggestTimer_->stop();
            searchModel_->setStringList(db_->searchHistory());  // back to history
            if (searchPage_) {
                searchPage_->clearResults();
            }
        }
    });

    lay->addWidget(search_, /*stretch=*/1);
    return bar;
}

void MainWindow::fetchSuggestions() {
    if (!search_) {
        return;
    }
    const QString text = search_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    if (suggestReply_) {  // supersede any in-flight request
        suggestReply_->abort();
        suggestReply_->deleteLater();
        suggestReply_ = nullptr;
    }
    QUrl url(QStringLiteral("https://suggestqueries.google.com/complete/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client"), QStringLiteral("firefox"));  // clean JSON
    q.addQueryItem(QStringLiteral("ds"), QStringLiteral("yt"));           // YouTube
    q.addQueryItem(QStringLiteral("q"), text);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (compatible; FreeFlume)"));
    suggestReply_ = suggestNam_->get(req);
    connect(suggestReply_, &QNetworkReply::finished, this, [this, text] {
        QNetworkReply* r = suggestReply_;
        if (!r) {
            return;
        }
        suggestReply_ = nullptr;
        r->deleteLater();
        if (r->error() == QNetworkReply::NoError) {
            applySuggestions(r->readAll(), text);
        }
    });
}

void MainWindow::applySuggestions(const QByteArray& data, const QString& forText) {
    // Ignore if the user has typed on since this request was issued.
    if (!search_ || search_->text().trimmed() != forText) {
        return;
    }
    // Response shape: ["query", ["suggestion 1", "suggestion 2", …], …]
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray() || doc.array().size() < 2 || !doc.array().at(1).isArray()) {
        return;
    }
    QStringList list;
    const QJsonArray suggestions = doc.array().at(1).toArray();
    for (const QJsonValue& v : suggestions) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty() && !list.contains(s, Qt::CaseInsensitive)) {
            list << s;
        }
    }
    if (list.isEmpty()) {
        return;
    }
    // Round it out with a few recent searches not already suggested.
    for (const QString& h : db_->searchHistory()) {
        if (list.size() >= 12) {
            break;
        }
        if (!list.contains(h, Qt::CaseInsensitive)) {
            list << h;
        }
    }
    searchModel_->setStringList(list);
    searchCompleter_->complete();  // show/refresh the dropdown
}

QWidget* MainWindow::buildPlaceholderPage(const QString& title, const QString& subtitle,
                                          const QString& iconName) {
    auto* page = new QWidget(this);
    auto* col = new QVBoxLayout(page);
    col->setAlignment(Qt::AlignCenter);

    auto* icon = new QLabel(page);
    icon->setPixmap(QIcon::fromTheme(iconName).pixmap(64, 64));
    icon->setAlignment(Qt::AlignCenter);

    auto* head = new QLabel(title, page);
    auto headFont = head->font();
    headFont.setPointSize(headFont.pointSize() + 8);
    headFont.setBold(true);
    head->setFont(headFont);
    head->setAlignment(Qt::AlignCenter);

    auto* sub = new QLabel(subtitle, page);
    sub->setAlignment(Qt::AlignCenter);
    sub->setEnabled(false);  // muted, theme-aware

    col->addWidget(icon);
    col->addSpacing(8);
    col->addWidget(head);
    col->addWidget(sub);
    return page;
}

void MainWindow::onNavChanged(int index) {
    if (index < 0 || !pages_) {
        return;
    }
    lastNavIndex_ = index;
    pages_->setCurrentIndex(index);
    if (playerState_ == PlayerFull) {
        collapsePlayer();  // browsing away shrinks (or stops) the player
    }
    // Refresh data-backed pages each time they're shown.
    switch (index) {
        case 1: subscriptionsPage_->refresh(); break;
        case 2: historyPage_->refresh(); break;
        case 3: playlistsPage_->refresh(); break;
        default: break;
    }
}

void MainWindow::onSearchSubmitted() {
    if (!search_) {
        return;
    }
    const QString query = search_->text().trimmed();
    if (query.isEmpty()) {
        return;
    }
    // Pasting a video link plays it directly instead of searching for the text.
    QString asUrl = query;
    if (asUrl.startsWith(QLatin1String("www.")) ||
        asUrl.startsWith(QLatin1String("youtube.com")) ||
        asUrl.startsWith(QLatin1String("youtu.be")) ||
        asUrl.startsWith(QLatin1String("m.youtube.com"))) {
        asUrl.prepend(QStringLiteral("https://"));
    }
    if ((asUrl.startsWith(QLatin1String("http://")) ||
         asUrl.startsWith(QLatin1String("https://"))) &&
        !share::videoId(asUrl).isEmpty()) {
        search_->clearFocus();
        playUrl(asUrl);
        return;
    }
    if (QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("history/rememberSearch"), true).toBool()) {
        db_->addSearch(query);
        searchModel_->setStringList(db_->searchHistory());
    }
    nav_->setCurrentRow(0);  // jump to Search page
    searchPage_->startSearch(query);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == content_ && event->type() == QEvent::Resize) {
        layoutPlayer();  // keep the floating player anchored on resize
    }
    // Clicking the sidebar download indicator opens the Downloads page (row 4).
    if (obj == dlStatus_ && event->type() == QEvent::MouseButtonRelease) {
        nav_->setCurrentRow(4);
    }
    // Show recent searches when the (empty) search box gains focus or is clicked.
    if (obj == search_ &&
        (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) &&
        searchCompleter_ && searchModel_->rowCount() > 0) {
        searchCompleter_->setCompletionPrefix(search_->text());
        searchCompleter_->complete();
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::search(const QString& query) {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    search_->setText(trimmed);
    onSearchSubmitted();
}

void MainWindow::selectTab(int index) {
    if (nav_ && index >= 0 && index < nav_->count()) {
        nav_->setCurrentRow(index);
    }
}

void MainWindow::setPlayerFullScreen(bool on) {
    if (on == playerFullScreen_) {
        return;
    }
    playerFullScreen_ = on;
    // Hide the chrome and maximize the window — we never reparent the video
    // widget, so the mpv/GL context stays intact.
    statusBar()->setVisible(!on);
    // topBar_ visibility is governed by the page rule (Search page only).
    if (on) {
        wasMaximized_ = isMaximized();  // so we can restore it on exit
        showFullScreen();
    } else if (wasMaximized_) {
        showMaximized();  // a maximized window stays maximized after fullscreen
    } else {
        showNormal();
    }
    playerPage_->setFullScreen(on);
    // Sidebar is hidden in fullscreen and restores to the correct windowed state
    // (collapsed during full playback unless toggled open) on exit.
    updateSidebarVisibility();  // also re-runs layoutPlayer()
}

void MainWindow::playUrl(const QString& url, const QString& title) {
    SearchResult r;
    r.url = url;
    r.title = title.isEmpty() ? url : title;
    setPlayerState(PlayerFull);
    playerPage_->play(r);  // emits nowPlaying() → history + status
}

void MainWindow::onPlayRequested(const SearchResult& result) {
    setPlayerState(PlayerFull);
    playerPage_->play(result);  // emits nowPlaying() → history + status
}

void MainWindow::onPlayQueueRequested(const QList<SearchResult>& items, int index,
                                      const QString& title) {
    setPlayerState(PlayerFull);
    playerPage_->playQueue(items, index, title);
}

void MainWindow::setPlayerState(PlayerState state) {
    if (state != PlayerFull && playerFullScreen_) {
        setPlayerFullScreen(false);  // mini/hidden can't stay fullscreen
    }
    const bool enteringFull = (state == PlayerFull && playerState_ != PlayerFull);
    playerState_ = state;
    if (enteringFull) {
        sidebarOpen_ = false;  // full playback starts with the sidebar collapsed
    }
    // The full player covers the page area, so don't leave a sidebar item looking
    // "current". Clearing the row (signal-blocked) also means clicking the page
    // you were on still registers as a change and collapses the player.
    if (nav_) {
        QSignalBlocker block(nav_);
        nav_->setCurrentRow(state == PlayerFull ? -1 : lastNavIndex_);
    }
    if (state == PlayerHidden) {
        playerPage_->hide();
        updateSidebarVisibility();
        return;
    }
    playerPage_->setMiniMode(state == PlayerMini);
    playerPage_->show();
    playerPage_->raise();
    updateSidebarVisibility();  // also re-runs layoutPlayer()
}

void MainWindow::updateSidebarVisibility() {
    if (!nav_) {
        return;
    }
    bool show = true;  // hidden / mini playback → sidebar always visible
    if (playerFullScreen_) {
        show = false;  // fullscreen is for immersion
    } else if (playerState_ == PlayerFull) {
        show = sidebarOpen_;  // windowed full playback → only when toggled open
    }
    sidebar_->setVisible(show);
    layoutPlayer();  // the content area resized; reposition the floating player
}

void MainWindow::collapsePlayer() {
    // A finished video has nothing to keep playing — close it instead of leaving
    // a dead mini-player behind when the user navigates back.
    const bool mini =
        QSettings(apppaths::configFile(), QSettings::IniFormat)
            .value(QStringLiteral("playback/miniPlayer"), true).toBool();
    if (mini && !playbackEnded_) {
        setPlayerState(PlayerMini);
    } else {
        playerPage_->stop();
        setPlayerState(PlayerHidden);
    }
}

void MainWindow::layoutPlayer() {
    if (!content_ || playerState_ == PlayerHidden || !playerPage_->isVisible()) {
        return;
    }
    if (playerState_ == PlayerFull) {
        playerPage_->setGeometry(content_->rect());  // cover the whole content area
    } else {  // PlayerMini — a floating widget anchored bottom-right
        const int w = qMin(420, content_->width() - 24);
        // The video area is 16:9; the control strip sits below it, so the widget
        // is taller than 16:9 (otherwise the video gets pillarboxed).
        const int h = w * 9 / 16 + playerPage_->miniBarHeight();
        playerPage_->setGeometry(content_->width() - w - 16,
                                 content_->height() - h - 16, w, h);
    }
}
