// FreeFlume — video detail pane implementation.
#include "detailpane.h"

#include <QDate>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

#include "database.h"
#include "playlistmenu.h"
#include "htmlutil.h"
#include "thumbnailloader.h"

namespace {

QString formatViews(qint64 n, const QString& noun) {
    if (n < 0) {
        return {};
    }
    auto compact = [&](double v, const char* suffix) {
        return QStringLiteral("%1%2 %3")
            .arg(v, 0, 'f', v < 10 ? 1 : 0)
            .arg(QLatin1String(suffix), noun);
    };
    if (n >= 1'000'000'000) return compact(n / 1e9, "B");
    if (n >= 1'000'000) return compact(n / 1e6, "M");
    if (n >= 1'000) return compact(n / 1e3, "K");
    return QStringLiteral("%1 %2").arg(n).arg(noun);
}

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

// "20080520" -> "20 May 2008" (matches the video/search lists).
QString formatDate(const QString& yyyymmdd) {
    const QDate d = QDate::fromString(yyyymmdd, QStringLiteral("yyyyMMdd"));
    return d.isValid() ? d.toString(QStringLiteral("d MMM yyyy")) : QString();
}

}  // namespace

DetailPane::DetailPane(ThumbnailLoader* thumbs, Database* db, QWidget* parent)
    : QWidget(parent), thumbs_(thumbs), db_(db) {
    setMinimumWidth(300);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 14, 14, 14);
    col->setSpacing(8);

    thumb_ = new QLabel(this);
    thumb_->setFixedSize(320, 180);
    thumb_->setScaledContents(false);
    thumb_->setAlignment(Qt::AlignCenter);
    thumb_->setStyleSheet(QStringLiteral("background:#000;border-radius:6px;"));

    title_ = new QLabel(this);
    title_->setWordWrap(true);
    QFont tf = title_->font();
    tf.setPointSize(tf.pointSize() + 3);
    tf.setBold(true);
    title_->setFont(tf);

    channel_ = new QLabel(this);
    channel_->setTextFormat(Qt::RichText);
    channel_->setOpenExternalLinks(false);
    connect(channel_, &QLabel::linkActivated, this, [this](const QString&) {
        if (!current_.channelUrl.isEmpty()) {
            emit channelRequested(current_.channelUrl);
        }
    });
    meta_ = new QLabel(this);
    meta_->setEnabled(false);
    meta_->setWordWrap(true);

    playButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")),
                                  tr("&Play"), this);
    subscribeButton_ = new QPushButton(this);
    addButton_ = new QToolButton(this);
    addButton_->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    addButton_->setToolTip(tr("Add to playlist"));
    addButton_->setPopupMode(QToolButton::InstantPopup);
    addButton_->setMenu(new QMenu(addButton_));

    auto* actions = new QWidget(this);
    auto* actionRow = new QHBoxLayout(actions);
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addWidget(playButton_, 1);
    actionRow->addWidget(subscribeButton_);
    actionRow->addWidget(addButton_);

    description_ = new QTextBrowser(this);
    description_->setOpenExternalLinks(true);
    description_->setFrameShape(QFrame::NoFrame);

    col->addWidget(thumb_, 0, Qt::AlignHCenter);
    col->addWidget(title_);
    col->addWidget(channel_);
    col->addWidget(meta_);
    col->addWidget(actions);
    col->addWidget(description_, /*stretch=*/1);

    connect(playButton_, &QPushButton::clicked, this, [this] {
        if (!current_.url.isEmpty()) {
            emit playRequested(currentAsResult());
        }
    });

    connect(subscribeButton_, &QPushButton::clicked, this, [this] {
        if (current_.channelUrl.isEmpty()) {
            return;
        }
        if (db_->isSubscribed(current_.channelUrl)) {
            db_->unsubscribe(current_.channelUrl);
        } else {
            db_->subscribe(current_.channel, current_.channelUrl,
                           currentIsChannel_ ? current_.thumbnailUrl : QString());
        }
        updateSubscribeButton();
    });

    // Build the "Add to playlist" menu fresh each time it opens.
    connect(addButton_->menu(), &QMenu::aboutToShow, this, [this] {
        if (current_.url.isEmpty()) {
            addButton_->menu()->clear();
            return;
        }
        playlistmenu::populate(addButton_->menu(), db_, currentAsResult(), this);
    });

