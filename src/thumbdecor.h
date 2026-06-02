// FreeFlume — paints a watch-progress bar / "watched" dimming onto a thumbnail.
#pragma once

#include <QPainter>
#include <QPixmap>
#include <QSize>

#include "database.h"  // WatchProgress

namespace thumbdecor {

// Scales `src` to fill `size` (cropping the overflow, like the plain thumbnails),
// then overlays a YT-style red progress line and dims it if watched.
inline QPixmap apply(const QPixmap& src, QSize size, const WatchProgress& p) {
    QPixmap pm = src.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (pm.size() != size) {  // centre-crop to the exact cell size
        const int x = (pm.width() - size.width()) / 2;
        const int y = (pm.height() - size.height()) / 2;
        pm = pm.copy(x, y, size.width(), size.height());
    }

    const bool hasProgress = p.duration > 0 && p.position > 0;
    if (!p.completed && !hasProgress) {
        return pm;  // never watched — leave it untouched
    }

    QPainter painter(&pm);
    const int w = pm.width(), h = pm.height();
    if (p.completed) {
        painter.fillRect(pm.rect(), QColor(0, 0, 0, 120));  // dim watched videos
    }
    // Progress line along the bottom.
    const double frac = p.completed
                            ? 1.0
                            : qBound(0.0, static_cast<double>(p.position) / p.duration, 1.0);
    const int barH = 3;
    painter.fillRect(0, h - barH, w, barH, QColor(255, 255, 255, 70));  // track
    painter.fillRect(0, h - barH, static_cast<int>(w * frac), barH, QColor(230, 40, 40));
    return pm;
}

}  // namespace thumbdecor
