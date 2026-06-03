// FreeFlume — subscriptions page implementation.
#include "subscriptionspage.h"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMenu>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include "database.h"
#include "hoverreveal.h"
#include "listcontextmenu.h"
#include "sharemenu.h"
#include "subscriptionfeed.h"
#include "thumbnailloader.h"
#include "videolist.h"

namespace {
constexpr int kUrlRole = Qt::UserRole + 1;
constexpr int kFeedRole = Qt::UserRole + 2;  // marks the "What's New" row

// Parses a subscriptions export, auto-detecting the format: FreeFlume JSON,
// NewPipe JSON, YT Data-API JSON, FreeTube .db (NDJSON), Google Takeout
// CSV, OPML, or a plain list of channel URLs.
QList<Subscription> parseSubscriptions(const QByteArray& data) {
    QList<Subscription> out;
    auto add = [&](QString name, QString url, QString avatar) {
        url = url.trimmed();
        if (url.isEmpty()) {
            return;
        }
        if (url.startsWith(QLatin1String("UC")) && !url.contains(QLatin1Char('/'))) {
            url = QStringLiteral("https://www.youtube.com/channel/") + url;  // bare channel id
        }
        out.push_back(Subscription{-1, name.trimmed(), url, avatar.trimmed()});
    };

    const QByteArray trimmed = data.trimmed();

    // ---- OPML / XML ----
    if (trimmed.startsWith('<')) {
        QXmlStreamReader xml(data);
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement &&
                xml.name().toString().compare(QLatin1String("outline"), Qt::CaseInsensitive) == 0) {
                const auto a = xml.attributes();
                const QString feed = a.value(QLatin1String("xmlUrl")).toString();
                QString name = a.value(QLatin1String("text")).toString();
                if (name.isEmpty()) {
                    name = a.value(QLatin1String("title")).toString();
                }
                if (!feed.isEmpty()) {
                    const QString cid =
                        QUrlQuery(QUrl(feed)).queryItemValue(QStringLiteral("channel_id"));
                    if (!cid.isEmpty()) {
                        add(name, cid, QString());
                    } else if (feed.contains(QLatin1String("/channel/"))) {
                        add(name, feed, QString());
                    }
                }
            }
        }
        return out;
    }

    // ---- Single JSON document ----
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error == QJsonParseError::NoError) {
        if (doc.isObject() && doc.object().contains(QLatin1String("subscriptions"))) {
            // NewPipe (url+name) or a single FreeTube profile (id+name+thumbnail).
            for (const QJsonValue& v : doc.object().value(QLatin1String("subscriptions")).toArray()) {
                const QJsonObject o = v.toObject();
                const QString url = o.value(QLatin1String("url")).toString();
                add(o.value(QLatin1String("name")).toString(),
                    url.isEmpty() ? o.value(QLatin1String("id")).toString() : url,
                    o.value(QLatin1String("thumbnail")).toString());
            }
        } else if (doc.isArray()) {
            for (const QJsonValue& v : doc.array()) {
                const QJsonObject o = v.toObject();
                if (o.contains(QLatin1String("snippet"))) {  // YT Data API
                    const QJsonObject sn = o.value(QLatin1String("snippet")).toObject();
                    QString cid = sn.value(QLatin1String("resourceId"))
                                      .toObject()
                                      .value(QLatin1String("channelId"))
                                      .toString();
                    if (cid.isEmpty()) {
                        cid = sn.value(QLatin1String("channelId")).toString();
                    }
                    add(sn.value(QLatin1String("title")).toString(), cid,
                        sn.value(QLatin1String("thumbnails"))
                            .toObject()
                            .value(QLatin1String("default"))
                            .toObject()
                            .value(QLatin1String("url"))
                            .toString());
                } else {  // FreeFlume's own format
                    add(o.value(QLatin1String("name")).toString(),
                        o.value(QLatin1String("url")).toString(),
                        o.value(QLatin1String("avatar")).toString());
                }
            }
        }
        if (!out.isEmpty()) {
            return out;
        }
    }

    // ---- NDJSON (FreeTube .db: one profile object per line) ----
    bool ndjson = false;
    for (const QByteArray& line : data.split('\n')) {
        const QJsonDocument ld = QJsonDocument::fromJson(line.trimmed());
        if (!ld.isObject()) {
            continue;
        }
        for (const QJsonValue& v : ld.object().value(QLatin1String("subscriptions")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString url = o.value(QLatin1String("url")).toString();
            add(o.value(QLatin1String("name")).toString(),
                url.isEmpty() ? o.value(QLatin1String("id")).toString() : url,
                o.value(QLatin1String("thumbnail")).toString());
            ndjson = true;
        }
    }
    if (ndjson) {
        return out;
    }

    // ---- CSV (Google Takeout: Channel Id, Channel Url, Channel Title) ----
    if (data.contains(',')) {
        for (const QByteArray& lb : data.split('\n')) {
            const QString line = QString::fromUtf8(lb).trimmed();
            if (line.isEmpty() ||
                line.startsWith(QLatin1String("Channel Id"), Qt::CaseInsensitive)) {
                continue;
            }
            const QStringList cols = line.split(QLatin1Char(','));
            if (cols.size() >= 3) {
                const QString url = cols[1].trimmed();
                add(QStringList(cols.mid(2)).join(QLatin1Char(',')),
                    url.isEmpty() ? cols[0].trimmed() : url, QString());
            }
        }
        if (!out.isEmpty()) {
            return out;
        }
    }

    // ---- Plain list of channel URLs ----
    for (const QByteArray& lb : data.split('\n')) {
        const QString url = QString::fromUtf8(lb).trimmed();
        if (url.startsWith(QLatin1String("http"))) {
            add(QString(), url, QString());
        }
    }
    return out;
}
}  // namespace

