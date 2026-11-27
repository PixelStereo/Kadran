#include "Explorer.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>
#include <functional>

namespace {

bool waitFor(const char *label, const std::function<bool()> &condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 6000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (condition()) return true;
    std::fprintf(stderr, "Timed out: %s\n", label);
    return false;
}

bool check(bool ok, const char *label)
{
    if (!ok) std::fprintf(stderr, "Failed: %s\n", label);
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;

    Explorer explorer;
    const auto device = explorer.addDevice("OSCQbench", "127.0.0.1", QString::fromLatin1(argv[1]).toUShort());
    bool ready = false;
    QObject::connect(&explorer, &Explorer::deviceReady, [&](quint64 id) { ready |= id == device; });
    explorer.openDevice(device);
    if (!waitFor("OSCQuery discovery", [&] { return ready; })) return 1;

    if (!check(explorer.value(device, "/old").toDouble() == 0.125, "initial removable node")) return 1;

    const auto nil = explorer.addControl(device, "/N", "tap");
    const auto impulse = explorer.addControl(device, "/I", "tap");
    if (!check(nil && impulse && explorer.control(nil)->widget == "tap"
               && explorer.control(impulse)->widget == "tap", "two momentary controls")) return 1;
    if (!check(explorer.widgetsFor(device, "/signal").isEmpty()
               && !explorer.setJson(device, "/signal", true), "read-only impulse cannot send")) return 1;
    if (!check(explorer.setValue(nil, 1) && explorer.setValue(impulse, 1), "press both controls")) return 1;

    if (!check(explorer.value(device, "/color").toString() == "#FF8040C0", "OSCQuery RGBA value")) return 1;
    if (!check(explorer.setJson(device, "/color", double(0xff8040c0u)), "send packed OSC RGBA")) return 1;

    const auto meter = explorer.addControl(device, "/meter");
    if (!check(meter != 0, "add a meter")) return 1;
    if (!waitFor("WebSocket LISTEN value", [&] { return explorer.control(meter)->value == 0.75; })) return 1;
    explorer.device(device)->client->listen("/color");
    if (!waitFor("PATH_ADDED", [&] { return explorer.value(device, "/added").toDouble() == 0.5; })) return 1;
    if (!check(explorer.addControl(device, "/added") != 0, "use the added node")) return 1;
    if (!waitFor("PATH_REMOVED", [&] { return explorer.value(device, "/old").isUndefined(); })) return 1;
    return 0;
}
