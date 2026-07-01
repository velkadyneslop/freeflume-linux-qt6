// FreeFlume — download manager implementation.
#include "apppaths.h"
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
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) +
        QStringLiteral("/FreeFlume");
    return QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("downloads/folder"), fallback).toString();
}

int DownloadManager::enqueue(const SearchResult& item, Download::Kind kind,
                             const QString& format, const QString& audioLang) {
    Download d;
    d.id = nextId_++;
    d.url = item.url;
    d.title = item.title.isEmpty() ? item.url : item.title;
    d.kind = kind;
    d.format = format;
    d.audioLang = audioLang;
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
    if (proc_ || probeProc_) {
        return;  // one download (or its probe) at a time
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

    // "Embed all languages" muxes every audio track into one mkv, so it needs the
    // language list first: probe (web_embedded), then download. Everything else
    // starts straight away.
    const bool embedAll = next->kind == Download::Video &&
        QSettings(apppaths::configFile(), QSettings::IniFormat)
            .value(QStringLiteral("downloads/embedAllAudio"), false).toBool();
    if (embedAll) {
        probeAudioThenStart();
    } else {
        beginDownload({});
    }
}

void DownloadManager::probeAudioThenStart() {
    Download* d = find(activeId_);
    if (!d) {
        activeId_ = -1;
        startNext();
        return;
    }
    probeProc_ = new QProcess(this);
    connect(probeProc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus st) {
                if (!probeProc_) {
                    return;  // the error handler already took over
                }
                const QByteArray out = probeProc_->readAllStandardOutput();
                probeProc_->deleteLater();
                probeProc_ = nullptr;
                const bool includeAuto =
                    QSettings(apppaths::configFile(), QSettings::IniFormat)
                        .value(QStringLiteral("downloads/embedAutoDubs"), false).toBool();
                QStringList langs;
                if (st == QProcess::NormalExit && code == 0) {
                    for (const AudioTrackInfo& t : parseAudioTracks(out)) {
                        // Always the original + manual dubs; auto-dubs are opt-in.
                        if (t.isDefault || !t.autoDub || includeAuto) {
                            langs << t.code;
                        }
                    }
                }
                beginDownload(langs);
            });
    connect(probeProc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !probeProc_) {
            return;
        }
        probeProc_->deleteLater();
        probeProc_ = nullptr;
        beginDownload({});  // probe couldn't run; fall back to a plain download
    });
    probeProc_->start(QStringLiteral("yt-dlp"),
                      {QStringLiteral("--dump-single-json"), QStringLiteral("--no-warnings"),
                       QStringLiteral("--no-playlist"), QStringLiteral("--extractor-args"),
                       QStringLiteral("youtube:player_client=web_embedded"), d->url});
}

