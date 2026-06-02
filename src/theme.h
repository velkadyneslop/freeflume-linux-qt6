// FreeFlume — theme helpers (color scheme + widget style).
//
// We never hardcode colors. On KDE we inherit Breeze; on GNOME the gtk3
// platform theme is used; everywhere we can follow the system light/dark
// scheme or let the user override it.
#pragma once

#include <QString>
#include <QStringList>

namespace theme {

// "system" (follow OS), "light", or "dark".
void applyColorScheme(const QString& mode);

// A QStyleFactory key, or "native" to keep the platform default.
void applyStyle(const QString& styleName);

// Style keys available on this system, with "native" prepended.
QStringList availableStyles();

// Reads the saved preferences (QSettings) and applies them. Call once at
// startup after QApplication is constructed.
void applySaved();

}  // namespace theme
