// FreeFlume — reusable "Save to Playlist" menu actions.
#pragma once

#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QObject>

#include "database.h"
#include "extractor.h"

namespace playlistmenu {

// Fills `menu` with one action per existing playlist (each adds `item` to it),
// then "New Playlist…" which prompts for a name. `parent` owns any dialogs.
inline void populate(QMenu* menu, Database* db, const SearchResult& item, QWidget* parent) {
    menu->clear();
    if (!db) {
        return;
    }
    const QList<Playlist> lists = db->playlists();
    for (const Playlist& p : lists) {
        const qint64 id = p.id;
        menu->addAction(p.name, parent, [db, id, item] { db->addToPlaylist(id, item); });
    }
    if (!lists.isEmpty()) {
        menu->addSeparator();
    }
    menu->addAction(QObject::tr("New Playlist…"), parent, [db, item, parent] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            parent, QObject::tr("New Playlist"), QObject::tr("Playlist name:"),
            QLineEdit::Normal, QObject::tr("My Playlist"), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            const qint64 id = db->createPlaylist(name.trimmed());
            if (id >= 0) {
                db->addToPlaylist(id, item);
            }
        }
    });
}

// Adds a "Save to Playlist ▸" submenu to `menu` (no-op without a database).
inline void addSubmenu(QMenu* menu, Database* db, const SearchResult& item, QWidget* parent) {
    if (!db) {
        return;
    }
    QMenu* sub = menu->addMenu(QObject::tr("Save to &Playlist"));
    populate(sub, db, item, parent);
}

}  // namespace playlistmenu
