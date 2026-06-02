// FreeFlume — video detail pane.
#pragma once

#include <QWidget>

#include "extractor.h"

class Database;
class ThumbnailLoader;
class QLabel;
class QPushButton;
class QToolButton;
class QTextBrowser;

// Shows full metadata for the selected video and a Play action.
class DetailPane : public QWidget {
    Q_OBJECT

public:
    DetailPane(ThumbnailLoader* thumbs, Database* db, QWidget* parent = nullptr);

    void showLoading(const QString& title);
    void showNote(const QString& title, const QString& note);
    void showChannel(const SearchResult& channel);  // enables Subscribe
    void setDetails(const VideoDetails& details);
    void clear();

signals:
    void playRequested(const SearchResult& result);
    void channelRequested(const QString& channelUrl);  // clicked the channel name

private:
    SearchResult currentAsResult() const;
    void updateSubscribeButton();

    ThumbnailLoader* thumbs_;  // not owned
    Database* db_;             // not owned
    QLabel* thumb_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* channel_ = nullptr;
    QLabel* meta_ = nullptr;
    QTextBrowser* description_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QPushButton* subscribeButton_ = nullptr;
    QToolButton* addButton_ = nullptr;

    VideoDetails current_;
    QString pendingThumbUrl_;
    bool currentIsChannel_ = false;  // true when current_ describes a channel
};
