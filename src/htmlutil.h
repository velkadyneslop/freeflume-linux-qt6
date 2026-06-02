// FreeFlume — small HTML helpers.
#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace htmlutil {

// Escapes plain text for HTML, turns http(s) URLs into clickable links, and
// converts newlines to <br>. Use with a QTextBrowser that has
// setOpenExternalLinks(true).
inline QString linkify(const QString& text) {
    QString s = text.toHtmlEscaped();
    static const QRegularExpression re(QStringLiteral("(https?://[^\\s<]+)"));
    s.replace(re, QStringLiteral("<a href=\"\\1\">\\1</a>"));
    s.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return s;
}

// Parses "M:SS" or "H:MM:SS" into seconds.
inline qint64 parseTimestamp(const QString& ts) {
    qint64 secs = 0;
    for (const QString& part : ts.split(QLatin1Char(':'))) {
        secs = secs * 60 + part.toLongLong();
    }
    return secs;
}

namespace detail {
inline QString timestampsInPlain(const QString& text) {
    static const QRegularExpression tsRe(QStringLiteral("\\b(?:\\d{1,2}:)?\\d{1,2}:\\d{2}\\b"));
    QString out;
    int pos = 0;
    auto it = tsRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(pos, m.capturedStart() - pos);
        const QString ts = m.captured();
        out += QStringLiteral("<a href=\"freeflume://seek/%1\">%2</a>")
                   .arg(parseTimestamp(ts))
                   .arg(ts);
        pos = m.capturedEnd();
    }
    out += text.mid(pos);
    return out;
}
}  // namespace detail

// Turns "1:23"-style timestamps into seek links, but leaves any text already
// inside an <a>…</a> (e.g. a linkified URL) untouched. Run on linkify()'s output.
inline QString linkifyTimestamps(const QString& html) {
    static const QRegularExpression anchorRe(
        QStringLiteral("<a\\b[^>]*>.*?</a>"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    QString out;
    int pos = 0;
    auto it = anchorRe.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        out += detail::timestampsInPlain(html.mid(pos, m.capturedStart() - pos));
        out += m.captured();  // keep existing anchors verbatim
        pos = m.capturedEnd();
    }
    out += detail::timestampsInPlain(html.mid(pos));
    return out;
}

}  // namespace htmlutil
