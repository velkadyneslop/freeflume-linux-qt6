// FreeFlume — shared SponsorBlock category metadata (settings + player agree).
#pragma once

#include "apppaths.h"
#include <QColor>
#include <QCoreApplication>
#include <QList>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace sponsor {

// How a category behaves when its segment is reached.
enum class Mode { Disabled = 0, Auto = 1, Manual = 2 };

struct CategoryInfo {
    const char* key;      // SponsorBlock API key + QSettings sub-key
    const char* label;    // human label (tr() at use site)
    bool defaultOn;       // skipped by default?
    const char* color;    // seek-bar tint
};

// The categories FreeFlume exposes, in display order.
inline QList<CategoryInfo> categories() {
    return {
        {"sponsor", QT_TRANSLATE_NOOP("SponsorBlock", "Sponsor"), true, "#00d400"},
        {"selfpromo", QT_TRANSLATE_NOOP("SponsorBlock", "Unpaid/self promotion"), true, "#ffff00"},
        {"interaction", QT_TRANSLATE_NOOP("SponsorBlock", "Interaction reminder"), true, "#cc00ff"},
        {"intro", QT_TRANSLATE_NOOP("SponsorBlock", "Intro / intermission"), false, "#00ffff"},
        {"outro", QT_TRANSLATE_NOOP("SponsorBlock", "Outro / endcards"), false, "#0202ed"},
        {"preview", QT_TRANSLATE_NOOP("SponsorBlock", "Preview / recap"), false, "#008fd6"},
        {"filler", QT_TRANSLATE_NOOP("SponsorBlock", "Filler tangent"), false, "#7300ff"},
        {"music_offtopic", QT_TRANSLATE_NOOP("SponsorBlock", "Non-music section"), true, "#ff9900"},
    };
}

// True when SponsorBlock is enabled at all.
inline bool enabled() {
    return QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("sponsorblock/enabled"), false).toBool();
}

// The configured behaviour for a category key.
inline Mode modeFor(const QString& key) {
    if (!enabled()) {
        return Mode::Disabled;
    }
    QSettings s(apppaths::configFile(), QSettings::IniFormat);
    for (const CategoryInfo& c : categories()) {
        if (key == QLatin1String(c.key)) {
            const int def = c.defaultOn ? int(Mode::Auto) : int(Mode::Disabled);
            const QString k = QStringLiteral("sponsorblock/mode/") + key;
            return static_cast<Mode>(qBound(0, s.value(k, def).toInt(), 2));
        }
    }
    return Mode::Disabled;
}

// Category keys that need fetching (auto or manual — anything but disabled).
inline QStringList enabledCategories() {
    if (!enabled()) {
        return {};
    }
    QStringList out;
    for (const CategoryInfo& c : categories()) {
        if (modeFor(QLatin1String(c.key)) != Mode::Disabled) {
            out << QLatin1String(c.key);
        }
    }
    return out;
}

// Human-readable label for a category key.
inline QString labelFor(const QString& key) {
    for (const CategoryInfo& c : categories()) {
        if (key == QLatin1String(c.key)) {
            return QCoreApplication::translate("SponsorBlock", c.label);
        }
    }
    return key;
}

// Seek-bar colour for a category key (falls back to a neutral green).
inline QColor colorFor(const QString& key) {
    for (const CategoryInfo& c : categories()) {
        if (key == QLatin1String(c.key)) {
            return QColor(QLatin1String(c.color));
        }
    }
    return QColor(QStringLiteral("#00d400"));
}

}  // namespace sponsor
