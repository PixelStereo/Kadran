#pragma once
// The Kadran logo (packaging/kadran.svg, minified): the front page's icon and the logo of its About window
#include <QByteArray>
#include <QUrl>

namespace logo {
inline constexpr char kSvg[] = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512"><defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2a2e3a"/><stop offset="1" stop-color="#14161c"/></linearGradient><linearGradient id="lit" x1="0" y1="1" x2="1" y2="0"><stop offset="0" stop-color="#3fa9ff"/><stop offset="1" stop-color="#7ff0d4"/></linearGradient></defs><rect width="512" height="512" rx="112" fill="url(#bg)"/><path d="M 140.7 371.3 A 160 160 0 1 1 371.3 371.3" fill="none" stroke="#3a3f4e" stroke-width="30" stroke-linecap="round"/><path d="M 140.7 371.3 A 160 160 0 0 1 345 129" fill="none" stroke="url(#lit)" stroke-width="30" stroke-linecap="round"/><line x1="256" y1="256" x2="345" y2="129" stroke="#f2f5fa" stroke-width="22" stroke-linecap="round"/><circle cx="256" cy="256" r="30" fill="#f2f5fa"/><circle cx="256" cy="256" r="11" fill="#14161c"/></svg>)SVG";

// As a data: URI, for an <img> or a <link rel=icon>
inline QByteArray dataUri() { return "data:image/svg+xml," + QUrl::toPercentEncoding(QString::fromLatin1(kSvg)); }
} // namespace logo
