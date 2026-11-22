#include "KadranServer.h"
#include "Logo.h"
#include "About.h"
#include <QCoreApplication>
#include "ShellPage.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTcpSocket>

KadranServer::KadranServer(Explorer *explorer, QObject *parent) : pxs::controls::Server(explorer, parent), m_ex(explorer)
{
    setName(QStringLiteral("Kadran"));
    setPageTitle(QStringLiteral("Kadran"));
    setFrameable(true); // the front page frames the controls page
    m_csp = policy(shell_page::script(), shell_page::kStyle, "frame-src 'self'; img-src data:");
    // What changes in the devices is told at the next tick
    connect(explorer, &Explorer::devicesChanged, this, [this] { m_dirty = true; });
    connect(explorer, &Explorer::deviceReady, this, [this] { m_dirty = true; });
    connect(explorer, &Explorer::nearbyChanged, this, [this] { m_dirty = true; });
    connect(explorer, &Explorer::prefsChanged, this, [this] {
        applyPrefs();
        const QByteArray json = prefsMessage();
        for (QTcpSocket *s : std::as_const(m_shells)) sendText(s, json);
    });
    connect(explorer, &Explorer::deviceTreeChanged, this, [this] { m_dirty = true; });
    connect(explorer, &Explorer::viewsChanged, this, [this] {
        const QByteArray json = viewsMessage();
        for (QTcpSocket *s : std::as_const(m_shells)) sendText(s, json);
    });
    connect(explorer, &Explorer::viewRecalled, this, [this] { // the view's first page is shown
        quint64 first = 0;
        for (const pxs::controls::Control &c : m_ex->controls())
            if (c.kind == pxs::controls::Control::Kind::Group && !c.group) { first = c.id; break; }
        setCurrentPage(first);
    });
    applyPrefs();
}

void KadranServer::applyPrefs()
{
    const QJsonObject p = m_ex->prefs();
    setDefaultSizes(p.value("sizes").toObject());
    setPageOptions(p.value("inspector").toString(), p.value("physics").toString());
    setSharedPage(p.value("pages").toString() != QLatin1String("perScreen"));
    setNameParts(2); // a tile is named by the last two words of its address: mix/level, not level
}

QByteArray KadranServer::viewsMessage() const
{
    return QJsonDocument(QJsonObject{{"t", "views"}, {"list", QJsonArray::fromStringList(m_ex->viewNames())}, {"active", m_ex->activeView()}}).toJson(QJsonDocument::Compact);
}

QByteArray KadranServer::prefsMessage() const
{
    return QJsonDocument(QJsonObject{{"t", "prefs"}, {"prefs", m_ex->prefs()}, {"options", Explorer::prefsOptions()}}).toJson(QJsonDocument::Compact);
}

QByteArray KadranServer::front() { return shell_page::page(); }

QStringList KadranServer::urls() const
{
    QStringList out;
    for (const QString &url : pxs::controls::Server::urls()) out << (url.endsWith(QStringLiteral("/control")) ? url.chopped(8) : url);
    return out;
}

bool KadranServer::servePage(const QString &path, QByteArray *html, QByteArray *csp)
{
    if (path != QLatin1String("/")) return false;
    *html = front();
    *csp = m_csp;
    return true;
}

void KadranServer::say(QTcpSocket *client, const QString &text)
{
    if (!text.isEmpty()) sendText(client, QJsonDocument(QJsonObject{{"t", "say"}, {"text", text}}).toJson(QJsonDocument::Compact));
}

void KadranServer::sendAll(const QByteArray &json)
{
    for (QTcpSocket *c : websockets())
        if (m_shells.contains(c)) sendText(c, json);
}

void KadranServer::sendDevices(QTcpSocket *client)
{
    const QByteArray json = QJsonDocument(QJsonObject{{"t", "devices"}, {"list", m_ex->devicesJson()}, {"nearby", m_ex->nearbyJson()}}).toJson(QJsonDocument::Compact);
    if (client) sendText(client, json);
    else if (json != m_devices) {
        m_devices = json;
        sendAll(json);
    }
}

void KadranServer::sendTree(quint64 device, QTcpSocket *client)
{
    const QJsonObject root = m_ex->treeJson(device);
    if (root.isEmpty()) return;
    const QByteArray json = QJsonDocument(QJsonObject{{"t", "tree"}, {"device", double(device)}, {"root", root}}).toJson(QJsonDocument::Compact);
    if (client) sendText(client, json);
    else if (m_trees.value(device) != json) {
        m_trees.insert(device, json);
        sendAll(json);
    }
}

// Every 50 ms: what changed in the devices and their namespaces goes to the front pages
void KadranServer::tick()
{
    if (m_shells.isEmpty() || !m_dirty) return;
    m_dirty = false;
    sendDevices();
    for (const auto &d : m_ex->devices()) sendTree(d->id);
}

