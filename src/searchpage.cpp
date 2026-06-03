// FreeFlume — search results page implementation.
#include "apppaths.h"
#include "searchpage.h"

#include <algorithm>

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVariant>

#include "database.h"
#include "detailpane.h"
#include "downloadmenu.h"
#include "playlistmenu.h"
#include "sharemenu.h"
#include "thumbdecor.h"
#include "thumbnailloader.h"

namespace {

constexpr int kResultRole = Qt::UserRole + 1;

QString formatDuration(qint64 seconds) {
    if (seconds <= 0) {
        return QStringLiteral("LIVE");
    }
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString formatViews(qint64 views) {
    if (views < 0) {
        return {};
    }
    auto compact = [](double value, const char* suffix) {
        return QStringLiteral("%1%2")
            .arg(value, 0, 'f', value < 10 ? 1 : 0)
            .arg(QLatin1String(suffix));
    };
    if (views >= 1'000'000'000) {
        return compact(views / 1e9, "B views");
    }
    if (views >= 1'000'000) {
        return compact(views / 1e6, "M views");
    }
    if (views >= 1'000) {
        return compact(views / 1e3, "K views");
    }
    return QStringLiteral("%1 views").arg(views);
}

// Builds the subtitle line, e.g. "Channel · 12:34 · 1.2M views" for a video,
// or a "Channel"/"Playlist" badge for those result kinds.
QString metaLine(const SearchResult& r) {
    if (r.kind == ResultKind::Channel) {
        return QStringLiteral("Channel  ·  double-click to view videos");
    }
    if (r.kind == ResultKind::Playlist) {
        return QStringLiteral("Playlist  ·  double-click to view videos");
    }
    QStringList parts;
    if (!r.channel.isEmpty()) {
        parts << r.channel;
    }
    if (r.kind == ResultKind::Short) {
        parts << QStringLiteral("Short");
    } else {
        parts << (r.isLive ? QStringLiteral("LIVE") : formatDuration(r.durationSeconds));
    }
    const QString views = formatViews(r.viewCount);
    if (!views.isEmpty()) {
        parts << views;
    }
    return parts.join(QStringLiteral("  ·  "));
}

bool kindEnabled(ResultKind kind, const QSettings& s) {
    switch (kind) {
        case ResultKind::Short:
            return false;  // shorts are a poor fit for desktop — never shown
        case ResultKind::Channel:
            return s.value(QStringLiteral("search/includeChannels"), true).toBool();
        case ResultKind::Playlist:
            return s.value(QStringLiteral("search/includePlaylists"), true).toBool();
        case ResultKind::Video:
        default:
            return true;
    }
}

}  // namespace

SearchPage::SearchPage(Extractor* extractor, ThumbnailLoader* thumbs, Database* db,
                       QWidget* parent)
    : QWidget(parent), extractor_(extractor) {
    db_ = db;
    thumbs_ = thumbs;

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left: a back/context header, the results, then a pagination bar.
    auto* left = new QWidget(splitter);
    auto* leftCol = new QVBoxLayout(left);
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftCol->setSpacing(0);

    auto* header = new QWidget(left);
    auto* headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(8, 4, 8, 4);
    backButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")),
                                  tr("Bac&k"), header);
    backButton_->setFocusPolicy(Qt::NoFocus);
    backButton_->setVisible(false);
    contextLabel_ = new QLabel(header);
    contextLabel_->setEnabled(false);  // muted
    channelSearch_ = new QLineEdit(header);
    channelSearch_->setClearButtonEnabled(true);
    channelSearch_->setPlaceholderText(tr("Search this channel…"));
    channelSearch_->setMaximumWidth(220);
    channelSearch_->setVisible(false);
    headerRow->addWidget(backButton_);
    headerRow->addWidget(contextLabel_, /*stretch=*/1);
    headerRow->addWidget(channelSearch_);
    leftCol->addWidget(header);
    connect(backButton_, &QPushButton::clicked, this, &SearchPage::goBack);
    connect(channelSearch_, &QLineEdit::returnPressed, this,
            [this] { searchCurrentChannel(channelSearch_->text().trimmed()); });

