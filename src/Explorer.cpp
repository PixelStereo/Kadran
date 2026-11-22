#include "Explorer.h"

#include <QHash>
#include <QJsonArray>
#include <algorithm>
#include <QHostInfo>
#include <QNetworkInterface>
#include <cmath>
#include <functional>
#include <QSet>

using ParamInfo = pxs::controls::ParamInfo; // what a view of a parameter knows of it (pxs-core)

using namespace pxs;
using pxs::controls::Control;

namespace {
// control/<key>: lower case, letters and digits, the rest "_"; numbered when taken
QString makeKey(const QString &name, const QStringList &taken, int index)
{
    QString key;
    for (const QChar c : name.toLower()) {
        if (c.isLetterOrNumber() && c.unicode() < 128) key += c;
        else if (!key.isEmpty() && !key.endsWith(QLatin1Char('_'))) key += QLatin1Char('_');
    }
    while (key.endsWith(QLatin1Char('_'))) key.chop(1);
    if (key.isEmpty()) key = QStringLiteral("p%1").arg(index);
    QString unique = key;
    for (int n = 2; taken.contains(unique); ++n) unique = QStringLiteral("%1_%2").arg(key).arg(n);
    return unique;
}
} // namespace

Explorer::Explorer(QObject *parent) : QObject(parent), m_discovery(this), m_oscDiscovery(this)
{
    m_retry.setInterval(5000); // a device the page shows that is not answering is tried again
    connect(&m_retry, &QTimer::timeout, this, &Explorer::retry);
    m_retry.start();
    m_filterTimer.setInterval(16);
    connect(&m_filterTimer, &QTimer::timeout, this, &Explorer::filterFrame);
    setPrefs({}); // the defaults, until the saved preferences are read
    m_server = new controls::Server(this, this);
    m_server->setName(QStringLiteral("Kadran"));
    m_server->setPageTitle(QStringLiteral("Kadran"));
    connect(&m_discovery, &oscquery::Discovery::deviceFound, this, [this](const oscquery::Device &d) {
        if (m_selfPort && d.port == m_selfPort && (d.address.isLoopback() || QNetworkInterface::allAddresses().contains(d.address))) return; // Kadran itself
        Dev *dev = device(addDevice(d.name, d.address.toString(), d.port));
        dev->online = true;
        if (dev->client && dev->client->state() == oscquery::Client::State::Failed) openDevice(dev->id); // it came back, maybe elsewhere
        emit devicesChanged();
    });
    connect(&m_discovery, &oscquery::Discovery::deviceLost, this, [this](const oscquery::Device &d) {
        for (auto &dev : m_devices)
            if (dev->kind == Dev::Kind::Query && dev->name == d.name) dev->online = false;
        pruneDevices();
        emit devicesChanged();
    });
    connect(&m_oscDiscovery, &oscquery::Discovery::deviceFound, this, [this](const oscquery::Device &d) { nearbyFound(d, false); });
    connect(&m_oscDiscovery, &oscquery::Discovery::deviceLost, this, [this](const oscquery::Device &d) { nearbyFound(d, true); });
}

bool Explorer::startSearch()
{
    const bool oscjson = m_discovery.start();
    m_oscDiscovery.start(QStringLiteral("_osc._udp")); // only offered: the plain OSC devices are added by the operator
    return oscjson;
}

// A plain OSC service on the network: kept to be offered ("added" tells which ones are devices already)
void Explorer::nearbyFound(const oscquery::Device &d, bool gone)
{
    const QString key = d.name + QLatin1Char('@') + QString::number(d.port);
    if (gone) {
        m_nearby.remove(key);
    } else {
        if (m_selfOscPort && d.port == m_selfOscPort && (d.address.isLoopback() || QNetworkInterface::allAddresses().contains(d.address))) return; // Kadran itself
        m_nearby.insert(key, d);
    }
    emit nearbyChanged();
}

QJsonArray Explorer::nearbyJson() const
{
    QJsonArray out;
    for (const oscquery::Device &d : m_nearby) {
        QString computer = d.host;
        while (computer.endsWith(QLatin1Char('.'))) computer.chop(1);
        if (computer.endsWith(QLatin1String(".local"), Qt::CaseInsensitive)) computer.chop(6);
        const bool added = std::any_of(m_devices.begin(), m_devices.end(), [&](const auto &x) { return x->kind == Dev::Kind::Osc && x->port == d.port && x->host == d.address.toString(); });
        out.append(QJsonObject{{"name", d.name}, {"computer", computer}, {"address", d.address.toString()}, {"port", d.port}, {"added", added}});
    }
    return out;
}

Explorer::~Explorer()
{
    m_server->stop();
}

Explorer::Dev *Explorer::device(quint64 id) const
{
    for (const auto &d : m_devices)
        if (d->id == id) return d.get();
    return nullptr;
}

quint64 Explorer::addDevice(const QString &name, const QString &host, quint16 port)
{
    for (const auto &d : m_devices)
        if (d->kind == Dev::Kind::Query && (d->name == name || (d->host == host && d->port == port))) {
            d->host = host; // the same device, announced again (maybe elsewhere)
            d->port = port;
            return d->id;
        }
    auto dev = std::make_unique<Dev>();
    dev->id = m_nextDevice++;
    dev->name = name;
    dev->host = host;
    dev->port = port;
    const quint64 id = dev->id;
    m_devices.push_back(std::move(dev));
    emit devicesChanged();
    return id;
}

void Explorer::openDevice(quint64 id)
{
    Dev *d = device(id);
    if (!d || d->kind != Dev::Kind::Query) return;
    if (d->client && d->client->state() != oscquery::Client::State::Failed) {
        if (d->client->state() == oscquery::Client::State::Ready) emit deviceReady(id);
        return;
    }
    d->client = std::make_unique<oscquery::Client>();
    oscquery::Client *c = d->client.get();
    connect(c, &oscquery::Client::ready, this, [this, id] {
        if (Dev *dev = device(id)) {
            if (!dev->client->name().isEmpty() && dev->name.isEmpty()) dev->name = dev->client->name();
            dev->online = true;
        }
        listenAll(id);
        changed(); // the ranges and choices of its views
        emit deviceReady(id);
        emit devicesChanged();
    });
    connect(c, &oscquery::Client::treeChanged, this, [this, id] {
        changed();
        emit deviceTreeChanged(id);
    });
    connect(c, &oscquery::Client::failed, this, [this, id](const QString &why) {
        if (Dev *dev = device(id)) dev->online = false;
        emit deviceFailed(id, why);
        pruneDevices(); // one nothing shows goes (it stays in m_pruned: this is its own client's signal)
        emit devicesChanged();
    });
    c->open(d->host, d->port);
}

