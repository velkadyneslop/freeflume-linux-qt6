// FreeFlume — one-time migration of data/config from the old layout.
//
// Before v1.0.0 the org and app names were both "FreeFlume", giving a redundant
// .../FreeFlume/FreeFlume path. The org name is now "velkadyne", so Qt derives
// .../velkadyne/FreeFlume itself; this just moves any pre-existing data over.
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

namespace apppaths {

inline bool inFlatpak() {
    return qEnvironmentVariableIsSet("FLATPAK_ID") ||
           QFileInfo::exists(QStringLiteral("/.flatpak-info"));
}

inline void migrateLegacy() {
    if (inFlatpak()) {
        return;  // Flatpak is a fresh install — nothing to migrate
    }
    auto move = [](const QString& oldPath, const QString& newPath) {
        if (QFile::exists(oldPath) && !QFile::exists(newPath)) {
            QDir().mkpath(QFileInfo(newPath).absolutePath());
            QFile::rename(oldPath, newPath);
        }
    };
    const QString d =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    for (const QString& f : {QStringLiteral("freeflume.db"),
                             QStringLiteral("freeflume.db-wal"),
                             QStringLiteral("freeflume.db-shm")}) {
        move(d + QStringLiteral("/FreeFlume/FreeFlume/") + f,
             d + QStringLiteral("/velkadyne/FreeFlume/") + f);
    }
    const QString c =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    move(c + QStringLiteral("/FreeFlume/FreeFlume.conf"),
         c + QStringLiteral("/velkadyne/FreeFlume.conf"));
}

}  // namespace apppaths