    // Filter bar (YT-style): Upload date · Type · Duration · Sort by.
    filterBar_ = new QWidget(left);
    auto* filterRow = new QHBoxLayout(filterBar_);
    filterRow->setContentsMargins(8, 0, 8, 4);
    filterRow->setSpacing(6);
    dateFilter_ = new QComboBox(filterBar_);
    dateFilter_->addItem(tr("Any date"), 0);
    dateFilter_->addItem(tr("Last hour"), 1);
    dateFilter_->addItem(tr("Today"), 2);
    dateFilter_->addItem(tr("This week"), 3);
    dateFilter_->addItem(tr("This month"), 4);
    dateFilter_->addItem(tr("This year"), 5);
    typeFilter_ = new QComboBox(filterBar_);
    typeFilter_->addItem(tr("Any type"), 0);
    typeFilter_->addItem(tr("Video"), 1);
    typeFilter_->addItem(tr("Channel"), 2);
    typeFilter_->addItem(tr("Playlist"), 3);
    durationFilter_ = new QComboBox(filterBar_);
    durationFilter_->addItem(tr("Any length"), 0);
    durationFilter_->addItem(tr("Under 4 min"), 1);
    durationFilter_->addItem(tr("4–20 min"), 3);
    durationFilter_->addItem(tr("Over 20 min"), 2);
    sortFilter_ = new QComboBox(filterBar_);
    sortFilter_->addItem(tr("Relevance"), 0);
    sortFilter_->addItem(tr("Upload date"), 2);
    sortFilter_->addItem(tr("View count"), 3);
    sortFilter_->addItem(tr("Rating"), 1);
    for (QComboBox* c : {dateFilter_, typeFilter_, durationFilter_, sortFilter_}) {
        c->setFocusPolicy(Qt::NoFocus);
        filterRow->addWidget(c);
        connect(c, &QComboBox::currentIndexChanged, this, &SearchPage::onFilterChanged);
    }

    // Features dropdown (checkable): HD · 4K · Subtitles · Live.
    featuresButton_ = new QToolButton(filterBar_);
    featuresButton_->setText(tr("Features"));
    featuresButton_->setFocusPolicy(Qt::NoFocus);
    featuresButton_->setPopupMode(QToolButton::InstantPopup);
    auto* fMenu = new QMenu(featuresButton_);
    auto addFeature = [&](const QString& label) {
        QAction* a = fMenu->addAction(label);
        a->setCheckable(true);
        connect(a, &QAction::toggled, this, &SearchPage::onFilterChanged);
        return a;
    };
    featHd_ = addFeature(tr("HD"));
    featFourK_ = addFeature(tr("4K"));
    featSubs_ = addFeature(tr("Subtitles/CC"));
    featLive_ = addFeature(tr("Live"));
    featuresButton_->setMenu(fMenu);
    filterRow->addWidget(featuresButton_);

    filterRow->addStretch();
    leftCol->addWidget(filterBar_);

    stack_ = new QStackedWidget(left);

    message_ = new QLabel(stack_);
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    message_->setEnabled(false);  // muted, theme-aware
    stack_->addWidget(message_);  // index 0

    list_ = new QListWidget(stack_);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSpacing(2);
    list_->setUniformItemSizes(false);
    list_->setIconSize(QSize(120, 68));
    stack_->addWidget(list_);  // index 1
    leftCol->addWidget(stack_, /*stretch=*/1);