SubscriptionsPage::SubscriptionsPage(Database* db, ThumbnailLoader* thumbs, QWidget* parent)
    : QWidget(parent), db_(db), extractor_(new Extractor(this)), thumbs_(thumbs) {
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left column: channel list (avatar · name · unsubscribe) + import/export.
    auto* left = new QWidget(splitter);
    auto* leftCol = new QVBoxLayout(left);
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftCol->setSpacing(0);

    channels_ = new QListWidget(left);
    channels_->setFrameShape(QFrame::NoFrame);
    leftCol->addWidget(channels_, 1);

    // One reliable right-click handler for the channel rows (filters ContextMenu
    // events on the viewport) — fires on the first click, selected or not.
    ListContextMenu::install(channels_, [this](QListWidgetItem* item, const QPoint& globalPos) {
        const QString chUrl = item->data(kUrlRole).toString();
        if (chUrl.isEmpty()) {  // the "What's New" pseudo-row has no channel
            return;
        }
        QMenu menu(this);
        menu.addAction(tr("&Open Channel"), [this, chUrl] { emit channelRequested(chUrl); });
        menu.addAction(QIcon::fromTheme(QStringLiteral("list-remove")), tr("&Unsubscribe"),
                       [this, chUrl] { db_->unsubscribe(chUrl); });
        menu.addSeparator();
        share::addActions(&menu, chUrl);
        menu.exec(globalPos);
    });

    auto* ioRow = new QHBoxLayout;
    ioRow->setContentsMargins(6, 6, 6, 6);
    auto* importBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-import")),
                                      tr("&Import"), left);
    auto* exportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-export")),
                                      tr("&Export"), left);
    ioRow->addWidget(importBtn);
    ioRow->addWidget(exportBtn);
    leftCol->addLayout(ioRow);

    left->setMinimumWidth(220);
    left->setMaximumWidth(360);
    splitter->addWidget(left);

    videos_ = new VideoList(thumbs, splitter);
    videos_->setPlaceholder(tr("Select a channel to see its latest videos."));
    videos_->setDatabase(db_);
    splitter->addWidget(videos_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    lay->addWidget(splitter);

    connect(channels_, &QListWidget::currentItemChanged, this,
            [this] { loadCurrentChannel(); });
    connect(videos_, &VideoList::activated, this, &SubscriptionsPage::playRequested);
    connect(importBtn, &QPushButton::clicked, this, &SubscriptionsPage::importSubscriptions);
    connect(exportBtn, &QPushButton::clicked, this, &SubscriptionsPage::exportSubscriptions);

    connect(extractor_, &Extractor::searchStarted, this,
            [this] { videos_->setPlaceholder(tr("Loading channel videos…")); });
    connect(extractor_, &Extractor::searchFinished, this,
            [this](const QList<SearchResult>& items) {
                videos_->setPlaceholder(tr("No videos found for this channel."));
                videos_->setItems(items);
            });
    connect(extractor_, &Extractor::searchFailed, this,
            [this](const QString&) { videos_->setPlaceholder(tr("Could not load channel.")); });

    feed_ = new SubscriptionFeed(db_, this);
    connect(feed_, &SubscriptionFeed::ready, this, [this](const QList<SearchResult>& items) {
        // Ignore late results if the user has switched to a channel since.
        QListWidgetItem* cur = channels_->currentItem();
        if (!cur || !cur->data(kFeedRole).toBool()) {
            return;
        }
        videos_->setPlaceholder(tr("No recent uploads from your subscriptions."));
        videos_->setItems(items);
    });

    connect(db_, &Database::subscriptionsChanged, this, &SubscriptionsPage::refresh);
}

