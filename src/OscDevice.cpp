#include "OscDevice.h"

#include "KadranServer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

using pxs::controls::Control;

OscDevice::OscDevice(Explorer *explorer, KadranServer *server, const QString &name, QObject *parent)
    : QObject(parent), m_ex(explorer), m_server(server), m_service(&m_tree, name, this)
{
    // The page and the view shown: parameters that anything can read and write
    pxs::Node &page = m_tree.add(QStringLiteral("/kadran/page"));
    page.type = QStringLiteral("i");
    page.access = 3;
    page.description = QStringLiteral("The page shown, 1 for the first: the same one on every screen");
    page.get = [this] { return QVariantList{pageIndex()}; };
    page.set = [this](const QVariantList &a) {
        if (a.isEmpty()) return false;
        showPage(a[0].toInt());
        return true;
    };
    pxs::Node &view = m_tree.add(QStringLiteral("/kadran/view"));
    view.type = QStringLiteral("s");
    view.access = 3;
    view.description = QStringLiteral("The view shown, by its name");
    view.get = [this] { return QVariantList{m_ex->activeView()}; };
    view.set = [this](const QVariantList &a) { return !a.isEmpty() && m_ex->recallView(a[0].toString()); };

    m_pump.setInterval(40);
    connect(&m_pump, &QTimer::timeout, this, &OscDevice::pump);
    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(100); // what changes together is rebuilt once
    connect(&m_rebuild, &QTimer::timeout, this, &OscDevice::rebuild);
    connect(m_ex, &Explorer::controlsChanged, &m_rebuild, qOverload<>(&QTimer::start));
    connect(m_ex, &Explorer::viewsChanged, &m_rebuild, qOverload<>(&QTimer::start));
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
        m_tree.remove(QStringLiteral("/kadran/pages"));
        m_tree.addAction(QStringLiteral("/kadran/pages/next"), QStringLiteral("N"), [this](const QVariantList &) { showPage(pageIndex() + 1); return true; }, QStringLiteral("The next page"));
        m_tree.addAction(QStringLiteral("/kadran/pages/prev"), QStringLiteral("N"), [this](const QVariantList &) { showPage(pageIndex() - 1); return true; }, QStringLiteral("The previous page"));
        QSet<QString> taken{QStringLiteral("next"), QStringLiteral("prev")};
        for (const Page &p : list) {
            const quint64 id = p.id;
            m_tree.addAction(QStringLiteral("/kadran/pages/") + unique(slug(p.name), taken), QStringLiteral("N"),
                             [this, id](const QVariantList &) { m_server->setCurrentPage(id); return true; }, QStringLiteral("Show the page ") + p.name);
        }
        if (pxs::Node *n = m_tree.find(QStringLiteral("/kadran/page")))
            n->range = QJsonArray{QJsonObject{{"MIN", 1}, {"MAX", std::max<int>(1, int(list.size()))}}};
    }

    // The views
    const QStringList views = m_ex->viewNames();
    if (views.join(QLatin1Char('\n')) != m_viewsSeen) {
        m_viewsSeen = views.join(QLatin1Char('\n'));
        m_tree.remove(QStringLiteral("/kadran/views"));
        const auto step = [this](int by) {
            const QStringList names = m_ex->viewNames();
            if (names.isEmpty()) return false;
            const int n = int(names.size()), at = names.indexOf(m_ex->activeView());
            return m_ex->recallView(names[((at + by) % n + n) % n]);
        };
        m_tree.addAction(QStringLiteral("/kadran/views/next"), QStringLiteral("N"), [step](const QVariantList &) { return step(1); }, QStringLiteral("The next view"));
        m_tree.addAction(QStringLiteral("/kadran/views/prev"), QStringLiteral("N"), [step](const QVariantList &) { return step(-1); }, QStringLiteral("The previous view"));
        QSet<QString> taken{QStringLiteral("next"), QStringLiteral("prev")};
        QJsonArray vals;
        for (const QString &name : views) {
            vals.append(name);
            m_tree.addAction(QStringLiteral("/kadran/views/") + unique(slug(name), taken), QStringLiteral("N"),
                             [this, name](const QVariantList &) { return m_ex->recallView(name); }, QStringLiteral("Show the view ") + name);
        }
        if (pxs::Node *n = m_tree.find(QStringLiteral("/kadran/view")))
            n->range = vals.isEmpty() ? QJsonArray() : QJsonArray{QJsonObject{{"VALS", vals}}};
    }

    // The widgets
    const QString csig = controlsSignature(all);
    if (csig != m_controlsSeen) {
        m_controlsSeen = csig;
        for (auto it = m_paths.begin(); it != m_paths.end(); ++it) m_told.remove(it.key());
        m_paths.clear();
        m_tree.remove(QStringLiteral("/kadran/controls"));
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
            if (m_tree.find(path)) continue; // two with the same key: the first
            const quint64 id = c.id;
            const auto number = [this, id](auto field) -> QVariant {
                const auto now = m_ex->control(id);
                return now ? QVariant(field(*now)) : QVariant(0.0);
            };
            if (c.kind == Control::Kind::Button) {
                m_tree.addAction(path, QStringLiteral("N"), [this, id](const QVariantList &) {
                    const bool on = m_ex->setValue(id, 1);
                    m_ex->setValue(id, 0);
                    return on;
                }, c.name);
                continue;
            }
            pxs::Node &n = m_tree.add(path);
            n.access = 3;
            n.description = c.name;
            if (c.kind == Control::Kind::Slider) {
                n.type = QStringLiteral("f");
                n.range = QJsonArray{QJsonObject{{"MIN", c.min}, {"MAX", c.max}}};
                n.clip = QStringLiteral("both");
                n.get = [number] { return QVariantList{number([](const Control &k) { return k.value; })}; };
                n.set = [this, id](const QVariantList &a) { return !a.isEmpty() && m_ex->setValue(id, a[0].toDouble()); };
            } else if (c.kind == Control::Kind::Toggle) {
                n.type = QStringLiteral("T");
                n.get = [number] { return QVariantList{number([](const Control &k) { return k.value; }).toDouble() >= 0.5}; };
                n.set = [this, id](const QVariantList &a) { return !a.isEmpty() && m_ex->setValue(id, a[0].toBool() || a[0].toDouble() >= 0.5 ? 1 : 0); };
            } else if (c.kind == Control::Kind::Choice) {
                n.type = QStringLiteral("s");
                QJsonArray vals;
                for (const QString &s : c.choices) vals.append(s);
                n.range = QJsonArray{QJsonObject{{"VALS", vals}}};
                n.get = [this, id] {
                    const auto now = m_ex->control(id);
                    const int i = now ? int(now->value) : 0;
                    return QVariantList{now && i >= 0 && i < now->choices.size() ? now->choices[i] : QString()};
                };
                n.set = [this, id](const QVariantList &a) {
                    if (a.isEmpty()) return false;
                    const auto now = m_ex->control(id);
                    if (!now) return false;
                    int i = now->choices.indexOf(a[0].toString()); // its name, or its index
                    if (i < 0 && a[0].typeId() != QMetaType::QString) i = a[0].toInt();
                    return i >= 0 && i < now->choices.size() && m_ex->setValue(id, i);
                };
            } else if (c.kind == Control::Kind::Pad) {
                n.type = QStringLiteral("ff");
                const QJsonObject r{{"MIN", c.min}, {"MAX", c.max}};
                n.range = QJsonArray{r, r};
                n.clip = QStringLiteral("both");
                n.get = [this, id] {
                    const auto now = m_ex->control(id);
                    return QVariantList{now ? now->value : 0.0, now ? now->value2 : 0.0};
                };
                n.set = [this, id](const QVariantList &a) { return a.size() >= 2 && m_ex->setPoint(id, a[0].toDouble(), a[1].toDouble()); };
            } else { // one ball
                n.type = QStringLiteral("ff");
                const auto &b = c.balls.empty() ? pxs::controls::Ball() : c.balls[0];
                n.range = QJsonArray{QJsonObject{{"MIN", b.minX}, {"MAX", b.maxX}}, QJsonObject{{"MIN", b.minY}, {"MAX", b.maxY}}};
                n.get = [this, id] {
                    const auto now = m_ex->control(id);
                    return QVariantList{now && !now->points.empty() ? now->points[0][0] : 0.0, now && !now->points.empty() ? now->points[0][1] : 0.0};
                };
                n.set = [this, id](const QVariantList &a) { return a.size() >= 2 && m_ex->setPoints(id, {a[0].toDouble(), a[1].toDouble()}); };
            }
            m_paths.insert(path, id);
            m_told.insert(path, n.get());
        }
    }
}

void OscDevice::pump()
{
    const auto tell = [this](const QString &path) {
        const pxs::Node *n = m_tree.find(path);
        if (!n || !n->get) return;
        const QVariantList now = n->get();
        const auto seen = m_told.constFind(path);
        if (seen != m_told.cend() && *seen == now) return;
        m_told.insert(path, now);
        m_tree.changed(path);
    };
    tell(QStringLiteral("/kadran/page"));
    tell(QStringLiteral("/kadran/view"));
    for (auto it = m_paths.cbegin(); it != m_paths.cend(); ++it) tell(it.key());
}
