#pragma once
// Kadran's model: the OSCQuery devices of the network (found, or typed in), a client for each one that is open, and the page of controls
// built from their parameters. It is the page's Backend (pxs/Server.h: the controls, as they are now, and what the page sets) and the
// ParameterSource of the controls that are views of a parameter (scope: the device's id), so a tile shows and sets a remote parameter.
#include <pxs/Client.h>
#include <pxs/ControlList.h>
#include <pxs/Discovery.h>
#include <pxs/Server.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <memory>
#include <vector>

class Explorer : public QObject, public pxs::controls::Backend, public pxs::controls::ParameterSource
{
    Q_OBJECT
public:
    struct Dev {
        quint64 id = 0;
        QString name, host;
        quint16 port = 0;
        bool online = false; // announced on the network, or typed in and reachable
        std::unique_ptr<pxs::oscquery::Client> client;
    };

    explicit Explorer(QObject *parent = nullptr);
    ~Explorer() override;

    const std::vector<std::unique_ptr<Dev>> &devices() const { return m_devices; }
    Dev *device(quint64 id) const;
    quint64 addDevice(const QString &name, const QString &host, quint16 port); // the same one again: the one known
    void openDevice(quint64 id); // connects; deviceReady() says when its tree is there
    bool startSearch() { return m_discovery.start(); }

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
    QString addTargets(const QList<Target> &targets, const QString &widget, bool pad, quint64 group = 0);
    static QList<Target> targetsFromJson(const QJsonArray &list);
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
    bool info(quint64 scope, const QString &path, ParamInfo *out) const override;
    QJsonValue value(quint64 scope, const QString &path) const override;
    bool setNumber(quint64 scope, const QString &path, double v) override;
    bool setJson(quint64 scope, const QString &path, const QJsonValue &v) override;

signals:
    void prefsChanged();
    void devicesChanged();
    void deviceReady(quint64 id);
    void deviceTreeChanged(quint64 id);
    void deviceFailed(quint64 id, const QString &reason);
    void controlsChanged();

private:
    void fillResets(pxs::controls::Control &c) const;
    void retry();
    QTimer m_retry;
    void changed() { ++m_revision; emit controlsChanged(); }
    void listenAll(quint64 deviceId);

    pxs::oscquery::Discovery m_discovery;
    std::vector<std::unique_ptr<Dev>> m_devices;
    std::vector<pxs::controls::Control> m_controls;
    QJsonObject m_prefs;
    quint64 m_nextControl = 1, m_nextDevice = 1, m_revision = 1;
    pxs::controls::Server *m_server = nullptr;
};
