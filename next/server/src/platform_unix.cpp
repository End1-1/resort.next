#include "platform.h"

#include "runner.h"

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

void printHelp()
{
    std::fputs(
        "hotel-api [--console]\n"
        "  Foreground process. On Linux, systemd (Type=simple) is the daemon supervisor.\n"
        "  See next/server/deploy/hotel-api.service. --install / --uninstall are Windows-only.\n"
        "  --console   same as the default on Linux\n"
        "  Config file: hotel-api.ini next to the executable, then /etc/hotel-api/hotel-api.ini.\n"
        "  HOTEL_CONFIG replaces that search when set and non-empty.\n"
        "  HOTEL_LISTEN, HOTEL_WS_LISTEN, and HOTEL_MYSQL_HOST, HOTEL_MYSQL_PORT,\n"
        "  HOTEL_MYSQL_SCHEMA, HOTEL_MYSQL_USER, HOTEL_MYSQL_PASSWORD,\n"
        "  HOTEL_MYSQL_SSL, HOTEL_MYSQL_SSL_CA override ini keys when non-empty.\n",
        stdout);
}

} // namespace

int platformMain(int argc, char **argv)
{
    if (hasFlag(argc, argv, "--help") || hasFlag(argc, argv, "-h")) {
        printHelp();
        return 0;
    }
    if (hasFlag(argc, argv, "--install") || hasFlag(argc, argv, "--uninstall")) {
        std::fputs("hotel-api: service install is implemented for Windows. On Linux use deploy/hotel-api.service.\n",
                   stderr);
        return 2;
    }
    return runApplication(argc, argv);
}
