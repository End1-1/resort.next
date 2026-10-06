#include "apiclient.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <functional>

namespace {

bool hasFlag(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0)
            return true;
    }
    return false;
}

template <typename Result, typename Signal>
Result waitFor(ApiClient *client, Signal signal, int timeoutMs, const std::function<void()> &start)
{
    Result result;
    bool done = false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client, signal, &loop, [&](const Result &value) {
        if (!value.current)
            return;
        result = value;
        done = true;
        loop.quit();
    });
    timer.start(timeoutMs + 2000);
    start();
    if (!done)
        loop.exec();
    if (!done) {
        result.error.transportFailure = true;
        result.error.userMessage = QStringLiteral("Превышено время ожидания ответа сервера.");
        result.error.technical = QStringLiteral("timed out waiting for a response");
    }
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hotel-desktop-stub"));

    if (hasFlag(argc, argv, "--help") || hasFlag(argc, argv, "-h")) {
        std::fputs(
            "hotel-desktop-stub\n"
            "  GET $HOTEL_API_BASE/health (default http://127.0.0.1:8080) and print the JSON.\n"
            "  If both HOTEL_LOGIN and HOTEL_PASSWORD are set, also POST /api/v1/sessions once.\n"
            "  The token and the password are not printed.\n"
            "  This program does not open MariaDB.\n",
            stdout);
        return 0;
    }

    const bool loginSet = qEnvironmentVariableIsSet("HOTEL_LOGIN");
    const bool passwordSet = qEnvironmentVariableIsSet("HOTEL_PASSWORD");
    if (loginSet != passwordSet) {
        std::fputs("hotel-desktop-stub: set both HOTEL_LOGIN and HOTEL_PASSWORD, or neither\n", stderr);
        return 2;
    }
    const QString login = qEnvironmentVariable("HOTEL_LOGIN");
    const QString password = qEnvironmentVariable("HOTEL_PASSWORD");
    if (loginSet && (login.isEmpty() || password.isEmpty())) {
        std::fputs("hotel-desktop-stub: HOTEL_LOGIN and HOTEL_PASSWORD must be non-empty\n", stderr);
        return 2;
    }

    ApiClient client;
    client.setBaseUrl(qEnvironmentVariable("HOTEL_API_BASE", QStringLiteral("http://127.0.0.1:8080")));

    const HealthStatus health = waitFor<HealthStatus>(
        &client, &ApiClient::healthFinished, 5000, [&client]() { client.requestHealth(); });

    if (!health.rawBody.isEmpty()) {
        std::fwrite(health.rawBody.constData(), 1, static_cast<size_t>(health.rawBody.size()), stdout);
        if (!health.rawBody.endsWith('\n'))
            std::fputc('\n', stdout);
        std::fflush(stdout);
    }
    if (!health.reachable) {
        const QByteArray message = (health.error.technical.isEmpty() ? health.error.userMessage : health.error.technical).toUtf8();
        std::fprintf(stderr, "hotel-desktop-stub: %s\n", message.constData());
        return 1;
    }
    if (health.httpStatus != 200) {
        std::fprintf(stderr, "hotel-desktop-stub: HTTP %d\n", health.httpStatus);
        return 1;
    }
    if (!loginSet)
        return 0;

    const SessionResult session = waitFor<SessionResult>(
        &client,
        &ApiClient::loginFinished,
        5000,
        [&client, &login, &password]() { client.requestLogin(login, password); });

    if (!session.ok) {
        if (session.error.transportFailure) {
            const QByteArray message = (session.error.technical.isEmpty() ? session.error.userMessage : session.error.technical).toUtf8();
            std::fprintf(stderr, "hotel-desktop-stub: login %s\n", message.constData());
            return 1;
        }
        const QByteArray code = session.error.code.toUtf8();
        std::fprintf(stderr, "login http=%d error=%s\n", session.error.httpStatus, code.constData());
        return 1;
    }

    const QByteArray allowed = session.commandsAllowed ? QByteArrayLiteral("true") : QByteArrayLiteral("false");
    std::fprintf(stdout,
                 "login http=200 user=%lld commands_allowed=%s\n",
                 static_cast<long long>(session.user.id),
                 allowed.constData());
    return 0;
}