// The devices the controls need that are not open (not yet found when a saved interface was opened, gone and back): tried again
void Explorer::retry()
{
    for (const auto &d : m_devices) {
        const bool needed = std::any_of(m_controls.begin(), m_controls.end(), [&](const Control &c) { return c.boundScope == d->id; });
        if (needed && d->client && d->client->state() == oscquery::Client::State::Failed) openDevice(d->id);
    }
}

void Explorer::listenAll(quint64 deviceId)
{
    Dev *d = device(deviceId);
    if (!d || !d->client) return;
    for (const Control &c : m_controls) {
        if (c.boundScope != deviceId || !c.bound()) continue;
        if (c.kind == Control::Kind::Color) continue;
        for (const QString &p : c.boundPaths()) d->client->listen(p);
    }
}

quint64 Explorer::addControl(quint64 deviceId, const QString &path, const QString &widget, quint64 group)
{
    Dev *d = device(deviceId);
    if (!d || !ready(d)) return 0;
    std::optional<Control> c = controls::forParameter(*this, deviceId, path, group);
    if (!c) return 0;
    ParamInfo info;
    QString wanted = widget; // the one asked for, or the one the preferences give to this type of parameter
    if (wanted.isEmpty() && this->info(deviceId, path, &info)) {
        static const QHash<int, const char *> keys = {{int(ParamInfo::Type::Float), "float"}, {int(ParamInfo::Type::Int), "int"}, {int(ParamInfo::Type::Bool), "bool"},
                                                      {int(ParamInfo::Type::Choice), "choice"}, {int(ParamInfo::Type::Trigger), "trigger"}};
        wanted = m_prefs.value("widgets").toObject().value(QLatin1String(keys.value(int(info.type), ""))).toString();
    }
    if (!wanted.isEmpty()) {
        const auto allowed = widgetsFor(deviceId, path);
        if (std::any_of(allowed.begin(), allowed.end(), [&](const auto &w) { return w.first == wanted; })) c->widget = wanted;
    }
    c->boundLabel = d->name + QLatin1Char(':') + path;
    const quint64 id = controls::add(m_controls, m_nextControl, *c, makeKey);
    if (!id) return 0;
    listenTo(d, path);
    changed();
    return id;
}

quint64 Explorer::addPad(quint64 deviceId, const QString &pathX, const QString &pathY, quint64 group)
{
    Dev *d = device(deviceId);
    if (!d || !ready(d)) return 0;
    std::optional<Control> c = controls::forParameters(*this, deviceId, pathX, pathY, group);
    if (!c) return 0;
    c->boundLabel = d->name + QLatin1Char(':') + pathX + QStringLiteral(", ") + pathY;
    const quint64 id = controls::add(m_controls, m_nextControl, *c, makeKey);
    if (!id) return 0;
    listenTo(d, pathX);
    listenTo(d, pathY);
    changed();
    return id;
}

QList<QPair<QString, QString>> Explorer::widgetsFor(quint64 deviceId, const QString &path) const
{
    ParamInfo info;
    if (!this->info(deviceId, path, &info) || info.type == ParamInfo::Type::Text) return {};
    const bool writable = info.access & ParamInfo::Write;
    // What a control only shows (a meter, a readout, a graph, a lamp) is there for what cannot be set, and for what can: it is seen as well
    const QList<QPair<QString, QString>> views = {{"meter", "Meter"}, {"hmeter", "Horizontal meter"}, {"readout", "Readout"}, {"graph", "Graph"}};
    QList<QPair<QString, QString>> out;
    if (info.isNumber()) { // a number that cannot be set is shown with a fader too: it moves with the value, and cannot be moved
        out = {{"vfader", "Vertical fader"}, {"fader", "Fader"}, {"knob", "Encoder"}, {"number", "Number"}, {"slim", "Slim fader"}, {"vslim", "Slim vertical fader"}};
        return writable ? out + views : views + out; // the default first: a meter for what is only read
    }
    if (info.type == ParamInfo::Type::Choice) return writable ? QList<QPair<QString, QString>>{{"buttons", "Buttons"}, {"menu", "Menu"}, {"readout", "Readout"}} : QList<QPair<QString, QString>>{{"readout", "Readout"}};
    if (info.type == ParamInfo::Type::Trigger) return writable ? QList<QPair<QString, QString>>{{"hold", "Hold button"}, {"tap", "Tap button"}} : QList<QPair<QString, QString>>{};
    if (writable) return {{"switch", "Switch"}, {"led", "LED"}};
    return {{"led", "LED"}};
}

// The preferences

static const QList<QPair<const char *, QStringList>> kTypeWidgets = {{"float", {"vfader", "fader", "knob", "number", "slim", "vslim", "meter", "hmeter", "readout", "graph"}},
                                                                     {"int", {"vfader", "fader", "knob", "number", "slim", "vslim", "meter", "hmeter", "readout", "graph"}},
                                                                     {"bool", {"switch", "led"}},
                                                                     {"choice", {"buttons", "menu", "readout"}},
                                                                     {"trigger", {"hold", "tap"}}};
static const QStringList kSizeWidgets = {"vfader", "fader", "knob", "number", "slim", "vslim", "meter", "hmeter", "readout", "graph", "switch", "led", "buttons", "menu", "hold", "tap",
                                         "picker", "pad", "adsr", "multi", "hmulti"};

QJsonObject Explorer::prefsOptions()
{
    QJsonObject types;
    for (const auto &t : kTypeWidgets) types.insert(QLatin1String(t.first), QJsonArray::fromStringList(t.second));
    return QJsonObject{{"types", types}, {"sizeWidgets", QJsonArray::fromStringList(kSizeWidgets)}};
}

// What a new control is by default: the smallest, the page's own minimum for its widget (a view 1 × 1, a vertical fader 1 × 3,
// a knob 2 × 2, the others 2 × 1), except the pad, the ADSR and the multi-sliders, a little bigger; and a number is a horizontal slim fader. What the preferences leave out is this.
static QJsonArray smallest(const QString &widget)
{
    if (widget == QLatin1String("vfader") || widget == QLatin1String("vslim")) return {1, 3};
    if (widget == QLatin1String("knob")) return {2, 2};
    // A little bigger than their minimum: these are played with several fingers, or read at a glance
    if (widget == QLatin1String("pad")) return {3, 3};   // the XY pad and the multiball pad
    if (widget == QLatin1String("adsr")) return {4, 2};
    if (widget == QLatin1String("multi") || widget == QLatin1String("hmulti")) return {4, 3};
    if (widget == QLatin1String("meter") || widget == QLatin1String("hmeter") || widget == QLatin1String("readout") || widget == QLatin1String("graph") ||
        widget == QLatin1String("led"))
        return {1, 1};
    return {2, 1};
}

