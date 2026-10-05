#include "apiclient.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstring>

namespace {

bool hasFlag(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0)
            return true;
    }
    return false;
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

    const QString base = qEnvironmentVariable("HOTEL_API_BASE", QStringLiteral("http://127.0.0.1:8080"));
    ApiClient client(base);
    const ApiClient::CallResult health = client.getHealth();

    if (!health.body.isEmpty()) {
        std::fwrite(health.body.constData(), 1, static_cast<size_t>(health.body.size()), stdout);
        if (!health.body.endsWith('\n'))
            std::fputc('\n', stdout);
        std::fflush(stdout);
    }
    if (!health.transportOk) {
        std::fprintf(stderr, "hotel-desktop-stub: %s\n", qPrintable(health.error));
        return 1;
    }
    if (health.httpStatus != 200) {
        std::fprintf(stderr, "hotel-desktop-stub: HTTP %d\n", health.httpStatus);
        return 1;
    }
    if (!loginSet)
        return 0;

    const ApiClient::CallResult session = client.postSession(login, password);
    if (!session.transportOk) {
        std::fprintf(stderr, "hotel-desktop-stub: login %s\n", qPrintable(session.error));
        return 1;
    }
    const QJsonObject object = QJsonDocument::fromJson(session.body).object();
    if (session.httpStatus != 200) {
        const QByteArray error = object.value(QStringLiteral("error")).toString().toUtf8();
        std::fprintf(stderr, "login http=%d error=%s\n", session.httpStatus, error.constData());
        return 1;
    }
    const int userId = object.value(QStringLiteral("user")).toObject().value(QStringLiteral("id")).toInt();
    const bool allowed = object.value(QStringLiteral("commands_allowed")).toBool();
    std::fprintf(stdout, "login http=200 user=%d commands_allowed=%s\n", userId, allowed ? "true" : "false");
    return 0;
}
