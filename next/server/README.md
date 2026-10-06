# next/server

`hotel-api` is the Qt/C++ HTTP service that will own hotel rules and be the only writer to MariaDB. This directory binds a port, serves `/health`, and implements `POST /api/v1/sessions` against `nx_user` / `nx_session`. It does not implement reservations or folios. Those tables exist in `next/dbdump/migrations/0002_nx_core.sql`; no folio route is registered.

It is not the old `Server/` tray program (UDP `"who"`). It does not link Qt Widgets and it does not compile `Resort/` sources.

Build system is CMake. There is no qmake `.pro`.

## Dependencies

Qt 6.4 or newer, modules **Core, Network, Sql, HttpServer, WebSockets**, plus a C++17 compiler and CMake 3.21+. The MariaDB/MySQL driver plugin (`QMYSQL`) is needed only when a database DSN is configured. The binary still runs without it; `/health` then reports `driver_not_loaded`.

The HTTP calls in `src/httpserver.cpp` follow the Qt 6.4 API (`setHeader`, `afterRequest`, `QHttpServer::listen`) and, from Qt 6.8 on (including 6.10), `QHttpHeaders`, `addAfterRequestHandler`, and `QTcpServer` plus `bind`. Both paths are in the same file.

Ubuntu 24.04 packages used to configure and build this tree:

```bash
sudo apt install cmake g++ ninja-build \
  qt6-base-dev qt6-httpserver-dev qt6-websockets-dev libqt6sql6-mysql
```

Windows: the same Qt modules, CMake, and MSVC (or another Qt 6 kit). This Linux tree does not compile `src/platform_windows.cpp`. That file is the Service Control Manager entry (`HotelApi`, `--install`, `--uninstall`, `--console`).

## Build

From the repository root:

```bash
cmake -S next -B /tmp/hotel-next-build -G Ninja
cmake --build /tmp/hotel-next-build
```

Checked with g++ 13 and Qt 6.4.2. If `c++` on the machine cannot link its standard library, configure with `-DCMAKE_CXX_COMPILER=g++`.

The service binary is `/tmp/hotel-next-build/server/hotel-api`. Building `next/server` on its own also works (`cmake -S next/server`).

## Run

Default listen address is loopback. No database is contacted until `mysql_host` and `mysql_schema` are set in `hotel-api.ini` or by non-empty `HOTEL_MYSQL_HOST` and `HOTEL_MYSQL_SCHEMA`.

```bash
/tmp/hotel-next-build/server/hotel-api
curl -sS http://127.0.0.1:8080/health
```

`GET /health` with no database configured returns 200 and `"db":{"configured":false,"state":"skipped"}`. With `mysql_host` and `mysql_schema`, the process opens `QMYSQL` (3 second connect, read, and write timeout) and returns 200 `"state":"up"` or 503 `"state":"down"`. The JSON never includes the password, the host, or the driver text. A failed open logs the MySQL native code and the driver text with the password scrubbed, and sets `db.error` to `access_denied` (1045), `unknown_database` (1049), `cannot_connect` (2002/2003), or `connection_failed`. Startup logs the config path, or `hotel-api config none`, and `hotel-api database 127.0.0.1:3306/hotelnext user=root` (never the password).

| Method and path | Now |
|-----------------|-----|
| `GET /health` | Implemented. No session. No DSN is 200 `db.state=skipped`. |
| `GET /api/v1` | Identity marker (`status=partial`). Requires a bearer token. |
| `POST /api/v1/sessions` | Login. JSON `login` + `password`. No bearer. No DSN is 503 `database_not_configured`. Wrong password is 401. |
| `GET /api/v1/sessions/current` | Current user, `commands_allowed`, `expires_at`. No token in the body. |
| `DELETE /api/v1/sessions` | Logout. Sets `nx_session.revoked_at`. Does not require `commands_allowed`. |
| `GET /api/v1/rooms`, `/room-types`, `/buildings` | Bearer. Names from `nx_label` (`?lang=` or `Accept-Language`). |
| `GET /api/v1/room-statuses` | Bearer. Codes only; the client translates them. |
| anything else under `/api/v1` | `401` without a bearer, otherwise `404` JSON |
| WebSocket `/api/v1/ws` | Only if `HOTEL_WS_LISTEN` is set. Hello frame, no PMS events. A browser `Origin` other than loopback (`127.0.0.1` or `localhost`) is rejected |

