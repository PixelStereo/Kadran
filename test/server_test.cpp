// Kadran's server: its front page, and the messages between the page and the application (devices, namespaces, what is put on the page, save and open).
#include "KadranServer.h"

#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QtEndian>
#include <cmath>
#include <cstdio>
#include <stdexcept>

static void require(bool ok, const char *why)
{
    if (!ok) throw std::runtime_error(why);
}

// A plain client: one connection, the replies as they come
struct Client {
    QTcpSocket socket;
    QByteArray in;
    bool connectTo(quint16 port)
    {
        socket.connectToHost(QHostAddress::LocalHost, port);
        return socket.waitForConnected(2000);
    }
    void pump(int ms = 400)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (!socket.isOpen()) break;
            socket.waitForReadyRead(5);
            if (socket.bytesAvailable()) in += socket.readAll();
        }
    }
    template<class Predicate>
    void pumpUntil(Predicate ready, int timeoutMs = 2000)
    {
        QElapsedTimer deadline;
        deadline.start();
        while (!ready() && deadline.elapsed() < timeoutMs) pump(10);
    }
    QByteArray request(const QByteArray &text)
    {
        socket.write(text);
        socket.flush();
        in.clear();
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 5000) {
            pump(10);
            const int headerEnd = in.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                if (socket.state() == QAbstractSocket::UnconnectedState) break;
                continue;
            }
            // Wait for the HTTP response, rather than assuming the runner
            // delivered it within a fixed sleep. WebSocket upgrades end here.
            if (in.startsWith("HTTP/1.1 101")) break;
            const QByteArray header = in.left(headerEnd).toLower();
            const int lengthAt = header.indexOf("\r\ncontent-length:");
            if (lengthAt >= 0) {
                const int valueAt = lengthAt + int(sizeof("\r\ncontent-length:") - 1);
                const int lineEnd = header.indexOf("\r\n", valueAt);
                bool valid = false;
                const auto size = header.mid(valueAt, lineEnd < 0 ? -1 : lineEnd - valueAt).trimmed().toLongLong(&valid);
                if (valid && size >= 0 && in.size() - headerEnd - 4 >= size) break;
            }
            if (socket.state() == QAbstractSocket::UnconnectedState) break;
        }
        return in;
    }
    QByteArray upgrade(const QByteArray &path, const QByteArray &origin = {}, const QByteArray &host = "127.0.0.1")
    {
        QByteArray r = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";
        if (!origin.isEmpty()) r += "Origin: " + origin + "\r\n";
        return request(r + "\r\n");
    }
    // A masked text frame, as a browser sends it
    void sendText(const QByteArray &payload)
    {
        const char mask[4] = {0x12, 0x34, 0x56, 0x78};
        QByteArray f;
        f.append(char(0x81));
        if (payload.size() < 126) {
            f.append(char(0x80 | payload.size()));
        } else { // a longer frame: its length in two more bytes, as a browser sends it
            f.append(char(0x80 | 126));
            f.append(char(payload.size() >> 8));
            f.append(char(payload.size() & 0xFF));
        }
        f.append(mask, 4);
        for (int i = 0; i < payload.size(); ++i) f.append(char(payload[i] ^ mask[i & 3]));
        socket.write(f);
        socket.flush();
    }
    // The server's text frames received so far, as JSON
    QList<QJsonObject> messages()
    {
        QList<QJsonObject> out;
        QByteArray rest = in;
        const int header = rest.indexOf("\r\n\r\n");
        if (rest.startsWith("HTTP/1.1 101") && header >= 0) rest.remove(0, header + 4);
        while (rest.size() >= 2) {
            quint64 len = quint8(rest[1]) & 0x7F;
            int pos = 2;
            if (len == 126) {
                if (rest.size() < 4) break;
                len = qFromBigEndian<quint16>(rest.constData() + 2);
                pos = 4;
            } else if (len == 127) {
                if (rest.size() < 10) break;
                len = qFromBigEndian<quint64>(rest.constData() + 2);
                pos = 10;
            }
            if (quint64(rest.size()) < quint64(pos) + len) break;
            out << QJsonDocument::fromJson(rest.mid(pos, int(len))).object();
            rest.remove(0, pos + int(len));
        }
        return out;
    }
};


static QJsonObject find(const QList<QJsonObject> &list, const char *type)
{
    for (const QJsonObject &m : list)
        if (m.value("t").toString() == type) return m;
    return {};
}

