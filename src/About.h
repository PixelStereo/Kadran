#pragma once
// Help > About: version, author, license and the credits of what Kadran is built on. One text for the window and the front page.
#include <QString>

// logo: the markup of the logo shown above the title (the page gives an <img>; the window shows the icon itself)
inline QString aboutHtml(const QString &version, const QString &logo = QString())
{
    struct Credit { const char *name, *url, *license; };
    static const Credit credits[] = {
        {"Qt 6", "https://www.qt.io", "LGPL-3.0 — the application and its network code"},
        {"pxs", "https://github.com/PixelStereo/pxs", "AGPL-3.0 — the controls page, the OSC and OSCQuery client, shared with NyxMap"},
        {"OSCQuery", "https://github.com/Vidvox/OSCQueryProposal", "the protocol of the devices Kadran explores"},
    };
    QString html = QStringLiteral(
        "%2<h2 style='margin:0'>Kadran</h2><p style='margin:2px 0'>OSCQuery explorer and control page · %1</p>"
        "<p style='margin:6px 0 0'>Play the OSCQuery devices of your network on a page of controls.<br>"
        "Built by <a href=\"https://pixelstereo.com\">Pixel Stereo</a>.</p>"
        "<h3 style='margin:12px 0 4px'>License</h3>"
        "<p style='margin:0'>Kadran is free software, released under the "
        "<a href=\"https://www.gnu.org/licenses/agpl-3.0.html\">GNU Affero General Public License, version 3</a>. "
        "You may use, study, share and modify it under the terms of that license; it comes without any warranty. "
        "The source is on <a href=\"https://github.com/PixelStereo/Kadran\">GitHub</a>.</p>"
        "<h3 style='margin:12px 0 4px'>Credits</h3><ul style='margin:0;padding-left:18px'>").arg(version.toHtmlEscaped(), logo);
    for (const Credit &c : credits) html += QStringLiteral("<li><a href=\"%1\">%2</a> — %3</li>").arg(QString::fromLatin1(c.url), QString::fromLatin1(c.name), QString::fromUtf8(c.license));
    return html + QStringLiteral("</ul>");
}
