#pragma once
// Kadran's server: the controls page and its protocol are pxs's (pxs::controls::Server, the Explorer is its Backend); this adds the front page at / (ShellPage.h)
// and its messages: the devices and their namespaces to the page, and from it: open a device, add a device by hand, put addresses on the page (or
// drop them there), save and open an interface.
//   client -> {"t":"hello"}                                   this client is the front page: devices and trees are pushed to it, controls are not
//   server -> {"t":"devices","list":[{"id","name","address","online","state"}]}
//   server -> {"t":"tree","device":id,"root":{"c":[{"n","p","t","d","w":[widgets],"c":[..]}]}}
//   client -> {"t":"open","device":id}   {"t":"addDevice","host":..,"port":..}
//   client -> {"t":"add","targets":[{"device","path"}],"widget":"","pad":true,"page":"<id>"}
//   client -> {"t":"drop","data":"<json of targets>","page":"<id>","x":..,"y":..}            (from the controls page)
//   client -> {"t":"save"}  server -> {"t":"file","name":..,"doc":{..}}      client -> {"t":"load","doc":{..}}
//   server -> {"t":"say","text":..}
//   server -> {"t":"confirm","n":..,"device":..,"path":..,"as":"group"|"page","page":".."}   a node of more than 20 controls: ask, then send addNode with "confirmed":true
//   client -> {"t":"addNode","device":..,"path":..,"as":"group"|"page","page":"..","confirmed":bool} / {"t":"undo"} / {"t":"redo"}
#include "Explorer.h"

#include <QSet>

class KadranServer : public pxs::controls::Server
{
    Q_OBJECT
public:
    explicit KadranServer(Explorer *explorer, QObject *parent = nullptr);

    QStringList urls() const override; // where to open Kadran: its front page, one address per network interface
    static QByteArray front() ;       // the front page as served

protected:
    bool servePage(const QString &path, QByteArray *html, QByteArray *csp) override;
    bool message(QTcpSocket *client, const QString &type, const QJsonObject &o) override;
    void tick() override;
    void clientGone(QTcpSocket *client) override;

private:
    QByteArray prefsMessage() const;
    void applyPrefs(); // the sizes, the inspector and the physics widget of the preferences, given to the page
    void sendAll(const QByteArray &json);
    void sendDevices(QTcpSocket *client = nullptr);
    void sendTree(quint64 device, QTcpSocket *client = nullptr);
    void say(QTcpSocket *client, const QString &text);
    void node(double device, const QString &path, bool asPage, quint64 page, bool confirmed);
    static constexpr int kConfirmAbove = 20; // more controls than this from one node: the operator confirms

    Explorer *m_ex;
    QSet<QTcpSocket *> m_shells; // the front pages
    QByteArray m_csp;
    QByteArray m_devices;        // the last list sent
    QHash<quint64, QByteArray> m_trees; // the last namespace sent, by device
    bool m_dirty = true;
};