void SubscriptionsPage::setDownloadManager(DownloadManager* m) {
    videos_->setDownloadManager(m);
}

void SubscriptionsPage::refresh() {
    const QString prevUrl =
        channels_->currentItem() ? channels_->currentItem()->data(kUrlRole).toString()
                                 : QString();
    if (!hoverGroup_) {
        hoverGroup_ = new HoverRevealGroup(this);
    }
    hoverGroup_->clear();  // drop references to the rows we're about to delete
    channels_->clear();
    const QList<Subscription> subs = db_->subscriptions();

    if (!subs.isEmpty()) {  // "What's New" pseudo-row at the top
        auto* feedItem = new QListWidgetItem(channels_);
        feedItem->setData(kFeedRole, true);
        feedItem->setSizeHint(QSize(0, 46));
        auto* row = new QWidget(channels_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(6, 4, 4, 4);
        h->setSpacing(8);
        auto* icon = new QLabel(row);
        icon->setPixmap(QIcon::fromTheme(QStringLiteral("view-calendar-upcoming-days"),
                                         QIcon::fromTheme(QStringLiteral("rss")))
                            .pixmap(26, 26));
        auto* label = new QLabel(tr("What's New"), row);
        QFont f = label->font();
        f.setBold(true);
        label->setFont(f);
        h->addWidget(icon);
        h->addWidget(label, 1);
        channels_->setItemWidget(feedItem, row);
    }

    for (const Subscription& s : subs) {
        auto* item = new QListWidgetItem(channels_);
        item->setData(kUrlRole, s.channelUrl);
        item->setSizeHint(QSize(0, 46));
        const QString chUrl = s.channelUrl;

        auto* row = new QWidget(channels_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(6, 4, 4, 4);
        h->setSpacing(8);

        // Square channel avatar (placeholder until it loads / when unknown).
        auto* avatar = new QLabel(row);
        avatar->setFixedSize(36, 36);
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setStyleSheet(QStringLiteral("background:#2b2b2b;border-radius:3px;"));
        avatar->setPixmap(QIcon::fromTheme(QStringLiteral("user-identity"),
                                           QIcon::fromTheme(QStringLiteral("system-users")))
                              .pixmap(22, 22));
        if (!s.avatarUrl.isEmpty()) {
            const QString url = s.avatarUrl;
            connect(thumbs_, &ThumbnailLoader::loaded, avatar,
                    [avatar, url](const QString& u, const QPixmap& pm) {
                        if (u == url && !pm.isNull()) {
                            avatar->setPixmap(pm.scaled(avatar->size(),
                                                        Qt::KeepAspectRatioByExpanding,
                                                        Qt::SmoothTransformation));
                        }
                    });
            thumbs_->request(url);
        }

        auto* name = new QLabel(s.channelName.isEmpty() ? s.channelUrl : s.channelName, row);
        name->setToolTip(s.channelUrl);

        auto* unsub = new QToolButton(row);
        unsub->setIcon(QIcon::fromTheme(QStringLiteral("list-remove")));
        unsub->setAutoRaise(true);
        unsub->setToolTip(tr("Unsubscribe"));
        connect(unsub, &QToolButton::clicked, this, [this, chUrl] { db_->unsubscribe(chUrl); });
        hoverGroup_->add(row, unsub);  // only show the X while the row is hovered

        h->addWidget(avatar);
        h->addWidget(name, 1);
        h->addWidget(unsub);
        channels_->setItemWidget(item, row);

        if (s.channelUrl == prevUrl) {
            channels_->setCurrentItem(item);
        }
    }

    if (subs.isEmpty()) {
        videos_->setPlaceholder(
            tr("No subscriptions yet.\nOpen a video and click Subscribe."));
        videos_->setItems({});
    } else if (!channels_->currentItem()) {
        channels_->setCurrentRow(0);
    }
}

void SubscriptionsPage::loadCurrentChannel() {
    QListWidgetItem* item = channels_->currentItem();
    if (!item) {
        return;
    }
    if (item->data(kFeedRole).toBool()) {
        loadFeed();
        return;
    }
    const QString url = item->data(kUrlRole).toString();
    if (!url.isEmpty()) {
        extractor_->fetchChannel(url);
    }
}

void SubscriptionsPage::loadFeed() {
    videos_->setPlaceholder(tr("Loading the latest uploads from your subscriptions…"));
    videos_->setItems({});
    feed_->refresh(db_->subscriptions());
}

void SubscriptionsPage::exportSubscriptions() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Subscriptions"), QStringLiteral("subscriptions.json"),
        tr("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QJsonArray arr;
    for (const Subscription& s : db_->subscriptions()) {
        arr.append(QJsonObject{{QStringLiteral("name"), s.channelName},
                               {QStringLiteral("url"), s.channelUrl},
                               {QStringLiteral("avatar"), s.avatarUrl}});
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        f.close();
    } else {
        QMessageBox::warning(this, tr("Export"), tr("Could not write to that file."));
    }
}

void SubscriptionsPage::importSubscriptions() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Subscriptions"), QString(),
        tr("Subscriptions (*.json *.db *.csv *.opml *.xml *.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import"), tr("Could not open that file."));
        return;
    }
    const QList<Subscription> parsed = parseSubscriptions(f.readAll());

    // Avoid a refresh per insert during a bulk import.
    disconnect(db_, &Database::subscriptionsChanged, this, &SubscriptionsPage::refresh);
    int count = 0;
    for (const Subscription& s : parsed) {
        db_->subscribe(s.channelName, s.channelUrl, s.avatarUrl);
        ++count;
    }
    connect(db_, &Database::subscriptionsChanged, this, &SubscriptionsPage::refresh);
    refresh();

    if (count > 0) {
        QMessageBox::information(this, tr("Import"),
                                tr("Imported %1 subscription(s).").arg(count));
    } else {
        QMessageBox::warning(
            this, tr("Import"),
            tr("No subscriptions found. Supported: FreeFlume/NewPipe/YT JSON, "
               "FreeTube .db, Takeout CSV, OPML, or a list of channel URLs."));
    }
}
