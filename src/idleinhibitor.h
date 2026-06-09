// FreeFlume — block the screensaver/lock and idle-suspend during playback.
//
// We render mpv into a QOpenGLWidget (vo=libmpv), so mpv can't tell the
// compositor to stay awake. Instead we hold an inhibit ourselves while a video
// is playing. Preferred path is the XDG desktop portal (its compositor backend
// inhibits the *real* idle/lock on Wayland — the legacy org.freedesktop.
// ScreenSaver call doesn't reach KWin's locker on Plasma 6 — and it works inside
// the Flatpak sandbox); the old screensaver/power-management calls are a
// fallback for systems without a portal.
#pragma once

#include <QList>
#include <QString>

class IdleInhibitor {
public:
    ~IdleInhibitor();
    void setActive(bool on);  // idempotent: on -> inhibit, off -> release

private:
    void acquire();
    void releaseAll();

    QString portalRequest_;  // XDG portal Inhibit request object path (preferred)
    struct Held {            // legacy freedesktop inhibit cookies (fallback)
        QString service;
        QString path;
        QString iface;
        unsigned int cookie;
    };
    QList<Held> held_;
    bool active_ = false;
};
