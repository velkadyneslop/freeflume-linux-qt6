#include "idleinhibitor.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace {
constexpr char kApp[] = "FreeFlume";
constexpr char kReason[] = "Playing video";
}  // namespace

IdleInhibitor::~IdleInhibitor() {
    releaseAll();
}

void IdleInhibitor::setActive(bool on) {
    if (on == active_) {
        return;
    }
    active_ = on;
    if (on) {
        acquire();
    } else {
        releaseAll();
    }
}

void IdleInhibitor::acquire() {
    auto tryOne = [this](const char* service, const char* path, const char* iface) -> bool {
        QDBusInterface obj(QString::fromLatin1(service), QString::fromLatin1(path),
                           QString::fromLatin1(iface), QDBusConnection::sessionBus());
        if (!obj.isValid()) {
            return false;
        }
        QDBusReply<unsigned int> reply =
            obj.call(QStringLiteral("Inhibit"), QString::fromLatin1(kApp),
                     QString::fromLatin1(kReason));
        if (!reply.isValid()) {
            return false;
        }
        held_.push_back({QString::fromLatin1(service), QString::fromLatin1(path),
                         QString::fromLatin1(iface), reply.value()});
        return true;
    };

    // Screensaver / screen lock (one path or the other depending on the DE).
    if (!tryOne("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                "org.freedesktop.ScreenSaver")) {
        tryOne("org.freedesktop.ScreenSaver", "/ScreenSaver", "org.freedesktop.ScreenSaver");
    }
    // Power management (auto-suspend on idle).
    tryOne("org.freedesktop.PowerManagement.Inhibit", "/org/freedesktop/PowerManagement/Inhibit",
           "org.freedesktop.PowerManagement.Inhibit");
}

void IdleInhibitor::releaseAll() {
    for (const Held& h : held_) {
        QDBusInterface obj(h.service, h.path, h.iface, QDBusConnection::sessionBus());
        obj.call(QStringLiteral("UnInhibit"), h.cookie);
    }
    held_.clear();
}
