// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 the FreeFlume authors.
//
// FreeFlume is free software: redistribute and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version. See
// the LICENSE file for the full text.
//
// FreeFlume — native Linux port.
// A libre, lightweight streaming front-end. Native C++ / Qt6 core.
//
// Backends: yt-dlp (extraction) + libmpv (playback, embedded in a later phase).
// Styling follows the active desktop environment (KDE Breeze, GNOME/GTK via the
// gtk3 platform theme) — we never hardcode a look.

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStandardPaths>

#include "apppaths.h"
#include "depcheck.h"
#include "mainwindow.h"
#include "theme.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("FreeFlume"));
    QApplication::setApplicationDisplayName(QStringLiteral("FreeFlume"));
    QApplication::setOrganizationName(QStringLiteral("velkadyne"));
    QApplication::setDesktopFileName(QStringLiteral("org.freeflume.Desktop"));

    // Move data/config over from the pre-1.0 "FreeFlume/FreeFlume" layout.
    apppaths::migrateLegacy();

    // The official Deno installer drops the binary in ~/.deno/bin but leaves the
    // PATH step to the user; yt-dlp needs Deno to unlock full-resolution YouTube
    // playback (it solves the "nsig" challenge there). If Deno isn't already on
    // PATH, add the installer's default location so the mpv/yt-dlp subprocesses we
    // spawn can find it — no manual PATH fiddling required.
    if (QStandardPaths::findExecutable(QStringLiteral("deno")).isEmpty()) {
        const QString denoBin = QDir::homePath() + QStringLiteral("/.deno/bin");
        if (QFileInfo::exists(denoBin + QStringLiteral("/deno"))) {
            qputenv("PATH",
                    (denoBin + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
        }
    }

    // App icon (embedded), with the installed theme icon as a fallback.
    QIcon icon = QIcon::fromTheme(QStringLiteral("freeflume"));
    for (int s : {16, 24, 32, 48, 64, 128, 256, 512}) {
        icon.addFile(QStringLiteral(":/icons/freeflume-%1.png").arg(s), QSize(s, s));
    }
    QApplication::setWindowIcon(icon);

    // Apply saved appearance preferences (color scheme + style). The platform
    // default style already adapts per desktop (Breeze on KDE, gtk3 on GNOME).
    theme::applySaved();

    MainWindow window;
    window.show();

    // Nudge the user if the one runtime subprocess dependency is missing.
    depcheck::warnIfMissing(&window);

    // CLI: `freeflume --play <url>` plays a URL directly; otherwise any
    // trailing arguments are treated as a search query.
    const QStringList args = QApplication::arguments();
    const int tabIdx = args.indexOf(QStringLiteral("--tab"));
    if (tabIdx >= 0 && tabIdx + 1 < args.size()) {
        window.selectTab(args.at(tabIdx + 1).toInt());
    }
    const int playIdx = args.indexOf(QStringLiteral("--play"));
    if (playIdx >= 0 && playIdx + 1 < args.size()) {
        window.playUrl(args.at(playIdx + 1));
    } else if (tabIdx < 0 && args.size() > 1) {
        window.search(args.mid(1).join(QLatin1Char(' ')));
    }

    return app.exec();
}