## Sessions

Apply the schema before login. The process does not create tables.

```bash
mariadb --default-character-set=utf8mb4 -h HOST -u USER -p DATABASE \
  < next/dbdump/migrations/0002_nx_core.sql
```

Then seed one `nx_user` from `next/dbdump/seed/nx_user.example.sql` (placeholders only; do not commit a real hash). `0001_hotel_api_session.sql` is not used.

`POST /api/v1/sessions` with `{"login":"...","password":"..."}`.

The row comes from `nx_user`: `login` (unique), `state = 'active'`, `password_scheme = 'md5'`, and `password_hash` equal to MD5 of the UTF-8 password (lowercase hex, compared in constant time). That is the same byte string the desktop still computes for `users.f_password`, but the API does not read `users`. After connect the service runs `SET NAMES utf8mb4`. The password is not sent to MariaDB and is not logged. Unknown user, disabled user, a non-`md5` scheme, and a wrong password all return `401 {"error":"unauthorized"}`. The hash is not rewritten.

If `nx_user` or `nx_session` is missing, the response is `503` with `error` `session_store_unavailable`.

A row in `nx_session` holds the session. The response token is 64 hex characters. The row stores SHA-256 of the token, the user id, the role id, and `commands_allowed`. Expiry is 12 hours, stored as UTC. `web_sessions`, `s_user_session`, and `hotel_api_session` are not used.

`commands_allowed` is false when the role is null or `nx_role_permission` has no row for it. Login still returns a token. A later mutating route (anything except `GET`, and except `DELETE /api/v1/sessions`) must call the bearer check with command access. That check returns `403` `commands_not_allowed` when the flag is false. Logout and read routes do not.

The JSON user object includes `role_id` (`nx_role.id`, or null). `group` is the same value.

`GET /health` stays unauthenticated. Every other `/api/v1` route reads `Authorization: Bearer <64 hex>`. The lookup key is SHA-256 of those bytes (`nx_session.token_hash`). Unknown or revoked is `401` `unauthorized`. Past `expires_at` (compared with `UTC_TIMESTAMP()`) is `401` `session_expired`. `nx_user.state` other than `active` is `401` `user_disabled`. A well-formed token with no database configured is `503` `database_not_configured`. The token is not logged.

`DELETE /api/v1/sessions` sets `revoked_at` on that row. `GET /api/v1/sessions/current` returns the user, `commands_allowed`, and `expires_at`, and does not repeat the token.

Room, room-type, and building reads are registered. Apply `0003_nx_label.sql` after `0002`. A missing `nx_label` is `503` `schema_outdated`. Example rooms: `next/dbdump/seed/nx_demo_rooms.example.sql`. Folio and reservation routes are not registered.

```bash
curl -sS -H 'Content-Type: application/json' \
  -d '{"login":"USER","password":"SECRET"}' \
  http://127.0.0.1:8080/api/v1/sessions
```

`SIGINT` and `SIGTERM` stop the process (so systemd can stop it).

## Configuration

The process reads `hotel-api.ini` by itself. It does not read a `.env` file. systemd `EnvironmentFile=` can still inject variables; see `deploy/hotel-api.service` and `config/hotel-api.env.example`.

Field-by-field format, search order, Windows DLL layout, and the error text are in [next/docs/server-config.md](../docs/server-config.md) (Russian).

Search order:

1. `HOTEL_CONFIG`, when the variable is set and the value is not empty. A missing file aborts startup. The path is not combined with the steps below.
2. `hotel-api.ini` in the executable's directory (`QCoreApplication::applicationDirPath`). This is not the process working directory. The Windows service is started with working directory `System32`, and Qt Creator may use the source tree as the working directory; the ini next to `hotel-api.exe` is still found.
3. On Linux only, `/etc/hotel-api/hotel-api.ini`.
4. If none of those files exist, built-in defaults: listen `127.0.0.1:8080`, no database, WebSocket off. The log line is `hotel-api config none`.