void Explorer::setPrefs(const QJsonObject &prefs)
{
    QJsonObject widgets{{"float", "slim"}, {"int", "slim"}}, sizes;
    for (const QString &k : kSizeWidgets) sizes.insert(k, smallest(k));
    const QJsonObject w = prefs.value("widgets").toObject();
    for (const auto &t : kTypeWidgets)
        if (const QString v = w.value(QLatin1String(t.first)).toString(); t.second.contains(v)) widgets.insert(QLatin1String(t.first), v);
    const QJsonObject s = prefs.value("sizes").toObject();
    for (const QString &k : kSizeWidgets) {
        const QJsonArray a = s.value(k).toArray();
        if (a.size() == 2) sizes.insert(k, QJsonArray{std::clamp(a[0].toInt(1), 1, 12), std::clamp(a[1].toInt(1), 1, 40)});
    }
    // The page: the inspector is always there in edit mode (or opens from a tile), and a physics setting becomes an encoder
    const QString inspector = prefs.value("inspector").toString() == QLatin1String("onDemand") ? QStringLiteral("onDemand") : QStringLiteral("always");
    const QString physics = kTypeWidgets.first().second.contains(prefs.value("physics").toString()) ? prefs.value("physics").toString() : QStringLiteral("knob");
    // Pages: the same current page on every screen, or one for each screen (the views stay the same for all: a view is the whole layout)
    const QString pages = prefs.value("pages").toString() == QLatin1String("perScreen") ? QStringLiteral("perScreen") : QStringLiteral("shared");
    const QJsonObject clean{{"widgets", widgets}, {"sizes", sizes}, {"inspector", inspector}, {"physics", physics}, {"pages", pages}};
    if (clean == m_prefs) return;
    m_prefs = clean;
    emit prefsChanged();
}

// The interface

// An interface as a document: the devices its controls show (by name) and the controls
QJsonObject Explorer::interfaceJson(const std::vector<Control> &controls) const
{
    QJsonArray devices, list;
    const auto named = [&](quint64 scope) {
        if (!scope || std::any_of(devices.begin(), devices.end(), [&](const QJsonValue &v) { return v.toObject().value("id").toString().toULongLong() == scope; })) return;
        const Dev *d = device(scope);
        if (!d) return;
        QJsonObject o{{"id", QString::number(d->id)}, {"name", d->name}, {"host", d->host}, {"port", d->port}};
        if (d->kind == Dev::Kind::Osc) {
            QJsonArray points;
            for (const Point &p : d->points) points.append(QJsonObject{{"path", p.path}, {"type", p.type}, {"min", p.min}, {"max", p.max}});
            o.insert("kind", "osc");
            o.insert("points", points);
        }
        devices.append(o);
    };
    for (const Control &c : controls) {
        named(c.boundScope);
        for (const controls::Target &t : c.targets) named(t.scope);
        list.append(c.toJson());
    }
    return QJsonObject{{"format", "kadran"}, {"version", 1}, {"devices", devices}, {"controls", list}};
}

QJsonObject Explorer::toJson() const
{
    QJsonObject o = interfaceJson(m_controls);
    QJsonArray views;
    for (const View &v : m_views) {
        QJsonObject doc = interfaceJson(v.controls);
        doc.insert("name", v.name);
        views.append(doc);
    }
    o.insert("views", views);
    o.insert("view", m_activeView);
    return o;
}

void Explorer::clearControls()
{
    m_filters.clear();
    for (const Control &c : m_controls)
        for (const QString &p : c.boundPaths())
            if (Dev *d = device(c.boundScope); d && d->client) d->client->ignore(p);
    m_controls.clear();
    changed();
}

static void unlisten(Explorer &ex, const std::vector<Control> &gone, const std::vector<Control> &kept)
{
    for (const Control &c : gone)
        for (const QString &p : c.boundPaths()) {
            const bool used = std::any_of(kept.begin(), kept.end(), [&](const Control &o) { return o.boundScope == c.boundScope && o.boundPaths().contains(p); });
            if (!used)
                if (Explorer::Dev *d = ex.device(c.boundScope); d && d->client) d->client->ignore(p);
        }
}

// What is held by a group, at any depth
static QSet<quint64> held(const std::vector<Control> &list, quint64 group)
{
    QSet<quint64> in;
    for (bool more = true; more;) {
        more = false;
        for (const Control &c : list)
            if (!in.contains(c.id) && c.group && (c.group == group || in.contains(c.group))) {
                in.insert(c.id);
                more = true;
            }
    }
    return in;
}

quint64 Explorer::addPage(const QString &name)
{
    int pages = 0;
    for (const Control &c : m_controls) pages += c.kind == Control::Kind::Group && c.group == 0;
    Control page;
    page.kind = Control::Kind::Group;
    page.name = name.isEmpty() ? QStringLiteral("Page %1").arg(pages + 1) : name.left(60);
    const quint64 id = controls::add(m_controls, m_nextControl, page, makeKey);
    if (id) changed();
    return id;
}

// The controls that go with `ids` are taken out of the list, and no longer listened to
static int takeOut(Explorer &ex, std::vector<Control> &list, const QSet<quint64> &ids)
{
    std::vector<Control> gone, kept;
    for (const Control &c : list) (ids.contains(c.id) ? gone : kept).push_back(c);
    list = kept;
    unlisten(ex, gone, kept);
    return int(gone.size());
}

int Explorer::removePage(quint64 page)
{
    const Control *c = controls::find(m_controls, page);
    if (!c || c->kind != Control::Kind::Group || c->group != 0) return -1;
    QSet<quint64> ids = held(m_controls, page);
    ids.insert(page);
    const int n = takeOut(*this, m_controls, ids);
    changed();
    return n - 1;
}

int Explorer::clearPage(quint64 page)
{
    QSet<quint64> ids;
    if (page) {
        ids = held(m_controls, page);
    } else { // the controls that are on no page
        for (const Control &c : m_controls)
            if (!c.group && c.kind != Control::Kind::Group) ids.insert(c.id);
        for (const Control &c : m_controls)
            if (c.group && ids.contains(c.group)) ids.insert(c.id);
    }
    const int n = takeOut(*this, m_controls, ids);
    changed();
    return n;
}

int Explorer::clearAll(bool onePage)
{
    const int n = int(m_controls.size());
    clearControls();
    if (onePage) { // a new interface has no views either
        m_views.clear();
        m_activeView.clear();
        emit viewsChanged();
        addPage();
    }
    return n;
}

