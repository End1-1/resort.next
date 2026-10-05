#include "apiclient.h"

#include <QCoreApplication>

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
            "  This program does not open MariaDB. The reception UI will call the same API.\n",
            stdout);
        return 0;
    }

    const QString base = qEnvironmentVariable("HOTEL_API_BASE", QStringLiteral("http://127.0.0.1:8080"));
    ApiClient client(base);
    const ApiClient::HealthResult health = client.getHealth();

    if (!health.body.isEmpty()) {
        std::fwrite(health.body.constData(), 1, static_cast<size_t>(health.body.size()), stdout);
        if (!health.body.endsWith('\n'))
            std::fputc('\n', stdout);
    }
    if (!health.transportOk) {
        std::fprintf(stderr, "hotel-desktop-stub: %s\n", qPrintable(health.error));
        return 1;
    }
    if (health.httpStatus != 200) {
        std::fprintf(stderr, "hotel-desktop-stub: HTTP %d\n", health.httpStatus);
        return 1;
    }
    return 0;
}
