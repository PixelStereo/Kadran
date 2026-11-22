// Kadran as an OSCQuery device: its pages, its views and the values of its widgets, read and written through the tree
#include "KadranServer.h"
#include "OscDevice.h"

#include <pxs/Convert.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static void pump(const std::function<bool()> &until, int ms = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!until() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static void write(OscDevice &dev, const QString &path, const QString &types, const QVariantList &args)
{
    osc::Message m;
    m.address = path;
    m.types = types;
    m.args = args;
    dev.tree().apply(pxs::convert::toCore(m));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationVersion("test");
    Explorer ex;
    KadranServer server(&ex);
    OscDevice dev(&ex, &server, "Kadran test");
    CHECK(dev.start(0, 0));

    const quint64 live = ex.addPage("Live");
    const quint64 second = ex.addPage("Second page");
    pxs::controls::Control level;
    level.kind = pxs::controls::Control::Kind::Slider;
    level.name = "Level";
    level.min = 0;
    level.max = 10;
    level.group = live;
    const quint64 id = ex.add(level);
    pxs::controls::Control mode;
    mode.kind = pxs::controls::Control::Kind::Choice;
    mode.name = "Mode";
    mode.choices = {"Red", "Green"};
    mode.group = second;
    const quint64 modeId = ex.add(mode);
    CHECK(id && modeId);
    pump([&] { return dev.tree().find("/kadran/controls/live/level") != nullptr; });

    // The widgets
    const pxs::core::tree::Node *n = dev.tree().find("/kadran/controls/live/level");
    CHECK(n && n->type == "f" && n->access == 3);
    write(dev, "/kadran/controls/live/level", "f", {4.5});
    CHECK(std::abs(ex.control(id)->value - 4.5) < 1e-6);
    CHECK(n->get().front().number().value_or(0.0) == 4.5);
    write(dev, "/kadran/controls/second_page/mode", "s", {"Green"});
    CHECK(ex.control(modeId)->value == 1);
    write(dev, "/kadran/controls/second_page/mode", "i", {0});
    CHECK(ex.control(modeId)->value == 0);

    // The pages: shown for every browser
    CHECK(dev.tree().find("/kadran/pages/live") && dev.tree().find("/kadran/pages/second_page"));
    write(dev, "/kadran/page", "i", {2});
    CHECK(server.currentPage() == second);
    write(dev, "/kadran/pages/prev", "N", {});
    CHECK(server.currentPage() == live);
    write(dev, "/kadran/pages/next", "N", {});
    CHECK(server.currentPage() == second);
    write(dev, "/kadran/pages/live", "N", {});
    CHECK(server.currentPage() == live && int(dev.tree().find("/kadran/page")->get().front().number().value_or(0.0)) == 1);

    // The views
    ex.newView("Front");
    ex.newView("Back");
    pump([&] { return dev.tree().find("/kadran/views/back") != nullptr; });
    CHECK(dev.tree().find("/kadran/views/front") && dev.tree().find("/kadran/views/back"));
    write(dev, "/kadran/views/front", "N", {});
    CHECK(ex.activeView() == "Front");
    write(dev, "/kadran/views/next", "N", {});
    CHECK(ex.activeView() == "Back");
    write(dev, "/kadran/view", "s", {"Front"});
    CHECK(ex.activeView() == "Front" && dev.tree().find("/kadran/view")->get().front().text() == "Front");

    // A widget removed, its node goes
    ex.remove(id);
    pump([&] { return dev.tree().find("/kadran/controls/live/level") == nullptr; });
    CHECK(!dev.tree().find("/kadran/controls/live/level"));

    if (!failures) std::printf("osc device: ok\n");
    return failures ? 1 : 0;
}