    // Pagination bar (hidden until there are results): Prev · 1 2 3 … · Next.
    pager_ = new QWidget(left);
    auto* pagerRow = new QHBoxLayout(pager_);
    pagerRow->setContentsMargins(8, 4, 8, 6);
    prevBtn_ = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")),
                               tr("Pre&vious"), pager_);
    prevBtn_->setFocusPolicy(Qt::NoFocus);
    nextBtn_ = new QPushButton(QIcon::fromTheme(QStringLiteral("go-next")),
                               tr("&Next"), pager_);
    nextBtn_->setFocusPolicy(Qt::NoFocus);
    numbersHost_ = new QWidget(pager_);
    numbersLayout_ = new QHBoxLayout(numbersHost_);
    numbersLayout_->setContentsMargins(0, 0, 0, 0);
    numbersLayout_->setSpacing(2);
    pagerRow->addWidget(prevBtn_);
    pagerRow->addStretch();
    pagerRow->addWidget(numbersHost_);
    pagerRow->addStretch();
    pagerRow->addWidget(nextBtn_);
    pager_->setVisible(false);
    leftCol->addWidget(pager_);

    splitter->addWidget(left);

    // Right: detail pane.
    detail_ = new DetailPane(thumbs_, db, splitter);
    splitter->addWidget(detail_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setCollapsible(1, true);

    lay->addWidget(splitter);

    showMessage(QStringLiteral("Search videos, channels and more across supported platforms."));

    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const SearchResult r = item->data(kResultRole).value<SearchResult>();
        if (r.url.isEmpty()) {
            return;
        }
        if (r.kind == ResultKind::Channel || r.kind == ResultKind::Playlist) {
            openChannel(r.url, r.title);  // drill in to this channel/playlist
        } else {
            playResult(r);
        }
    });
    connect(list_, &QListWidget::currentItemChanged, this, &SearchPage::onSelectionChanged);
    connect(detail_, &DetailPane::playRequested, this, &SearchPage::playRequested);
    connect(detail_, &DetailPane::channelRequested, this,
            [this](const QString& url) { openChannel(url); });

    // Per-row context menu is set in populate(); this is a view-level fallback.
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (QListWidgetItem* item = list_->itemAt(pos)) {
            showContextMenu(item->data(kResultRole).value<SearchResult>(),
                            list_->viewport()->mapToGlobal(pos));
        }
    });

    connect(prevBtn_, &QPushButton::clicked, this, [this] {
        if (currentPage_ > 1) {
            --currentPage_;
            fetchPage();
        }
    });
    connect(nextBtn_, &QPushButton::clicked, this, [this] {
        ++currentPage_;
        fetchPage();
    });

    connect(extractor_, &Extractor::searchStarted, this, [this](const QString& q) {
        detail_->clear();
        showMessage(isChannelSource_ ? QStringLiteral("Loading videos…")
                                     : QStringLiteral("Searching for \"%1\"…").arg(q));
    });
    connect(extractor_, &Extractor::searchTotalKnown, this,
            [this](int total) { totalItems_ = total; });
    connect(extractor_, &Extractor::searchFinished, this, &SearchPage::populate);
    connect(extractor_, &Extractor::searchFailed, this, [this](const QString& msg) {
        showMessage(QStringLiteral("Search failed:\n%1").arg(msg));
        emit statusMessage(QStringLiteral("Search failed"), 4000);
    });

    connect(extractor_, &Extractor::detailsFinished, this,
            [this](const VideoDetails& d) { detail_->setDetails(d); });
    connect(extractor_, &Extractor::detailsFailed, this,
            [this](const QString& msg) { detail_->showLoading(msg); });

    connect(extractor_, &Extractor::playlistItemsReady, this,
            [this](const QList<SearchResult>& items, const QString& url) {
                if (url != pendingPlaylistUrl_ || items.isEmpty()) {
                    return;
                }
                if (pendingPlaylistPlayAll_) {
                    pendingPlaylistPlayAll_ = false;
                    emit playQueueRequested(items, 0, pendingPlaylistTitle_);
                } else if (items.size() > 1) {
                    emit playlistResolved(items, pendingPlaylistTitle_);
                }
            });
}

void SearchPage::showContextMenu(const SearchResult& r, const QPoint& globalPos) {
    if (r.url.isEmpty()) {
        return;
    }
    QMenu menu(this);
    if (r.kind == ResultKind::Channel) {
        menu.addAction(tr("&Open Channel"), [this, r] { openChannel(r.url, r.title); });
        if (db_ && db_->isSubscribed(r.url)) {
            menu.addAction(tr("&Unsubscribe"), [this, r] { db_->unsubscribe(r.url); });
        } else if (db_) {
            menu.addAction(tr("&Subscribe"),
                           [this, r] { db_->subscribe(r.title, r.url, r.thumbnailUrl); });
        }
    } else if (r.kind == ResultKind::Playlist) {
        menu.addAction(tr("&Open Playlist"), [this, r] { openChannel(r.url, r.title); });
        menu.addAction(tr("&Play All"), [this, r] { playPlaylist(r); });
    } else {
        menu.addAction(tr("&Play"), [this, r] { playResult(r); });
        playlistmenu::addSubmenu(&menu, db_, r, this);
        downloadmenu::addSubmenu(&menu, downloads_, r);
    }
    menu.addSeparator();
    share::addActions(&menu, r.url);
    menu.exec(globalPos);
}

