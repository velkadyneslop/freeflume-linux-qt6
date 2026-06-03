// FreeFlume — block the screensaver/lock and idle-suspend during playback.
//
// We render mpv into a QOpenGLWidget (vo=libmpv), so mpv can't tell the
// compositor to stay awake. Instead we hold the standard freedesktop inhibit
// locks ourselves while a video is actually playing.
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

    struct Held {
        QString service;
        QString path;
        QString iface;
        unsigned int cookie;
    };
    QList<Held> held_;
    bool active_ = false;
};
