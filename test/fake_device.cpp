// A small OSCQuery device to try Kadran on (a development tool, not part of the application): a few parameters, one drifting, announced on the network.
#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QTimer>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pxs::core::tree::Tree tree;
    double level = 0.5, rate = 2;
    int steps = 4;
    bool on = true;
    QString mode = "sine";
    auto add = [&](const char *path, const char *label, pxs::core::tree::Parameter::Type type, auto setup) {
        auto p = std::make_unique<pxs::core::tree::Parameter>(QString(path).section('/', -1).toStdString(), label, type);
        setup(*p);
        tree.add(path, std::move(p));
    };
    add("/mix/level", "Level", pxs::core::tree::Parameter::Type::Float, [&](pxs::core::tree::Parameter &p) {
        p.range(0, 1).bind([&] { return pxs::core::Value(level); }, [&](const pxs::core::Value &v) { level = v.number().value_or(0.0); });
    });
    add("/mix/rate", "Rate (Hz)", pxs::core::tree::Parameter::Type::Float, [&](pxs::core::tree::Parameter &p) {
        p.range(0.1, 20).bind([&] { return pxs::core::Value(rate); }, [&](const pxs::core::Value &v) { rate = v.number().value_or(0.0); });
    });
    add("/mix/steps", "Steps", pxs::core::tree::Parameter::Type::Int, [&](pxs::core::tree::Parameter &p) {
        p.range(1, 16).bind([&] { return pxs::core::Value(steps); }, [&](const pxs::core::Value &v) { steps = int(v.number().value_or(0.0)); });
    });
    add("/mix/on", "On", pxs::core::tree::Parameter::Type::Bool, [&](pxs::core::tree::Parameter &p) {
        p.bind([&] { return pxs::core::Value(on); }, [&](const pxs::core::Value &v) { on = v.truthy(); });
    });
    add("/osc/mode", "Mode", pxs::core::tree::Parameter::Type::Choice, [&](pxs::core::tree::Parameter &p) {
        p.choices({"sine", "saw", "square"}).bind([&] { return pxs::core::Value(mode); }, [&](const pxs::core::Value &v) { mode = QString::fromStdString(v.text()); });
    });
    tree.addAction("/osc/reset", "N", [&](const pxs::core::ValueList &) { level = 0.5; std::printf("reset\n"); return true; }, "Back to 0.5");

    pxs::OscService service(&tree, QStringLiteral("Demo device"));
    if (!service.start(0, 0, true)) return 1;
    std::printf("Demo device: OSCQuery http://127.0.0.1:%u, OSC udp %u\n", service.httpPort(), service.oscPort());
    std::fflush(stdout);

    QTimer drift; // the level moves by itself: what a view shows follows
    double t = 0;
    QObject::connect(&drift, &QTimer::timeout, [&] {
        t += 0.05;
        if (on) level = 0.5 + 0.4 * std::sin(t * rate);
    });
    drift.start(50);
    return app.exec();
}
