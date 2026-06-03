// FreeFlume — theme helpers implementation.
#include "apppaths.h"
#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace theme {
namespace {
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
    if (mode == QLatin1String("light")) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        hints->setColorScheme(Qt::ColorScheme::Light);
#endif
        QApplication::setPalette(lightPalette());
    } else if (mode == QLatin1String("dark")) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        hints->setColorScheme(Qt::ColorScheme::Dark);
#endif
        QApplication::setPalette(darkPalette());
    } else {
        // Follow the system: drop our override and restore the platform palette.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        hints->setColorScheme(Qt::ColorScheme::Unknown);
#endif
        QApplication::setPalette(defaultPalette());
    }
}

void applyStyle(const QString& styleName) {
    const QString target =
        (styleName.isEmpty() || styleName == QLatin1String("native"))
            ? defaultStyleKey()
            : styleName;
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