// A document's controls, with the devices they name: the one known by that name, else one added (offline until it is found)
bool Explorer::parseInterface(const QJsonObject &o, std::vector<Control> *out, QList<quint64> *devices, QString *error)
{
    if (o.value("format").toString() != QLatin1String("kadran")) {
        if (error) *error = QStringLiteral("Not a Kadran interface");
        return false;
    }
    const QJsonArray list = o.value("controls").toArray();
    if (list.size() > controls::kMaxControls) {
        if (error) *error = QStringLiteral("Too many controls");
        return false;
    }
    QHash<quint64, quint64> scopes; // saved id -> id here
    for (const QJsonValue &v : o.value("devices").toArray()) {
        const QJsonObject d = v.toObject();
        const quint64 saved = d.value("id").toString().toULongLong();
        const QString name = d.value("name").toString();
        const quint16 port = quint16(d.value("port").toInt());
        if (!saved || name.isEmpty()) continue;
        quint64 id = 0;
        if (d.value("kind").toString() == QLatin1String("osc")) { // by where it is: its name is the operator's
            const QString host = d.value("host").toString();
            for (const auto &known : m_devices)
                if (known->kind == Dev::Kind::Osc && known->host == host && known->port == port) id = known->id;
            if (!id) id = addOscDevice(name, host, port);
            if (id) {
                for (const QJsonValue &pv : d.value("points").toArray()) {
                    const QJsonObject po = pv.toObject();
                    Point pt{po.value("path").toString(), po.value("type").toString(), po.value("min").toDouble(0), po.value("max").toDouble(1)};
                    addPoint(id, pt); // the ones already there are refused
                }
            }
        } else {
            for (const auto &known : m_devices)
                if (known->kind == Dev::Kind::Query && known->name == name) id = known->id;
            if (!id) id = addDevice(name, d.value("host").toString(), port);
        }
        if (!id) continue;
        scopes.insert(saved, id);
    }
    std::vector<Control> next;
    quint64 highest = 0;
    for (const QJsonValue &v : list) {
        if (!v.isObject()) continue;
        Control c = Control::fromJson(v.toObject());
        if (c.bound()) {
            const auto it = scopes.constFind(c.boundScope);
            if (it == scopes.cend()) continue; // a view of a device the file does not give
            c.boundScope = it.value();
        }
        for (size_t i = c.targets.size(); i-- > 0;) { // the devices a macro sends to, as they are here
            const auto it = scopes.constFind(c.targets[i].scope);
            if (it == scopes.cend()) c.targets.erase(c.targets.begin() + long(i));
            else c.targets[i].scope = it.value();
        }
        highest = std::max(highest, c.id);
        next.push_back(std::move(c));
    }
    QSet<quint64> ids;
    for (Control &c : next) {
        if (!c.id || ids.contains(c.id)) c.id = ++highest;
        ids.insert(c.id);
    }
    controls::keyAll(next, makeKey);
    controls::tidyGroups(next);
    m_nextControl = std::max(m_nextControl, highest + 1);
    *out = std::move(next);
    for (quint64 id : std::as_const(scopes)) devices->append(id);
    return true;
}

bool Explorer::fromJson(const QJsonObject &o, QString *error)
{
    std::vector<Control> next;
    QList<quint64> devices;
    if (!parseInterface(o, &next, &devices, error)) return false;
    std::vector<View> views;
    for (const QJsonValue &v : o.value("views").toArray()) {
        const QJsonObject doc = v.toObject();
        View view;
        view.name = doc.value("name").toString();
        if (view.name.isEmpty() || !parseInterface(doc, &view.controls, &devices, nullptr)) continue;
        views.push_back(std::move(view));
    }
    m_holdPrune = true; // the devices the document names are not referenced until its controls are in
    clearControls();
    m_controls = std::move(next);
    m_views = std::move(views);
    m_activeView = o.value("view").toString();
    if (viewNames().indexOf(m_activeView) < 0) m_activeView.clear();
    m_holdPrune = false;
    changed();
    emit viewsChanged();
    for (quint64 id : std::as_const(devices)) { // their values come when they answer
        openDevice(id);
        listenAll(id);
    }
    return true;
}

// The views

void Explorer::changed()
{
    pruneDevices();
    ++m_revision;
    if (!m_activeView.isEmpty()) // the page is the active view
        for (View &v : m_views)
            if (v.name == m_activeView) v.controls = m_controls;
    emit controlsChanged();
}

QStringList Explorer::viewNames() const
{
    QStringList out;
    for (const View &v : m_views) out << v.name;
    return out;
}

QString Explorer::newView(const QString &name)
{
    if (m_views.size() >= 64) return QString();
    QString base = name.trimmed().left(48);
    if (base.isEmpty()) base = QStringLiteral("View");
    QString given = base;
    for (int n = 2; viewNames().contains(given); ++n) given = base + QLatin1Char(' ') + QString::number(n);
    m_views.push_back({given, m_controls});
    m_activeView = given;
    emit viewsChanged();
    return given;
}

bool Explorer::recallView(const QString &name)
{
    if (name == m_activeView) return true;
    for (const View &v : m_views)
        if (v.name == name) {
            const std::vector<Control> next = v.controls; // a copy: changed() writes the page into the active view
            m_activeView = name;
            const std::vector<Control> gone = m_controls;
            unlisten(*this, gone, next);
            m_controls = next;
            changed();
            for (const auto &d : m_devices) listenAll(d->id);
            emit viewsChanged();
            emit viewRecalled();
            return true;
        }
    return false;
}

bool Explorer::deleteView(const QString &name)
{
    for (size_t i = 0; i < m_views.size(); ++i)
        if (m_views[i].name == name) {
            m_views.erase(m_views.begin() + long(i));
            if (m_activeView == name) m_activeView.clear(); // the page stays as it is
            emit viewsChanged();
            return true;
        }
    return false;
}

bool Explorer::renameView(const QString &name, const QString &to)
{
    const QString clean = to.trimmed().left(48);
    if (clean.isEmpty() || (clean != name && viewNames().contains(clean))) return false;
    for (View &v : m_views)
        if (v.name == name) {
            v.name = clean;
            if (m_activeView == name) m_activeView = clean;
            emit viewsChanged();
            return true;
        }
    return false;
}

// The page

bool Explorer::serve()
{
    m_server->stop();
    return m_server->start(0, QString());
}

QString Explorer::pageUrl() const
{
    return m_server->isRunning() ? QStringLiteral("http://127.0.0.1:%1/control").arg(m_server->port()) : QString();
}

QStringList Explorer::networkUrls() const { return m_server->urls(); }

// Backend

// The reset addresses of what a control is a view of (a device that has them says so: NYXMAP_RESET)
void Explorer::fillResets(Control &c) const
{
    c.resets.clear();
    const Dev *d = device(c.boundScope);
    if (!d || !d->client) return;
    for (const QString &p : c.boundPaths())
        if (const auto *n = d->client->tree().find(p); n && !n->resetPath.isEmpty() && !c.resets.contains(n->resetPath)) c.resets << n->resetPath;
}

std::vector<Control> Explorer::controls() const
{
    std::vector<Control> list = m_controls;
    for (Control &c : list) {
        if (c.bound()) {
            controls::fillBound(c, *this);
            fillResets(c);
        }
        for (controls::Target &t : c.targets)
            if (const Dev *d = device(t.scope)) t.label = d->name + QLatin1Char(':') + t.path;
        if (c.macro()) controls::fillFeedback(c, *this); // the target that gives the feedback
    }
    controls::fillPhysics(list);
    return list;
}

