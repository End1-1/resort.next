# next/server

`hotel-api` is the Qt/C++ HTTP service that will own hotel rules and be the only writer to MariaDB. This directory binds a port, serves `/health`, and implements `POST /api/v1/sessions` against the existing `users` table. It does not implement reservations or folios. It does not rewrite `users.f_password` to Argon2id.

It is not the old `Server/` tray program (UDP `"who"`). It does not link Qt Widgets and it does not compile `Resort/` sources.

Build system is CMake. There is no qmake `.pro`.

## Dependencies

Qt 6.4 or newer, modules **Core, Network, Sql, HttpServer, WebSockets**, plus a C++17 compiler and CMake 3.21+. The MariaDB/MySQL driver plugin (`QMYSQL`) is needed only when `HOTEL_DSN` is set. The binary still runs without it; `/health` then reports `driver_not_loaded`.

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

Default listen address is loopback. No database is contacted until you set a DSN.

```bash
HOTEL_LISTEN=127.0.0.1:8080 /tmp/hotel-next-build/server/hotel-api
curl -sS http://127.0.0.1:8080/health
```

`GET /health` with no `HOTEL_DSN` returns 200 and `"db":{"configured":false,"state":"skipped"}`. With a DSN, the process opens `QMYSQL` (3 second connect timeout) and returns 200 `"state":"up"` or 503 `"state":"down"`. The JSON and the log do not include the URL or the password.

| Method and path | Now |
|-----------------|-----|
| `GET /health` | Implemented. Empty `HOTEL_DSN` is 200 `db.state=skipped`. |
| `GET /api/v1` | Identity marker (`status=partial`) |
| `POST /api/v1/sessions` | Login. JSON `login` + `password`. Empty `HOTEL_DSN` is 503 `database_not_configured`. Wrong password is 401. |
| anything else | `404` JSON |
| WebSocket `/api/v1/ws` | Only if `HOTEL_WS_LISTEN` is set. Hello frame, no PMS events. A browser `Origin` other than loopback (`127.0.0.1` or `localhost`) is rejected |

## Sessions

`POST /api/v1/sessions` with `{"login":"...","password":"..."}`.

The check matches the desktop (`Resort/login.cpp`): `users.f_username`, `f_state = 1`, and `f_password` equal to MD5 of the password. The hash is computed in the process (UTF-8 bytes, lowercase hex) and compared in constant time. After connect the service runs `SET NAMES utf8mb4`. The password is not sent to MariaDB and is not logged. Unknown user, inactive user, and a wrong password all return `401 {"error":"unauthorized"}`.

Argon2id is not applied on login. `f_password` is `varchar(32)`, and the desktop still runs `f_password = MD5(:password)`. Replacing the hash would lock the shift out of the reception program. When that desktop check is gone, add a nullable `users.f_password_argon2` column, fill it on a successful MD5 login, and keep the MD5 column until nothing else reads it. This binary does not link an Argon2 library, so it cannot do that write yet.

A new table `hotel_api_session` holds the session (`next/dbdump/migrations/0001_hotel_api_session.sql`). The process runs the same `CREATE TABLE IF NOT EXISTS` on login. The response token is 64 hex characters. The row stores SHA-256 of the token, the user id, the group, and `f_commands_allowed`. Expiry is 12 hours, stored as UTC. `web_sessions` and `s_user_session` are not used.

`commands_allowed` is false when `users_rights` has no `f_flag = 1` row for the group. Login still returns a token. There is no command route in this build; the next one must reject that session.

`GET /health` stays unauthenticated. No other route checks the bearer token yet.

```bash
curl -sS -H 'Content-Type: application/json' \
  -d '{"login":"USER","password":"SECRET"}' \
  http://127.0.0.1:8080/api/v1/sessions
```

`SIGINT` and `SIGTERM` stop the process (so systemd can stop it).

## Configuration

The process reads the environment. It does not load a `.env` file by itself. systemd `EnvironmentFile=` does that; see `deploy/hotel-api.service` and `config/hotel-api.env.example`.

Optional INI, path in `HOTEL_CONFIG` (`config/hotel-api.ini.example`):

| Key | Environment override | Meaning |
|-----|----------------------|---------|
| `listen` | `HOTEL_LISTEN` | `<ip>:<port>` or a bare port (then `127.0.0.1`). Default `127.0.0.1:8080`. Host must be numeric. |
| `dsn` | `HOTEL_DSN` | `mysql://USER:PASSWORD@HOST:3306/DATABASE`, or empty. Percent-encode `@` and `:` inside the user or password. |
| `ws_listen` | `HOTEL_WS_LISTEN` | Same shape as `listen`. Unset or empty: WebSocket stays off. |

If a variable is set, even to empty, it overrides the INI key. Copy the examples outside the repository before filling them in. Do not commit a real DSN.

A malformed DSN aborts startup. The error text does not repeat the URL.

## Linux daemon

Run it in the foreground. systemd is the supervisor (`Type=simple`). `deploy/hotel-api.service` is an example: it reads `/etc/hotel-api/hotel-api.env` and execs `/usr/local/bin/hotel-api` (`cmake --install`). `--install` on Linux exits with a message; it does not fork.

## Windows service

Compiled only into the Windows binary:

- Started by the Service Control Manager: service name `HotelApi`, display name `Hotel API`.
- `hotel-api --install` registers that service (elevated). `hotel-api --uninstall` removes it. Stop it before uninstall.
- `hotel-api --console`, or any start that is not the SCM, runs the same listeners in the foreground.
- The service sees the **system** environment. Set `HOTEL_LISTEN` / `HOTEL_DSN` there, or set `HOTEL_CONFIG` to an ini that is not in git.

## Contract

`openapi.yaml` is the HTTP contract the desktop (and, later, the web client) will share. Planned resources from the audit are not registered as routes yet.