static QJsonObject findLast(const QList<QJsonObject> &list, const char *type)
{
    for (int i = list.size() - 1; i >= 0; --i)
        if (list[i].value("t").toString() == type) return list[i];
    return {};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        // A device
        pxs::Tree tree;
        double level = 0.25, rate = 5;
        auto a = std::make_unique<Parameter>("level", "Level", Parameter::Type::Float);
        a->range(0, 1).bind([&] { return QVariant(level); }, [&](const QVariant &v) { level = v.toDouble(); });
        tree.add("/mix/level", std::move(a));
        auto b = std::make_unique<Parameter>("rate", "Rate", Parameter::Type::Float);
        b->range(0, 10).bind([&] { return QVariant(rate); }, [&](const QVariant &v) { rate = v.toDouble(); });
        tree.add("/mix/rate", std::move(b));
        std::vector<double> many(1500, 0.5); // a namespace of a size a real device has: the message is far over 64 KB
        for (size_t i = 0; i < many.size(); ++i) {
            auto p = std::make_unique<Parameter>(QStringLiteral("p%1").arg(i), QStringLiteral("P%1").arg(i), Parameter::Type::Float);
            p->range(0, 1).bind([&many, i] { return QVariant(many[i]); }, [&many, i](const QVariant &v) { many[i] = v.toDouble(); });
            tree.add(QStringLiteral("/big/p%1").arg(i), std::move(p));
        }
        pxs::OscService device(&tree, "Device");
        require(device.start(0, 0, false), "the device starts");

        Explorer explorer;
        KadranServer server(&explorer);
        require(server.start(0, QString(), "127.0.0.1"), "Kadran's server starts");
        const quint16 port = server.port();
        const QByteArray host = "127.0.0.1:" + QByteArray::number(port);
        const quint64 id = explorer.addDevice("Device", "127.0.0.1", device.httpPort());

        { // the front page, under a policy that lets only its own script run and frames only the controls page
            Client c;
            require(c.connectTo(port), "connect");
            const QByteArray reply = c.request("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
            const QByteArray script = "'sha256-" + QCryptographicHash::hash(KadranServer::front().mid(KadranServer::front().indexOf("<script>") + 8,
                KadranServer::front().indexOf("</script>") - KadranServer::front().indexOf("<script>") - 8), QCryptographicHash::Sha256).toBase64() + "'";
            require(reply.startsWith("HTTP/1.1 200") && reply.contains("<title>Kadran</title>") && reply.contains(script) && reply.contains("frame-src 'self'"),
                    "the front page is served at /, its script hashed in the policy");
            Client d;
            require(d.connectTo(port), "connect");
            const QByteArray control = d.request("GET /control HTTP/1.1\r\nHost: x\r\n\r\n");
            require(control.startsWith("HTTP/1.1 200") && control.contains("X-Frame-Options: SAMEORIGIN"), "and the controls page it frames");
        }

        Client page;
        require(page.connectTo(port), "connect");
        require(page.upgrade("/ws", "http://" + host, host).startsWith("HTTP/1.1 101"), "the page opens its WebSocket");
        page.sendText("{\"t\":\"hello\"}");
        page.pumpUntil([&] { return !find(page.messages(), "devices").isEmpty(); });
        QJsonObject devices = find(page.messages(), "devices");
        require(devices.value("list").toArray().size() == 1 && devices.value("list").toArray()[0].toObject().value("name").toString() == "Device", "the devices are told at hello");

        // Open a device: its namespace comes
        page.in.clear();
        page.sendText("{\"t\":\"open\",\"device\":" + QByteArray::number(double(id)) + "}");
        page.pumpUntil([&] { return !find(page.messages(), "tree").isEmpty(); }, 5000);
        const QJsonObject treeMessage = find(page.messages(), "tree");
        require(!treeMessage.isEmpty(), "its namespace is sent when it is read");
        const QJsonArray top = treeMessage.value("root").toObject().value("c").toArray();
        require(top.size() == 2 && top[0].toObject().value("n").toString() == "big" && top[0].toObject().value("c").toArray().size() == 1500, "a large namespace arrives whole");
        require(top[1].toObject().value("n").toString() == "mix", "the namespace is a tree of nodes");
        const QJsonObject leaf = top[1].toObject().value("c").toArray()[0].toObject();
        require(leaf.value("p").toString() == "/mix/level" && leaf.value("t").toString() == "f" && leaf.value("w").toArray().at(0).toString() == "vfader", "a leaf says its address, type and widgets");

        // Put addresses on the page: one, two (a pad), by a menu and by a drop
        const QByteArray one = "[{\"device\":" + QByteArray::number(double(id)) + ",\"path\":\"/mix/level\"}]";
        const QByteArray two = "[{\"device\":" + QByteArray::number(double(id)) + ",\"path\":\"/mix/level\"},{\"device\":" + QByteArray::number(double(id)) + ",\"path\":\"/mix/rate\"}]";
        page.in.clear();
        page.sendText("{\"t\":\"add\",\"targets\":" + one + ",\"widget\":\"knob\",\"pad\":false,\"page\":\"0\"}");
        page.pumpUntil([&] { return !find(page.messages(), "say").isEmpty(); });
        require(explorer.controls().size() == 1 && explorer.controls()[0].widget == "knob" && explorer.controls()[0].boundLabel == "Device:/mix/level", "a control added with its widget and where it comes from");
        page.sendText("{\"t\":\"add\",\"targets\":" + two + ",\"widget\":\"\",\"pad\":true,\"page\":\"0\"}");
        page.pumpUntil([&] { return explorer.controls().size() == 2; });
        require(explorer.controls().size() == 2 && explorer.controls()[1].kind == pxs::controls::Control::Kind::Points && explorer.controls()[1].balls.size() == 1, "two numbers make an XY pad: a ball on two addresses");
        Client frame; // the controls page, which gets a drop
        require(frame.connectTo(port), "connect");
        require(frame.upgrade("/ws", "http://" + host, host).startsWith("HTTP/1.1 101"), "the controls page opens its WebSocket");
        QByteArray data = one;
        data.replace('"', "\\\"");
        frame.sendText("{\"t\":\"drop\",\"data\":\"" + data + "\",\"page\":\"0\",\"x\":3,\"y\":2}");
        frame.pumpUntil([&] { return explorer.controls().size() == 3; });
        require(explorer.controls().size() == 3, "a drop on the controls page adds the control");
        { // Physics on the ball of the pad, end to end: held by a spring to the hand, then drawn to where the hand was once let go
            const quint64 padId = explorer.controls()[1].id;
            const QByteArray ball = "{\"physics\":{\"mode\":2,\"friction\":30,\"attraction\":200}}";
            frame.sendText("{\"t\":\"edit\",\"id\":\"" + QByteArray::number(padId) + "\",\"props\":" + ball + "}");
            frame.pumpUntil([&] { return explorer.controls()[1].physics.mode == 2; });
            require(explorer.controls()[1].physics.mode == 2 && explorer.controls()[1].balls[0].px == "/mix/level", "a ball's physics is set from the page, its addresses kept");
            frame.sendText("{\"t\":\"points\",\"id\":\"" + QByteArray::number(padId) + "\",\"v\":[0.9,9]}");
            frame.pumpUntil([&] { return level > 0.8; });
            require(level > 0.8 && rate > 7, "held by the hand, the ball goes to it through the spring");
            frame.sendText("{\"t\":\"rel\",\"id\":\"" + QByteArray::number(padId) + "\"}");
            frame.pump(1500);
            require(level > 0.8 && level <= 1.0 && rate > 7, "let go, the ball goes on with its push and stops, the device follows");
        }
        { // A slider on a physics setting of the ball: made from the page, it sets that setting
            const quint64 padId = explorer.controls()[1].id;
            const size_t before = explorer.controls().size();
            frame.sendText("{\"t\":\"physicsWidget\",\"id\":\"" + QByteArray::number(padId) + "\",\"key\":\"1.friction\"}");
            frame.pumpUntil([&] { return explorer.controls().size() == before + 1; });
            require(explorer.controls().size() == before + 1 && explorer.controls().back().physTarget == padId, "a widget for a physics setting is made");
            const quint64 viewId = explorer.controls().back().id;
            frame.sendText("{\"t\":\"set\",\"id\":\"" + QByteArray::number(viewId) + "\",\"v\":22}");
            frame.pumpUntil([&] { return std::abs(explorer.control(padId)->balls[0].physics.friction - 22) < 1e-6; });
            require(std::abs(explorer.control(padId)->balls[0].physics.friction - 22) < 1e-6 && std::abs(explorer.control(viewId)->value - 22) < 1e-6, "the slider sets the ball's friction and shows it");
            frame.sendText("{\"t\":\"remove\",\"id\":\"" + QByteArray::number(viewId) + "\"}");
            frame.pumpUntil([&] { return explorer.controls().size() == before; });
        }
        page.sendText("{\"t\":\"add\",\"targets\":[{\"device\":99,\"path\":\"/nope\"}]}");
        page.sendText("not json");
        page.pump(100);
        require(explorer.controls().size() == 3 && server.isRunning(), "an address that is not there adds nothing");
        require(find(page.messages(), "controls").isEmpty(), "the front page is not sent the controls it does not show");

        // The controls page is sent the controls, with where each comes from
        frame.pumpUntil([&] { return findLast(frame.messages(), "controls").value("list").toArray().size() >= 3; });
        const QJsonArray list = findLast(frame.messages(), "controls").value("list").toArray();
        require(list.size() >= 3 && list[0].toObject().value("dest").toString() == "Device:/mix/level", "the controls page is told where each control comes from");

        // Save and open
        page.in.clear();
        page.sendText("{\"t\":\"save\"}");
        page.pumpUntil([&] { return !find(page.messages(), "file").isEmpty(); });
        const QJsonObject file = find(page.messages(), "file");
        require(file.value("name").toString().endsWith(".kadran") && file.value("doc").toObject().value("controls").toArray().size() == 3, "the interface is given to be saved");
        require(explorer.remove(explorer.controls()[0].id) && explorer.controls().size() == 2, "a control removed");
        page.in.clear();
        page.sendText("{\"t\":\"load\",\"doc\":" + QJsonDocument(file.value("doc").toObject()).toJson(QJsonDocument::Compact) + "}");
        page.pumpUntil([&] { return explorer.controls().size() == 3; });
        require(explorer.controls().size() == 3, "the interface is opened from what was saved");
        page.sendText("{\"t\":\"load\",\"doc\":{\"format\":\"other\"}}");
        page.pump(150);
        require(explorer.controls().size() == 3, "something that is not an interface changes nothing");

        std::printf("kadran server: OK\n");
        return 0;
    } catch (const std::exception &e) {
        std::printf("FAIL kadran server: %s\n", e.what());
        return 1;
    }
}
