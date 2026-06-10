// FreeFlume — reusable video list implementation.
#include "videolist.h"

#include <QDate>
#include <QDateTime>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include "metaenricher.h"

#include "database.h"
#include "downloadmenu.h"
#include "hoverreveal.h"
#include "listcontextmenu.h"
#include "playlistmenu.h"
#include "reorderlistwidget.h"
#include "sharemenu.h"
#include "thumbdecor.h"
#include "thumbnailloader.h"

namespace {
constexpr int kResultRole = Qt::UserRole + 1;

QString formatDuration(qint64 seconds) {
    if (seconds <= 0) {
        return QStringLiteral("LIVE");
    }
    const qint64 h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString formatViews(qint64 views) {
    if (views < 0) return {};
    auto compact = [](double v, const char* suffix) {
        return QStringLiteral("%1%2").arg(v, 0, 'f', v < 10 ? 1 : 0).arg(QLatin1String(suffix));
    };
    if (views >= 1'000'000'000) return compact(views / 1e9, "B views");
    if (views >= 1'000'000) return compact(views / 1e6, "M views");
    if (views >= 1'000) return compact(views / 1e3, "K views");
    return QStringLiteral("%1 views").arg(views);
}

QString relativeTime(qint64 secs) {
    const qint64 diff = QDateTime::currentSecsSinceEpoch() - secs;
    if (diff < 60) return QObject::tr("just now");
    struct Unit { qint64 s; const char* one; const char* many; };
    static const Unit units[] = {
        {31536000, QT_TRANSLATE_NOOP("rt", "%1 year ago"), QT_TRANSLATE_NOOP("rt", "%1 years ago")},
        {2592000, QT_TRANSLATE_NOOP("rt", "%1 month ago"), QT_TRANSLATE_NOOP("rt", "%1 months ago")},
        {604800, QT_TRANSLATE_NOOP("rt", "%1 week ago"), QT_TRANSLATE_NOOP("rt", "%1 weeks ago")},
        {86400, QT_TRANSLATE_NOOP("rt", "%1 day ago"), QT_TRANSLATE_NOOP("rt", "%1 days ago")},
        {3600, QT_TRANSLATE_NOOP("rt", "%1 hour ago"), QT_TRANSLATE_NOOP("rt", "%1 hours ago")},
        {60, QT_TRANSLATE_NOOP("rt", "%1 minute ago"), QT_TRANSLATE_NOOP("rt", "%1 minutes ago")},
    };
    for (const Unit& u : units) {
        if (diff >= u.s) {
            const qint64 n = diff / u.s;
            return QObject::tr(n == 1 ? u.one : u.many).arg(n);
        }
    }
    return QObject::tr("just now");
}

QString metaLine(const SearchResult& r, const QString& uploadDate = {}) {
    QStringList parts;
    if (!r.channel.isEmpty()) parts << r.channel;
    if (r.published > 0) {  // feed item: show how long ago instead of duration/views
        parts << relativeTime(r.published);
    } else {
        parts << (r.isLive ? QStringLiteral("LIVE") : formatDuration(r.durationSeconds));
        const QString views = formatViews(r.viewCount);
        if (!views.isEmpty()) parts << views;
        // Upload date (YYYYMMDD) once the background enricher has fetched it.
        const QDate d = QDate::fromString(uploadDate, QStringLiteral("yyyyMMdd"));
        if (d.isValid()) parts << d.toString(QStringLiteral("d MMM yyyy"));
    }
    return parts.join(QStringLiteral("  ·  "));
}
}  // namespace

VideoList::VideoList(ThumbnailLoader* thumbs, QWidget* parent)
    : QWidget(parent), thumbs_(thumbs) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    stack_ = new QStackedWidget(this);
    placeholder_ = new QLabel(this);
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);
    placeholder_->setEnabled(false);
    stack_->addWidget(placeholder_);  // 0

    list_ = new ReorderableListWidget(this);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSpacing(2);
    stack_->addWidget(list_);  // 1

    lay->addWidget(stack_);

    connect(list_, &ReorderableListWidget::itemMoved, this, [this](int from, int insertRow) {
        if (from < 0 || from >= data_.size()) {
            return;
        }
        int to = insertRow;
        if (from < to) {
            --to;  // removing the source shifts everything after it up by one
        }
        to = qBound(0, to, data_.size() - 1);
        if (to == from) {
            return;
        }
        data_.move(from, to);
        QStringList order;
        order.reserve(data_.size());
        for (const SearchResult& r : data_) {
            order << r.url;
        }
        // Defer so the drag machinery fully unwinds before the list rebuilds.
        QTimer::singleShot(0, this, [this, order] { emit reordered(order); });
    });

    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit activated(item->data(kResultRole).value<SearchResult>());
    });
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item) {
            emit currentChanged(item->data(kResultRole).value<SearchResult>());
        }
    });

    // One reliable right-click handler (filters ContextMenu events on the
    // viewport) — works on the first click over any row, selected or not.
    ListContextMenu::install(list_, [this](QListWidgetItem* item, const QPoint& globalPos) {
        showContextMenu(item->data(kResultRole).value<SearchResult>(), globalPos);
    });

    // Lazy upload-date enrichment: fetch dates only for rows scrolled into view
    // (debounced), and refresh the row's meta line when one arrives.
    dateScrollTimer_ = new QTimer(this);
    dateScrollTimer_->setSingleShot(true);
    dateScrollTimer_->setInterval(200);
    connect(dateScrollTimer_, &QTimer::timeout, this, &VideoList::enqueueVisibleDates);
    connect(list_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { dateScrollTimer_->start(); });
    connect(MetaEnricher::instance(), &MetaEnricher::uploadDateReady, this,
            [this](const QString& url, const QString& date) {
                QLabel* lbl = metaByUrl_.value(url);
                if (!lbl) {
                    return;
                }
                for (const SearchResult& r : data_) {
                    if (r.url == url) {
                        lbl->setText(metaLine(r, date));
                        break;
                    }
                }
                metaByUrl_.remove(url);
            });
}