std::optional<Control> Explorer::control(quint64 id) const
{
    const Control *c = controls::find(m_controls, id);
    if (!c) return std::nullopt;
    Control out = *c;
    if (out.bound()) {
        controls::fillBound(out, *this);
        fillResets(out);
    }
    if (out.macro()) controls::fillFeedback(out, *this);
    if (out.physTarget) {
        std::vector<Control> list = m_controls;
        controls::fillPhysics(list);
        if (const Control *filled = controls::find(list, id)) out.value = filled->value;
    }
    return out;
}

// What a tile sends goes through its filters (0..1 of its range), then to the device
bool Explorer::setValue(quint64 id, double value)
{
    const Control *c = controls::find(m_controls, id);
    if (!c || c->kind == Control::Kind::Color || c->kind == Control::Kind::Group || !std::isfinite(value)) return false;
    if (!c->physTarget && (c->kind == Control::Kind::Slider || c->kind == Control::Kind::Toggle) && controls::anyFilterOn(c->filters)) {
        const bool slider = c->kind == Control::Kind::Slider;
        const double lo = slider ? c->min : 0, span = slider ? c->max - c->min : 1;
        const std::optional<double> out = m_filters.input(id, c->filters, span > 0 ? (value - lo) / span : 0);
        if (m_filters.active() && !m_filterTimer.isActive()) {
            m_filterClock.restart();
            m_filterTimer.start();
        }
        return out ? deliverValue(id, lo + *out * span) : true; // a stage that holds it back: it comes later
    }
    m_filters.forget(id);
    return deliverValue(id, value);
}

// The frame of the filters that go on by themselves
void Explorer::filterFrame()
{
    const double dt = double(m_filterClock.restart()) / 1000.0;
    for (const auto &[id, x] : m_filters.step(dt)) {
        const Control *c = controls::find(m_controls, id);
        if (!c || !controls::anyFilterOn(c->filters)) {
            m_filters.forget(id);
            continue;
        }
        const bool slider = c->kind == Control::Kind::Slider;
        const double lo = slider ? c->min : 0, span = slider ? c->max - c->min : 1;
        deliverValue(id, lo + x * span);
    }
    if (!m_filters.active()) m_filterTimer.stop();
}

bool Explorer::deliverValue(quint64 id, double value)
{
    const Control *c = controls::find(m_controls, id);
    if (!c || c->kind == Control::Kind::Color || c->kind == Control::Kind::Group || !std::isfinite(value)) return false;
    if (c->physTarget) { // a view of a physics setting of another control
        const bool ok = controls::setPhysics(m_controls, id, value);
        if (ok) emit controlsChanged(); // kept, with no new list for the pages
        return ok;
    }
    if (!c->boundParam.isEmpty()) return controls::setBound(*c, *this, value);
    const bool kept = controls::setValue(m_controls, id, value, {});
    if (const Control *m = controls::find(m_controls, id); m && m->macro()) // a macro: its value goes to what it sends to
        return controls::setTargets(*m, *this, value, 0, m->widget != QLatin1String("tap")) || kept;
    return kept;
}

bool Explorer::setPoint(quint64 id, double x, double y)
{
    const Control *c = controls::find(m_controls, id);
    if (c && !c->boundParam2.isEmpty()) return controls::setBoundPoint(*c, *this, x, y); // a pad over two parameters
    const bool kept = controls::setPoint(m_controls, id, x, y, {});
    if (const Control *m = controls::find(m_controls, id); m && m->macro()) { // a macro pad: X and Y each go to their targets
        const bool a = controls::setTargets(*m, *this, x, 0), b = controls::setTargets(*m, *this, y, 1);
        return a || b || kept;
    }
    return kept;
}
bool Explorer::setPoints(quint64 id, const std::vector<double> &xy)
{
    const Control *c = controls::find(m_controls, id);
    if (c && c->kind == Control::Kind::Points && c->boundScope && !c->balls.empty()) { // balls on parameters: they are set, the points follow what they answer
        const bool ok = controls::setPoints(m_controls, id, xy, {}); // the points kept as well (a ball with no address has only these)
        return controls::setBoundPoints(*controls::find(m_controls, id), *this, xy) || ok;
    }
    return controls::setPoints(m_controls, id, xy, {});
}
bool Explorer::setMulti(quint64 id, const std::vector<double> &v) { return controls::setMulti(m_controls, id, v, {}); }
bool Explorer::setAdsr(quint64 id, const std::array<double, 4> &v) { return controls::setAdsr(m_controls, id, v, {}); }

bool Explorer::setColor(quint64 id, double r, double g, double b)
{
    Control *c = controls::find(m_controls, id);
    if (!c || c->kind != Control::Kind::Color) return false;
    return c->boundParam.isEmpty() ? controls::setColor(*c, r, g, b, {}) : controls::setBoundColor(*c, *this, r, g, b);
}

bool Explorer::setLayouts(const QHash<quint64, std::array<int, 4>> &places)
{
    const bool did = controls::setLayouts(m_controls, places);
    if (did) changed();
    return did;
}

bool Explorer::update(quint64 id, const Control &change)
{
    const Control *old = controls::find(m_controls, id);
    if (!old) return false;
    const QStringList before = old->boundPaths();
    const quint64 scope = old->boundScope;
    if (!controls::update(m_controls, id, change, makeKey).found) return false;
    const Control *now = controls::find(m_controls, id);
    if (now && scope) { // the addresses of a ball can be changed: the new ones are listened to, the ones nothing shows any more are not
        const QStringList after = now->boundPaths();
        if (Dev *d = device(scope); d && d->client) {
            for (const QString &p : after) d->client->listen(p);
            for (const QString &p : before) {
                if (after.contains(p)) continue;
                const bool used = std::any_of(m_controls.begin(), m_controls.end(), [&](const Control &o) { return o.boundScope == scope && o.boundPaths().contains(p); });
                if (!used) d->client->ignore(p);
            }
        }
    }
    changed();
    return true;
}

quint64 Explorer::add(const Control &control)
{
    const quint64 id = controls::add(m_controls, m_nextControl, control, makeKey);
    if (id) changed();
    return id;
}

bool Explorer::reset(quint64 id)
{
    const Control *c = controls::find(m_controls, id);
    if (!c || !c->bound()) return false;
    Control filled = *c;
    fillResets(filled);
    bool any = false;
    for (const QString &path : filled.resets) any |= setJson(c->boundScope, path, true);
    return any;
}

bool Explorer::remove(quint64 id)
{
    const Control *c = controls::find(m_controls, id);
    if (!c) return false;
    const quint64 scope = c->boundScope;
    const QStringList paths = c->boundPaths();
    if (!controls::remove(m_controls, id)) return false;
    if (scope && !paths.isEmpty()) { // nothing else shows it: not listened to any more
        for (const QString &p : paths) {
            const bool used = std::any_of(m_controls.begin(), m_controls.end(), [&](const Control &o) { return o.boundScope == scope && o.boundPaths().contains(p); });
            if (!used)
                if (Dev *d = device(scope); d && d->client) d->client->ignore(p);
        }
    }
    changed();
    return true;
}

