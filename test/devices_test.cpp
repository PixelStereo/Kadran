// The devices of the explorer: plain OSC ones (declared addresses, values sent over UDP, several ports of one host, renamed, saved) and the
// OSCQuery ones that go when nothing shows them any more.
#include "Explorer.h"

#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUdpSocket>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static void pump(const std::function<bool()> &until, int ms = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!until() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// What arrives on a UDP port, decoded
static bool receive(QUdpSocket &in, osc::Message *out)
{
    pump([&] { return in.hasPendingDatagrams(); });
    if (!in.hasPendingDatagrams()) return false;
    QByteArray data;
    data.resize(int(in.pendingDatagramSize()));
    in.readDatagram(data.data(), data.size());
    std::vector<osc::Message> list;
    if (!osc::decode(data, list) || list.size() != 1) return false;
    *out = list[0];
    return true;
}

// The next message to an address (a datagram of an earlier step that came late is skipped)
static bool receiveAt(QUdpSocket &in, osc::Message *out, const QString &address)
{
    for (int i = 0; i < 8; ++i)
        if (receive(in, out) && out->address == address) return true;
    return false;
}

static bool hasDevice(const Explorer &ex, quint64 id)
{
    for (const auto &d : ex.devices())
        if (d->id == id) return true;
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // --- a plain OSC device -------------------------------------------------------------------------------------------
    QUdpSocket in, in2;
    CHECK(in.bind(QHostAddress::LocalHost, 0));
    CHECK(in2.bind(QHostAddress::LocalHost, 0));
    Explorer ex;
    const quint64 beamer = ex.addOscDevice("Beamer", "127.0.0.1", in.localPort());
    CHECK(beamer != 0);
    CHECK(ex.addOscDevice("Other name", "127.0.0.1", in.localPort()) == beamer); // the same host and port: the one known
    const quint64 second = ex.addOscDevice("Beamer 2", "127.0.0.1", in2.localPort()); // another UDP port of the same host
    CHECK(second != 0 && second != beamer);
    CHECK(ex.addOscDevice("x", "", 9000) == 0 && ex.addOscDevice("x", "127.0.0.1", 0) == 0);
    CHECK(ex.devices().size() == 2 && ex.devices()[0]->kind == Explorer::Dev::Kind::Osc);

    CHECK(ex.addPoint(beamer, {"/layer/1/opacity", "f", 0, 1}));
    CHECK(ex.addPoint(beamer, {"/layer/1/index", "i", 0, 9}));
    CHECK(ex.addPoint(beamer, {"/on", "T", 0, 1}));
    CHECK(ex.addPoint(beamer, {"/go", "N", 0, 1}));
    CHECK(!ex.addPoint(beamer, {"/on", "T", 0, 1}));            // the same address
    CHECK(!ex.addPoint(beamer, {"/layer/1", "f", 0, 1}));       // a node that holds others
    CHECK(!ex.addPoint(beamer, {"/on/more", "f", 0, 1}));       // under a parameter
    CHECK(!ex.addPoint(beamer, {"layer", "f", 0, 1}));          // no slash
    CHECK(!ex.addPoint(beamer, {"/a b", "f", 0, 1}));           // a space
    CHECK(!ex.addPoint(beamer, {"/a/*", "f", 0, 1}));           // a pattern
    CHECK(!ex.addPoint(beamer, {"/a/", "f", 0, 1}));
    CHECK(!ex.addPoint(beamer, {"/text", "s", 0, 1}));          // not offered
    CHECK(!ex.addPoint(12345, {"/x", "f", 0, 1}));

    // Its namespace is what the front page shows, and every point can be a control
    const QJsonObject root = ex.treeJson(beamer);
    CHECK(!root.isEmpty() && root.value("c").toArray().size() == 3); // layer, on, go
    CHECK(!ex.widgetsFor(beamer, "/layer/1/opacity").isEmpty());
    CHECK(ex.widgetsFor(beamer, "/go").first().first == "hold" || ex.widgetsFor(beamer, "/go").first().first == "tap");
    CHECK(ex.devicesJson()[0].toObject().value("kind").toString() == "osc");

    const quint64 fader = ex.addControl(beamer, "/layer/1/opacity");
    CHECK(fader != 0);
    CHECK(ex.setValue(fader, 0.5));
    osc::Message m;
    CHECK(receive(in, &m) && m.address == "/layer/1/opacity" && m.types == "f" && std::abs(m.args[0].toDouble() - 0.5) < 1e-6);
    pump([&] { return ex.control(fader)->value == 0.5; }, 200);
    CHECK(ex.control(fader)->value == 0.5); // it shows what was sent
    CHECK(!in2.hasPendingDatagrams());       // the other port heard nothing

    const quint64 toggle = ex.addControl(beamer, "/on");
    CHECK(toggle != 0 && ex.setValue(toggle, 1));
    CHECK(receive(in, &m) && m.address == "/on" && m.types == "T");
    CHECK(ex.setValue(toggle, 0));
    CHECK(receive(in, &m) && m.types == "F");
    const quint64 go = ex.addControl(beamer, "/go");
    CHECK(go != 0 && ex.setValue(go, 1));
    CHECK(receive(in, &m) && m.address == "/go" && m.types == "N");

    // A point or a device a control shows stays
    CHECK(!ex.removePoint(beamer, "/layer/1/opacity"));
    CHECK(!ex.removeDevice(beamer));
    CHECK(ex.removePoint(second, "/nothing") == false);

    // Renamed; two of one name are refused, and the controls' labels follow
    CHECK(ex.renameDevice(beamer, "Stage beamer") && ex.device(beamer)->name == "Stage beamer");
    CHECK(!ex.renameDevice(beamer, "   ") && !ex.renameDevice(second, "Stage beamer") && !ex.renameDevice(999, "x"));
    CHECK(ex.control(fader)->boundLabel.startsWith("Stage beamer"));

    // Saved with its addresses, and open again: the same device and the same control
    const QJsonObject doc = ex.toJson();
    Explorer back;
    QString why;
    CHECK(back.fromJson(doc, &why));
    CHECK(back.devices().size() == 1 && back.devices()[0]->kind == Explorer::Dev::Kind::Osc && back.devices()[0]->points.size() == 4);
    CHECK(back.devices()[0]->name == "Stage beamer" && back.devices()[0]->port == in.localPort());
    CHECK(back.controls().size() == 3);
    const auto restored = back.controls().front();
    CHECK(back.setValue(restored.id, 0.75));
    CHECK(receive(in, &m) && std::abs(m.args[0].toDouble() - 0.75) < 1e-6);
    CHECK(back.fromJson(doc, &why) && back.devices().size() == 1); // again: not duplicated

    // Filters on a tile: what it sends goes through them, a smoothing goes on after the value was set
    {
        const auto edit = [&](const QJsonArray &filters) {
            pxs::controls::Control c = *ex.control(fader);
            c.applyProps(QJsonObject{{"filters", filters}});
            return ex.update(fader, c) && ex.control(fader)->filters.size() == filters.size();
        };
        CHECK(edit(QJsonArray{QJsonObject{{"type", "clamp"}, {"a", 0.2}, {"b", 0.6}}}));
        CHECK(ex.setValue(fader, 1.0));
        CHECK(receive(in, &m) && std::abs(m.args[0].toDouble() - 0.6) < 1e-6); // at once
        CHECK(edit(QJsonArray{QJsonObject{{"type", "weighted_average"}, {"a", 0.15}}}));
        CHECK(ex.setValue(fader, 0.0) && receive(in, &m));
        CHECK(ex.setValue(fader, 1.0));
        CHECK(receive(in, &m) && m.args[0].toDouble() < 0.5); // it starts behind
        double top = 0;
        QElapsedTimer clock;
        clock.start();
        while (top < 0.95 && clock.elapsed() < 5000) {
            pump([&] { return in.hasPendingDatagrams(); }, 200);
            while (in.hasPendingDatagrams()) {
                QByteArray d;
                d.resize(int(in.pendingDatagramSize()));
                in.readDatagram(d.data(), d.size());
                std::vector<osc::Message> list;
                if (osc::decode(d, list) && !list.empty()) top = std::max(top, list[0].args[0].toDouble());
            }
        }
        CHECK(top >= 0.95); // and gets there by itself
        CHECK(edit(QJsonArray{}));
        CHECK(ex.setValue(fader, 0.3) && receive(in, &m) && std::abs(m.args[0].toDouble() - 0.3) < 1e-6); // no filter: as before
    }

    // A tile has a range of its own; the address's is changed on the address
    {
        CHECK(ex.addPoint(beamer, {"/count", "i", 0, 127}));
        const quint64 enc = ex.addControl(beamer, "/count", "knob");
        const quint64 enc2 = ex.addControl(beamer, "/count", "knob");
        CHECK(enc != 0 && enc2 != 0 && ex.control(enc)->max == 127);
        pxs::controls::Control c = *ex.control(enc);
        c.applyProps(QJsonObject{{"min", 0}, {"max", 50}});
        CHECK(ex.update(enc, c));
        CHECK(ex.control(enc)->max == 50 && ex.control(enc2)->max == 127); // it takes, on this tile only
        CHECK(ex.setValue(enc, 40) && receive(in, &m) && m.types == "i" && m.args[0].toInt() == 40);
        CHECK(ex.setPointRange(beamer, "/count", 0, 10));
        CHECK(ex.control(enc2)->max == 10 && ex.control(enc)->max == 50); // the address's range: the tiles that follow it
        CHECK(ex.setValue(enc, 40) && receive(in, &m) && m.args[0].toInt() == 10); // and what is sent stays within what the address takes
        CHECK(!ex.setPointRange(beamer, "/count", 5, 5) && !ex.setPointRange(beamer, "/nothing", 0, 1));
        CHECK(ex.remove(enc) && ex.remove(enc2) && ex.removePoint(beamer, "/count"));
        while (in.hasPendingDatagrams()) in.readDatagram(nullptr, 0);
    }

    // The plain OSC services of the network that are OSCQuery applications as well (same name) are not offered as plain OSC
    {
        Explorer net;
        pxs::oscquery::Device both, only;
        both.name = "Studio app (mac)";
        both.address = QHostAddress("192.168.1.20");
        both.port = 9000;
        only.name = "Synth";
        only.address = QHostAddress("192.168.1.21");
        only.port = 9001;
        net.nearbyFound(both, false);
        net.nearbyFound(only, false);
        CHECK(net.nearbyJson().size() == 2);
        net.addDevice("Studio app (mac)", "192.168.1.20", 5678); // it is known as an OSCQuery device
        const QJsonArray offers = net.nearbyJson();
        CHECK(offers.size() == 1 && offers[0].toObject().value("name").toString() == "Synth");
    }

    // A macro that sends to two addresses: the one that gives the feedback is chosen
    {
        CHECK(ex.addPoint(beamer, {"/gain", "f", 0, 10}));
        const quint64 macro = ex.addMacro({{beamer, "/layer/1/opacity"}, {beamer, "/gain"}}, "slider");
        CHECK(macro != 0 && ex.control(macro)->targets.size() == 2 && ex.control(macro)->feedback == -1);
        CHECK(ex.setValue(macro, 0.25));
        pxs::controls::Control c = *ex.control(macro);
        c.applyProps(QJsonObject{{"feedback", 1}}); // /gain, 0..10
        CHECK(ex.update(macro, c) && ex.control(macro)->feedback == 1);
        CHECK(ex.setValue(macro, 0.5));
        CHECK(std::abs(ex.control(macro)->value - 0.5) < 1e-6); // taken back from /gain (5 of 0..10)
        CHECK(ex.remove(macro));
        CHECK(ex.removePoint(beamer, "/gain"));
        while (in.hasPendingDatagrams()) in.readDatagram(nullptr, 0);
    }

    // A tile talks to another address; a tile becomes a multi-target interaction
    {
        const quint64 tile = ex.addControl(beamer, "/layer/1/opacity");
        CHECK(tile != 0);
        CHECK(ex.addPoint(beamer, {"/layer/2/opacity", "f", 0, 5}));
        CHECK(ex.addPoint(beamer, {"/other", "T", 0, 1}));
        CHECK(ex.retarget(tile, {beamer, "/other"}).contains("kind")); // a switch is not a fader's kind
        CHECK(ex.control(tile)->boundParam == "/layer/1/opacity");
        CHECK(ex.retarget(tile, {beamer, "/layer/2/opacity"}).startsWith("Now talks"));
        const auto moved = ex.control(tile);
        CHECK(moved && moved->boundParam == "/layer/2/opacity" && moved->max == 5 && moved->id == tile);
        CHECK(ex.setValue(tile, 2.5) && receiveAt(in, &m, "/layer/2/opacity") && std::abs(m.args[0].toDouble() - 2.5) < 1e-6);
        QString why;
        CHECK(ex.toMulti(tile, &why) && ex.control(tile)->macro() && ex.control(tile)->feedback == 0);
        CHECK(ex.addToMacro(tile, {{beamer, "/layer/1/opacity"}}) == 1);
        CHECK(std::abs(ex.control(tile)->value - 0.5) < 1e-6); // the feedback: 2.5 of 0..5
        CHECK(!ex.toMulti(tile, &why)); // it is one already
        CHECK(ex.remove(tile));
        CHECK(ex.removePoint(beamer, "/layer/2/opacity") && ex.removePoint(beamer, "/other"));
        while (in.hasPendingDatagrams()) in.readDatagram(nullptr, 0);
    }

    // The controls gone, the device is kept (it is not announced by anything): until it is removed
    CHECK(ex.remove(fader) && ex.remove(toggle) && ex.remove(go));
    CHECK(hasDevice(ex, beamer) && hasDevice(ex, second));
    CHECK(ex.removePoint(beamer, "/go") && ex.treeJson(beamer).value("c").toArray().size() == 2);
    CHECK(ex.removeDevice(second) && !hasDevice(ex, second));
    CHECK(ex.removeDevice(beamer) && ex.devices().empty());

    // --- OSCQuery devices that are gone ------------------------------------------------------------------------------
    pxs::core::tree::Tree tree;
    double level = 0.25;
    auto p = std::make_unique<pxs::core::tree::Parameter>("level", "Level", pxs::core::tree::Parameter::Type::Float);
    p->range(0, 1).bind([&] { return pxs::core::Value(level); }, [&](const pxs::core::Value &v) { level = v.number().value_or(0.0); });
    tree.add("/mix/level", std::move(p));
    pxs::OscService service(&tree, "Device");
    CHECK(service.start(0, 0, false));

    Explorer q;
    const quint64 dev = q.addDevice("Device", "127.0.0.1", service.httpPort());
    bool ready = false;
    QObject::connect(&q, &Explorer::deviceReady, [&](quint64) { ready = true; });
    q.openDevice(dev);
    pump([&] { return ready; });
    CHECK(ready);
    const quint64 shown = q.addControl(dev, "/mix/level");
    CHECK(shown != 0);
    const std::vector<pxs::controls::Control> withControl = q.controls();

    q.device(dev)->online = false; // it said goodbye on the network
    q.pruneDevices();
    CHECK(hasDevice(q, dev));      // a control shows it: it stays
    CHECK(q.remove(shown));        // nothing does now
    CHECK(!hasDevice(q, dev));     // it goes
    CHECK(q.restore(withControl)); // an undo: the control is back, so is its device
    CHECK(hasDevice(q, dev) && q.control(shown) && q.control(shown)->boundScope == dev);

    // A device that does not answer, shown by nothing, does not stay
    Explorer lost;
    const quint64 ghost = lost.addDevice("Ghost", "127.0.0.1", 1);
    bool failed = false;
    QObject::connect(&lost, &Explorer::deviceFailed, [&](quint64, const QString &) { failed = true; });
    lost.openDevice(ghost);
    pump([&] { return failed; }, 8000);
    CHECK(failed && !hasDevice(lost, ghost));

    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("devices ok\n");
    return failures ? 1 : 0;
}
