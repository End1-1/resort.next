#pragma once

#include <QString>

// Machine code for HTTP JSON, mapped only from the MySQL native error number.
// 1045 -> access_denied, 1049 -> unknown_database, 2002 and 2003 -> cannot_connect,
// 2026 -> tls_error.
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

// Which client library the loaded QMYSQL plugin was built against.
// LibMySql receives MYSQL_OPT_SSL_MODE. MariaDb does not: Qt 6.10 leaves that
// option out of a Connector/C build and then logs
// "Illegal connect option value 'MYSQL_OPT_SSL_MODE=...'".
enum class MysqlClientKind {
    LibMySql,
    MariaDb,
};

// mysql_get_client_info() text. Connector/C 3.x is "3.4.5" (no "MariaDB" word).
// A string that contains "MariaDB" is also Connector/C or the server client.
// libmysqlclient is "8.0.x" or "5.7.x". Empty is LibMySql.
MysqlClientKind mysqlClientKindFromInfo(const QString &clientInfo);

// Semicolon-separated QMYSQL connect options. Timeouts are seconds.
// sslMode is off, preferred, required, or verify (empty means preferred).
// sslCa is sent only for required and verify. It must not contain ';'.
// client selects whether MYSQL_OPT_SSL_MODE is included. Default is LibMySql.
QString mysqlConnectOptions(int connectTimeoutSec,
                            const QString &sslMode,
                            const QString &sslCa,
                            MysqlClientKind client = MysqlClientKind::LibMySql);