When a file is read, the log line is `hotel-api config` plus that path. The line never includes the password.

The first existing file wins. An unreadable file aborts startup instead of skipping to the next candidate. CMake copies `config/hotel-api.ini.example` next to the built executable under that same example name. Rename or copy it to `hotel-api.ini` in that directory before filling it in. `hotel-api.ini` is gitignored under `next/`. Do not commit a real password.

| Key | Environment override | Meaning |
|-----|----------------------|---------|
| `listen` | `HOTEL_LISTEN` | `<ip>:<port>` or a bare port (then `127.0.0.1`). Default `127.0.0.1:8080` when the key is absent. Host must be numeric. |
| `mysql_host` | `HOTEL_MYSQL_HOST` | MariaDB host. Together with `mysql_schema`, this turns the database on. |
| `mysql_port` | `HOTEL_MYSQL_PORT` | Optional. Default `3306`. |
| `mysql_schema` | `HOTEL_MYSQL_SCHEMA` | Database name. |
| `mysql_user` | `HOTEL_MYSQL_USER` | Required once the host and schema are set. Missing user aborts startup. |
| `mysql_password` | `HOTEL_MYSQL_PASSWORD` | Literal password. `@`, `:`, `%`, `#`, and `;` are not encoded. Never logged. |
| `ws_listen` | `HOTEL_WS_LISTEN` | Same shape as `listen`. Absent or empty: WebSocket stays off. |

An environment variable overrides the matching ini key only when it is set and not empty (after trimming). `HOTEL_MYSQL_PASSWORD=` does not clear a password written in the ini. That keeps a file-first setup working when a shell, Qt Creator kit, or `EnvironmentFile` exports the variable as empty. To leave the database off, leave `mysql_host` and `mysql_schema` empty or point `HOTEL_CONFIG` at a different file. A non-empty variable still wins, including `HOTEL_LISTEN=127.0.0.1:8080` in the env example.

Spaces around `=` are allowed (`mysql_user = root`). A value may be wrapped in double quotes when it needs leading or trailing spaces. A missing `mysql_user` while the host and schema are set aborts startup with `mysql_user is required`. The error text does not include the password.

A legacy `dsn` key, or `HOTEL_DSN`, is still accepted when no `mysql_*` value is set, and startup logs `dsn is deprecated`. If both are present, `mysql_*` wins and startup logs `mysql_* overrides dsn`. New files should not use `dsn`.

## Linux daemon

Run it in the foreground. systemd is the supervisor (`Type=simple`). `deploy/hotel-api.service` is an example: it reads `/etc/hotel-api/hotel-api.env` and execs `/usr/local/bin/hotel-api` (`cmake --install`). `--install` on Linux exits with a message; it does not fork.

## Windows service

Compiled only into the Windows binary:

- Started by the Service Control Manager: service name `HotelApi`, display name `Hotel API`.
- `hotel-api --install` registers that service (elevated). `hotel-api --uninstall` removes it. Stop it before uninstall.
- `hotel-api --console`, or any start that is not the SCM, runs the same listeners in the foreground.
- The service looks for `hotel-api.ini` next to `hotel-api.exe`. It does not look in `System32`. A non-empty `HOTEL_CONFIG` replaces that path. A non-empty `HOTEL_LISTEN`, `HOTEL_WS_LISTEN`, or `HOTEL_MYSQL_HOST` / `HOTEL_MYSQL_PORT` / `HOTEL_MYSQL_SCHEMA` / `HOTEL_MYSQL_USER` / `HOTEL_MYSQL_PASSWORD` in the system environment overrides the matching ini key.

## Contract

`openapi.yaml` is the HTTP contract the desktop (and, later, the web client) will share. Planned resources from the audit are not registered as routes yet.
