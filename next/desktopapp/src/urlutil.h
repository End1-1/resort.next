#pragma once

#include <QString>

// Parsed connection address. error is translated for the UI. technical stays English.
// technical is English and never includes a password.
struct UrlParse {
    bool ok = false;
    QString url;
    QString error;
    QString technical;
};

// Accepts host:port or an absolute http(s) origin. No path, query, or userinfo.
// https is accepted so a later TLS listener can be stored before the server speaks it.
UrlParse parseServerBase(const QString &text);

// Empty text is valid and means "HTTP health only".
// host:port becomes ws://host:port/api/v1/ws. wss is accepted.
UrlParse parseWebSocketUrl(const QString &text);
