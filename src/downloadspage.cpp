// FreeFlume — Downloads page implementation.
#include "downloadspage.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "downloadmanager.h"

namespace {
const Download* findDownload(DownloadManager* mgr, int id) {
    for (const Download& d : mgr->downloads()) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}
}  // namespace

DownloadsPage::DownloadsPage(DownloadManager* mgr, QWidget* parent)
    : QWidget(parent), mgr_(mgr) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto* bar = new QWidget(this);
    auto* barLay = new QHBoxLayout(bar);
    barLay->setContentsMargins(12, 8, 12, 8);
    auto* heading = new QLabel(tr("Downloads"), bar);
    QFont hf = heading->font();
    hf.setBold(true);
    hf.setPointSize(hf.pointSize() + 2);
    heading->setFont(hf);
    auto* folderBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("folder-download")),
                                      tr("Open &Folder"), bar);
    auto* clearBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")),
                                     tr("&Clear Finished"), bar);
    barLay->addWidget(heading);
    barLay->addStretch();
    barLay->addWidget(folderBtn);
    barLay->addWidget(clearBtn);
    col->addWidget(bar);

    stack_ = new QStackedWidget(this);
    placeholder_ = new QLabel(tr("Downloads will appear here.\n"
                                 "Right-click a video and choose Download."),
                              this);
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setEnabled(false);
    stack_->addWidget(placeholder_);  // 0
    list_ = new QListWidget(this);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSpacing(2);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    stack_->addWidget(list_);  // 1
    col->addWidget(stack_, 1);

    connect(folderBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(DownloadManager::downloadDir()));
    });
    connect(clearBtn, &QPushButton::clicked, mgr_, &DownloadManager::clearFinished);
    connect(mgr_, &DownloadManager::changed, this, &DownloadsPage::refresh);
}

void DownloadsPage::createRow(int id) {
    Row row;
    row.item = new QListWidgetItem(list_);
    row.item->setSizeHint(QSize(0, 76));

    auto* widget = new QWidget(list_);
    auto* hbox = new QHBoxLayout(widget);
    hbox->setContentsMargins(10, 6, 10, 6);
    hbox->setSpacing(10);

    auto* textCol = new QVBoxLayout;
    textCol->setSpacing(3);
    row.title = new QLabel(widget);
    QFont tf = row.title->font();
    tf.setBold(true);
    row.title->setFont(tf);
    row.bar = new QProgressBar(widget);
    row.bar->setRange(0, 100);
    row.bar->setTextVisible(false);
    row.bar->setFixedHeight(6);
    row.status = new QLabel(widget);
    row.status->setEnabled(false);
    textCol->addWidget(row.title);
    textCol->addWidget(row.bar);
    textCol->addWidget(row.status);

    auto iconBtn = [widget](const char* icon, const QString& tip) {
        auto* b = new QToolButton(widget);
        b->setIcon(QIcon::fromTheme(QString::fromUtf8(icon)));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    row.openBtn = iconBtn("media-playback-start", tr("Open file"));
    row.folderBtn = iconBtn("folder-open", tr("Show in folder"));
    row.cancelBtn = iconBtn("process-stop", tr("Cancel"));
    row.removeBtn = iconBtn("list-remove", tr("Remove from list"));

    connect(row.cancelBtn, &QToolButton::clicked, this, [this, id] { mgr_->cancel(id); });
    connect(row.removeBtn, &QToolButton::clicked, this, [this, id] { mgr_->removeFromList(id); });
    connect(row.openBtn, &QToolButton::clicked, this, [this, id] {
        if (const Download* d = findDownload(mgr_, id); d && !d->filePath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(d->filePath));
        }
    });
    connect(row.folderBtn, &QToolButton::clicked, this, [this, id] {
        if (const Download* d = findDownload(mgr_, id); d && !d->filePath.isEmpty()) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(d->filePath).absolutePath()));
        }
    });

    hbox->addLayout(textCol, 1);
    hbox->addWidget(row.openBtn);
    hbox->addWidget(row.folderBtn);
    hbox->addWidget(row.cancelBtn);
    hbox->addWidget(row.removeBtn);
    list_->setItemWidget(row.item, widget);
    rows_.insert(id, row);
}

void DownloadsPage::updateRow(Row& row, int id) {
    const Download* d = findDownload(mgr_, id);
    if (!d) {
        return;
    }
    row.title->setText(d->title);
    const bool running = d->status == Download::Running;
    const bool queued = d->status == Download::Queued;
    const bool done = d->status == Download::Completed;

    row.bar->setVisible(running || queued);
    row.bar->setValue(d->percent);

    QString kind = tr("Video");
    if (d->kind == Download::Audio) kind = tr("Audio");
    else if (d->kind == Download::Subtitles) kind = tr("Subtitles");
    QString fmtLabel = d->format.toUpper();  // audio codecs (MP3, M4A, …)
    if (d->format == QLatin1String("av01")) fmtLabel = QStringLiteral("AV1");
    else if (d->format == QLatin1String("vp9")) fmtLabel = QStringLiteral("VP9");
    else if (d->format == QLatin1String("avc1")) fmtLabel = QStringLiteral("H.264");
    const QString type =
        d->format.isEmpty() ? kind : QStringLiteral("%1 (%2)").arg(kind, fmtLabel);
    switch (d->status) {
        case Download::Queued:
            row.status->setText(tr("%1  ·  Queued").arg(type));
            break;
        case Download::Running:
            row.status->setText(tr("%1  ·  %2%  ·  %3  ·  ETA %4")
                                    .arg(type).arg(d->percent)
                                    .arg(d->speed.isEmpty() ? QStringLiteral("—") : d->speed,
                                         d->eta.isEmpty() ? QStringLiteral("—") : d->eta));
            break;
        case Download::Completed:
            row.status->setText(tr("Completed  ·  %1").arg(QFileInfo(d->filePath).fileName()));
            break;
        case Download::Failed:
            row.status->setText(tr("Failed: %1").arg(d->error));
            break;
        case Download::Canceled:
            row.status->setText(tr("Canceled"));
            break;
    }

    // No "open file" for subtitles — a .srt isn't something to launch/play.
    row.openBtn->setVisible(done && !d->filePath.isEmpty() &&
                            d->kind != Download::Subtitles);
    row.folderBtn->setVisible(done && !d->filePath.isEmpty());
    row.cancelBtn->setVisible(running || queued);
    row.removeBtn->setVisible(!running);
}

void DownloadsPage::refresh() {
    // Drop rows for downloads that no longer exist.
    const auto& downloads = mgr_->downloads();
    for (auto it = rows_.begin(); it != rows_.end();) {
        bool present = false;
        for (const Download& d : downloads) {
            if (d.id == it.key()) {
                present = true;
                break;
            }
        }
        if (!present) {
            delete list_->takeItem(list_->row(it.value().item));
            it = rows_.erase(it);
        } else {
            ++it;
        }
    }
    // Add new rows and update existing ones in place.
    for (const Download& d : downloads) {
        if (!rows_.contains(d.id)) {
            createRow(d.id);
        }
        updateRow(rows_[d.id], d.id);
    }
    stack_->setCurrentIndex(downloads.isEmpty() ? 0 : 1);
}
