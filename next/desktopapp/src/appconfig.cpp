#include "appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

bool isPasswordKey(const QString &key)
{
    return key.compare(QLatin1String("password"), Qt::CaseInsensitive) == 0
        || key.endsWith(QLatin1String("/password"), Qt::CaseInsensitive);
}

void stripSecrets(QSettings *settings)
{
    const QStringList keys = settings->allKeys();
    for (const QString &key : keys) {
        if (isPasswordKey(key))
            settings->remove(key);
    }
}

void readInto(DesktopConfig *config, const QString &path, QString *warning)
{
    QSettings settings(path, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    if (settings.status() != QSettings::NoError) {
        if (warning) {
            *warning = QStringLiteral("Не удалось прочитать файл настроек: %1")
                           .arg(QDir::toNativeSeparators(path));
        }
        return;
    }
    if (settings.contains(QStringLiteral("base_url")))
        config->baseUrl = settings.value(QStringLiteral("base_url")).toString().trimmed();
    if (settings.contains(QStringLiteral("websocket_url")))
        config->webSocketUrl = settings.value(QStringLiteral("websocket_url")).toString().trimmed();
    if (settings.contains(QStringLiteral("last_login")))
        config->lastLogin = settings.value(QStringLiteral("last_login")).toString().trimmed();
}

} // namespace

void AppConfig::applyIdentity()
{
    QCoreApplication::setOrganizationName(QStringLiteral("Resort"));
    QCoreApplication::setApplicationName(QStringLiteral("hotel-desktop"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
}

QString AppConfig::userFilePath()
{
    applyIdentity();
#if defined(Q_OS_WIN)
    // Roaming AppData (%APPDATA%), not Local and not HKLM/HKCU.
    const QStandardPaths::StandardLocation location = QStandardPaths::AppDataLocation;
#else
    // ~/.config/<Org>/<App> (XDG). AppDataLocation would be ~/.local/share.
    const QStandardPaths::StandardLocation location = QStandardPaths::AppConfigLocation;
#endif
    const QString directory = QStandardPaths::writableLocation(location);
    return QDir(directory).filePath(QStringLiteral("hotel-desktop.ini"));
}

QString AppConfig::bundledDefaultsPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("hotel-desktop.ini"));
}

ConfigLoad AppConfig::load()
{
    return loadFrom(userFilePath(), bundledDefaultsPath());
}

ConfigLoad AppConfig::loadFrom(const QString &userPath, const QString &bundledPath)
{
    ConfigLoad loaded;
    if (!userPath.isEmpty() && QFileInfo::exists(userPath)) {
        loaded.source = ConfigLoad::Source::UserFile;
        readInto(&loaded.config, userPath, &loaded.warning);
        return loaded;
    }
    if (!bundledPath.isEmpty() && QFileInfo::exists(bundledPath)) {
        loaded.source = ConfigLoad::Source::BundledDefaults;
        readInto(&loaded.config, bundledPath, &loaded.warning);
    }
    return loaded;
}

bool AppConfig::save(const DesktopConfig &config, QString *error)
{
    return saveTo(userFilePath(), config, error);
}

bool AppConfig::saveTo(const QString &userPath, const DesktopConfig &config, QString *error)
{
    const QFileInfo info(userPath);
    const QString directory = info.absolutePath();
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        if (error) {
            *error = QStringLiteral("Не удалось создать каталог настроек: %1")
                         .arg(QDir::toNativeSeparators(directory));
        }
        return false;
    }

    QSettings settings(info.absoluteFilePath(), QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    settings.setValue(QStringLiteral("base_url"), config.baseUrl);
    settings.setValue(QStringLiteral("websocket_url"), config.webSocketUrl);
    settings.setValue(QStringLiteral("last_login"), config.lastLogin);
    stripSecrets(&settings);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("Не удалось записать файл настроек: %1")
                         .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
        }
        return false;
    }
    return true;
}
