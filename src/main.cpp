// Kadran: browse the OSCQuery devices of the network and play them on a page of controls (Pixel Stereo). A small application: it serves the page
// (the browser is the interface) and keeps the interface where it was left.
#include "Explorer.h"
#include "KadranServer.h"

#include <pxs/Theme.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

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
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Kadran"));
    app.setOrganizationName(QStringLiteral("Pixel Stereo"));
    app.setOrganizationDomain(QStringLiteral("pixelstereo.fr"));
    pxs::apply(app, pxs::graphite());

    QSettings settings;
    Explorer explorer;
    KadranServer server(&explorer);
    server.setMaxFrame(16 << 20); // an interface opened from the page
    const quint16 wanted = quint16(settings.value("port", kDefaultPort).toUInt());
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
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(open);
    buttons->addWidget(copy);
    buttons->addStretch();
    layout->addLayout(buttons);
    auto *autoOpen = new QCheckBox(QStringLiteral("Open the browser when Kadran starts"));
    autoOpen->setChecked(settings.value("openBrowser", true).toBool());
    layout->addWidget(autoOpen);
    auto *state = new QLabel;
    state->setStyleSheet(QStringLiteral("color:#8a8a94;"));
    layout->addWidget(state);
    QObject::connect(open, &QPushButton::clicked, [&] { QDesktopServices::openUrl(QUrl(local)); });
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
