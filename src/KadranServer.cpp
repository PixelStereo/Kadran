#include "KadranServer.h"
#include "ShellPage.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTcpSocket>

KadranServer::KadranServer(Explorer *explorer, QObject *parent) : pxs::controls::Server(explorer, parent), m_ex(explorer)
{
    setName(QStringLiteral("Kadran"));
    setPageTitle(QStringLiteral("Kadran"));
    setFrameable(true); // the front page frames the controls page
    m_csp = policy(shell_page::kScript, shell_page::kStyle, "frame-src 'self'");
    // What changes in the devices is told at the next tick
    connect(explorer, &Explorer::devicesChanged, this, [this] { m_dirty = true; });
    connect(explorer, &Explorer::deviceReady, this, [this] { m_dirty = true; });
    connect(explorer, &Explorer::deviceTreeChanged, this, [this] { m_dirty = true; });
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
    const QByteArray json = QJsonDocument(QJsonObject{{"t", "devices"}, {"list", m_ex->devicesJson()}}).toJson(QJsonDocument::Compact);
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

bool KadranServer::message(QTcpSocket *client, const QString &type, const QJsonObject &o)
{
    if (type == QLatin1String("hello")) {
        m_shells.insert(client);
        leaveControls(client); // the front page has no controls of its own
        sendDevices(client);
        for (const auto &d : m_ex->devices()) sendTree(d->id, client);
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
        const quint64 id = m_ex->addDevice(QString(), host, port);
        m_ex->openDevice(id);
        m_dirty = true;
    } else if (type == QLatin1String("add")) {
        const QString text = m_ex->addTargets(Explorer::targetsFromJson(o.value("targets").toArray()), o.value("widget").toString(), o.value("pad").toBool(),
                                              o.value("page").toString().toULongLong());
        say(client, text);
    } else if (type == QLatin1String("drop")) { // from the controls page: its text is what the tree gave
        const QJsonArray list = QJsonDocument::fromJson(o.value("data").toString().toUtf8()).array();
        const QString text = m_ex->addTargets(Explorer::targetsFromJson(list), QString(), true, o.value("page").toString().toULongLong());
        for (QTcpSocket *s : std::as_const(m_shells)) say(s, text);
    } else if (type == QLatin1String("save")) {
        sendText(client, QJsonDocument(QJsonObject{{"t", "file"}, {"name", "Interface.kadran"}, {"doc", m_ex->toJson()}}).toJson(QJsonDocument::Compact));
    } else if (type == QLatin1String("load")) {
        QString why;
        say(client, m_ex->fromJson(o.value("doc").toObject(), &why) ? QStringLiteral("Interface opened") : why);
        m_dirty = true;
    } else {
        return false;
    }
    return true;
}
