#pragma once
// Kadran's model: the OSCQuery devices of the network (found, or typed in), a client for each one that is open, and the page of controls
// built from their parameters. It is the page's Backend (pxs/Server.h: the controls, as they are now, and what the page sets) and the
// ParameterSource of the controls that are views of a parameter (scope: the device's id), so a tile shows and sets a remote parameter.
#include <pxs/Client.h>
#include <pxs/ControlList.h>
#include <pxs/Discovery.h>
#include <pxs/Filters.h>
#include <pxs/RemoteTree.h>
#include <pxs/Server.h>

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QElapsedTimer>
#include <QTimer>
#include <QUdpSocket>
#include <memory>
#include <vector>

class Explorer : public QObject, public pxs::controls::Backend, public pxs::controls::ParameterSource
{
    Q_OBJECT
public:
    // A plain OSC device has no namespace to read: the addresses it takes are declared by hand (a Point), and its values are sent over UDP
    struct Point {
        QString path;
        QString type = QStringLiteral("f"); // OSC type tag: f (float), i (int), T (on/off), s (text), N (a trigger)
        double min = 0, max = 1;            // f and i only
    };
    struct Dev {
        enum class Kind { Query, Osc };
        quint64 id = 0;
        Kind kind = Kind::Query;
        QString name, host;
        quint16 port = 0;    // OSCQuery: the HTTP port; OSC: the UDP port
        bool online = false; // announced on the network, or typed in and reachable
        std::unique_ptr<pxs::oscquery::Client> client; // OSCQuery
        QList<Point> points;                           // OSC
        pxs::oscquery::RemoteTree local;               // OSC: what the points make, and the values sent
        QHostAddress address;                          // OSC: where to send
    };

    explicit Explorer(QObject *parent = nullptr);
    // Kadran's own OSCQuery service: the one on this port, on this machine, is not listed among the devices
    void setSelfPort(quint16 port) { m_selfPort = port; }
    ~Explorer() override;

    const std::vector<std::unique_ptr<Dev>> &devices() const { return m_devices; }
    Dev *device(quint64 id) const;
    quint64 addDevice(const QString &name, const QString &host, quint16 port); // the same one again: the one known
    void openDevice(quint64 id); // connects; deviceReady() says when its tree is there
    // The network: OSCQuery devices are listed as they appear; the plain OSC ones (_osc._udp) are offered to be added
    bool startSearch();
    void setSelfOscPort(quint16 port) { m_selfOscPort = port; }
    quint64 addOscDevice(const QString &name, const QString &host, quint16 port); // the same host and port again: the one known
    bool renameDevice(quint64 id, const QString &name);
    // false: a control still shows it (or no such device)
    bool removeDevice(quint64 id);
    // The addresses of a plain OSC device: false when it is not one, the address is not valid, or (removePoint) a control uses it
    bool addPoint(quint64 id, const Point &point);
    bool removePoint(quint64 id, const QString &path);
    bool setPointRange(quint64 id, const QString &path, double min, double max); // the address's: the tiles keep their own
    // Offline devices nothing shows any more go (a plain OSC device is kept until it is removed)
    void pruneDevices();
    QJsonArray nearbyJson() const; // [{name, computer, address, port, added}]
    void nearbyFound(const pxs::oscquery::Device &d, bool gone); // a plain OSC service of the network appeared or went (the discovery's, and tests')

    // A control that is a view of this parameter of this device (false: it has no such parameter, or not one a control can show)
    // `widget`: how the page draws it ("": the default one for its kind)
    quint64 addControl(quint64 deviceId, const QString &path, const QString &widget = QString(), quint64 group = 0);
    // A pad over two numeric parameters (X, Y)
    quint64 addPad(quint64 deviceId, const QString &pathX, const QString &pathY, quint64 group = 0);
    // What the front page asks: parameters of a device, to put on a page (`group`: the page). Two numbers of one device make an XY pad (`pad`),
    // unless a widget is asked for. Returns what to tell the operator
    struct Target {
        quint64 device = 0;
        QString path;
    };
    QString addTargets(const QList<Target> &targets, const QString &widget, bool pad, quint64 group = 0, const QString &macro = QString());
    // A macro: one control that sends its value to all of these parameters, each in its own range (the parameter's own, to begin with) and with its own
    // curve. `kind`: "slider", "switch", "button" (sends its high value once when pressed: a scene) or "hold" (its high value when pressed, its low when let go).
    // Returns the control's id (0: nothing to send to)
    quint64 addMacro(const QList<Target> &targets, const QString &kind, quint64 group = 0);
    // More parameters for a macro; how many were added
    int addToMacro(quint64 id, const QList<Target> &targets);
    static QList<Target> targetsFromJson(const QJsonArray &list);
    // A tile that is a view of one address: it talks to another (an address dropped on it), keeping its place, its widget and its filters. Returns what to tell the operator
    QString retarget(quint64 id, const Target &target);
    // A tile that is a view of one address becomes a multi-target interaction (that address is its first target, and gives the feedback). false: it cannot
    bool toMulti(quint64 id, QString *why = nullptr);
    // A node of a device's namespace: all the controls it holds, in a group of the page (`page`: where; 0 the first one) or in a page of
    // their own (`asPage`). `count` is how many controls that makes (the front page asks to confirm a long one)
    int countNode(quint64 deviceId, const QString &path) const;
    QString addNode(quint64 deviceId, const QString &path, bool asPage, quint64 page);

