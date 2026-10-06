#pragma once

#include <QString>

// Machine code for HTTP JSON, mapped only from the MySQL native error number.
// 1045 -> access_denied, 1049 -> unknown_database, 2002 and 2003 -> cannot_connect.
// Any other number, including empty, is connection_failed.
// Never returns driver text, a host, a user, or a password.
QString publicDatabaseErrorCode(const QString &nativeErrorCode);

// Codes that may appear in GET /health db.error. Anything else becomes
// connection_failed so a raw driver string cannot reach the response.
QString probeDatabaseError(const QString &failure);

// One line, no password. driverText and databaseText must already be scrubbed.
QString formatConnectFailureLog(const QString &publicCode,
                                const QString &nativeErrorCode,
                                const QString &host,
                                int port,
                                const QString &database,
                                const QString &user,
                                const QString &driverText,
                                const QString &databaseText);

// Flattens CR/LF. Replaces the password and its percent-encoded form with ***.
// An empty password is left untouched (searching for it would match everywhere).
QString scrubDatabaseMessage(const QString &text, const QString &password);

// Semicolon-separated QMYSQL connect options. Timeouts are seconds.
// MariaDB Connector/C 10.x and 11.x honor these on Windows before real_connect.
QString mysqlConnectOptions(int connectTimeoutSec);
