// FreeFlume — shared "Share" context-menu actions for a video/channel URL.
#pragma once

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QMenu>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

namespace share {

inline void copy(const QString& text) { QApplication::clipboard()->setText(text); }

// Extracts the YT video id from a watch or youtu.be URL ("" if not a video).
inline QString videoId(const QString& url) {
    const QUrl u(url);
    if (u.host().contains(QLatin1String("youtu.be"))) {
        return u.path().mid(1);
    }
    return QUrlQuery(u).queryItemValue(QStringLiteral("v"));
}

// Appends Copy Link / Copy Short Link / [Copy Link at Current Time] / Open in
// Browser to `menu`. Pass atSeconds >= 0 (e.g. the player position) to include
// the timestamped link.
inline void addActions(QMenu* menu, const QString& url, double atSeconds = -1.0) {
    const QString id = videoId(url);
    // Prefer the compact youtu.be link for videos; fall back to the full URL
    // for channels/playlists that have no video id.
    const QString link =
        id.isEmpty() ? url : QStringLiteral("https://youtu.be/%1").arg(id);
    menu->addAction(QObject::tr("&Copy Link"), [link] { copy(link); });

    if (!id.isEmpty() && atSeconds >= 0.0) {
        const qint64 t = static_cast<qint64>(atSeconds);
        menu->addAction(QObject::tr("Copy Link at Current &Time"), [id, t] {
            copy(QStringLiteral("https://youtu.be/%1?t=%2").arg(id).arg(t));
        });
    }

    menu->addAction(QObject::tr("&Open in Browser"),
                    [url] { QDesktopServices::openUrl(QUrl(url)); });
}

}  // namespace share
