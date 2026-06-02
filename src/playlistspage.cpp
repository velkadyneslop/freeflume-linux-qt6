// FreeFlume — local playlists page implementation.
#include "playlistspage.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "database.h"
#include "videolist.h"

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kNameRole = Qt::UserRole + 2;
}

PlaylistsPage::PlaylistsPage(Database* db, ThumbnailLoader* thumbs, QWidget* parent)
    : QWidget(parent), db_(db) {
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left column: playlists list + New / Delete buttons.
    auto* left = new QWidget(splitter);
    auto* leftCol = new QVBoxLayout(left);
    leftCol->setContentsMargins(0, 0, 0, 0);

    playlists_ = new QListWidget(left);
    leftCol->addWidget(playlists_, 1);

    auto* btnRow = new QWidget(left);
    auto* btnLay = new QHBoxLayout(btnRow);
    btnLay->setContentsMargins(6, 6, 6, 6);
    auto* newBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                                   tr("&New"), btnRow);
    auto* delBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                   tr("&Delete"), btnRow);
    btnLay->addWidget(newBtn);
    btnLay->addWidget(delBtn);
    leftCol->addWidget(btnRow);

    left->setMinimumWidth(180);
    left->setMaximumWidth(320);
    splitter->addWidget(left);

    items_ = new VideoList(thumbs, splitter);
    items_->setPlaceholder(tr("Select a playlist to see its videos.\n"
                              "Drag to reorder; hover a video to remove it."));
    items_->setDatabase(db_);
    items_->setRemovable(true, tr("&Remove from Playlist"));
    items_->setReorderable(true);
    splitter->addWidget(items_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    lay->addWidget(splitter);

    connect(playlists_, &QListWidget::currentItemChanged, this,
            [this] { loadCurrentPlaylist(); });
    connect(items_, &VideoList::activated, this, [this](const SearchResult& r) {
        for (int i = 0; i < currentItems_.size(); ++i) {
            if (currentItems_[i].url == r.url) {
                emit playQueueRequested(currentItems_, i, currentName_);
                return;
            }
        }
        emit playRequested(r);  // fallback (shouldn't normally happen)
    });
    connect(items_, &VideoList::removeRequested, this, [this](const SearchResult& r) {
        const qint64 id = currentPlaylistId();
        if (id >= 0) {
            db_->removeFromPlaylist(id, r.url);
        }
    });
    connect(items_, &VideoList::reordered, this, [this](const QStringList& urls) {
        const qint64 id = currentPlaylistId();
        if (id >= 0) {
            db_->reorderPlaylist(id, urls);
        }
    });

    connect(newBtn, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("New Playlist"), tr("Playlist name:"), QLineEdit::Normal,
            tr("My Playlist"), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            db_->createPlaylist(name.trimmed());
        }
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        const qint64 id = currentPlaylistId();
        if (id < 0) {
            return;
        }
        QListWidgetItem* item = playlists_->currentItem();
        const QString name = item ? item->text() : tr("this playlist");
        const auto reply = QMessageBox::question(
            this, tr("Delete Playlist"),
            tr("Delete \"%1\"? This cannot be undone.").arg(name));
        if (reply == QMessageBox::Yes) {
            db_->deletePlaylist(id);
        }
    });

    connect(db_, &Database::playlistsChanged, this, &PlaylistsPage::refresh);
}

void PlaylistsPage::setDownloadManager(DownloadManager* m) {
    items_->setDownloadManager(m);
}

qint64 PlaylistsPage::currentPlaylistId() const {
    if (auto* item = playlists_->currentItem()) {
        return item->data(kIdRole).toLongLong();
    }
    return -1;
}

void PlaylistsPage::refresh() {
    const qint64 prev = currentPlaylistId();
    playlists_->clear();
    for (const Playlist& p : db_->playlists()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  (%2)").arg(p.name).arg(p.itemCount), playlists_);
        item->setData(kIdRole, p.id);
        item->setData(kNameRole, p.name);
        if (p.id == prev) {
            playlists_->setCurrentItem(item);
        }
    }
    if (!playlists_->currentItem() && playlists_->count() > 0) {
        playlists_->setCurrentRow(0);
    }
}

void PlaylistsPage::loadCurrentPlaylist() {
    const qint64 id = currentPlaylistId();
    if (id < 0) {
        currentItems_.clear();
        currentName_.clear();
        items_->setPlaceholder(tr("Select a playlist to see its videos."));
        items_->setItems({});
        return;
    }
    QListWidgetItem* sel = playlists_->currentItem();
    currentName_ = sel ? sel->data(kNameRole).toString() : QString();
    currentItems_ = db_->playlistItems(id);
    items_->setPlaceholder(tr("This playlist is empty.\nAdd videos from a video's detail pane."));
    items_->setItems(currentItems_);
}