// ParameterSource

bool Explorer::info(quint64 scope, const QString &path, ParamInfo *out) const
{
    const Dev *d = device(scope);
    const oscquery::RemoteTree *t = treeOf(d);
    const oscquery::RemoteNode *n = t ? t->find(path) : nullptr;
    return n && n->toParamInfo(out);
}

QJsonValue Explorer::value(quint64 scope, const QString &path) const
{
    const Dev *d = device(scope);
    const oscquery::RemoteTree *t = treeOf(d);
    const oscquery::RemoteNode *n = t ? t->find(path) : nullptr;
    return n && !n->value.isEmpty() ? n->value[0] : QJsonValue(QJsonValue::Undefined);
}

bool Explorer::setNumber(quint64 scope, const QString &path, double v)
{
    Dev *d = device(scope);
    if (d && d->kind == Dev::Kind::Osc) return sendOsc(d, path, QJsonArray{v});
    return d && d->client && d->client->write(path, QJsonArray{v});
}

bool Explorer::setJson(quint64 scope, const QString &path, const QJsonValue &v)
{
    Dev *d = device(scope);
    if (d && d->kind == Dev::Kind::Osc) return sendOsc(d, path, QJsonArray{v});
    if (!d || !d->client) return false;
    const oscquery::RemoteNode *n = d->client->tree().find(path);
    if (n && n->type == QLatin1String("N")) return d->client->write(path, {});
    return d->client->write(path, QJsonArray{v});
}

// The front page

QList<Explorer::Target> Explorer::targetsFromJson(const QJsonArray &list)
{
    QList<Target> out;
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        const quint64 device = o.value("device").toVariant().toULongLong();
        const QString path = o.value("path").toString();
        if (device && path.startsWith(QLatin1Char('/')) && path.size() < 512 && out.size() < 64) out.append({device, path});
    }
    return out;
}

// The parameter as a target: its own range to begin with
bool Explorer::makeTarget(const Target &t, controls::Target *out) const
{
    ParamInfo info;
    if (!this->info(t.device, t.path, &info) || !(info.access & ParamInfo::Write)) return false;
    out->scope = t.device;
    out->path = t.path;
    out->outMin = 0;
    out->outMax = 1;
    if (info.type == ParamInfo::Type::Float || info.type == ParamInfo::Type::Int) {
        out->outMin = info.min;
        out->outMax = info.max > info.min ? info.max : info.min + 1;
    } else if (info.type == ParamInfo::Type::Choice) {
        out->outMax = std::max(0, int(info.choices.size()) - 1);
    }
    return true;
}

quint64 Explorer::addMacro(const QList<Target> &targets, const QString &kind, quint64 group)
{
    Control c;
    c.kind = kind == QLatin1String("switch") ? Control::Kind::Toggle : (kind == QLatin1String("button") || kind == QLatin1String("hold")) ? Control::Kind::Button : Control::Kind::Slider;
    c.min = 0;
    c.max = 1;
    c.step = 0.01;
    c.group = group;
    if (kind == QLatin1String("button")) c.widget = QStringLiteral("tap");
    else if (kind == QLatin1String("hold")) c.widget = QStringLiteral("hold");
    for (const Target &t : targets) {
        controls::Target n;
        if (c.targets.size() >= size_t(Control::kTargetsMax) || !makeTarget(t, &n)) continue;
        c.targets.push_back(n);
    }
    if (c.targets.empty()) return 0;
    const Dev *first = device(c.targets[0].scope);
    const QString leaf = c.targets[0].path.section(QLatin1Char('/'), -1);
    c.name = leaf + (c.targets.size() > 1 ? QStringLiteral(" +%1").arg(c.targets.size() - 1) : QString());
    Q_UNUSED(first)
    const quint64 id = controls::add(m_controls, m_nextControl, c, makeKey);
    if (id) changed();
    return id;
}

int Explorer::addToMacro(quint64 id, const QList<Target> &targets)
{
    Control *c = controls::find(m_controls, id);
    if (!c || !c->macro()) return 0;
    int added = 0;
    for (const Target &t : targets) {
        controls::Target n;
        if (c->targets.size() >= size_t(Control::kTargetsMax) || !makeTarget(t, &n)) continue;
        c->targets.push_back(n);
        ++added;
    }
    if (added) changed();
    return added;
}

QString Explorer::addTargets(const QList<Target> &targets, const QString &widget, bool pad, quint64 group, const QString &macro)
{
    if (targets.isEmpty()) return QString();
    if (!macro.isEmpty()) return addMacro(targets, macro, group) ? QStringLiteral("Added a multi-target interaction for %1 parameters").arg(targets.size()) : QStringLiteral("These cannot be sent to");
    const auto numeric = [this](const Target &t) { // a number that can be set
        ParamInfo info;
        return this->info(t.device, t.path, &info) && info.isNumber() && (info.access & ParamInfo::Write);
    };
    if (pad && widget.isEmpty() && targets.size() == 2 && targets[0].device == targets[1].device && numeric(targets[0]) && numeric(targets[1]))
        return addPad(targets[0].device, targets[0].path, targets[1].path, group) ? QStringLiteral("Added an XY pad") : QStringLiteral("These cannot make a pad");
    int added = 0;
    for (const Target &t : targets)
        if (addControl(t.device, t.path, widget, group)) ++added;
    return added ? QStringLiteral("Added %1 control(s)").arg(added) : QStringLiteral("Not shown as a control");
}

// The parameters under a node that a control can show, in the order of the namespace
static void collect(const Explorer &ex, quint64 device, const pxs::oscquery::RemoteNode &n, QStringList &out, int depth)
{
    if (n.isLeaf() && n.children.empty()) {
        if (!ex.widgetsFor(device, n.path).isEmpty()) out << n.path;
        return;
    }
    if (depth > 64) return;
    if (n.isLeaf() && !ex.widgetsFor(device, n.path).isEmpty()) out << n.path; // a parameter that has nodes under it (its reset)
    for (const auto &c : n.children)
        if (c.second.path != n.resetPath) collect(ex, device, c.second, out, depth + 1);
}

int Explorer::countNode(quint64 deviceId, const QString &path) const
{
    const Dev *d = device(deviceId);
    if (!d || !ready(d)) return 0;
    const auto *node = treeOf(d)->find(path);
    QStringList paths;
    if (node) collect(*this, deviceId, *node, paths, 0);
    return int(paths.size());
}

