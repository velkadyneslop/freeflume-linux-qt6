#include "idleinhibitor.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QVariantMap>

namespace {
constexpr char kApp[] = "FreeFlume";
constexpr char kReason[] = "Playing video";
// XDG portal Inhibit flags: 4 = suspend, 8 = idle (screensaver/lock).
constexpr unsigned int kFlagSuspend = 4;
constexpr unsigned int kFlagIdle = 8;
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
    // Preferred: the XDG desktop portal. Its compositor backend inhibits the
    // real idle/lock on Wayland (which org.freedesktop.ScreenSaver doesn't reach
    // on Plasma 6) and works in the Flatpak sandbox without extra permissions.
    {
        QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                              QStringLiteral("/org/freedesktop/portal/desktop"),
                              QStringLiteral("org.freedesktop.portal.Inhibit"),
                              QDBusConnection::sessionBus());
        if (portal.isValid()) {
            QVariantMap options;
            options.insert(QStringLiteral("reason"), QString::fromLatin1(kReason));
            QDBusReply<QDBusObjectPath> reply =
                portal.call(QStringLiteral("Inhibit"), QString(),
                            kFlagSuspend | kFlagIdle, options);
            if (reply.isValid()) {
                portalRequest_ = reply.value().path();
                return;  // portal handled it; skip the legacy fallback
            }
        }
    }

    // Fallback (no portal): the legacy screensaver + power-management inhibits.
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
    if (!portalRequest_.isEmpty()) {
        QDBusInterface req(QStringLiteral("org.freedesktop.portal.Desktop"), portalRequest_,
                           QStringLiteral("org.freedesktop.portal.Request"),
                           QDBusConnection::sessionBus());
        req.call(QStringLiteral("Close"));
        portalRequest_.clear();
    }
    for (const Held& h : held_) {
        QDBusInterface obj(h.service, h.path, h.iface, QDBusConnection::sessionBus());
        obj.call(QStringLiteral("UnInhibit"), h.cookie);
    }
    held_.clear();
}