    // For the front page: the devices (id, name, state), and a device's namespace (name, address, type tags, the widgets it can take)
    QJsonArray devicesJson() const;
    QJsonObject treeJson(quint64 deviceId) const;

    // The widgets a parameter can be shown with: {key, name}, the default first (empty: not a control)
    QList<QPair<QString, QString>> widgetsFor(quint64 deviceId, const QString &path) const;
    // The preferences: {"widgets": {"float": "vfader", "int": ..., "bool": ..., "choice": ..., "trigger": ...}, "sizes": {"vfader": [w, h], ...}}.
    // The widget a dragged address becomes when none is chosen, and the size of a new control by widget
    QJsonObject prefs() const { return m_prefs; }
    void setPrefs(const QJsonObject &prefs);
    static QJsonObject prefsOptions(); // what the preferences may hold: the widgets of each type, and the sizes the page gives by default

    // The interface, to keep: the page's controls and the devices they show (by name: a device's address changes from one day to the next)
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &o, QString *error = nullptr); // replaces the page; its devices are opened
    void clearControls();
    // The pages (the top-level groups): one more, one removed with what it holds, one emptied (page 0: what is on no page), all of it emptied.
    // The count is how many controls went
    quint64 addPage(const QString &name = QString());
    int removePage(quint64 page);
    int clearPage(quint64 page);
    int clearAll(bool onePage); // `onePage`: a new, empty interface starts with a page of its own

    // The page: served to the network, with no code; this machine's own view uses the loopback
    bool serve();
    QString pageUrl() const;
    QStringList networkUrls() const;
    pxs::controls::Server &server() { return *m_server; }

    // Backend
    std::vector<pxs::controls::Control> controls() const override;
    std::optional<pxs::controls::Control> control(quint64 id) const override;
    quint64 revision() const override { return m_revision; }
    QString title() const override { return QStringLiteral("Kadran"); }
    std::optional<pxs::controls::FilterTrace> filterTrace(quint64 id) const override { return m_filters.trace(id); }
    bool setValue(quint64 id, double value) override;
    bool setPoint(quint64 id, double x, double y) override;
    bool setPoints(quint64 id, const std::vector<double> &xy) override;
    bool setMulti(quint64 id, const std::vector<double> &values) override;
    bool setAdsr(quint64 id, const std::array<double, 4> &values) override;
    bool setColor(quint64 id, double r, double g, double b) override;
    bool setLayouts(const QHash<quint64, std::array<int, 4>> &places) override;
    bool update(quint64 id, const pxs::controls::Control &changed) override;
    bool remove(quint64 id) override;
    quint64 add(const pxs::controls::Control &control) override;
    bool reset(quint64 id) override; // what a control shows goes back to its default: the device's own reset address
    bool restore(const std::vector<pxs::controls::Control> &controls) override; // undo, redo

    // ParameterSource: scope is a device's id
    bool info(quint64 scope, const QString &path, pxs::controls::ParamInfo *out) const override;
    QJsonValue value(quint64 scope, const QString &path) const override;
    bool setNumber(quint64 scope, const QString &path, double v) override;
    bool setJson(quint64 scope, const QString &path, const QJsonValue &v) override;

signals:
    void nearbyChanged();
    void prefsChanged();
    void devicesChanged();
    void deviceReady(quint64 id);
    void deviceTreeChanged(quint64 id);
    void deviceFailed(quint64 id, const QString &reason);
    void controlsChanged();

private:
    void fillResets(pxs::controls::Control &c) const;
    bool makeTarget(const Target &t, pxs::controls::Target *out) const;
    void retry();
    QTimer m_retry;
    void changed();
    bool parseInterface(const QJsonObject &o, std::vector<pxs::controls::Control> *out, QList<quint64> *devices, QString *error);
    void listenAll(quint64 deviceId);

    // What a device offers, whatever it is: its namespace (null: none yet), whether it can be used now, and the listening that only OSCQuery needs
    const pxs::oscquery::RemoteTree *treeOf(const Dev *d) const;
    bool ready(const Dev *d) const;
    void listenTo(Dev *d, const QString &path);
    bool sendOsc(Dev *d, const QString &path, const QJsonArray &values);
    void rebuild(Dev *d); // the namespace of a plain OSC device, from its points
    bool referenced(quint64 id) const;

    pxs::oscquery::Discovery m_discovery, m_oscDiscovery;
    QMap<QString, pxs::oscquery::Device> m_nearby; // plain OSC services of the network, by instance
    std::vector<std::unique_ptr<Dev>> m_devices, m_pruned; // the ones that went stay a while: an undo can bring a control back
    QUdpSocket m_udp;
    quint16 m_selfOscPort = 0;
    bool m_holdPrune = false;
    // The filters of the tiles (a slider, a switch): what is set goes through them before it is sent; the ones that move by themselves go on at each frame
    bool deliverValue(quint64 id, double value);
    void filterFrame();
    pxs::controls::FilterRunner m_filters;
    QTimer m_filterTimer;
    QElapsedTimer m_filterClock;
    std::vector<pxs::controls::Control> m_controls;
    QJsonObject m_prefs;
    quint16 m_selfPort = 0;
    quint64 m_nextControl = 1, m_nextDevice = 1, m_revision = 1;
    pxs::controls::Server *m_server = nullptr;
};
