// FreeFlume — watch history page implementation.
#include "historypage.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "database.h"
#include "videolist.h"

HistoryPage::HistoryPage(Database* db, ThumbnailLoader* thumbs, QWidget* parent)
    : QWidget(parent), db_(db) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto* bar = new QWidget(this);
    auto* barLay = new QHBoxLayout(bar);
    barLay->setContentsMargins(12, 8, 12, 8);
    auto* heading = new QLabel(tr("History"), bar);
    QFont hf = heading->font();
    hf.setBold(true);
    hf.setPointSize(hf.pointSize() + 2);
    heading->setFont(hf);
    auto* clearBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")),
                                     tr("Clear &History"), bar);
    barLay->addWidget(heading);
    barLay->addStretch();
    barLay->addWidget(clearBtn);
    col->addWidget(bar);

    list_ = new VideoList(thumbs, this);
    list_->setPlaceholder(tr("Videos you watch will appear here."));
    list_->setRemovable(true, tr("&Remove from History"));  // hover button + menu
    list_->setDatabase(db_);
    col->addWidget(list_, 1);

    connect(list_, &VideoList::activated, this, &HistoryPage::playRequested);
    connect(list_, &VideoList::removeRequested, this,
            [this](const SearchResult& r) { db_->removeHistoryItem(r.url); });
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        const auto reply = QMessageBox::question(
            this, tr("Clear History"),
            tr("Remove all videos from your watch history? This cannot be undone."));
        if (reply == QMessageBox::Yes) {
            db_->clearHistory();
            refresh();
        }
    });
    connect(db_, &Database::historyChanged, this, &HistoryPage::refresh);
}

void HistoryPage::refresh() {
    list_->setItems(db_->history());
}

void HistoryPage::setDownloadManager(DownloadManager* m) {
    list_->setDownloadManager(m);
}
