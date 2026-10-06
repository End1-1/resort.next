#include "runner.h"

#include "config.h"
#include "httpserver.h"
#include "realtimehub.h"
#include "version.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>

#ifdef Q_OS_UNIX
#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef Q_OS_UNIX
int g_signalFds[2] = {-1, -1};

void handleStopSignal(int)
{
    const char byte = 1;
    if (g_signalFds[0] >= 0) {
        const ssize_t written = ::write(g_signalFds[0], &byte, 1);
        (void)written;
    }
}

void installStopSignals(QCoreApplication *app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFds) != 0)
        return;

    auto *notifier = new QSocketNotifier(g_signalFds[1], QSocketNotifier::Read, app);
    QObject::connect(notifier,
                     &QSocketNotifier::activated,
                     app,
                     [app](QSocketDescriptor, QSocketNotifier::Type) {
                         char byte = 0;
                         if (::read(g_signalFds[1], &byte, 1) < 0)
                             return;
                         app->quit();
                     });

    struct sigaction action {};
    action.sa_handler = handleStopSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}
#endif

} // namespace

void requestApplicationQuit()
{
    if (QCoreApplication *app = QCoreApplication::instance())
        QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection);
}

int runApplication(int argc, char **argv, const std::function<void()> &onReady)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hotel-api"));
    QCoreApplication::setApplicationVersion(QStringLiteral(HOTEL_API_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("SmartHotel"));

    const ConfigLoadResult loaded = loadConfig();
    if (!loaded.configPath.isEmpty())
        qInfo().noquote() << "hotel-api config" << loaded.configPath;
    else if (loaded.ok)
        qInfo().noquote() << "hotel-api config none";
    const QString databaseNotice = databaseConfigNoticeLine(loaded.databaseNotice);
    if (!databaseNotice.isEmpty())
        qWarning().noquote() << databaseNotice;
    if (!loaded.ok) {
        qCritical().noquote() << loaded.error;
        return 1;
    }

    HttpApi api(loaded.config);
    QString error;
    if (!api.listen(&error)) {
        qCritical().noquote() << error;
        return 1;
    }

    RealtimeHub hub;
    if (loaded.config.websocketEnabled) {
        if (!hub.listen(loaded.config.websocket, &error)) {
            qCritical().noquote() << error;
            return 1;
        }
    }

#ifdef Q_OS_UNIX
    installStopSignals(&app);
#endif

    if (onReady)
        onReady();
    return app.exec();
}