QString Explorer::addNode(quint64 deviceId, const QString &path, bool asPage, quint64 page)
{
    Dev *d = device(deviceId);
    if (!d || !ready(d)) return QStringLiteral("This device is not ready");
    const auto *node = treeOf(d)->find(path);
    QStringList paths;
    if (node) collect(*this, deviceId, *node, paths, 0);
    if (paths.isEmpty()) return QStringLiteral("Nothing to add here");
    if (m_controls.size() + size_t(paths.size()) + 1 > size_t(controls::kMaxControls)) return QStringLiteral("Too many controls on the page");
    Control group;
    group.kind = Control::Kind::Group;
    group.name = node->name.isEmpty() ? d->name : node->name;
    group.group = asPage ? 0 : page;
    const quint64 groupId = controls::add(m_controls, m_nextControl, group, makeKey);
    if (!groupId) return QStringLiteral("The page is full");
    int added = 0;
    for (const QString &p : paths)
        if (addControl(deviceId, p, QString(), groupId)) ++added;
    changed();
    return asPage ? QStringLiteral("Added a page with %1 control(s)").arg(added) : QStringLiteral("Added a group with %1 control(s)").arg(added);
}

bool Explorer::restore(const std::vector<Control> &list)
{
    for (const Control &c : list) { // a control back (undo) whose device went with it: the device is back too
        QList<quint64> scopes{c.boundScope};
        for (const controls::Target &t : c.targets) scopes << t.scope;
        for (quint64 scope : std::as_const(scopes)) {
            if (!scope || device(scope)) continue;
            const auto it = std::find_if(m_pruned.begin(), m_pruned.end(), [&](const auto &d) { return d->id == scope; });
            if (it == m_pruned.end()) continue;
            m_devices.push_back(std::move(*it));
            m_pruned.erase(it);
        }
    }
    m_controls = list;
    controls::tidyGroups(m_controls);
    changed();
    for (const auto &d : m_devices) listenAll(d->id);
    return true;
}

QJsonArray Explorer::devicesJson() const
{
    QJsonArray out;
    for (const auto &d : m_devices) {
        QString state = QStringLiteral("idle");
        if (d->kind == Dev::Kind::Osc) {
            state = QStringLiteral("ready");
        } else if (d->client) {
            switch (d->client->state()) {
            case pxs::oscquery::Client::State::Connecting: state = QStringLiteral("connecting"); break;
            case pxs::oscquery::Client::State::Ready: state = QStringLiteral("ready"); break;
            case pxs::oscquery::Client::State::Failed: state = QStringLiteral("failed"); break;
            default: break;
            }
        }
        out.append(QJsonObject{{"id", double(d->id)}, {"name", d->name.isEmpty() ? QStringLiteral("%1:%2").arg(d->host).arg(d->port) : d->name},
                               {"address", QStringLiteral("%1:%2").arg(d->host).arg(d->port)}, {"online", d->online || d->kind == Dev::Kind::Osc}, {"state", state},
                               {"kind", d->kind == Dev::Kind::Osc ? "osc" : "oscquery"}});
    }
    return out;
}

static QJsonObject nodeJson(const Explorer &ex, quint64 device, const pxs::oscquery::RemoteNode &n, int depth)
{
    QJsonObject o{{"n", n.name}, {"p", n.path}};
    if (!n.type.isEmpty()) o["t"] = n.type;
    if (!n.description.isEmpty()) o["d"] = n.description.left(200);
    if (n.isLeaf()) {
        QJsonArray widgets;
        for (const auto &w : ex.widgetsFor(device, n.path)) widgets.append(w.first);
        if (!widgets.isEmpty()) o["w"] = widgets;
    }
    // A parameter's reset is not shown: the controls have a button for it
    QJsonArray children;
    for (const auto &c : n.children)
        if (depth < 64 && c.second.path != n.resetPath) children.append(nodeJson(ex, device, c.second, depth + 1));
    if (!children.isEmpty()) o["c"] = children;
    return o;
}

QJsonObject Explorer::treeJson(quint64 deviceId) const
{
    const Dev *d = device(deviceId);
    if (!d || !ready(d)) return {};
    return nodeJson(*this, deviceId, treeOf(d)->root(), 0);
}

// Devices

const oscquery::RemoteTree *Explorer::treeOf(const Dev *d) const
{
    if (!d) return nullptr;
    if (d->kind == Dev::Kind::Osc) return &d->local;
    return d->client ? &d->client->tree() : nullptr;
}

bool Explorer::ready(const Dev *d) const
{
    if (!d) return false;
    if (d->kind == Dev::Kind::Osc) return true;
    return d->client && d->client->state() == oscquery::Client::State::Ready;
}

void Explorer::listenTo(Dev *d, const QString &path)
{
    if (d && d->client) d->client->listen(path);
}

quint64 Explorer::addOscDevice(const QString &name, const QString &host, quint16 port)
{
    const QString h = host.trimmed();
    if (!port || h.isEmpty() || h.size() > 253) return 0;
    for (const auto &d : m_devices)
        if (d->kind == Dev::Kind::Osc && d->host == h && d->port == port) return d->id;
    auto dev = std::make_unique<Dev>();
    dev->id = m_nextDevice++;
    dev->kind = Dev::Kind::Osc;
    dev->host = h;
    dev->port = port;
    dev->name = name.trimmed().left(64);
    if (dev->name.isEmpty()) dev->name = QStringLiteral("%1:%2").arg(h).arg(port);
    dev->online = true;
    rebuild(dev.get());
    const quint64 id = dev->id;
    Dev *raw = dev.get();
    if (!raw->address.setAddress(h)) { // a name: found out, not waited for
        raw->address.clear();
        QHostInfo::lookupHost(h, this, [this, id](const QHostInfo &info) {
            if (Dev *x = device(id); x && !info.addresses().isEmpty()) {
                const auto &all = info.addresses();
                const auto v4 = std::find_if(all.begin(), all.end(), [](const QHostAddress &a) { return a.protocol() == QAbstractSocket::IPv4Protocol; });
                x->address = v4 != all.end() ? *v4 : all.first();
            }
        });
    }
    m_devices.push_back(std::move(dev));
    emit devicesChanged();
    emit nearbyChanged(); // "added" of the offers
    return id;
}

bool Explorer::renameDevice(quint64 id, const QString &name)
{
    Dev *d = device(id);
    const QString n = name.trimmed().left(64);
    if (!d || n.isEmpty()) return false;
    if (d->name == n) return true;
    for (const auto &o : m_devices)
        if (o->id != id && o->kind == d->kind && o->name == n) return false; // two devices of one name could not be told apart in a saved interface
    const QString before = d->name + QLatin1Char(':');
    d->name = n;
    const auto relabel = [&](std::vector<Control> &list) {
        for (Control &c : list)
            if (c.boundScope == id && c.boundLabel.startsWith(before)) c.boundLabel = n + QLatin1Char(':') + c.boundLabel.mid(before.size());
    };
    relabel(m_controls);
    for (View &v : m_views) relabel(v.controls);
    changed(); // the controls' labels: "name:/path"
    emit devicesChanged();
    return true;
}

