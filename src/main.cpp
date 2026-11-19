// Kadran: browse the OSCQuery devices of the network and play them on a page of controls (Pixel Stereo). A small application: it serves the page
// (the browser is the interface) and keeps the interface where it was left.
#include "Explorer.h"
#include "KadranServer.h"

#include <pxs/Theme.h>

#include <QApplication>
#include "About.h"
#include <QCheckBox>
#include <QMessageBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QEventLoop>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <cstdio>
#include <cstring>

namespace {
constexpr quint16 kDefaultPort = 47800; // a stable address: a bookmark, a tablet

QString lastInterface() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/last.kadran"); }

void saveInterface(const Explorer &ex)
{
    QDir().mkpath(QFileInfo(lastInterface()).absolutePath());
    QSaveFile f(lastInterface());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(ex.toJson()).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void loadInterface(Explorer &ex)
{
    QFile f(lastInterface());
    if (!f.open(QIODevice::ReadOnly)) return;
    ex.fromJson(QJsonDocument::fromJson(f.read(32 << 20)).object());
}
// Another Kadran already on the wanted port: opening Kadran again only shows its page
bool kadranListening(quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(300)) return false;
    socket.write("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    QByteArray reply;
    while (socket.waitForReadyRead(800)) reply += socket.readAll();
    return reply.contains("<title>Kadran</title>");
}

// --check: the server starts and serves its two pages. For the package to be tried before it is published
int selfTest()
{
    Explorer explorer;
    KadranServer server(&explorer);
    if (!server.start(0, QString(), QStringLiteral("127.0.0.1"))) return 2;
    QNetworkAccessManager net;
    int answered = 0, ok = 0;
    QEventLoop loop;
    for (const char *path : {"/", "/control"}) {
        QNetworkReply *reply = net.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server.port()).arg(QLatin1String(path)))));
        QObject::connect(reply, &QNetworkReply::finished, [&, reply, path] {
            const QByteArray body = reply->readAll();
            if (reply->error() == QNetworkReply::NoError && body.contains(path[1] ? "Controls" : "Kadran")) ++ok;
            reply->deleteLater();
            if (++answered == 2) loop.quit();
        });
    }
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    std::puts(ok == 2 ? "Kadran: ok" : "Kadran: the server did not serve its pages");
    return ok == 2 ? 0 : 1;
}
} // namespace

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--check") == 0) { // what CI runs on the package: no window, no display needed
            QCoreApplication core(argc, argv);
            return selfTest();
        }
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Kadran"));
    app.setApplicationVersion(QStringLiteral(KADRAN_VERSION));
    app.setOrganizationName(QStringLiteral("Pixel Stereo"));
    app.setOrganizationDomain(QStringLiteral("pixelstereo.fr"));
    pxs::apply(app, pxs::graphite());

    QSettings settings;
    Explorer explorer;
    KadranServer server(&explorer);
    server.setMaxFrame(16 << 20); // an interface opened from the page
    const quint16 wanted = quint16(settings.value("port", kDefaultPort).toUInt());
    if (kadranListening(wanted)) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(wanted)));
        return 0;
    }
    if (!server.start(wanted, QString()) && !server.start(0, QString())) {
        qWarning("Kadran cannot serve its page: %s", qPrintable(server.status()));
        return 1;
    }
    const QString local = QStringLiteral("http://127.0.0.1:%1/").arg(server.port());
    explorer.startSearch();
    loadInterface(explorer);

    // What is on the page is kept: a moment after the last change, and when the application ends
    QTimer autosave;
    autosave.setSingleShot(true);
    autosave.setInterval(800);
    QObject::connect(&autosave, &QTimer::timeout, [&] { saveInterface(explorer); });
    QObject::connect(&explorer, &Explorer::controlsChanged, &autosave, qOverload<>(&QTimer::start));
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] { saveInterface(explorer); });

    // The window: where to open it
    QWidget window;
    window.setWindowTitle(QStringLiteral("Kadran"));
    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("Kadran"));
    title->setStyleSheet(QStringLiteral("font-size:26px; font-weight:600;"));
    layout->addWidget(title);
    layout->addWidget(new QLabel(QStringLiteral("The OSCQuery devices of the network, on a page of controls.\nIt opens in your browser, here and on any device of the network:")));
    QStringList urls = server.urls();
    if (urls.isEmpty()) urls << local;
    auto *address = new QLabel(urls.join(QLatin1Char('\n')));
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    address->setStyleSheet(QStringLiteral("font-family:Menlo,Consolas,monospace; padding:8px 0;"));
    layout->addWidget(address);
    auto *open = new QPushButton(QStringLiteral("Open in the browser"));
    auto *copy = new QPushButton(QStringLiteral("Copy the address"));
    auto *about = new QPushButton(QStringLiteral("About"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(open);
    buttons->addWidget(copy);
    buttons->addWidget(about);
    buttons->addStretch();
    layout->addLayout(buttons);
    auto *autoOpen = new QCheckBox(QStringLiteral("Open the browser when Kadran starts"));
    autoOpen->setChecked(settings.value("openBrowser", true).toBool());
    layout->addWidget(autoOpen);
    auto *state = new QLabel;
    state->setStyleSheet(QStringLiteral("color:#8a8a94;"));
    layout->addWidget(state);
    QObject::connect(open, &QPushButton::clicked, [&] { QDesktopServices::openUrl(QUrl(local)); });
    QObject::connect(about, &QPushButton::clicked, [&] {
        QMessageBox box(&window);
        box.setWindowTitle(QStringLiteral("About Kadran"));
        box.setTextFormat(Qt::RichText);
        box.setText(aboutHtml(QCoreApplication::applicationVersion()));
        box.setStandardButtons(QMessageBox::Close);
        box.exec();
    });
    QObject::connect(copy, &QPushButton::clicked, [&] { QApplication::clipboard()->setText(urls.first()); });
    QObject::connect(autoOpen, &QCheckBox::toggled, [&](bool on) { settings.setValue("openBrowser", on); });
    QTimer refresh;
    const auto update = [&] {
        state->setText(QStringLiteral("%1 device(s) found, %2 control(s), %3 browser(s) connected")
                           .arg(explorer.devices().size()).arg(explorer.controls().size()).arg(server.clientCount()));
    };
    QObject::connect(&refresh, &QTimer::timeout, update);
    refresh.start(1000);
    update();
    window.setMinimumWidth(460);
    window.show();

    settings.setValue("port", server.port() == wanted ? wanted : kDefaultPort);
    if (autoOpen->isChecked()) QTimer::singleShot(600, [&] { QDesktopServices::openUrl(QUrl(local)); });
    return app.exec();
}