void DownloadManager::beginDownload(const QStringList& embedLangs) {
    Download* next = find(activeId_);
    if (!next || next->status != Download::Running) {
        activeId_ = -1;
        startNext();  // canceled during the probe, or gone from the list
        return;
    }
    // More than one language to embed => a multi-audio mkv.
    const bool embedAll = embedLangs.size() > 1;

    // Tag a single-language dub so it doesn't overwrite the original; an
    // embed-all mkv keeps the plain title (it holds every language).
    const QString stem = (embedAll || next->audioLang.isEmpty())
        ? QStringLiteral("%(title)s")
        : QStringLiteral("%(title)s.%1").arg(next->audioLang);
    const QString out = QDir(downloadDir()).filePath(stem + QStringLiteral(".%(ext)s"));
    // Dubs (single or all) are only exposed by the web_embedded client; the plain
    // original needs only the default client, so keep that path fast.
    const bool needEmbedded = embedAll || !next->audioLang.isEmpty();
    const QString clients = needEmbedded
        ? QStringLiteral("youtube:player_client=default,web_embedded,android")
        : QStringLiteral("youtube:player_client=default,android");
    QStringList args = {
        QStringLiteral("--no-playlist"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--newline"),
        // Same client fallback the player uses, so videos that play can download.
        QStringLiteral("--extractor-args"),
        clients,
        QStringLiteral("--progress-template"),
        QStringLiteral("FFDL %(progress._percent_str)s|%(progress._speed_str)s|"
                       "%(progress._eta_str)s"),
        QStringLiteral("-o"),
        out,
    };
    if (next->kind == Download::Subtitles) {
        const QString lang =
            QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("subtitles/language"), QStringLiteral("en"))
                .toString();
        args << QStringLiteral("--skip-download") << QStringLiteral("--write-subs")
             << QStringLiteral("--convert-subs") << QStringLiteral("srt")
             << QStringLiteral("--sleep-subtitles") << QStringLiteral("1");  // dodge rate limits
        if (lang == QLatin1String("all")) {
            // Every MANUAL track. Auto-captions are skipped here: their machine
            // translations are hundreds of tracks and trip YT's rate limit.
            args << QStringLiteral("--sub-langs") << QStringLiteral("all");
        } else {
            // The chosen language, manual or auto — but exact codes only, so we
            // never pull in every auto-translation (en-de, en-fr, …).
            args << QStringLiteral("--write-auto-subs") << QStringLiteral("--sub-langs")
                 << QStringLiteral("%1,%1-orig").arg(lang);
        }
    } else if (next->kind == Download::Audio) {
        const QString codec = next->format.isEmpty() ? QStringLiteral("mp3") : next->format;
        if (!next->audioLang.isEmpty()) {
            // Pick the chosen dub's audio, falling back to best so it never fails.
            args << QStringLiteral("-f")
                 << QStringLiteral("ba[language=%1]/ba/b").arg(next->audioLang);
        }
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
        const int maxH = QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("downloads/maxHeight"), 0).toInt();
        const QString hf = maxH > 0 ? QStringLiteral("[height<=%1]").arg(maxH) : QString();
        const QString v = QStringLiteral("bv*%1%2").arg(vcodec, hf);  // capped video stream
        QString sel;
        if (embedAll) {
            // One mkv holding every requested language as a tagged audio track.
            // mkv is the only container that carries many labelled audio streams.
            container = QStringLiteral("mkv");
            QString audios;
            for (const QString& code : embedLangs) {
                audios += QStringLiteral("+ba[language=%1]").arg(code);
            }
            // All the languages, else fall back to best single audio so it never fails.
            sel = v + audios + QLatin1Char('/') + v + QStringLiteral("+ba/b") + hf;
            // --embed-metadata tags each audio track with its real language;
            // without it the merge leaves every track labelled "eng".
            args << QStringLiteral("--audio-multistreams") << QStringLiteral("--embed-metadata");
        } else {
            // Preference order: the chosen dub first, then high-quality original
            // audio, then a single progressive file — so a download never fails,
            // and a missing dub falls back to good original rather than low-res.
            const QString aLang = next->audioLang.isEmpty()
                ? QString()
                : QStringLiteral("[language=%1]").arg(next->audioLang);
            if (!aLang.isEmpty()) {
                if (!aPref.isEmpty()) {
                    sel += v + QStringLiteral("+ba") + aPref + aLang + QLatin1Char('/');
                }
                sel += v + QStringLiteral("+ba") + aLang + QLatin1Char('/');  // the dub
            }
            if (!aPref.isEmpty()) {
                sel += v + QStringLiteral("+ba") + aPref + QLatin1Char('/');
            }
            sel += v + QStringLiteral("+ba/b") + hf;  // best original pair, then single-file
            if (!hf.isEmpty()) {
                sel += QStringLiteral("/b");  // ultimate fallback so it never fails
            }
        }
        args << QStringLiteral("-f") << sel
             << QStringLiteral("--merge-output-format") << container;

        // Optionally bake captions into the file (preferred language, manual or
        // auto — exact codes only so we don't pull every auto-translation).
        QSettings s(apppaths::configFile(), QSettings::IniFormat);
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
                    // Aggregate the current part's percent across all parts, so the
                    // bar climbs 0→100 once instead of resetting for every stream.
                    const int denom = d->totalParts > 0 ? d->totalParts : 1;
                    const int done = d->partIndex > 0 ? d->partIndex - 1 : 0;
                    const double overall = (done * 100.0 + v) / denom;
                    d->percent = qBound(0, static_cast<int>(overall + 0.5), 100);
                }
            }
            if (parts.size() > 1) d->speed = parts.at(1).trimmed();
            if (parts.size() > 2) d->eta = parts.at(2).trimmed();
            emit changed();
        } else if (line.contains(QLatin1String("format(s): "))) {
            // "[info] …: Downloading N format(s): 137+251-0+251-9" — the '+'-joined
            // ids are the streams we'll fetch before muxing.
            const QString spec = line.section(QLatin1String("format(s): "), 1).trimmed();
            d->totalParts = spec.section(QLatin1Char(','), 0, 0).split(QLatin1Char('+')).size();
            d->partIndex = 0;
            d->merging = false;
        } else if (line.contains(QLatin1String("Merging formats into "))) {
            const int q = line.indexOf(QLatin1Char('"'));
            const int q2 = line.lastIndexOf(QLatin1Char('"'));
            if (q >= 0 && q2 > q) {
                d->filePath = line.mid(q + 1, q2 - q - 1);
            }
            d->merging = true;
            d->percent = 100;  // all streams fetched; now just muxing
            emit changed();
        } else if (line.contains(QLatin1String("Destination: "))) {
            d->filePath = line.section(QLatin1String("Destination: "), 1).trimmed();
            d->partIndex++;  // a new stream started downloading
            emit changed();
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
    } else if (id == activeId_ && probeProc_) {
        // Canceled while probing: drop the probe and move on (beginDownload will
        // see the Canceled status and skip it).
        probeProc_->disconnect(this);
        probeProc_->kill();
        probeProc_->deleteLater();
        probeProc_ = nullptr;
        activeId_ = -1;
        startNext();
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
