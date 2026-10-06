// Built only on Windows (see CMakeLists.txt). Linux CI does not compile this file.
// Default start: register with the Service Control Manager as HotelApi.
// A console launch (double-click, debugger, `--console`) falls back to the foreground.

#include "platform.h"

#include "runner.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

wchar_t g_serviceName[] = L"HotelApi";
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
DWORD g_checkpoint = 1;
int g_argc = 0;
char **g_argv = nullptr;

void reportStatus(DWORD state, DWORD win32Exit = NO_ERROR)
{
    SERVICE_STATUS status {};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwWin32ExitCode = win32Exit;
    status.dwServiceSpecificExitCode = (win32Exit == ERROR_SERVICE_SPECIFIC_ERROR) ? 1 : 0;
    status.dwControlsAccepted = (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING)
                                    ? 0
                                    : (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
    status.dwCheckPoint = (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : g_checkpoint++;
    status.dwWaitHint = (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING) ? 10000 : 0;
    if (g_statusHandle)
        SetServiceStatus(g_statusHandle, &status);
}

void WINAPI controlHandler(DWORD code)
{
    switch (code) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        reportStatus(SERVICE_STOP_PENDING);
        requestApplicationQuit();
        return;
    case SERVICE_CONTROL_INTERROGATE:
        return;
    default:
        return;
    }
}

void WINAPI serviceMain(DWORD, LPWSTR *)
{
    g_statusHandle = RegisterServiceCtrlHandlerW(g_serviceName, controlHandler);
    if (!g_statusHandle)
        return;
    reportStatus(SERVICE_START_PENDING);
    const int code = runApplication(g_argc, g_argv, []() { reportStatus(SERVICE_RUNNING); });
    reportStatus(SERVICE_STOPPED, code == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR);
}

std::wstring quotedExecutablePath()
{
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return {};
    return L"\"" + std::wstring(buffer, length) + L"\"";
}

int installService()
{
    const std::wstring path = quotedExecutablePath();
    if (path.empty()) {
        std::fputs("hotel-api: cannot read the executable path\n", stderr);
        return 1;
    }

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!manager) {
        std::fprintf(stderr, "hotel-api: OpenSCManager failed (%lu). Run from an elevated prompt.\n", GetLastError());
        return 1;
    }

    SC_HANDLE service = CreateServiceW(manager,
                                       g_serviceName,
                                       L"Hotel API",
                                       SERVICE_ALL_ACCESS,
                                       SERVICE_WIN32_OWN_PROCESS,
                                       SERVICE_AUTO_START,
                                       SERVICE_ERROR_NORMAL,
                                       path.c_str(),
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr);
    if (!service) {
        const DWORD err = GetLastError();
        CloseServiceHandle(manager);
        if (err == ERROR_SERVICE_EXISTS)
            std::fputs("hotel-api: service HotelApi already exists\n", stderr);
        else
            std::fprintf(stderr, "hotel-api: CreateService failed (%lu)\n", err);
        return 1;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    std::fputs("hotel-api: installed service HotelApi (auto start).\n"
               "Place hotel-api.ini next to the executable. The service working directory is\n"
               "System32, so the ini is read from the executable directory, not the current directory.\n"
               "A non-empty HOTEL_CONFIG replaces that path. A non-empty HOTEL_LISTEN,\n"
               "HOTEL_WS_LISTEN, or HOTEL_MYSQL_HOST, HOTEL_MYSQL_PORT, HOTEL_MYSQL_SCHEMA,\n"
               "HOTEL_MYSQL_USER, HOTEL_MYSQL_PASSWORD overrides the matching ini key.\n",
               stdout);
    return 0;
}

int uninstallService()
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        std::fprintf(stderr, "hotel-api: OpenSCManager failed (%lu)\n", GetLastError());
        return 1;
    }

    SC_HANDLE service = OpenServiceW(manager, g_serviceName, DELETE);
    if (!service) {
        const DWORD err = GetLastError();
        CloseServiceHandle(manager);
        std::fprintf(stderr, "hotel-api: OpenService failed (%lu)\n", err);
        return 1;
    }

    const BOOL deleted = DeleteService(service);
    const DWORD err = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    if (!deleted) {
        std::fprintf(stderr, "hotel-api: DeleteService failed (%lu). Stop HotelApi first if it is running.\n", err);
        return 1;
    }
    std::fputs("hotel-api: removed service HotelApi\n", stdout);
    return 0;
}

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
        "hotel-api [--console | --install | --uninstall]\n"
        "  Started by the Service Control Manager: runs as Windows service HotelApi.\n"
        "  Otherwise runs in the foreground (console).\n"
        "  --console     force the foreground, do not contact the SCM\n"
        "  --install     register HotelApi (elevated prompt)\n"
        "  --uninstall   remove HotelApi (elevated prompt; stop it first)\n"
        "  Config file: hotel-api.ini next to the executable (not the working directory).\n"
        "  HOTEL_CONFIG replaces that path when set and non-empty.\n"
        "  HOTEL_LISTEN, HOTEL_WS_LISTEN, and HOTEL_MYSQL_HOST, HOTEL_MYSQL_PORT,\n"
        "  HOTEL_MYSQL_SCHEMA, HOTEL_MYSQL_USER, HOTEL_MYSQL_PASSWORD override ini\n"
        "  keys when non-empty.\n"
        "  The service process sees the system environment, not a user shell profile.\n",
        stdout);
}

} // namespace

int platformMain(int argc, char **argv)
{
    g_argc = argc;
    g_argv = argv;

    if (hasFlag(argc, argv, "--help") || hasFlag(argc, argv, "-h")) {
        printHelp();
        return 0;
    }
    if (hasFlag(argc, argv, "--install"))
        return installService();
    if (hasFlag(argc, argv, "--uninstall"))
        return uninstallService();
    if (hasFlag(argc, argv, "--console"))
        return runApplication(argc, argv);

    SERVICE_TABLE_ENTRYW table[] = {
        {g_serviceName, serviceMain},
        {nullptr, nullptr},
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
            return runApplication(argc, argv);
        std::fprintf(stderr, "hotel-api: StartServiceCtrlDispatcher failed (%lu)\n", GetLastError());
        return 1;
    }
    return 0;
}