void VideoList::showContextMenu(const SearchResult& r, const QPoint& globalPos) {
    QMenu menu(this);
    menu.addAction(tr("&Play"), [this, r] { emit activated(r); });
    playlistmenu::addSubmenu(&menu, db_, r, this);
    downloadmenu::addSubmenu(&menu, downloads_, r);
    menu.addSeparator();
    share::addActions(&menu, r.url);
    if (removable_) {
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme(QStringLiteral("list-remove")), removeLabel_,
                       [this, r] { emit removeRequested(r); });
    }
    menu.exec(globalPos);
}

void VideoList::setPlaceholder(const QString& text) {
    placeholder_->setText(text);
    stack_->setCurrentIndex(0);
}

void VideoList::setRemovable(bool on, const QString& label) {
    removable_ = on;
    removeLabel_ = label.isEmpty() ? tr("&Remove") : label;
}

void VideoList::setReorderable(bool on) {
    reorderable_ = on;
    list_->setDragEnabled(on);
    list_->setAcceptDrops(on);
    list_->setDropIndicatorShown(on);
    list_->setDragDropMode(on ? QAbstractItemView::InternalMove
                              : QAbstractItemView::NoDragDrop);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
}

SearchResult VideoList::currentItem() const {
    if (auto* item = list_->currentItem()) {
        return item->data(kResultRole).value<SearchResult>();
    }
    return {};
}

void VideoList::setCurrentRow(int row) {
    if (row >= 0 && row < list_->count()) {
        list_->setCurrentRow(row);
        list_->scrollToItem(list_->item(row));
    }
}

void VideoList::setItems(const QList<SearchResult>& items) {
    if (hoverGroup_) {
        hoverGroup_->clear();  // drop references to rows we're about to delete
    }
    data_ = items;
    metaByUrl_.clear();  // stale labels from the previous list
    list_->clear();
    if (items.isEmpty()) {
        setPlaceholder(placeholder_->text().isEmpty() ? tr("Nothing here yet.")
                                                       : placeholder_->text());
        return;
    }
    const QHash<QString, WatchProgress> progress = db_ ? db_->allProgress()
                                                       : QHash<QString, WatchProgress>{};

    for (const SearchResult& r : items) {
        auto* item = new QListWidgetItem(list_);
        item->setData(kResultRole, QVariant::fromValue(r));
        item->setSizeHint(QSize(0, 80));

        auto* row = new QWidget(list_);
        auto* hbox = new QHBoxLayout(row);
        hbox->setContentsMargins(8, 6, 10, 6);
        hbox->setSpacing(10);

        auto* thumb = new QLabel(row);
        thumb->setFixedSize(120, 68);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setStyleSheet(QStringLiteral("background:#000;border-radius:4px;"));
        if (!r.thumbnailUrl.isEmpty()) {
            const QString url = r.thumbnailUrl;
            const WatchProgress wp = progress.value(r.url);
            connect(thumbs_, &ThumbnailLoader::loaded, thumb,
                    [thumb, url, wp](const QString& u, const QPixmap& pm) {
                        if (u == url && !pm.isNull()) {
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
        // Upload date: show it if already cached; otherwise register this row so
        // the enricher can fill it in once it (lazily) fetches it. Only videos
        // (not feed rows, which already show "X ago", nor channels/playlists).
        const bool datable = r.kind == ResultKind::Video && r.published <= 0;
        const QString cachedDate = (datable && db_) ? db_->cachedUploadDate(r.url) : QString();
        auto* meta = new QLabel(metaLine(r, cachedDate), row);
        meta->setEnabled(false);
        if (datable && cachedDate.isEmpty()) {
            metaByUrl_.insert(r.url, meta);
        }
        col->addWidget(title);
        col->addWidget(meta);
        col->addStretch();

        hbox->addWidget(thumb);
        hbox->addLayout(col, 1);

        if (removable_) {
            auto* remove = new QToolButton(row);
            remove->setIcon(QIcon::fromTheme(QStringLiteral("list-remove")));
            remove->setAutoRaise(true);
            QString tip = removeLabel_;
            tip.remove(QLatin1Char('&'));
            remove->setToolTip(tip);
            connect(remove, &QToolButton::clicked, this,
                    [this, r] { emit removeRequested(r); });
            if (!hoverGroup_) {
                hoverGroup_ = new HoverRevealGroup(this);
            }
            hoverGroup_->add(row, remove);  // only show on hover
            hbox->addWidget(remove);
        }

        list_->setItemWidget(item, row);
    }
    stack_->setCurrentIndex(1);
    // Defer so the list has laid out and visualItemRect() is meaningful.
    QTimer::singleShot(0, this, &VideoList::enqueueVisibleDates);
}

void VideoList::enqueueVisibleDates() {
    if (metaByUrl_.isEmpty()) {
        return;
    }
    const QRect vp = list_->viewport()->rect();
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        if (!list_->visualItemRect(item).intersects(vp)) {
            continue;  // off-screen — fetch only what the user actually sees
        }
        const QString url = item->data(kResultRole).value<SearchResult>().url;
        if (metaByUrl_.contains(url)) {  // still awaiting a date
            MetaEnricher::instance()->requestUploadDate(url);
        }
    }
}
