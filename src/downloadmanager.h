// FreeFlume — download manager (runs yt-dlp to save videos/audio offline).
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "extractor.h"

class QProcess;

struct Download {
    enum Kind { Video, Audio, Subtitles };
    enum Status { Queued, Running, Completed, Failed, Canceled };
    int id = 0;
    QString url;
    QString title;
    Kind kind = Video;
    QString format;  // video codec (av01/vp9/avc1) or audio codec (mp3/opus/…)
    Status status = Queued;
    int percent = 0;     // 0–100
    QString speed;       // e.g. "1.2MiB/s"
    QString eta;         // e.g. "00:42"
    QString filePath;    // destination, once known
    QString error;       // message when failed
};

// Runs one yt-dlp download at a time; the rest queue. Parses yt-dlp's progress
// output and tracks the resulting file.
class DownloadManager : public QObject {
    Q_OBJECT

public:
    explicit DownloadManager(QObject* parent = nullptr);

    // Video: `format` is the codec (empty = best). Audio: `format` is the codec
    // (mp3/m4a/opus/vorbis). Subtitles: uses the language from Settings.
    int enqueue(const SearchResult& item, Download::Kind kind, const QString& format);
    void cancel(int id);
    void removeFromList(int id);   // drop a finished/queued item from the list
    void clearFinished();
    const QList<Download>& downloads() const { return items_; }

    static QString downloadDir();  // configured folder (defaults to ~/Downloads)

signals:
    void changed();  // the list or any item updated

private:
    void startNext();
    void handleStdout();
    void handleFinished(int exitCode, int /*QProcess::ExitStatus*/ status);
    Download* find(int id);

    QList<Download> items_;
    QProcess* proc_ = nullptr;
    int activeId_ = -1;
    int nextId_ = 1;
    QString lastOutput_;  // last non-progress line, for error reporting
};
