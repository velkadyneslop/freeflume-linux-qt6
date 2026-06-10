// FreeFlume — reusable list of videos with async thumbnails.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "extractor.h"

class Database;
class DownloadManager;
class HoverRevealGroup;
class ReorderableListWidget;
class ThumbnailLoader;
class QListWidgetItem;
class QStackedWidget;
class QLabel;
class QTimer;

// A scrollable list of SearchResults (thumbnail + title + meta). Used by the
// History, Subscriptions and Playlists pages. Emits activated() on double-click
// or Enter, and currentChanged() on selection.
class VideoList : public QWidget {
    Q_OBJECT

public:
    explicit VideoList(ThumbnailLoader* thumbs, QWidget* parent = nullptr);

    void setItems(const QList<SearchResult>& items);
    void showContextMenu(const SearchResult& r, const QPoint& globalPos);
    void setPlaceholder(const QString& text);
    SearchResult currentItem() const;
    void setCurrentRow(int row);  // highlight + scroll to a row (e.g. now playing)

    // When enabled, each row shows a remove button on hover (and a context-menu
    // entry) that emits removeRequested(). `label` names the action.
    void setRemovable(bool on, const QString& label = QString());

    // When enabled, rows can be drag-reordered; emits reordered() on a drop.
    void setReorderable(bool on);

    // Enables a "Save to Playlist" entry in the row context menu.
    void setDatabase(Database* db) { db_ = db; }

    // Enables a "Download" entry in the row context menu.
    void setDownloadManager(DownloadManager* m) { downloads_ = m; }

signals:
    void activated(const SearchResult& item);
    void currentChanged(const SearchResult& item);
    void removeRequested(const SearchResult& item);
    void reordered(const QStringList& urlsInOrder);  // new order after a drag

private:
    void enqueueVisibleDates();  // lazily fetch upload dates for on-screen rows

    ThumbnailLoader* thumbs_;  // not owned
    QStackedWidget* stack_ = nullptr;
    QLabel* placeholder_ = nullptr;
    ReorderableListWidget* list_ = nullptr;
    HoverRevealGroup* hoverGroup_ = nullptr;  // per-row remove buttons (hover)
    Database* db_ = nullptr;                   // for "Save to Playlist" (optional)
    DownloadManager* downloads_ = nullptr;     // for "Download" (optional)
    QList<SearchResult> data_;                 // current items, in display order
    QHash<QString, QLabel*> metaByUrl_;        // meta labels awaiting an upload date
    QTimer* dateScrollTimer_ = nullptr;        // debounces date enqueue on scroll
    QString removeLabel_;                      // text for the remove action
    bool removable_ = false;
    bool reorderable_ = false;
};
