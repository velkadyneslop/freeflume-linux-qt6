// FreeFlume — reusable "Download" context-menu actions.
#pragma once

#include <QMenu>
#include <QObject>

#include "downloadmanager.h"
#include "extractor.h"

namespace downloadmenu {

// Adds a "Download ▸" submenu (Video / Audio) to `menu` for `item`. No-op
// without a manager, or for non-video results. audioLang selects a specific
// audio-language dub for the download ("" = original); pass the player's current
// selection so a download matches what's being watched.
inline void addSubmenu(QMenu* menu, DownloadManager* mgr, const SearchResult& item,
                       const QString& audioLang = QString()) {
    if (!mgr || item.url.isEmpty() ||
        (item.kind != ResultKind::Video && item.kind != ResultKind::Short)) {
        return;
    }
    struct Fmt {
        const char* label;
        const char* format;
    };
    QMenu* sub = menu->addMenu(QObject::tr("&Download"));
    sub->setMinimumWidth(160);  // keep the submenu arrows clear of the short labels

    QMenu* video = sub->addMenu(QObject::tr("&Video"));
    for (const Fmt& f : {Fmt{"MKV (&Best Quality)", ""}, Fmt{"MKV (A&V1)", "av01"},
                         Fmt{"&WebM (VP9)", "vp9"}, Fmt{"&MP4 (H.264/AVC)", "avc1"}}) {
        const QString fmt = QString::fromLatin1(f.format);
        video->addAction(QObject::tr(f.label), [mgr, item, fmt, audioLang] {
            mgr->enqueue(item, Download::Video, fmt, audioLang);
        });
    }

    QMenu* audio = sub->addMenu(QObject::tr("&Audio"));
    for (const Fmt& f : {Fmt{"MP&3", "mp3"}, Fmt{"M&4A (AAC)", "m4a"},
                         Fmt{"&Opus", "opus"}, Fmt{"&Vorbis (OGG)", "vorbis"}}) {
        const QString fmt = QString::fromLatin1(f.format);
        audio->addAction(QObject::tr(f.label), [mgr, item, fmt, audioLang] {
            mgr->enqueue(item, Download::Audio, fmt, audioLang);
        });
    }

    sub->addAction(QObject::tr("&Subtitles (SRT)"),
                   [mgr, item] { mgr->enqueue(item, Download::Subtitles, QString()); });
}

}  // namespace downloadmenu