void SearchPage::playPlaylist(const SearchResult& playlist) {
    pendingPlaylistUrl_ = playlist.url;
    pendingPlaylistTitle_ = playlist.title.isEmpty() ? tr("Playlist") : playlist.title;
    pendingPlaylistPlayAll_ = true;
    emit statusMessage(tr("Loading playlist…"), 0);
    extractor_->fetchPlaylistItems(playlist.url);
}

void SearchPage::onSelectionChanged() {
    QListWidgetItem* item = list_->currentItem();
    if (!item) {
        return;
    }
    const SearchResult r = item->data(kResultRole).value<SearchResult>();
    if (r.url.isEmpty()) {
        return;
    }
    if (r.kind == ResultKind::Channel) {
        detail_->showChannel(r);  // shows avatar + an active Subscribe button
        return;
    }
    if (r.kind == ResultKind::Playlist) {
        detail_->showNote(r.title, tr("Playlist — double-click to view its videos."));
        return;
    }
    detail_->showLoading(r.title);
    extractor_->fetchDetails(r.url);
}

void SearchPage::clearResults() {
    list_->clear();
    detail_->clear();
    pager_->setVisible(false);
    backStack_.clear();
    currentQuery_.clear();
    currentChannelUrl_.clear();
    currentChannelQuery_.clear();
    currentLabel_.clear();
    isChannelSource_ = false;
    canPaginate_ = false;
    updateContextUi();
    showMessage(QStringLiteral("Search videos, channels and more across supported platforms."));
}

void SearchPage::startSearch(const QString& query) {
    NavState s;
    s.isChannel = false;
    s.query = query;
    s.label = tr("Results for \"%1\"").arg(query);
    s.filters = currentFilters_;  // keep the active filters across searches
    goToState(s, /*pushCurrent=*/true);
}

void SearchPage::openChannel(const QString& url, const QString& label) {
    NavState s;
    s.isChannel = true;
    s.channelUrl = url;
    s.label = label.isEmpty() ? tr("Channel videos") : label;
    goToState(s, /*pushCurrent=*/true);
}

void SearchPage::searchCurrentChannel(const QString& query) {
    if (!isCurrentChannel()) {
        return;
    }
    // Keep the channel name as the label; the query is shown in the context.
    NavState s;
    s.isChannel = true;
    s.channelUrl = currentChannelUrl_;
    s.channelQuery = query;
    s.label = currentLabel_;
    goToState(s, /*pushCurrent=*/true);
}

SearchPage::NavState SearchPage::captureCurrent() const {
    NavState s;
    s.isChannel = isChannelSource_;
    s.query = currentQuery_;
    s.channelUrl = currentChannelUrl_;
    s.channelQuery = currentChannelQuery_;
    s.label = currentLabel_;
    s.filters = currentFilters_;
    s.page = currentPage_;
    return s;
}

SearchFilters SearchPage::readFilters() const {
    SearchFilters f;
    f.uploadDate = dateFilter_->currentData().toInt();
    f.type = typeFilter_->currentData().toInt();
    f.duration = durationFilter_->currentData().toInt();
    f.sort = sortFilter_->currentData().toInt();
    f.hd = featHd_->isChecked();
    f.fourK = featFourK_->isChecked();
    f.subtitles = featSubs_->isChecked();
    f.live = featLive_->isChecked();
    return f;
}

