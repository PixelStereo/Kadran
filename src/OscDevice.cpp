#include "OscDevice.h"

#include "KadranServer.h"

#include <pxs/Convert.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

using pxs::controls::Control;
using pxs::core::Value;
using pxs::core::ValueList;
using pxs::core::tree::ArgRange;

namespace {
// The tree speaks std::string and Value (pxs-core); the rest of Kadran, QString and QVariant: the edge is here
std::string u8(const QString &s) { return pxs::convert::toUtf8(s); }
double number(const Value &v) { return v.number().value_or(0.0); }
int whole(const Value &v) { return int(std::lround(number(v))); }
ArgRange between(double lo, double hi) { return ArgRange{Value(lo), Value(hi), {}, {}}; }
ArgRange oneOf(const QStringList &names)
{
    ArgRange r;
    for (const QString &name : names) r.vals.emplace_back(u8(name));
    return r;
}
} // namespace

OscDevice::OscDevice(Explorer *explorer, KadranServer *server, const QString &name, QObject *parent)
    : QObject(parent), m_ex(explorer), m_server(server), m_service(&m_tree, name, this)
{
    // The current page can be read and written
    pxs::core::tree::Node &page = m_tree.add("/kadran/page");
    page.type = "i";
    page.access = 3;
    page.description = "The page shown, 1 for the first: the same one on every screen";
    page.get = [this] { return ValueList{Value(pageIndex())}; };
    page.set = [this](const ValueList &a) {
        if (a.empty()) return false;
        showPage(whole(a[0]));
        return true;
    };

    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(100); // what changes together is rebuilt once
    connect(&m_rebuild, &QTimer::timeout, this, &OscDevice::rebuild);
    connect(m_ex, &Explorer::controlsChanged, &m_rebuild, qOverload<>(&QTimer::start));
    connect(m_server, &pxs::controls::Server::pageShown, this, [this] { pump(); });
    rebuild();
}

bool OscDevice::start(quint16 oscPort, quint16 httpPort)
{
    if (!m_service.start(oscPort, httpPort) && !m_service.start(0, 0)) return false;
    m_pump.start();
    return true;
}

QString OscDevice::slug(const QString &name)
{
    QString s = name.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    while (s.startsWith(QLatin1Char('_'))) s.remove(0, 1);
    while (s.endsWith(QLatin1Char('_'))) s.chop(1);
    return s;
}

QString OscDevice::unique(const QString &base, QSet<QString> &taken)
{
    QString given = base.isEmpty() ? QStringLiteral("x") : base;
    for (int n = 2; taken.contains(given); ++n) given = base + QLatin1Char('_') + QString::number(n);
    taken.insert(given);
    return given;
}

std::vector<OscDevice::Page> OscDevice::pages() const
{
    std::vector<Page> out;
    for (const Control &c : m_ex->controls())
        if (c.kind == Control::Kind::Group && !c.group) out.push_back({c.id, c.name});
    return out;
}

int OscDevice::pageIndex() const
{
    const std::vector<Page> list = pages();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == m_server->currentPage()) return int(i) + 1;
    return list.empty() ? 0 : 1;
}

void OscDevice::showPage(int index)
{
    const std::vector<Page> list = pages();
    if (list.empty()) return;
    const int n = int(list.size());
    m_server->setCurrentPage(list[size_t(((index - 1) % n + n) % n)].id);
}

// The widgets as OSCQuery nodes: a kind each of them takes, its path, its range
static bool exposed(const Control &c)
{
    switch (c.kind) {
    case Control::Kind::Slider:
    case Control::Kind::Toggle:
    case Control::Kind::Choice:
    case Control::Kind::Button:
    case Control::Kind::Pad: return true;
    case Control::Kind::Points: return c.points.size() == 1;
    default: return false;
    }
}

QString OscDevice::controlsSignature(const std::vector<Control> &list) const
{
    QString sig;
    for (const Control &c : list)
        if (exposed(c)) sig += QStringLiteral("%1|%2|%3|%4|%5|%6|%7\n").arg(c.id).arg(c.key).arg(int(c.kind)).arg(c.min).arg(c.max).arg(c.choices.join(QLatin1Char(','))).arg(c.group);
    for (const Control &c : list)
        if (c.kind == Control::Kind::Group) sig += QStringLiteral("g%1|%2\n").arg(c.id).arg(c.name);
    return sig;
}