void KadranServer::clientGone(QTcpSocket *client) { m_shells.remove(client); }

// All the controls of a node, in a group of the page or a page of their own. A long list is confirmed first, on the front page
void KadranServer::node(double device, const QString &path, bool asPage, quint64 page, bool confirmed)
{
    const int n = m_ex->countNode(quint64(device), path);
    if (n > kConfirmAbove && !confirmed) {
        const QByteArray ask = QJsonDocument(QJsonObject{{"t", "confirm"}, {"n", n}, {"device", device}, {"path", path}, {"as", asPage ? "page" : "group"}, {"page", QString::number(page)}})
                                   .toJson(QJsonDocument::Compact);
        for (QTcpSocket *s : std::as_const(m_shells)) sendText(s, ask);
        return;
    }
    checkpoint();
    const QString text = m_ex->addNode(quint64(device), path, asPage, page);
    for (QTcpSocket *s : std::as_const(m_shells)) say(s, text);
}

bool KadranServer::message(QTcpSocket *client, const QString &type, const QJsonObject &o)
{
    if (type == QLatin1String("hello")) {
        m_shells.insert(client);
        leaveControls(client); // the front page has no controls of its own
        sendText(client, QJsonDocument(QJsonObject{{"t", "about"}, {"html", aboutHtml(QCoreApplication::applicationVersion(), QStringLiteral("<img src=\"%1\" width=\"72\" height=\"72\" alt=\"\" style=\"display:block;margin-bottom:8px\">").arg(QString::fromLatin1(logo::dataUri())))}}).toJson(QJsonDocument::Compact));
        sendText(client, prefsMessage());
        sendText(client, viewsMessage());
        sendDevices(client);
        for (const auto &d : m_ex->devices()) sendTree(d->id, client);
        return true;
    }
    if (type == QLatin1String("multi")) { // from the controls page: a tile that is a view of one address sends to several
        QString why;
        checkpoint();
        const bool done = m_ex->toMulti(o.value("id").toString().toULongLong(), &why);
        for (QTcpSocket *s : std::as_const(m_shells)) say(s, done ? QStringLiteral("A multi-target interaction now: drop more addresses on it with Alt held") : why);
        return true;
    }
    if (!m_shells.contains(client) && type != QLatin1String("drop")) return false;
    if (type == QLatin1String("open")) {
        m_ex->openDevice(quint64(o.value("device").toDouble()));
        sendTree(quint64(o.value("device").toDouble()), client);
    } else if (type == QLatin1String("addDevice")) {
        const quint16 port = quint16(o.value("port").toInt());
        const QString host = o.value("host").toString().trimmed();
        if (!port || host.isEmpty() || host.size() > 253) return true;
        if (o.value("kind").toString() == QLatin1String("osc")) { // plain OSC: the addresses it takes are declared, not read
            m_ex->addOscDevice(o.value("name").toString(), host, port);
        } else {
            const quint64 id = m_ex->addDevice(QString(), host, port);
            m_ex->openDevice(id);
        }
        m_dirty = true;
    } else if (type == QLatin1String("renameDevice")) {
        if (!m_ex->renameDevice(quint64(o.value("device").toDouble()), o.value("name").toString())) say(client, QStringLiteral("This name cannot be used"));
        m_dirty = true;
    } else if (type == QLatin1String("removeDevice")) {
        if (!m_ex->removeDevice(quint64(o.value("device").toDouble()))) say(client, QStringLiteral("Controls still show this device: delete them first"));
        m_dirty = true;
    } else if (type == QLatin1String("addPoint")) {
        Explorer::Point p;
        p.path = o.value("path").toString().trimmed();
        p.type = o.value("type").toString();
        p.min = o.value("min").toDouble(0);
        p.max = o.value("max").toDouble(1);
        if (!m_ex->addPoint(quint64(o.value("device").toDouble()), p)) say(client, QStringLiteral("This address cannot be added (a path like /layer/1/opacity, not used yet)"));
        m_dirty = true;
    } else if (type == QLatin1String("removePoint")) {
        if (!m_ex->removePoint(quint64(o.value("device").toDouble()), o.value("path").toString())) say(client, QStringLiteral("A control still uses this address"));
        m_dirty = true;
    } else if (type == QLatin1String("add")) {
        checkpoint();
        const QString text = m_ex->addTargets(Explorer::targetsFromJson(o.value("targets").toArray()), o.value("widget").toString(), o.value("pad").toBool(),
                                              o.value("page").toString().toULongLong(), o.value("macro").toString());
        say(client, text);
    } else if (type == QLatin1String("drop")) { // from the controls page: its text is what the tree gave
        const QJsonArray list = QJsonDocument::fromJson(o.value("data").toString().toUtf8()).array();
        const quint64 page = o.value("page").toString().toULongLong();
        if (const quint64 onto = o.value("onto").toString().toULongLong()) { // on a macro's tile: they are sent to as well
            checkpoint();
            const QList<Explorer::Target> targets = Explorer::targetsFromJson(list);
            QString text;
            const auto tile = m_ex->control(onto);
            if (tile && !tile->macro() && !o.value("add").toBool()) { // a view of one address: it talks to the one dropped on it
                if (!targets.isEmpty()) text = m_ex->retarget(onto, targets.first());
            } else {
                if (tile && !tile->macro() && !m_ex->toMulti(onto, &text)) { // Alt: it sends to this one as well, so it becomes a multi-target interaction
                    for (QTcpSocket *s : std::as_const(m_shells)) say(s, text);
                    return true;
                }
                const int n = m_ex->addToMacro(onto, targets);
                text = n ? QStringLiteral("Now sending to %1 more").arg(n) : QStringLiteral("Nothing to add");
            }
            for (QTcpSocket *s : std::as_const(m_shells)) say(s, text);
            return true;
        }
        if (list.size() == 1 && list[0].toObject().value("node").toBool()) { // a node, not a parameter: all it holds, in a group or a page
            const QJsonObject n = list[0].toObject();
            node(n.value("device").toDouble(), n.value("path").toString(), n.value("as").toString() == QLatin1String("page"), page, false);
            return true;
        }
        checkpoint();
        const QString text = m_ex->addTargets(Explorer::targetsFromJson(list), QString(), true, page);
        for (QTcpSocket *s : std::as_const(m_shells)) say(s, text);
    } else if (type == QLatin1String("pages")) { // the pages: add, remove, clear this one, clear all, a new empty interface
        const QString op = o.value("op").toString();
        const quint64 page = o.value("page").toString().toULongLong();
        checkpoint();
        QString text;
        if (op == QLatin1String("add")) text = m_ex->addPage(o.value("name").toString()) ? QStringLiteral("Page added") : QStringLiteral("The page is full");
        else if (op == QLatin1String("remove")) { const int n = m_ex->removePage(page); text = n < 0 ? QStringLiteral("No page to remove") : QStringLiteral("Page removed (%1 controls)").arg(n); }
        else if (op == QLatin1String("clear")) text = QStringLiteral("%1 controls removed").arg(m_ex->clearPage(page));
        else if (op == QLatin1String("clearAll")) text = QStringLiteral("%1 controls removed").arg(m_ex->clearAll(false));
        else if (op == QLatin1String("new")) { m_ex->clearAll(true); text = QStringLiteral("A new, empty interface"); }
        for (QTcpSocket *s : std::as_const(m_shells)) say(s, text);
    } else if (type == QLatin1String("addNode")) {
        node(o.value("device").toDouble(), o.value("path").toString(), o.value("as").toString() == QLatin1String("page"), o.value("page").toString().toULongLong(),
             o.value("confirmed").toBool());
    } else if (type == QLatin1String("undo") || type == QLatin1String("redo")) {
        const bool done = type == QLatin1String("undo") ? undo() : redo();
        if (done) emit layoutEdited();
        say(client, done ? (type == QLatin1String("undo") ? QStringLiteral("Undone") : QStringLiteral("Redone")) : QStringLiteral("Nothing to %1").arg(type));
    } else if (type == QLatin1String("view")) { // the views: a new one from the page, one recalled, renamed, deleted
        const QString op = o.value("op").toString(), name = o.value("name").toString();
        QString text;
        if (op == QLatin1String("new")) { const QString given = m_ex->newView(name); text = given.isEmpty() ? QStringLiteral("Too many views") : QStringLiteral("View \"%1\" made from this page").arg(given); }
        else if (op == QLatin1String("recall")) { if (!m_ex->recallView(name)) text = QStringLiteral("No such view"); }
        else if (op == QLatin1String("delete")) { if (m_ex->deleteView(name)) text = QStringLiteral("View deleted"); }
        else if (op == QLatin1String("rename")) { if (!m_ex->renameView(name, o.value("to").toString())) text = QStringLiteral("That name is taken"); }
        if (!text.isEmpty()) say(client, text);
    } else if (type == QLatin1String("setPrefs")) {
        m_ex->setPrefs(o.value("prefs").toObject());
    } else if (type == QLatin1String("save")) {
        sendText(client, QJsonDocument(QJsonObject{{"t", "file"}, {"name", "Interface.kadran"}, {"doc", m_ex->toJson()}}).toJson(QJsonDocument::Compact));
    } else if (type == QLatin1String("load")) {
        checkpoint();
        QString why;
        say(client, m_ex->fromJson(o.value("doc").toObject(), &why) ? QStringLiteral("Interface opened") : why);
        m_dirty = true;
    } else {
        return false;
    }
    return true;
}