void SearchPage::setFiltersUi(const SearchFilters& f) {
    auto set = [](QComboBox* c, int code) {
        const QSignalBlocker block(c);
        c->setCurrentIndex(qMax(0, c->findData(code)));
    };
    set(dateFilter_, f.uploadDate);
    set(typeFilter_, f.type);
    set(durationFilter_, f.duration);
    set(sortFilter_, f.sort);
    auto setAct = [](QAction* a, bool on) {
        const QSignalBlocker block(a);
        a->setChecked(on);
    };
    setAct(featHd_, f.hd);
    setAct(featFourK_, f.fourK);
    setAct(featSubs_, f.subtitles);
    setAct(featLive_, f.live);
    const int n = (f.hd ? 1 : 0) + (f.fourK ? 1 : 0) + (f.subtitles ? 1 : 0) + (f.live ? 1 : 0);
    featuresButton_->setText(n > 0 ? tr("Features (%1)").arg(n) : tr("Features"));
}

void SearchPage::onFilterChanged() {
    currentFilters_ = readFilters();
    // Re-run the active search with the new filters (refine in place).
    if (!isChannelSource_ && !currentQuery_.isEmpty()) {
        NavState s;
        s.query = currentQuery_;
        s.label = tr("Results for \"%1\"").arg(currentQuery_);
        s.filters = currentFilters_;
        goToState(s, /*pushCurrent=*/false);
    }
}

bool SearchPage::hasCurrentSource() const {
    return isChannelSource_ ? !currentChannelUrl_.isEmpty() : !currentQuery_.isEmpty();
}

bool SearchPage::isCurrentChannel() const {
    return isChannelSource_ && !currentChannelUrl_.isEmpty() &&
           !currentChannelUrl_.contains(QLatin1String("list=")) &&
           !currentChannelUrl_.contains(QLatin1String("/playlist"));
}

bool SearchPage::isCurrentPlaylist() const {
    return isChannelSource_ && !currentChannelUrl_.isEmpty() &&
           (currentChannelUrl_.contains(QLatin1String("list=")) ||
            currentChannelUrl_.contains(QLatin1String("/playlist")));
}

void SearchPage::playResult(const SearchResult& r) {
    // Inside a playlist view, play the whole (loaded) page as a queue so it
    // continues automatically; otherwise just play the single video.
    if (isCurrentPlaylist()) {
        QList<SearchResult> items;
        int index = 0;
        for (int i = 0; i < list_->count(); ++i) {
            const SearchResult it = list_->item(i)->data(kResultRole).value<SearchResult>();
            if (it.kind != ResultKind::Video && it.kind != ResultKind::Short) {
                continue;
            }
            if (it.url == r.url) {
                index = items.size();
            }
            items.push_back(it);
        }
        const QString title = currentLabel_.isEmpty() ? tr("Playlist") : currentLabel_;
        if (items.size() > 1) {
            emit playQueueRequested(items, index, title);
        } else {
            emit playRequested(r);
        }
        // Long playlists are paginated, so the visible page is only part of it.
        // Fetch the whole thing in the background and upgrade the queue when ready.
        pendingPlaylistUrl_ = currentChannelUrl_;
        pendingPlaylistTitle_ = title;
        extractor_->fetchPlaylistItems(currentChannelUrl_);
        return;
    }
    emit playRequested(r);
}

void SearchPage::goToState(const NavState& s, bool pushCurrent) {
    if (pushCurrent && hasCurrentSource()) {
        backStack_.push_back(captureCurrent());
    }
    isChannelSource_ = s.isChannel;
    currentQuery_ = s.query;
    currentChannelUrl_ = s.channelUrl;
    currentChannelQuery_ = s.channelQuery;
    currentLabel_ = s.label;
    currentFilters_ = s.filters;
    setFiltersUi(currentFilters_);
    currentPage_ = qMax(1, s.page);
    totalItems_ = 0;
    maxKnownPage_ = currentPage_;
    canPaginate_ = true;
    updateContextUi();
    fetchPage();
}

void SearchPage::goBack() {
    if (backStack_.isEmpty()) {
        return;
    }
    goToState(backStack_.takeLast(), /*pushCurrent=*/false);
}

