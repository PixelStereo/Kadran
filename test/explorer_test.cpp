// The explorer's model against a device: open it, add a control for one of its parameters, and the control follows the device and drives it.
#include "Explorer.h"

#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pxs::Tree tree;
    double level = 0.25;
    bool on = false;
    auto p = std::make_unique<Parameter>("level", "Level", Parameter::Type::Float);
    p->range(0, 1).bind([&] { return QVariant(level); }, [&](const QVariant &v) { level = v.toDouble(); });
    tree.add("/mix/level", std::move(p));
    auto q = std::make_unique<Parameter>("on", "On", Parameter::Type::Bool);
    q->bind([&] { return QVariant(on); }, [&](const QVariant &v) { on = v.toBool(); });
    tree.add("/mix/on", std::move(q));
    double vu = 0.4;
    auto vuParam = std::make_unique<Parameter>("vu", "Vu", Parameter::Type::Float);
    vuParam->range(0, 1).bind([&] { return QVariant(vu); }, [&](const QVariant &) {}).readOnly();
    tree.add("/mix/vu", std::move(vuParam));
    pxs::OscService service(&tree, "Device");
    CHECK(service.start(0, 0, false));

    Explorer ex;
    const quint64 dev = ex.addDevice("Device", "127.0.0.1", service.httpPort());
    CHECK(ex.addDevice("Device", "127.0.0.1", service.httpPort()) == dev); // the same one
    CHECK(ex.addControl(dev, "/mix/level") == 0); // not open yet
    bool ready = false;
    QObject::connect(&ex, &Explorer::deviceReady, [&](quint64) { ready = true; });
    ex.openDevice(dev);
    pump([&] { return ready; });
    CHECK(ready);

    const quint64 rev = ex.revision();
    const quint64 id = ex.addControl(dev, "/mix/level");
    CHECK(id != 0 && ex.revision() > rev);
    CHECK(ex.addControl(dev, "/nowhere") == 0);
    const quint64 toggle = ex.addControl(dev, "/mix/on");
    CHECK(toggle != 0);
    CHECK(ex.controls().size() == 2);
    auto c = ex.control(id);
    CHECK(c && c->name == "Level" && c->key == "level" && c->value == 0.25 && c->max == 1);

    level = 0.9; // the device moves: the control follows
    pump([&] { return ex.control(id)->value == 0.9; });
    CHECK(ex.control(id)->value == 0.9);

    CHECK(ex.setValue(id, 0.4)); // the control drives the device
    pump([&] { return std::abs(level - 0.4) < 1e-6; });
    CHECK(std::abs(level - 0.4) < 1e-6); // over OSC: a float
    CHECK(ex.setValue(toggle, 1));
    pump([&] { return on; });
    CHECK(on);

    pxs::controls::Control renamed = *ex.control(id);
    renamed.name = "Master";
    CHECK(ex.update(id, renamed) && ex.control(id)->key == "master" && ex.control(id)->boundParam == "/mix/level");
    CHECK(ex.remove(id) && !ex.control(id) && ex.controls().size() == 1);
    CHECK(!ex.remove(id));

    // Two parameters make a pad, 0..1 on each, mapped to each one's range
    auto r = std::make_unique<Parameter>("rate", "Rate", Parameter::Type::Float);
    double rate = 10;
    r->range(0, 20).bind([&] { return QVariant(rate); }, [&](const QVariant &v) { rate = v.toDouble(); });
    tree.add("/mix/rate", std::move(r));
    ready = false;
    ex.openDevice(dev);
    ex.device(dev)->client->refresh();
    pump([&] { return ex.device(dev)->client->tree().find("/mix/rate") != nullptr; });
    level = 0.5;
    const quint64 pad = ex.addPad(dev, "/mix/level", "/mix/rate");
    CHECK(pad != 0);
    pump([&] { return ex.control(pad)->points.size() == 1 && std::abs(ex.control(pad)->points[0][0] - 0.5) < 1e-6; }); // asked for when it was added
    auto pc = ex.control(pad);
    CHECK(pc && pc->kind == pxs::controls::Control::Kind::Points && pc->balls.size() == 1 && pc->balls[0].px == "/mix/level" && pc->balls[0].maxY == 20);
    pump([&] { return std::abs(ex.control(pad)->points[0][1] - 10) < 1e-6; });
    CHECK(std::abs(ex.control(pad)->points[0][1] - 10) < 1e-6 && pc->boundLabel == "Device:/mix/level, /mix/rate");
    CHECK(ex.setPoints(pad, {0.25, 15}));
    pump([&] { return std::abs(rate - 15) < 1e-4; });
    CHECK(std::abs(level - 0.25) < 1e-4 && std::abs(rate - 15) < 1e-4);
    { // a ball added from the page, on addresses of its own, with a range of its own
        auto changed = *ex.control(pad);
        changed.applyProps(QJsonObject{{"balls", QJsonArray{changed.balls[0].toJson(), QJsonObject{{"x", "/mix/rate"}, {"y", "/mix/level"}, {"minX", 0}, {"maxX", 40}}}}});
        CHECK(ex.update(pad, changed));
        auto two = ex.control(pad);
        CHECK(two && two->count == 2 && two->balls[1].px == "/mix/rate" && two->balls[1].maxX == 40 && two->points.size() == 2);
        CHECK(ex.setPoints(pad, {0.25, 15, 20, 0.5}));
        pump([&] { return std::abs(rate - 20) < 1e-4; });
        CHECK(std::abs(rate - 20) < 1e-4 && std::abs(level - 0.5) < 1e-4);
        ex.setPoints(pad, {0.25, 15, 10, 0.25});
        pump([&] { return std::abs(rate - 10) < 1e-4; });
    }
    CHECK(ex.addControl(dev, "/mix/level", "knob") != 0);
    CHECK(ex.controls().back().widget == "knob" && ex.controls().back().boundLabel == "Device:/mix/level");
    { // The preferences: the widget a dropped parameter becomes, and sizes kept within the grid
        ex.setPrefs(QJsonObject{{"widgets", QJsonObject{{"float", "knob"}, {"bool", "nonsense"}}}, {"sizes", QJsonObject{{"knob", QJsonArray{99, 0}}, {"nope", QJsonArray{1, 1}}}}});
        const QJsonObject p = ex.prefs();
        CHECK(p.value("widgets").toObject().value("float").toString() == "knob" && !p.value("widgets").toObject().contains("bool"));
        CHECK(p.value("sizes").toObject().value("knob").toArray()[0].toInt() == 12 && p.value("sizes").toObject().value("knob").toArray()[1].toInt() == 1 && !p.value("sizes").toObject().contains("nope"));
        const quint64 viaPrefs = ex.addControl(dev, "/mix/level");
        CHECK(viaPrefs && ex.control(viaPrefs)->widget == "knob");
        ex.setPrefs({});
        // With nothing chosen: a number is a horizontal slim fader, and every widget is its smallest
        const QJsonObject d = ex.prefs();
        CHECK(d.value("widgets").toObject().value("float").toString() == "slim" && d.value("widgets").toObject().value("int").toString() == "slim");
        CHECK(d.value("sizes").toObject().value("slim").toArray() == QJsonArray({2, 1}) && d.value("sizes").toObject().value("vfader").toArray() == QJsonArray({1, 3}));
        CHECK(d.value("sizes").toObject().value("knob").toArray() == QJsonArray({2, 2}) && d.value("sizes").toObject().value("led").toArray() == QJsonArray({1, 1}));
        CHECK(d.value("sizes").toObject().value("pad").toArray() == QJsonArray({3, 3}) && d.value("sizes").toObject().value("adsr").toArray() == QJsonArray({4, 2}) &&
              d.value("sizes").toObject().value("multi").toArray() == QJsonArray({4, 3}) && d.value("sizes").toObject().value("hmulti").toArray() == QJsonArray({4, 3}));
        const quint64 byDefault = ex.addControl(dev, "/mix/level");
        CHECK(byDefault && ex.control(byDefault)->widget == "slim");
    }
    const quint64 plain = ex.addControl(dev, "/mix/level", "hold");
    CHECK(plain != 0 && ex.control(plain)->widget.isEmpty()); // a widget of another kind is ignored

    using pxs::controls::Control;
    // A node: all it holds, in a group of the page or in a page of its own; and the list goes back as it was
    {
        const std::vector<Control> before = ex.controls();
        CHECK(ex.countNode(dev, "/mix") == 4); // level, rate, on, vu (a view)
        CHECK(ex.countNode(dev, "/nowhere") == 0);
        CHECK(ex.addNode(dev, "/mix", false, 0).startsWith("Added a group"));
        size_t groups = 0, held = 0;
        quint64 groupId = 0;
        for (const Control &c : ex.controls()) if (c.kind == Control::Kind::Group) { ++groups; groupId = c.id; }
        for (const Control &c : ex.controls()) if (c.group == groupId && groupId) ++held;
        CHECK(groups == 1 && held == 4);
        CHECK(ex.addNode(dev, "/mix", true, 0).startsWith("Added a page"));
        quint64 pageId = 0;
        for (const Control &c : ex.controls()) if (c.kind == Control::Kind::Group && c.name.toLower() == "mix" && c.id != groupId) pageId = c.id;
        CHECK(pageId && ex.control(pageId)->group == 0);
        CHECK(ex.restore(before) && ex.controls().size() == before.size());
    }

    // What cannot be set is shown: a meter; a number that can be set can be shown too
    {
        CHECK(ex.widgetsFor(dev, "/mix/vu").first().first == "meter");
        const quint64 view = ex.addControl(dev, "/mix/vu");
        CHECK(view && ex.control(view)->widget == "meter" && std::abs(ex.control(view)->value - 0.4) < 1e-6);
        CHECK(!ex.setValue(view, 0.9) || std::abs(vu - 0.4) < 1e-9); // nothing sets it
        const quint64 graph = ex.addControl(dev, "/mix/level", "graph");
        CHECK(graph && ex.control(graph)->widget == "graph");
    }

    // Pages: one added, emptied, removed; all cleared
    {
        const size_t start = ex.controls().size();
        const quint64 page = ex.addPage("Mine");
        CHECK(page && ex.control(page)->name == "Mine" && ex.control(page)->group == 0);
        CHECK(ex.addControl(dev, "/mix/level", QString(), page) != 0 && ex.addControl(dev, "/mix/on", QString(), page) != 0);
        CHECK(ex.clearPage(page) == 2 && ex.controls().size() == start + 1);
        CHECK(ex.addControl(dev, "/mix/level", QString(), page) != 0);
        CHECK(ex.removePage(page) == 1 && ex.controls().size() == start);
        CHECK(ex.removePage(page) == -1);
        const std::vector<Control> before = ex.controls();
        CHECK(ex.clearAll(true) == int(before.size()) && ex.controls().size() == 1);
        CHECK(ex.restore(before) && ex.controls().size() == before.size());
    }

    // The interface is kept and found again
    const QJsonObject saved = ex.toJson();
    CHECK(saved.value("devices").toArray().size() == 1);
    const size_t count = ex.controls().size();
    Explorer other;
    CHECK(!other.fromJson(QJsonObject{{"format", "nope"}}));
    CHECK(other.fromJson(saved));
    CHECK(other.controls().size() == count);
    CHECK(other.devices().size() == 1 && other.devices()[0]->name == "Device");
    bool back = false;
    QObject::connect(&other, &Explorer::deviceReady, [&](quint64) { back = true; });
    pump([&] { return back; });
    CHECK(back);
    quint64 padId = 0;
    for (const auto &c : other.controls()) if (c.kind == pxs::controls::Control::Kind::Points && !c.balls.empty() && c.balls[0].py == "/mix/rate") padId = c.id;
    CHECK(padId && other.control(padId)->boundScope == other.devices()[0]->id);
    pump([&] { return other.control(padId)->points.size() == 2 && std::abs(other.control(padId)->points[0][0] - 0.25) < 1e-3; });
    CHECK(other.control(padId)->points.size() == 2 && std::abs(other.control(padId)->points[0][0] - 0.25) < 1e-3);

    // The page is served on the loopback
    CHECK(ex.serve() && ex.pageUrl().startsWith("http://127.0.0.1:"));

    if (!failures) std::printf("explorer: ok\n");
    return failures ? 1 : 0;
}
