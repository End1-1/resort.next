#pragma once

#include "dberror.h"

#include <QString>
#include <QStringList>

// Client libraries named in the plugin's ELF DT_NEEDED or PE import table.
// Only names that contain "mysql" or "mariadb". Empty when the file is not a
// readable image or does not import such a library.
QStringList mysqlClientLibrariesInPlugin(const QString &pluginPath);

// Path of the QMYSQL plugin mapped into this process. Empty if it is not loaded.
QString loadedQmysqlPluginPath();

// Cached after the plugin is identified. Call once QMYSQL is loaded.
// LibMySql when the plugin is libmysqlclient, or when it cannot be identified
// (the connect string then still carries MYSQL_OPT_SSL_MODE).
MysqlClientKind detectedMysqlClientKind();