void SearchPage::updateContextUi() {
    backButton_->setVisible(!backStack_.isEmpty());
    channelSearch_->setVisible(isCurrentChannel());
    filterBar_->setVisible(!isChannelSource_);  // filters apply to general search
    if (channelSearch_->text() != currentChannelQuery_) {
        channelSearch_->setText(currentChannelQuery_);
    }
    contextLabel_->setText(
        (isChannelSource_ && !currentChannelQuery_.isEmpty())
            ? tr("%1 · \"%2\"").arg(currentLabel_, currentChannelQuery_)
            : currentLabel_);
}

void SearchPage::goToPage(int page) {
    if (page < 1 || page == currentPage_) {
        return;
    }
    currentPage_ = page;
    fetchPage();
}

void SearchPage::fetchPage() {
    const QSettings s(apppaths::configFile(), QSettings::IniFormat);
    pageSize_ = qMax(1, s.value(QStringLiteral("search/limit"), 20).toInt());
    if (isChannelSource_) {
        if (currentChannelUrl_.isEmpty()) {
            return;
        }
        if (currentChannelQuery_.isEmpty()) {
            extractor_->fetchChannel(currentChannelUrl_, currentPage_, pageSize_);
        } else {
            extractor_->searchInChannel(currentChannelUrl_, currentChannelQuery_, currentPage_,
                                        pageSize_);
        }
        return;
    }
    if (currentQuery_.isEmpty()) {
        return;
    }
    const bool channels = s.value(QStringLiteral("search/includeChannels"), true).toBool();
    const bool playlists = s.value(QStringLiteral("search/includePlaylists"), true).toBool();
    // Only the richer (slower) results page returns channels/playlists/filters.
    extractor_->search(currentQuery_, currentPage_, pageSize_, channels || playlists,
                       currentFilters_);
}

void SearchPage::updatePager(int rawCount) {
    bool hasNext;
    int lastPage;
    if (totalItems_ > 0) {
        lastPage = (totalItems_ + pageSize_ - 1) / pageSize_;
        hasNext = currentPage_ < lastPage;
    } else {
        hasNext = rawCount >= pageSize_;
        maxKnownPage_ = qMax(maxKnownPage_, currentPage_ + (hasNext ? 1 : 0));
        lastPage = maxKnownPage_;
    }
    prevBtn_->setEnabled(currentPage_ > 1);
    nextBtn_->setEnabled(hasNext);
    rebuildPageNumbers(lastPage);
    pager_->setVisible(canPaginate_ && lastPage > 1);
}

void SearchPage::rebuildPageNumbers(int lastPage) {
    // Clear existing number buttons.
    while (QLayoutItem* it = numbersLayout_->takeAt(0)) {
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }

    // Pages to show: 1, the window around current, and the last — with gaps.
    QList<int> pages;
    auto add = [&](int p) {
        if (p >= 1 && p <= lastPage && !pages.contains(p)) {
            pages.push_back(p);
        }
    };
    add(1);
    for (int p = currentPage_ - 2; p <= currentPage_ + 2; ++p) {
        add(p);
    }
    add(lastPage);
    std::sort(pages.begin(), pages.end());

    // Upper bound for the "jump to page" dialog: the real last page for a
    // known total, otherwise open-ended (channels/search have no known end).
    const int inputMax = totalItems_ > 0 ? lastPage : 100000;

    int prev = 0;
    for (int p : pages) {
        if (prev && p > prev + 1) {
            // A clickable gap: type a specific page to jump to.
            auto* dots = new QPushButton(QStringLiteral("…"), numbersHost_);
            dots->setFlat(true);
            dots->setFocusPolicy(Qt::NoFocus);
            dots->setMaximumWidth(34);
            dots->setToolTip(tr("Go to page…"));
            connect(dots, &QPushButton::clicked, this, [this, inputMax] {
                bool ok = false;
                const int page = QInputDialog::getInt(this, tr("Go to Page"),
                                                      tr("Page number:"), currentPage_, 1,
                                                      inputMax, 1, &ok);
                if (ok) {
                    goToPage(page);
                }
            });
            numbersLayout_->addWidget(dots);
        }
        prev = p;
        auto* b = new QPushButton(QString::number(p), numbersHost_);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFlat(p != currentPage_);
        b->setEnabled(p != currentPage_);
        b->setMaximumWidth(44);
        const int target = p;
        connect(b, &QPushButton::clicked, this, [this, target] { goToPage(target); });
        numbersLayout_->addWidget(b);
    }
}