    connect(thumbs_, &ThumbnailLoader::loaded, this,
            [this](const QString& url, const QPixmap& pm) {
                if (url == pendingThumbUrl_ && !pm.isNull()) {
                    thumb_->setPixmap(pm.scaled(thumb_->size(), Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
                }
            });

    clear();
}

SearchResult DetailPane::currentAsResult() const {
    SearchResult r;
    r.url = current_.url;
    r.title = current_.title;
    r.channel = current_.channel;
    r.durationSeconds = current_.durationSeconds;
    r.thumbnailUrl = current_.thumbnailUrl;
    return r;
}

void DetailPane::updateSubscribeButton() {
    const bool hasChannel = !current_.channelUrl.isEmpty();
    subscribeButton_->setEnabled(hasChannel);
    const bool subscribed = hasChannel && db_->isSubscribed(current_.channelUrl);
    subscribeButton_->setText(subscribed ? tr("&Subscribed") : tr("&Subscribe"));
    subscribeButton_->setIcon(QIcon::fromTheme(
        subscribed ? QStringLiteral("starred") : QStringLiteral("non-starred")));
}

void DetailPane::clear() {
    current_ = {};
    currentIsChannel_ = false;
    pendingThumbUrl_.clear();
    thumb_->clear();
    title_->clear();
    channel_->clear();
    meta_->clear();
    description_->clear();
    playButton_->setEnabled(false);
    subscribeButton_->setEnabled(false);
    subscribeButton_->setText(tr("&Subscribe"));
    addButton_->setEnabled(false);
    title_->setText(tr("Select a video to see details."));
    title_->setEnabled(false);
}

void DetailPane::showLoading(const QString& title) {
    current_ = {};
    thumb_->setFixedSize(320, 180);  // 16:9 frame for a video
    title_->setEnabled(true);
    title_->setText(title.isEmpty() ? tr("Loading…") : title);
    channel_->clear();
    meta_->setText(tr("Loading details…"));
    description_->clear();
    thumb_->clear();
    playButton_->setEnabled(false);
    subscribeButton_->setEnabled(false);
    addButton_->setEnabled(false);
}

void DetailPane::showNote(const QString& title, const QString& note) {
    current_ = {};
    thumb_->setFixedSize(320, 180);
    currentIsChannel_ = false;
    pendingThumbUrl_.clear();
    thumb_->clear();
    title_->setEnabled(true);
    title_->setText(title);
    channel_->clear();
    meta_->setText(note);
    description_->clear();
    playButton_->setEnabled(false);
    subscribeButton_->setEnabled(false);
    addButton_->setEnabled(false);
}

void DetailPane::showChannel(const SearchResult& c) {
    current_ = {};
    thumb_->setFixedSize(200, 200);  // square frame for a channel avatar
    currentIsChannel_ = true;
    current_.channel = c.title;     // a channel result's title is its name
    current_.channelUrl = c.url;    // and its url is the channel page
    current_.thumbnailUrl = c.thumbnailUrl;

    title_->setEnabled(true);
    title_->setText(c.title);
    channel_->clear();
    meta_->setText(tr("Channel — double-click to view its videos."));
    description_->clear();
    playButton_->setEnabled(false);
    addButton_->setEnabled(false);
    updateSubscribeButton();  // enables Subscribe for this channel

    pendingThumbUrl_ = c.thumbnailUrl;
    thumb_->clear();
    if (!c.thumbnailUrl.isEmpty()) {
        thumbs_->request(c.thumbnailUrl);
    }
}

void DetailPane::setDetails(const VideoDetails& d) {
    current_ = d;
    currentIsChannel_ = false;
    title_->setEnabled(true);
    title_->setText(d.title);
    // Channel name links to the channel when we know its URL.
    if (d.channel.isEmpty()) {
        channel_->clear();
    } else if (d.channelUrl.isEmpty()) {
        channel_->setText(d.channel.toHtmlEscaped());
    } else {
        channel_->setText(QStringLiteral("<a href=\"#\">%1</a>").arg(d.channel.toHtmlEscaped()));
    }

    QStringList parts;
    if (d.viewCount >= 0) parts << formatViews(d.viewCount, tr("views"));
    if (d.likeCount >= 0) parts << formatViews(d.likeCount, tr("likes"));
    parts << formatDuration(d.durationSeconds);
    const QString date = formatDate(d.uploadDate);
    if (!date.isEmpty()) parts << date;
    meta_->setText(parts.join(QStringLiteral("  ·  ")));

    description_->setHtml(htmlutil::linkify(d.description));
    playButton_->setEnabled(!d.url.isEmpty());
    addButton_->setEnabled(!d.url.isEmpty());
    updateSubscribeButton();

    pendingThumbUrl_ = d.thumbnailUrl;
    if (!d.thumbnailUrl.isEmpty()) {
        thumbs_->request(d.thumbnailUrl);
    }
}
