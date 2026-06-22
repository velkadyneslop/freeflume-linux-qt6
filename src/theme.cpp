// FreeFlume — theme helpers implementation.
#include "apppaths.h"
#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace theme {
namespace {

// Running as an AppImage? Then Qt has no desktop integration, so we bundle the
// Breeze icons + pick the style/scheme ourselves. Native/Flatpak get this from
// the host/runtime and must be left alone.
bool inAppImage() {
    return qEnvironmentVariableIsSet("FREEFLUME_APPIMAGE") ||
           qEnvironmentVariableIsSet("APPIMAGE");
}

// Ask the freedesktop appearance portal whether the host prefers dark (works on
// KDE + GNOME). 1 = dark, 2 = light, 0 = no preference.
bool systemPrefersDark() {
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.Settings"),
                          QDBusConnection::sessionBus());
    for (const char* method : {"ReadOne", "Read"}) {
        QDBusReply<QDBusVariant> reply =
            portal.call(QString::fromLatin1(method), QStringLiteral("org.freedesktop.appearance"),
                        QStringLiteral("color-scheme"));
        if (reply.isValid()) {
            return reply.value().variant().toUInt() == 1;
        }
    }
    return false;
}

// In the AppImage, select the bundled Breeze theme so fromTheme() resolves and
// the icons match the light/dark look. No-op natively.
void applyBundledIconTheme(bool dark) {
    if (!inAppImage()) {
        return;
    }
    const QString name = dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze");
    QIcon::setThemeName(name);
    QIcon::setFallbackThemeName(name);
}
// The platform's default style key, captured before any override so we can
// restore "native" at runtime.
QString defaultStyleKey() {
    static const QString key = QApplication::style()->name();
    return key;
}

// The palette the platform gave us at startup — used to restore "Follow system".
const QPalette& defaultPalette() {
    static const QPalette pal = QApplication::palette();
    return pal;
}

QPalette lightPalette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor(0xf5, 0xf5, 0xf5));
    p.setColor(QPalette::WindowText, QColor(0x1a, 0x1a, 0x1a));
    p.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::AlternateBase, QColor(0xed, 0xed, 0xed));
    p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::ToolTipText, QColor(0x1a, 0x1a, 0x1a));
    p.setColor(QPalette::Text, QColor(0x1a, 0x1a, 0x1a));
    p.setColor(QPalette::Button, QColor(0xe8, 0xe8, 0xe8));
    p.setColor(QPalette::ButtonText, QColor(0x1a, 0x1a, 0x1a));
    p.setColor(QPalette::Highlight, QColor(0x2a, 0x82, 0xda));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x18, 0x6a, 0xc4));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x9a, 0x9a, 0x9a));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x9a, 0x9a, 0x9a));
    return p;
}

QPalette darkPalette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x35, 0x35, 0x35));
    p.setColor(QPalette::WindowText, QColor(0xea, 0xea, 0xea));
    p.setColor(QPalette::Base, QColor(0x23, 0x23, 0x23));
    p.setColor(QPalette::AlternateBase, QColor(0x2e, 0x2e, 0x2e));
    p.setColor(QPalette::ToolTipBase, QColor(0x23, 0x23, 0x23));
    p.setColor(QPalette::ToolTipText, QColor(0xea, 0xea, 0xea));
    p.setColor(QPalette::Text, QColor(0xea, 0xea, 0xea));
    p.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
    p.setColor(QPalette::ButtonText, QColor(0xea, 0xea, 0xea));
    p.setColor(QPalette::Highlight, QColor(0x2a, 0x82, 0xda));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x5a, 0xb0, 0xff));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x7a, 0x7a, 0x7a));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x7a, 0x7a, 0x7a));
    return p;
}
}  // namespace

void applyColorScheme(const QString& mode) {
    defaultPalette();  // capture once, before any override
    // QStyleHints::setColorScheme is Qt 6.8+. On older Qt the palette override
    // below still applies the light/dark look; the style just isn't told the
    // scheme (used by the AppImage's bundled Qt).
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QStyleHints* hints = QApplication::styleHints();
#endif
    bool dark = false;
    if (mode == QLatin1String("light")) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        hints->setColorScheme(Qt::ColorScheme::Light);
#endif
        QApplication::setPalette(lightPalette());
        dark = false;
    } else if (mode == QLatin1String("dark")) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        hints->setColorScheme(Qt::ColorScheme::Dark);
#endif
        QApplication::setPalette(darkPalette());
        dark = true;
    } else if (inAppImage()) {
        // No desktop integration in the AppImage — follow the host via the
        // appearance portal and apply our own palette.
        dark = systemPrefersDark();
        QApplication::setPalette(dark ? darkPalette() : lightPalette());
    } else {
        // Native/Flatpak: drop our override and let the platform follow the system.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        hints->setColorScheme(Qt::ColorScheme::Unknown);
#endif
        QApplication::setPalette(defaultPalette());
        dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    }
    applyBundledIconTheme(dark);
}

void applyStyle(const QString& styleName) {
    QString target;
    if (styleName.isEmpty() || styleName == QLatin1String("native")) {
        if (inAppImage()) {
            // The AppImage bundles the Qt6 Breeze style, so default to it for the
            // KDE look that matches the Flatpak. If that plugin somehow isn't
            // present, fall back to Fusion (always built into Qt) for a clean,
            // consistent look. Everywhere else keep the platform default.
            target = QStyleFactory::keys().contains(QStringLiteral("Breeze"))
                         ? QStringLiteral("Breeze")
                         : QStringLiteral("Fusion");
        } else {
            target = defaultStyleKey();
        }
    } else {
        target = styleName;
    }
    if (QStyle* s = QStyleFactory::create(target)) {
        QApplication::setStyle(s);
    }
}

QStringList availableStyles() {
    QStringList keys{QStringLiteral("native")};
    keys += QStyleFactory::keys();
    return keys;
}

void applySaved() {
    defaultStyleKey();  // capture default before any override
    QSettings s(apppaths::configFile(), QSettings::IniFormat);
    applyStyle(s.value(QStringLiteral("appearance/style"),
                       QStringLiteral("native")).toString());
    applyColorScheme(s.value(QStringLiteral("appearance/colorScheme"),
                             QStringLiteral("system")).toString());
}

}  // namespace theme