bool Explorer::referenced(quint64 id) const
{
    const auto uses = [&](const std::vector<Control> &list) {
        return std::any_of(list.begin(), list.end(), [&](const Control &c) {
            return c.boundScope == id || std::any_of(c.targets.begin(), c.targets.end(), [&](const controls::Target &t) { return t.scope == id; });
        });
    };
    if (uses(m_controls)) return true;
    return std::any_of(m_views.begin(), m_views.end(), [&](const View &v) { return uses(v.controls); });
}

bool Explorer::removeDevice(quint64 id)
{
    const auto it = std::find_if(m_devices.begin(), m_devices.end(), [&](const auto &d) { return d->id == id; });
    if (it == m_devices.end() || referenced(id)) return false;
    m_pruned.push_back(std::move(*it));
    m_devices.erase(it);
    emit devicesChanged();
    emit nearbyChanged();
    return true;
}

// A device that is gone and that no control shows is forgotten (a typed-in or announced one that is not answering, once its controls are deleted)
void Explorer::pruneDevices()
{
    if (m_holdPrune) return;
    bool any = false;
    for (size_t i = m_devices.size(); i-- > 0;) {
        Dev &d = *m_devices[i];
        if (d.kind != Dev::Kind::Query || d.online) continue;
        const auto state = d.client ? d.client->state() : oscquery::Client::State::Idle;
        if (state == oscquery::Client::State::Connecting) continue;
        if (referenced(d.id)) continue;
        m_pruned.push_back(std::move(m_devices[i]));
        m_devices.erase(m_devices.begin() + long(i));
        any = true;
    }
    while (m_pruned.size() > 32) m_pruned.erase(m_pruned.begin());
    if (any) emit devicesChanged();
}

// Points

static bool validPath(const QString &p)
{
    if (!p.startsWith(QLatin1Char('/')) || p.size() > 256 || p.endsWith(QLatin1Char('/'))) return false;
    for (const QString &seg : p.split(QLatin1Char('/')).mid(1))
        if (seg.isEmpty()) return false;
    for (const QChar c : p)
        if (c.unicode() < 33 || c.unicode() == 127 || QStringLiteral("#*,?[]{}").contains(c)) return false; // spaces, controls and the pattern characters
    return true;
}

void Explorer::rebuild(Dev *d)
{
    const oscquery::RemoteTree before = d->local; // the values sent stay
    std::function<QJsonObject(const QString &, const QStringList &)> make = [&](const QString &path, const QStringList &points) {
        QJsonObject contents;
        QStringList names;
        for (const QString &p : points) {
            const QString rest = p.mid(path == QLatin1String("/") ? 1 : path.size() + 1);
            const QString first = rest.section(QLatin1Char('/'), 0, 0);
            if (!names.contains(first)) names << first;
        }
        for (const QString &n : std::as_const(names)) {
            const QString child = (path == QLatin1String("/") ? QString() : path) + QLatin1Char('/') + n;
            const Point *leaf = nullptr;
            QStringList below;
            for (const Point &pt : std::as_const(d->points)) {
                if (pt.path == child) leaf = &pt;
                else if (pt.path.startsWith(child + QLatin1Char('/'))) below << pt.path;
            }
            if (leaf) {
                QJsonObject o{{"FULL_PATH", child}, {"TYPE", leaf->type}, {"ACCESS", leaf->type == QLatin1String("N") ? 2 : 3}};
                if (leaf->type == QLatin1String("f") || leaf->type == QLatin1String("i")) o.insert("RANGE", QJsonArray{QJsonObject{{"MIN", leaf->min}, {"MAX", leaf->max}}});
                if (const auto *old = before.find(child); old && !old->value.isEmpty()) o.insert("VALUE", old->value);
                contents.insert(n, o);
            } else {
                contents.insert(n, make(child, below));
            }
        }
        return QJsonObject{{"FULL_PATH", path}, {"ACCESS", 0}, {"CONTENTS", contents}};
    };
    QStringList all;
    for (const Point &p : std::as_const(d->points)) all << p.path;
    d->local.load(make(QStringLiteral("/"), all));
}

bool Explorer::addPoint(quint64 id, const Point &point)
{
    Dev *d = device(id);
    if (!d || d->kind != Dev::Kind::Osc || !validPath(point.path) || d->points.size() >= 512) return false;
    static const QStringList types = {"f", "i", "T", "N"};
    if (!types.contains(point.type)) return false;
    Point p = point;
    if (!(p.max > p.min)) { p.min = 0; p.max = p.type == QLatin1String("i") ? 127 : 1; }
    for (const Point &o : std::as_const(d->points))
        if (o.path == p.path || o.path.startsWith(p.path + QLatin1Char('/')) || p.path.startsWith(o.path + QLatin1Char('/'))) return false;
    d->points.append(p);
    rebuild(d);
    emit devicesChanged();
    emit deviceTreeChanged(id);
    return true;
}

bool Explorer::removePoint(quint64 id, const QString &path)
{
    Dev *d = device(id);
    if (!d || d->kind != Dev::Kind::Osc) return false;
    const auto it = std::find_if(d->points.begin(), d->points.end(), [&](const Point &p) { return p.path == path; });
    if (it == d->points.end()) return false;
    const bool used = std::any_of(m_controls.begin(), m_controls.end(), [&](const Control &c) { return c.boundScope == id && c.boundPaths().contains(path); }) ||
                      std::any_of(m_controls.begin(), m_controls.end(), [&](const Control &c) { return std::any_of(c.targets.begin(), c.targets.end(), [&](const controls::Target &t) { return t.scope == id && t.path == path; }); });
    if (used) return false;
    d->points.erase(it);
    rebuild(d);
    emit deviceTreeChanged(id);
    return true;
}

// The message of a value for a point, as the OSCQuery client builds it
bool Explorer::sendOsc(Dev *d, const QString &path, const QJsonArray &values)
{
    const auto *n = d->local.find(path);
    if (!n || n->type.size() != 1 || d->address.isNull() || !d->port) return false;
    osc::Message m;
    m.address = path;
    m.types = n->type;
    const QJsonValue v = values.isEmpty() ? QJsonValue() : values[0];
    const QChar tag = n->type[0];
    if (tag == QLatin1Char('T')) {
        const bool on = v.isBool() ? v.toBool() : v.toDouble() != 0;
        m.types = on ? QStringLiteral("T") : QStringLiteral("F");
        m.args << on;
    } else if (tag == QLatin1Char('i')) {
        m.args << int(std::lround(v.toDouble()));
    } else if (tag == QLatin1Char('f')) {
        m.args << v.toDouble();
    } else if (tag != QLatin1Char('N')) {
        return false;
    }
    if (m_udp.state() == QAbstractSocket::UnconnectedState) m_udp.bind(QHostAddress::AnyIPv4, 0);
    if (m_udp.writeDatagram(osc::encode(m), d->address, d->port) < 0) return false;
    if (tag != QLatin1Char('N')) d->local.setValue(path, QJsonArray{v});
    return true;
}
