#pragma once

#include <QString>

// Per-user INI. The password and the bearer token are never stored.
struct DesktopConfig {
    QString baseUrl = QStringLiteral("http://127.0.0.1:8080");
    QString webSocketUrl;
    QString lastLogin;
    // hy, en, or ru. Empty means: system locale if it is one of those, otherwise ru.
    QString language;
};

struct ConfigLoad {
    enum class Source {
        BuiltIn,
        BundledDefaults,
        UserFile
    };

    DesktopConfig config;
    Source source = Source::BuiltIn;
    QString warning;
};

class AppConfig {
public:
    static void applyIdentity();

    // Windows: %APPDATA%\Resort\hotel-desktop\hotel-desktop.ini
    // Linux:   ~/.config/Resort/hotel-desktop/hotel-desktop.ini
    // (or $XDG_CONFIG_HOME/...). Not the exe directory and not the registry.
    static QString userFilePath();

    // Optional read-only defaults beside the executable. Used only when the
    // user file does not exist yet. The program never writes this path.
    static QString bundledDefaultsPath();

    static ConfigLoad load();
    static ConfigLoad loadFrom(const QString &userPath, const QString &bundledPath);

    static bool save(const DesktopConfig &config, QString *error);
    static bool saveTo(const QString &userPath, const DesktopConfig &config, QString *error);
};
