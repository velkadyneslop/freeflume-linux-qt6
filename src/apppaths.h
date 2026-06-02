// FreeFlume — data/config locations.
//
// Native:  <data>/velkadyne/FreeFlume/…   and  <config>/velkadyne/FreeFlume.conf
// Flatpak: <data>/…           and  <config>/freeflume.conf
//          (the app-id com.velkadyne.FreeFlume already namespaces the dirs, so
//           nothing extra is appended)
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace apppaths {

inline bool inFlatpak() {
    return qEnvironmentVariableIsSet("FLATPAK_ID") ||
           QFileInfo::exists(QStringLiteral("/.flatpak-info"));
}

// Directory holding the SQLite database (created if missing).
inline QString dataDir() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString dir =
        inFlatpak() ? base : base + QStringLiteral("/velkadyne/FreeFlume");
    QDir().mkpath(dir);
    return dir;
}

// The QSettings INI file (parent dir created if missing).
inline QString configFile() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (inFlatpak()) {
        QDir().mkpath(base);
        return base + QStringLiteral("/freeflume.conf");
    }
    QDir().mkpath(base + QStringLiteral("/velkadyne"));
    return base + QStringLiteral("/velkadyne/FreeFlume.conf");
}

// One-time move of any pre-existing data/config into the current layout.
inline void migrateLegacy() {
    const QString d =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString c =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QStringList oldDbDirs;
    QStringList oldCfgs;
    if (inFlatpak()) {
        oldDbDirs = {d + QStringLiteral("/velkadyne/FreeFlume"),
                     d + QStringLiteral("/FreeFlume/FreeFlume")};
        oldCfgs = {c + QStringLiteral("/velkadyne/FreeFlume.conf"),
                   c + QStringLiteral("/FreeFlume/FreeFlume.conf")};
    } else {
        oldDbDirs = {d + QStringLiteral("/FreeFlume/FreeFlume")};
        oldCfgs = {c + QStringLiteral("/FreeFlume/FreeFlume.conf")};
    }

    const QString newDb = dataDir() + QStringLiteral("/freeflume.db");
    for (const QString& old : oldDbDirs) {
        if (QFile::exists(old + QStringLiteral("/freeflume.db")) && !QFile::exists(newDb)) {
            for (const QString& f : {QStringLiteral("/freeflume.db"),
                                     QStringLiteral("/freeflume.db-wal"),
                                     QStringLiteral("/freeflume.db-shm")}) {
                if (QFile::exists(old + f)) {
                    QFile::rename(old + f, dataDir() + f);
                }
            }
            break;
        }
    }

    const QString newCfg = configFile();
    for (const QString& old : oldCfgs) {
        if (QFile::exists(old) && !QFile::exists(newCfg)) {
            QFile::rename(old, newCfg);
            break;
        }
    }

    // Remove the now-empty legacy directories (rmdir only deletes empty ones).
    for (const QString& old : oldDbDirs) {
        QDir().rmdir(old);                       // <data>/<org>/FreeFlume
        QDir().rmdir(QFileInfo(old).absolutePath());  // <data>/<org>
    }
    for (const QString& old : oldCfgs) {
        QDir().rmdir(QFileInfo(old).absolutePath());  // <config>/<org>
    }
}

}  // namespace apppaths
