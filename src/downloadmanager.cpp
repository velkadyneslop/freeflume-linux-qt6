// FreeFlume — download manager implementation.
#include "downloadmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

DownloadManager::DownloadManager(QObject* parent) : QObject(parent) {}

QString DownloadManager::downloadDir() {
    const QString fallback =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return QSettings().value(QStringLiteral("downloads/folder"), fallback).toString();
}

int DownloadManager::enqueue(const SearchResult& item, Download::Kind kind,
                             const QString& format) {
    Download d;
    d.id = nextId_++;
    d.url = item.url;
    d.title = item.title.isEmpty() ? item.url : item.title;
    d.kind = kind;
    d.format = format;
    d.status = Download::Queued;
    items_.push_back(d);
    emit changed();
    startNext();
    return d.id;
}

Download* DownloadManager::find(int id) {
    for (Download& d : items_) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

void DownloadManager::startNext() {
    if (proc_) {
        return;  // one at a time
    }
    Download* next = nullptr;
    for (Download& d : items_) {
        if (d.status == Download::Queued) {
            next = &d;
            break;
        }
    }
    if (!next) {
        return;
    }
    next->status = Download::Running;
    activeId_ = next->id;
    lastOutput_.clear();

    const QString out = QDir(downloadDir()).filePath(QStringLiteral("%(title)s.%(ext)s"));
    QStringList args = {
        QStringLiteral("--no-playlist"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--newline"),
        // Same client fallback the player uses, so videos that play can download.
        QStringLiteral("--extractor-args"),
        QStringLiteral("youtube:player_client=default,android"),
        QStringLiteral("--progress-template"),
        QStringLiteral("FFDL %(progress._percent_str)s|%(progress._speed_str)s|"
                       "%(progress._eta_str)s"),
        QStringLiteral("-o"),
        out,
    };
    if (next->kind == Download::Subtitles) {
        const QString lang =
            QSettings().value(QStringLiteral("subtitles/language"), QStringLiteral("en"))
                .toString();
        args << QStringLiteral("--skip-download") << QStringLiteral("--write-subs")
             << QStringLiteral("--convert-subs") << QStringLiteral("srt")
             << QStringLiteral("--sleep-subtitles") << QStringLiteral("1");  // dodge rate limits
        if (lang == QLatin1String("all")) {
            // Every MANUAL track. Auto-captions are skipped here: their machine
            // translations are hundreds of tracks and trip YouTube's rate limit.
            args << QStringLiteral("--sub-langs") << QStringLiteral("all");
        } else {
            // The chosen language, manual or auto — but exact codes only, so we
            // never pull in every auto-translation (en-de, en-fr, …).
            args << QStringLiteral("--write-auto-subs") << QStringLiteral("--sub-langs")
                 << QStringLiteral("%1,%1-orig").arg(lang);
        }
    } else if (next->kind == Download::Audio) {
        const QString codec = next->format.isEmpty() ? QStringLiteral("mp3") : next->format;
        args << QStringLiteral("-x") << QStringLiteral("--audio-format") << codec
             << QStringLiteral("--audio-quality") << QStringLiteral("0");
    } else {
        // `format` is a video codec; pick a matching container (mkv muxes any
        // codec losslessly). Each selector prefers the codec, then falls back to
        // the overall best so a download never fails when a codec is missing.
        QString vcodec, aPref, container;
        const QString codec = next->format;
        if (codec == QLatin1String("av01")) {
            vcodec = QStringLiteral("[vcodec^=av01]");
            container = QStringLiteral("mkv");
        } else if (codec == QLatin1String("vp9")) {
            vcodec = QStringLiteral("[vcodec^=vp9]");
            aPref = QStringLiteral("[ext=webm]");
            container = QStringLiteral("webm");
        } else if (codec == QLatin1String("avc1")) {
            vcodec = QStringLiteral("[vcodec^=avc1]");
            aPref = QStringLiteral("[ext=m4a]");
            container = QStringLiteral("mp4");
        } else {  // best available
            container = QStringLiteral("mkv");
        }
        // Optional resolution cap (0 = no limit).
        const int maxH = QSettings().value(QStringLiteral("downloads/maxHeight"), 0).toInt();
        const QString hf = maxH > 0 ? QStringLiteral("[height<=%1]").arg(maxH) : QString();
        const QString v = QStringLiteral("bv*%1%2").arg(vcodec, hf);  // capped video stream
        QString sel;
        if (!aPref.isEmpty()) {
            sel += v + QStringLiteral("+ba") + aPref + QLatin1Char('/');
        }
        sel += v + QStringLiteral("+ba/b") + hf;  // codec+cap, then best single-file in cap
        if (!hf.isEmpty()) {
            sel += QStringLiteral("/b");  // ultimate fallback so a download never fails
        }
        args << QStringLiteral("-f") << sel
             << QStringLiteral("--merge-output-format") << container;

        // Optionally bake captions into the file (preferred language, manual or
        // auto — exact codes only so we don't pull every auto-translation).
        QSettings s;
        if (s.value(QStringLiteral("downloads/embedSubs"), false).toBool()) {
            const QString lang =
                s.value(QStringLiteral("subtitles/language"), QStringLiteral("en")).toString();
            args << QStringLiteral("--embed-subs") << QStringLiteral("--sleep-subtitles")
                 << QStringLiteral("1") << QStringLiteral("--sub-langs");
            if (lang == QLatin1String("all")) {
                args << QStringLiteral("all");  // all manual tracks
            } else {
                args << QStringLiteral("%1,%1-orig").arg(lang)
                     << QStringLiteral("--write-auto-subs");
            }
        }
    }
    args << next->url;

    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc_, &QProcess::readyReadStandardOutput, this, &DownloadManager::handleStdout);
    connect(proc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus st) {
                handleFinished(code, static_cast<int>(st));
            });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) {
            return;  // other errors are followed by finished()
        }
        if (Download* d = find(activeId_); d && d->status == Download::Running) {
            d->status = Download::Failed;
            d->error = tr("Could not run yt-dlp. Is it installed and on your PATH?");
        }
        if (proc_) {
            proc_->deleteLater();
            proc_ = nullptr;
        }
        activeId_ = -1;
        emit changed();
        startNext();
    });
    emit changed();
    proc_->start(QStringLiteral("yt-dlp"), args);
}

