#pragma once
// Kadran as an OSCQuery device, like every other Pixel Stereo application: OSC in (UDP), OSCQuery over HTTP, announced on the network.
// What it says about itself:
//   /kadran/page                  the page shown (1, 2, 3…), read and written: the same page for every browser
//   /kadran/pages/next, prev      the page after, the page before
//   /kadran/pages/<name>          show that page (one per page, they come and go with the pages)
//   /kadran/view                  the view shown (its name), read and written
//   /kadran/views/next, prev      the view after, the view before
//   /kadran/views/<name>          show that view (one per view)
//   /kadran/controls/<page>/<key> the value of a widget: a slider (f), a switch (T), a choice (s, its names), a button (an action), an XY pad (ff).
//                                 Written, it goes to the device the widget shows; read, it is what the widget shows
// The multi-sliders, the ADSRs, the colors and the balls are not exposed yet.
#include "Explorer.h"

#include <pxs/OscService.h>
#include <pxs/core/tree/tree.h>

#include <QHash>
#include <QSet>
#include <QTimer>

class KadranServer;

class OscDevice : public QObject
{
    Q_OBJECT
public:
    // `name` is what the network sees
    OscDevice(Explorer *explorer, KadranServer *server, const QString &name, QObject *parent = nullptr);
    // Ports 0: any free one
    bool start(quint16 oscPort, quint16 httpPort);
    quint16 oscPort() const { return m_service.oscPort(); }
    quint16 httpPort() const { return m_service.httpPort(); }
    pxs::core::tree::Tree &tree() { return m_tree; }

private:
    struct Page {
        quint64 id = 0;
        QString name;
    };
    std::vector<Page> pages() const;
    int pageIndex() const;
    void showPage(int index); // 1-based, wraps
    void rebuild();           // the nodes that come and go: pages, views, widgets (only when what they say changed)
    void pump();              // the values that changed, told to the listeners
    static QString slug(const QString &name);
    static QString unique(const QString &base, QSet<QString> &taken);
    QString controlsSignature(const std::vector<pxs::controls::Control> &list) const;

    Explorer *m_ex;
    KadranServer *m_server;
    pxs::core::tree::Tree m_tree;
    pxs::OscService m_service;
    QTimer m_pump, m_rebuild;
    QString m_pagesSeen, m_viewsSeen, m_controlsSeen;
    QHash<QString, pxs::core::ValueList> m_told; // the last value told, by path
    QHash<QString, quint64> m_paths;     // widget path -> control id
};