void OscDevice::rebuild()
{
    const std::vector<Control> all = m_ex->controls();
    const std::vector<Page> list = pages();

    // The pages
    QString sig;
    for (const Page &p : list) sig += QString::number(p.id) + QLatin1Char('|') + p.name + QLatin1Char('\n');
    if (sig != m_pagesSeen) {
        m_pagesSeen = sig;
        m_tree.remove("/kadran/pages");
        m_tree.addAction("/kadran/pages/next", "N", [this](const ValueList &) { showPage(pageIndex() + 1); return true; }, "The next page");
        m_tree.addAction("/kadran/pages/prev", "N", [this](const ValueList &) { showPage(pageIndex() - 1); return true; }, "The previous page");
        QSet<QString> taken{QStringLiteral("next"), QStringLiteral("prev")};
        for (const Page &p : list) {
            const quint64 id = p.id;
            m_tree.addAction("/kadran/pages/" + u8(unique(slug(p.name), taken)), "N",
                             [this, id](const ValueList &) { m_server->setCurrentPage(id); return true; }, "Show the page " + u8(p.name));
        }
        if (pxs::core::tree::Node *n = m_tree.find("/kadran/page"))
            n->range = {between(1, std::max<int>(1, int(list.size())))};
    }

    // The widgets
    const QString csig = controlsSignature(all);
    if (csig != m_controlsSeen) {
        m_controlsSeen = csig;
        for (auto it = m_paths.begin(); it != m_paths.end(); ++it) m_told.remove(it.key());
        m_paths.clear();
        m_tree.remove("/kadran/controls");
        QHash<quint64, QString> pageOf; // a control's page: the top-level group that holds it
        for (const Control &c : all) {
            quint64 top = c.group;
            for (int guard = 0; top && guard < 8; ++guard)
                if (const Control *g = pxs::controls::find(all, top); g && g->group) top = g->group; else break;
            if (c.kind != Control::Kind::Group)
                for (const Control &g : all)
                    if (g.id == top && g.kind == Control::Kind::Group) pageOf.insert(c.id, g.name);
        }
        QSet<QString> pageNames;
        QHash<QString, QString> pageSlugs; // the page's name -> its slug
        for (const Control &c : all) {
            if (!exposed(c)) continue;
            const QString pageName = pageOf.value(c.id, QStringLiteral("main"));
            if (!pageSlugs.contains(pageName)) pageSlugs.insert(pageName, unique(slug(pageName), pageNames));
            const QString path = QStringLiteral("/kadran/controls/") + pageSlugs.value(pageName) + QLatin1Char('/') + slug(c.key.isEmpty() ? c.name : c.key);
            const std::string where = u8(path);
            if (m_tree.find(where)) continue; // two with the same key: the first
            const quint64 id = c.id;
            const auto read = [this, id](auto field) -> double { // what the widget shows now, as a number
                const auto now = m_ex->control(id);
                return now ? double(field(*now)) : 0.0;
            };
            if (c.kind == Control::Kind::Button) {
                m_tree.addAction(where, "N", [this, id](const ValueList &) {
                    const bool on = m_ex->setValue(id, 1);
                    m_ex->setValue(id, 0);
                    return on;
                }, u8(c.name));
                continue;
            }
            pxs::core::tree::Node &n = m_tree.add(where);
            n.access = 3;
            n.description = u8(c.name);
            if (c.kind == Control::Kind::Slider) {
                n.type = "f";
                n.range = {between(c.min, c.max)};
                n.clip = "both";
                n.get = [read] { return ValueList{Value(read([](const Control &k) { return k.value; }))}; };
                n.set = [this, id](const ValueList &a) { return !a.empty() && m_ex->setValue(id, number(a[0])); };
            } else if (c.kind == Control::Kind::Toggle) {
                n.type = "T";
                n.get = [read] { return ValueList{Value(read([](const Control &k) { return k.value; }) >= 0.5)}; };
                n.set = [this, id](const ValueList &a) { return !a.empty() && m_ex->setValue(id, a[0].truthy() || number(a[0]) >= 0.5 ? 1 : 0); };
            } else if (c.kind == Control::Kind::Choice) {
                n.type = "s";
                n.range = {oneOf(c.choices)};
                n.get = [this, id] {
                    const auto now = m_ex->control(id);
                    const int i = now ? int(now->value) : 0;
                    return ValueList{Value(u8(now && i >= 0 && i < now->choices.size() ? now->choices[i] : QString()))};
                };
                n.set = [this, id](const ValueList &a) {
                    if (a.empty()) return false;
                    const auto now = m_ex->control(id);
                    if (!now) return false;
                    int i = now->choices.indexOf(pxs::convert::fromUtf8(a[0].text())); // its name, or its index
                    if (i < 0 && !a[0].is<std::string>()) i = whole(a[0]);
                    return i >= 0 && i < now->choices.size() && m_ex->setValue(id, i);
                };
            } else if (c.kind == Control::Kind::Pad) {
                n.type = "ff";
                n.range = {between(c.min, c.max), between(c.min, c.max)};
                n.clip = "both";
                n.get = [this, id] {
                    const auto now = m_ex->control(id);
                    return ValueList{Value(now ? double(now->value) : 0.0), Value(now ? double(now->value2) : 0.0)};
                };
                n.set = [this, id](const ValueList &a) { return a.size() >= 2 && m_ex->setPoint(id, number(a[0]), number(a[1])); };
            } else { // one ball
                n.type = "ff";
                const auto &b = c.balls.empty() ? pxs::controls::Ball() : c.balls[0];
                n.range = {between(b.minX, b.maxX), between(b.minY, b.maxY)};
                n.get = [this, id] {
                    const auto now = m_ex->control(id);
                    return ValueList{Value(now && !now->points.empty() ? double(now->points[0][0]) : 0.0), Value(now && !now->points.empty() ? double(now->points[0][1]) : 0.0)};
                };
                n.set = [this, id](const ValueList &a) { return a.size() >= 2 && m_ex->setPoints(id, {number(a[0]), number(a[1])}); };
            }
            m_paths.insert(path, id);
            m_told.insert(path, n.get());
        }
    }
}

void OscDevice::pump()
{
    const auto tell = [this](const QString &path) {
        const std::string where = u8(path);
        const pxs::core::tree::Node *n = m_tree.find(where);
        if (!n || !n->get) return;
        const ValueList now = n->get();
        const auto seen = m_told.constFind(path);
        if (seen != m_told.cend() && *seen == now) return;
        m_told.insert(path, now);
        m_tree.changed(where);
    };
    tell(QStringLiteral("/kadran/page"));
    for (auto it = m_paths.cbegin(); it != m_paths.cend(); ++it) tell(it.key());
}