void DownloadManager::handleStdout() {
    if (!proc_) {
        return;
    }
    Download* d = find(activeId_);
    if (!d) {
        return;
    }
    while (proc_->canReadLine()) {
        const QString line = QString::fromUtf8(proc_->readLine()).trimmed();
        if (line.startsWith(QLatin1String("FFDL "))) {
            const QStringList parts = line.mid(5).split(QLatin1Char('|'));
            if (!parts.isEmpty()) {
                const QString pct = parts.at(0).trimmed();  // e.g. "42.3%"
                bool ok = false;
                const double v = QStringView(pct).left(pct.size() - 1).toDouble(&ok);
                if (ok) {
                    d->percent = qBound(0, static_cast<int>(v + 0.5), 100);
                }
            }
            if (parts.size() > 1) d->speed = parts.at(1).trimmed();
            if (parts.size() > 2) d->eta = parts.at(2).trimmed();
            emit changed();
        } else if (line.contains(QLatin1String("Merging formats into "))) {
            const int q = line.indexOf(QLatin1Char('"'));
            const int q2 = line.lastIndexOf(QLatin1Char('"'));
            if (q >= 0 && q2 > q) {
                d->filePath = line.mid(q + 1, q2 - q - 1);
            }
        } else if (line.contains(QLatin1String("Destination: "))) {
            d->filePath = line.section(QLatin1String("Destination: "), 1).trimmed();
        } else if (!line.isEmpty()) {
            lastOutput_ = line;  // remember for an error message
            if (line.contains(QLatin1String("ERROR"))) {
                d->error = line.section(QLatin1String("ERROR:"), 1).trimmed();
                if (d->error.isEmpty()) {
                    d->error = line;
                }
            }
        }
    }
}

void DownloadManager::handleFinished(int exitCode, int status) {
    Download* d = find(activeId_);
    if (d && d->status == Download::Running) {
        if (status != 0 /*CrashExit*/ || exitCode != 0) {
            d->status = Download::Failed;
            if (d->error.isEmpty()) {
                d->error = lastOutput_.isEmpty()
                               ? tr("yt-dlp exited with code %1.").arg(exitCode)
                               : lastOutput_;
            }
        } else if (d->kind == Download::Subtitles && d->filePath.isEmpty()) {
            d->status = Download::Failed;
            d->error = tr("No subtitles available in the chosen language.");
        } else {
            d->status = Download::Completed;
            d->percent = 100;
            if (d->kind == Download::Subtitles) {
                // We --convert-subs to srt; the captured path is the original (.vtt).
                const QFileInfo fi(d->filePath);
                d->filePath = fi.path() + QLatin1Char('/') + fi.completeBaseName() +
                              QStringLiteral(".srt");
            }
        }
    }
    if (proc_) {
        proc_->deleteLater();
        proc_ = nullptr;
    }
    activeId_ = -1;
    emit changed();
    startNext();
}

void DownloadManager::cancel(int id) {
    Download* d = find(id);
    if (!d) {
        return;
    }
    d->status = Download::Canceled;
    if (id == activeId_ && proc_) {
        proc_->kill();  // handleFinished will see status already Canceled
    }
    emit changed();
}

void DownloadManager::removeFromList(int id) {
    if (id == activeId_) {
        return;  // don't drop a running download from under itself
    }
    for (int i = 0; i < items_.size(); ++i) {
        if (items_.at(i).id == id) {
            items_.removeAt(i);
            break;
        }
    }
    emit changed();
}

void DownloadManager::clearFinished() {
    for (int i = items_.size() - 1; i >= 0; --i) {
        const Download::Status s = items_.at(i).status;
        if (s == Download::Completed || s == Download::Failed || s == Download::Canceled) {
            items_.removeAt(i);
        }
    }
    emit changed();
}