void SearchPage::showMessage(const QString& text) {
    message_->setText(text);
    stack_->setCurrentIndex(0);
}

void SearchPage::populate(const QList<SearchResult>& allResults) {
    list_->clear();

    // Update the pager from the raw (pre-filter) count so paging through a page
    // that's been fully filtered out still works.
    updatePager(allResults.size());

    // Filter by the include/exclude settings (videos are always shown). When an
    // explicit Type filter is active, the user asked for that kind — show it all.
    const QSettings s(apppaths::configFile(), QSettings::IniFormat);
    const bool typeFiltered = currentFilters_.type != 0;
    QList<SearchResult> results;
    for (const SearchResult& r : allResults) {
        if (typeFiltered || kindEnabled(r.kind, s)) {
            results.push_back(r);
        }
    }

    if (results.isEmpty()) {
        showMessage(!allResults.isEmpty() ? QStringLiteral("No matching results on this page.")
                                          : QStringLiteral("No results found."));
        emit statusMessage(QStringLiteral("No results"), 3000);
        return;
    }

    const QHash<QString, WatchProgress> progress = db_ ? db_->allProgress()
                                                       : QHash<QString, WatchProgress>{};
    for (const SearchResult& r : results) {
        auto* item = new QListWidgetItem(list_);
        item->setData(kResultRole, QVariant::fromValue(r));
        item->setSizeHint(QSize(0, 80));

        auto* row = new QWidget(list_);
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(row, &QWidget::customContextMenuRequested, this, [this, r, row](const QPoint& p) {
            showContextMenu(r, row->mapToGlobal(p));
        });
        auto* hbox = new QHBoxLayout(row);
        hbox->setContentsMargins(8, 6, 10, 6);
        hbox->setSpacing(10);

        // Thumbnail: 16:9 for videos/playlists; a centred square avatar for
        // channels (their artwork is square, not a wide frame).
        const bool isChannel = r.kind == ResultKind::Channel;
        auto* thumb = new QLabel(row);
        thumb->setFixedSize(120, 68);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setStyleSheet(isChannel ? QStringLiteral("background:transparent;")
                                       : QStringLiteral("background:#000;border-radius:4px;"));
        if (!r.thumbnailUrl.isEmpty()) {
            const QString url = r.thumbnailUrl;
            const WatchProgress wp = progress.value(r.url);
            connect(thumbs_, &ThumbnailLoader::loaded, thumb,
                    [thumb, url, isChannel, wp](const QString& u, const QPixmap& pm) {
                        if (u != url || pm.isNull()) {
                            return;
                        }
                        if (isChannel) {
                            thumb->setPixmap(pm.scaled(QSize(64, 64), Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation));
                        } else {
                            thumb->setPixmap(thumbdecor::apply(pm, thumb->size(), wp));
                        }
                    });
            thumbs_->request(url);
        }

        auto* col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);

        auto* title = new QLabel(r.title, row);
        title->setWordWrap(true);
        QFont tf = title->font();
        tf.setBold(true);
        title->setFont(tf);
        title->setTextInteractionFlags(Qt::NoTextInteraction);

        auto* meta = new QLabel(metaLine(r), row);
        meta->setEnabled(false);  // muted

        col->addWidget(title);
        col->addWidget(meta);
        col->addStretch();

        hbox->addWidget(thumb);
        hbox->addLayout(col, /*stretch=*/1);
        list_->setItemWidget(item, row);
    }

    stack_->setCurrentIndex(1);
    // Auto-select the first result and load its details. We call
    // onSelectionChanged() explicitly because QListWidget may already have
    // made row 0 current on insert, in which case setCurrentRow emits nothing.
    list_->setCurrentRow(0);
    onSelectionChanged();
    emit statusMessage(QStringLiteral("%1 results").arg(results.size()), 3000);
}
