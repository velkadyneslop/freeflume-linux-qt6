// FreeFlume — Downloads page (active + finished downloads).
#pragma once

#include <QHash>
#include <QWidget>

class DownloadManager;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QProgressBar;
class QToolButton;
class QStackedWidget;

class DownloadsPage : public QWidget {
    Q_OBJECT

public:
    DownloadsPage(DownloadManager* mgr, QWidget* parent = nullptr);

    void refresh();

private:
    struct Row {
        QListWidgetItem* item = nullptr;
        QLabel* title = nullptr;
        QLabel* status = nullptr;
        QProgressBar* bar = nullptr;
        QToolButton* cancelBtn = nullptr;
        QToolButton* openBtn = nullptr;
        QToolButton* folderBtn = nullptr;
        QToolButton* removeBtn = nullptr;
    };

    void createRow(int id);
    void updateRow(Row& row, int id);

    DownloadManager* mgr_;
    QStackedWidget* stack_ = nullptr;
    QLabel* placeholder_ = nullptr;
    QListWidget* list_ = nullptr;
    QHash<int, Row> rows_;
};
