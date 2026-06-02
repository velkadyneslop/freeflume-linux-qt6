// FreeFlume — user-configurable player keyboard shortcuts.
#include "apppaths.h"
#include "shortcuts.h"

#include <QSettings>
#include <Qt>

namespace shortcuts {

const QList<Action>& actions() {
    static const QList<Action> kActions = {
        {QStringLiteral("playPause"), QStringLiteral("Play / Pause"), Qt::Key_Space},
        {QStringLiteral("seekBack"), QStringLiteral("Seek backward 5s"), Qt::Key_Left},
        {QStringLiteral("seekForward"), QStringLiteral("Seek forward 5s"), Qt::Key_Right},
        {QStringLiteral("prevFrame"), QStringLiteral("Previous frame"), Qt::Key_Comma},
        {QStringLiteral("nextFrame"), QStringLiteral("Next frame"), Qt::Key_Period},
        {QStringLiteral("volUp"), QStringLiteral("Volume up"), Qt::Key_Up},
        {QStringLiteral("volDown"), QStringLiteral("Volume down"), Qt::Key_Down},
        {QStringLiteral("mute"), QStringLiteral("Mute"), Qt::Key_M},
        {QStringLiteral("subtitles"), QStringLiteral("Cycle subtitles"), Qt::Key_C},
        {QStringLiteral("fullscreen"), QStringLiteral("Fullscreen"), Qt::Key_F},
        {QStringLiteral("loop"), QStringLiteral("Loop video"), Qt::Key_R},
        {QStringLiteral("nextVideo"), QStringLiteral("Next video"), Qt::Key_N},
        {QStringLiteral("prevVideo"), QStringLiteral("Previous video"), Qt::Key_P},
        {QStringLiteral("screenshot"), QStringLiteral("Take screenshot"), Qt::Key_S},
        {QStringLiteral("info"), QStringLiteral("Toggle info panel"), Qt::Key_I},
        {QStringLiteral("queue"), QStringLiteral("Toggle queue panel"), Qt::Key_Q},
    };
    return kActions;
}

int keyFor(const QString& id) {
    int def = 0;
    for (const Action& a : actions()) {
        if (a.id == id) {
            def = a.defaultKey;
            break;
        }
    }
    const QVariant v = QSettings(apppaths::configFile(), QSettings::IniFormat).value(QStringLiteral("shortcuts/") + id);
    if (!v.isValid()) {
        return def;
    }
    bool ok = false;
    const int code = v.toInt(&ok);
    return ok ? code : def;  // ignore stale (e.g. older string-format) values
}

}  // namespace shortcuts
