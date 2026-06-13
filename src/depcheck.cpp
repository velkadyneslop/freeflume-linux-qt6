// FreeFlume — runtime dependency check implementation.
#include "depcheck.h"

#include "apppaths.h"

#include <QFile>
#include <QMessageBox>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace {

// A field from /etc/os-release (value unquoted), or "" if absent.
QString osRelease(const QString& key) {
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString prefix = key + QLatin1Char('=');
    for (const QByteArray& raw : f.readAll().split('\n')) {
        QString line = QString::fromUtf8(raw).trimmed();
        if (!line.startsWith(prefix)) {
            continue;
        }
        QString v = line.mid(prefix.size()).trimmed();
        if (v.size() >= 2 && (v.startsWith(QLatin1Char('"')) || v.startsWith(QLatin1Char('\'')))) {
            v = v.mid(1, v.size() - 2);
        }
        return v;
    }
    return {};
}

// The distro's yt-dlp install command, or "" when the distro is unrecognized.
// yt-dlp is packaged as "yt-dlp" across every major distro, so only the package
// manager differs (verified against Fedora; ID_LIKE catches derivatives).
QString distroYtdlpCommand() {
    const QString id = osRelease(QStringLiteral("ID")).toLower();
    const QStringList like = osRelease(QStringLiteral("ID_LIKE")).toLower().split(
        QLatin1Char(' '), Qt::SkipEmptyParts);
    auto is = [&](const char* d) {
        return id == QLatin1String(d) || like.contains(QLatin1String(d));
    };
    if (is("fedora") || is("rhel") || is("centos")) return QStringLiteral("sudo dnf install yt-dlp");
    if (is("debian") || is("ubuntu")) return QStringLiteral("sudo apt install yt-dlp");
    if (is("arch")) return QStringLiteral("sudo pacman -S yt-dlp");
    if (is("opensuse") || is("suse")) return QStringLiteral("sudo zypper install yt-dlp");
    if (is("alpine")) return QStringLiteral("sudo apk add yt-dlp");
    if (is("void")) return QStringLiteral("sudo xbps-install yt-dlp");
    if (is("gentoo")) return QStringLiteral("sudo emerge net-misc/yt-dlp");
    return {};
}

// Why Deno matters + how to get it. YouTube gates high-res streams behind a JS
// "nsig" challenge that yt-dlp solves by running the player code in Deno; without
// it, playback is capped to low-res/SABR formats.
QString denoHint() {
    return QObject::tr(
        "<b>For full-resolution playback, install Deno.</b> YouTube hides its "
        "high-resolution streams behind a JavaScript challenge that yt-dlp solves by "
        "running the player code in Deno — without it, playback is capped to lower "
        "quality.<br><code>curl -fsSL https://deno.land/install.sh | sh</code><br>"
        "(Arch: <code>sudo pacman -S deno</code>. The Flatpak build already bundles it.)");
}

}  // namespace

void depcheck::warnIfMissing(QWidget* parent) {
    const bool haveYtdlp = !QStandardPaths::findExecutable(QStringLiteral("yt-dlp")).isEmpty();
    const bool haveDeno = !QStandardPaths::findExecutable(QStringLiteral("deno")).isEmpty();
    if (haveYtdlp && haveDeno) {
        return;  // both present — nothing to do
    }

    if (!haveYtdlp) {
        // yt-dlp is required, so always warn (and fold in the Deno tip if it's
        // also missing).
        const QString cmd = distroYtdlpCommand();
        QString body = QObject::tr(
            "<b>FreeFlume needs yt-dlp</b> to search and play videos, but it isn't on "
            "your PATH.<br><br>");
        if (!cmd.isEmpty()) {
            body += QObject::tr("Install it with:<br><code>%1</code><br><br>").arg(cmd);
        }
        body += QObject::tr(
            "Or get the newest version (recommended — yt-dlp updates often to keep up "
            "with the site):<br><code>pipx install yt-dlp</code><br><br>"
            "Tip: the Flatpak build bundles yt-dlp, so there's nothing to install.");
        if (!haveDeno) {
            body += QStringLiteral("<br><hr>") + denoHint();
        }
        QMessageBox box(QMessageBox::Warning, QObject::tr("yt-dlp not found"), body,
                        QMessageBox::Ok, parent);
        box.setTextFormat(Qt::RichText);
        box.exec();
        return;
    }

    // yt-dlp is present but Deno isn't: Deno is optional (only affects quality),
    // so recommend it just once rather than nagging on every launch.
    QSettings s(apppaths::configFile(), QSettings::IniFormat);
    if (s.value(QStringLiteral("depcheck/denoHintShown"), false).toBool()) {
        return;
    }
    s.setValue(QStringLiteral("depcheck/denoHintShown"), true);
    QMessageBox box(QMessageBox::Information,
                    QObject::tr("Tip: install Deno for full-quality playback"), denoHint(),
                    QMessageBox::Ok, parent);
    box.setTextFormat(Qt::RichText);
    box.exec();
}
