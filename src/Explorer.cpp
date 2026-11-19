#include "Explorer.h"

#include <QJsonArray>
#include <QNetworkInterface>
#include <QSet>

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

Explorer::Explorer(QObject *parent) : QObject(parent), m_discovery(this)
{
    m_retry.setInterval(5000); // a device the page shows that is not answering is tried again
    connect(&m_retry, &QTimer::timeout, this, &Explorer::retry);
    m_retry.start();
    m_server = new controls::Server(this, this);
    m_server->setName(QStringLiteral("Kadran"));
    m_server->setPageTitle(QStringLiteral("Kadran"));
    connect(&m_discovery, &oscquery::Discovery::deviceFound, this, [this](const oscquery::Device &d) {
        Dev *dev = device(addDevice(d.name, d.address.toString(), d.port));
        dev->online = true;
        if (dev->client && dev->client->state() == oscquery::Client::State::Failed) openDevice(dev->id); // it came back, maybe elsewhere
        emit devicesChanged();
    });
    connect(&m_discovery, &oscquery::Discovery::deviceLost, this, [this](const oscquery::Device &d) {
        for (auto &dev : m_devices)
            if (dev->name == d.name) dev->online = false;
        emit devicesChanged();
    });
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
        if (d->name == name || (d->host == host && d->port == port)) {
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
    if (!d) return;
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
    if (!d || !d->client || d->client->state() != oscquery::Client::State::Ready) return 0;
    std::optional<Control> c = controls::forParameter(*this, deviceId, path, group);
    if (!c) return 0;
    if (!widget.isEmpty()) {
        const auto allowed = widgetsFor(deviceId, path);
        if (std::any_of(allowed.begin(), allowed.end(), [&](const auto &w) { return w.first == widget; })) c->widget = widget;
    }
    c->boundLabel = d->name + QLatin1Char(':') + path;
    const quint64 id = controls::add(m_controls, m_nextControl, *c, makeKey);
    if (!id) return 0;
    d->client->listen(path);
    changed();
    return id;
}

quint64 Explorer::addPad(quint64 deviceId, const QString &pathX, const QString &pathY, quint64 group)
{
    Dev *d = device(deviceId);
    if (!d || !d->client || d->client->state() != oscquery::Client::State::Ready) return 0;
    std::optional<Control> c = controls::forParameters(*this, deviceId, pathX, pathY, group);
    if (!c) return 0;
    c->boundLabel = d->name + QLatin1Char(':') + pathX + QStringLiteral(", ") + pathY;
    const quint64 id = controls::add(m_controls, m_nextControl, *c, makeKey);
    if (!id) return 0;
    d->client->listen(pathX);
    d->client->listen(pathY);
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
    if (info.isNumber()) {
        if (writable)
            out = {{"vfader", "Vertical fader"}, {"fader", "Fader"}, {"knob", "Encoder"}, {"number", "Number"}, {"slim", "Slim fader"}, {"vslim", "Slim vertical fader"}};
        return out + views;
    }
    if (info.type == ParamInfo::Type::Choice) return writable ? QList<QPair<QString, QString>>{{"buttons", "Buttons"}, {"menu", "Menu"}, {"readout", "Readout"}} : QList<QPair<QString, QString>>{{"readout", "Readout"}};
    if (info.type == ParamInfo::Type::Trigger) return writable ? QList<QPair<QString, QString>>{{"hold", "Hold button"}, {"tap", "Tap button"}} : QList<QPair<QString, QString>>{};
    if (writable) return {{"switch", "Switch"}, {"led", "LED"}};
    return {{"led", "LED"}};
}

// The interface

QJsonObject Explorer::toJson() const
{
    QJsonArray devices, list;
    for (const Control &c : m_controls) {
        if (c.boundScope && !std::any_of(devices.begin(), devices.end(), [&](const QJsonValue &v) { return v.toObject().value("id").toString().toULongLong() == c.boundScope; })) {
            if (const Dev *d = device(c.boundScope))
                devices.append(QJsonObject{{"id", QString::number(d->id)}, {"name", d->name}, {"host", d->host}, {"port", d->port}});
        }
        list.append(c.toJson());
    }
    return QJsonObject{{"format", "kadran"}, {"version", 1}, {"devices", devices}, {"controls", list}};
}

void Explorer::clearControls()
{
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
    if (onePage) addPage();
    return n;
}

bool Explorer::fromJson(const QJsonObject &o, QString *error)
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
    // The devices it names: the one known by that name, else one added (offline until it is found)
    QHash<quint64, quint64> scopes; // saved id -> id here
    for (const QJsonValue &v : o.value("devices").toArray()) {
        const QJsonObject d = v.toObject();
        const quint64 saved = d.value("id").toString().toULongLong();
        const QString name = d.value("name").toString();
        const quint16 port = quint16(d.value("port").toInt());
        if (!saved || name.isEmpty()) continue;
        quint64 id = 0;
        for (const auto &known : m_devices)
            if (known->name == name) id = known->id;
        if (!id) id = addDevice(name, d.value("host").toString(), port);
        scopes.insert(saved, id);
    }
    clearControls();
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
    m_controls = std::move(next);
    m_nextControl = std::max(m_nextControl, highest + 1);
    changed();
    for (quint64 id : std::as_const(scopes)) { // their values come when they answer
        openDevice(id);
        listenAll(id);
    }
    return true;
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

std::vector<Control> Explorer::controls() const
{
    std::vector<Control> list = m_controls;
    for (Control &c : list)
        if (c.bound()) controls::fillBound(c, *this);
    return list;
}

std::optional<Control> Explorer::control(quint64 id) const
{
    const Control *c = controls::find(m_controls, id);
    if (!c) return std::nullopt;
    Control out = *c;
    if (out.bound()) controls::fillBound(out, *this);
    return out;
}

bool Explorer::setValue(quint64 id, double value)
{
    const Control *c = controls::find(m_controls, id);
    if (!c || c->kind == Control::Kind::Color || c->kind == Control::Kind::Group || !std::isfinite(value)) return false;
    if (!c->boundParam.isEmpty()) return controls::setBound(*c, *this, value);
    return controls::setValue(m_controls, id, value, {});
}

bool Explorer::setPoint(quint64 id, double x, double y)
{
    const Control *c = controls::find(m_controls, id);
    if (c && !c->boundParam2.isEmpty()) return controls::setBoundPoint(*c, *this, x, y); // a pad over two parameters
    return controls::setPoint(m_controls, id, x, y, {});
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
    if (!d || !d->client) return false;
    const oscquery::RemoteNode *n = d->client->tree().find(path);
    return n && n->toParamInfo(out);
}

QJsonValue Explorer::value(quint64 scope, const QString &path) const
{
    const Dev *d = device(scope);
    const oscquery::RemoteNode *n = d && d->client ? d->client->tree().find(path) : nullptr;
    return n && !n->value.isEmpty() ? n->value[0] : QJsonValue(QJsonValue::Undefined);
}

bool Explorer::setNumber(quint64 scope, const QString &path, double v)
{
    Dev *d = device(scope);
    return d && d->client && d->client->write(path, QJsonArray{v});
}

bool Explorer::setJson(quint64 scope, const QString &path, const QJsonValue &v)
{
    Dev *d = device(scope);
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

QString Explorer::addTargets(const QList<Target> &targets, const QString &widget, bool pad, quint64 group)
{
    if (targets.isEmpty()) return QString();
    const auto numeric = [this](const Target &t) {
        const auto w = widgetsFor(t.device, t.path);
        return !w.isEmpty() && w.first().first == QLatin1String("vfader");
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
    if (n.isLeaf()) {
        if (!ex.widgetsFor(device, n.path).isEmpty()) out << n.path;
        return;
    }
    if (depth > 64) return;
    for (const auto &c : n.children) collect(ex, device, c.second, out, depth + 1);
}

int Explorer::countNode(quint64 deviceId, const QString &path) const
{
    const Dev *d = device(deviceId);
    if (!d || !d->client || d->client->state() != oscquery::Client::State::Ready) return 0;
    const auto *node = d->client->tree().find(path);
    QStringList paths;
    if (node) collect(*this, deviceId, *node, paths, 0);
    return int(paths.size());
}

QString Explorer::addNode(quint64 deviceId, const QString &path, bool asPage, quint64 page)
{
    Dev *d = device(deviceId);
    if (!d || !d->client || d->client->state() != oscquery::Client::State::Ready) return QStringLiteral("This device is not ready");
    const auto *node = d->client->tree().find(path);
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
        if (d->client) {
            switch (d->client->state()) {
            case pxs::oscquery::Client::State::Connecting: state = QStringLiteral("connecting"); break;
            case pxs::oscquery::Client::State::Ready: state = QStringLiteral("ready"); break;
            case pxs::oscquery::Client::State::Failed: state = QStringLiteral("failed"); break;
            default: break;
            }
        }
        out.append(QJsonObject{{"id", double(d->id)}, {"name", d->name.isEmpty() ? QStringLiteral("%1:%2").arg(d->host).arg(d->port) : d->name},
                               {"address", QStringLiteral("%1:%2").arg(d->host).arg(d->port)}, {"online", d->online}, {"state", state}});
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
    if (!n.children.empty() && depth < 64) {
        QJsonArray children;
        for (const auto &c : n.children) children.append(nodeJson(ex, device, c.second, depth + 1));
        o["c"] = children;
    }
    return o;
}

QJsonObject Explorer::treeJson(quint64 deviceId) const
{
    const Dev *d = device(deviceId);
    if (!d || !d->client || d->client->state() != oscquery::Client::State::Ready) return {};
    return nodeJson(*this, deviceId, d->client->tree().root(), 0);
}
