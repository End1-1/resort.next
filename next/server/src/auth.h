#pragma once

#include "config.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Result of an HTTP handler that is not the login form.
struct ApiResult {
    int httpStatus = 500;
    QJsonObject body;
};

// JSON {"error": code, "message": ...}. message may be null (omitted). Never a password.
ApiResult apiError(int httpStatus, const char *code, const char *message);

// Session: any live bearer. Command: live bearer and commands_allowed.
// Logout and GET current are Session. Mutating hotel routes are Command.
enum class RouteAccess {
    Session,
    Command
};

struct SessionPrincipal {
    qint64 sessionId = 0;
    qint64 userId = 0;
    qint64 propertyId = 0;
    bool roleNull = true;
    qint64 roleId = 0;
    bool commandsAllowed = false;
    QString login;
    QString name;
    QString expiresAt;
};

// Pure policy. No database. found is false for an unknown hash.
// revoked is checked before expiry. A disabled user is rejected even when
// the row has not expired. Command routes need commands_allowed.
struct AccessDecision {
    int httpStatus = 200;
    const char *code = nullptr;
};

AccessDecision decideAccess(bool found,
                            bool revoked,
                            bool expired,
                            bool disabled,
                            bool commandsAllowed,
                            bool requiresCommand);

// Empty when the header is missing or not "Bearer" plus a 64-hex token.
// The scheme is case-insensitive. The token bytes are returned unchanged.
QByteArray bearerTokenFromHeader(const QByteArray &authorization);

bool isSessionToken(const QByteArray &token);

// Lowercase hex SHA-256 of the raw token bytes. This is nx_session.token_hash.
QByteArray sha256Hex(const QByteArray &token);

struct AuthOutcome {
    bool allowed = false;
    ApiResult result;
    SessionPrincipal principal;
};

// Looks the token up by SHA-256 in nx_session. Checks expiry (UTC), revoked_at,
// and nx_user.state. Does not log the token.
AuthOutcome authenticate(const DatabaseTarget &target,
                         int connectTimeoutSec,
                         const QByteArray &authorization,
                         RouteAccess access);

// Sets nx_session.revoked_at for this row. The caller has already authenticated.
ApiResult revokeSession(const DatabaseTarget &target, int connectTimeoutSec, qint64 sessionId);

// Body of GET /api/v1/sessions/current. Does not include the bearer token.
QJsonObject currentSessionBody(const SessionPrincipal &principal);
