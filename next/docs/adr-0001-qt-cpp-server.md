# ADR 0001: Qt/C++ HTTP service under `next/`

**Status:** accepted (owner decision, 2026-10-05).

**Supersedes:** the Go (or ASP.NET) engine recommendation in [docs/audit/03-celevaya-arhitektura.md](../../docs/audit/03-celevaya-arhitektura.md) §3.2. The rest of that document still stands: one writer to MariaDB, REST JSON under `/api/v1`, OpenAPI, server-side sessions, WebSocket only as a hint to re-read over HTTP.

Кратко: новый backend — процесс на Qt/C++ (CMake), не Go. Linux — демон под systemd (процесс на переднем плане, `Type=simple`). Windows — служба. Десктоп и будущий веб ходят в один HTTP-контракт; WebSocket не заменяет HTTP. Весь новый код миграции лежит в `next/`. Веб-клиент (`webport`) не начинаем, пока не готов `next/desktopapp`. Фазы 0–10 по-прежнему в `docs/audit/05-dorozhnaya-karta.md`.

## Context

SmartHotel is a thick Qt/C++ client. Operational rules live in widgets and write SQL to MariaDB. The audit's path is a strangler fig: a new HTTP service becomes the only door to the database, and the desktop moves over module by module. Phases 0 through 10 are written down in [docs/audit/05-dorozhnaya-karta.md](../../docs/audit/05-dorozhnaya-karta.md).

§3.2 of the architecture note argues for Go, and against "living on a Qt server", because today's rules sit inside `QWidget` and the existing CMake tree hard-codes Windows paths. That risk is real. The owner still wants the service in Qt/C++, so the team keeps one language. The constraint that comes with that choice: `next/server` does not link Qt Widgets and does not compile `Resort/` dialogs into the service. Rules get rewritten as service code, not cut-and-pasted from a form.

## Decision

- The future writer is one process, `hotel-api`, built from `next/server` with CMake. C++17, Qt 6 (Core, Network, Sql, HttpServer, WebSockets). No qmake project, no Go module.
- Linux runs it in the foreground. systemd is the daemon supervisor (`Type=simple`). The unit example is `next/server/deploy/hotel-api.service`. There is no double-fork.
- Windows runs it as service `HotelApi` (`--install`, `--uninstall`). `--console` forces a foreground process for debugging. The Service Control Manager entry is a separate translation unit and is compiled only on Windows, so Linux CI does not need a Windows SDK.
- Public contract: HTTP, JSON, prefix `/api/v1`, description in `next/server/openapi.yaml`. Clients, including the future web UI, use that contract. They do not receive a MariaDB DSN.
- WebSocket (`/api/v1/ws`, separate listen address `HOTEL_WS_LISTEN`) is for events after a commit. The payload is an id and a type. The client re-reads the resource with HTTP. WebSocket is off unless `HOTEL_WS_LISTEN` is set. It is not a second API.
- Configuration is `hotel-api.ini` next to the executable, then `/etc/hotel-api/hotel-api.ini` on Linux. A non-empty `HOTEL_CONFIG` replaces that search. `HOTEL_LISTEN`, `HOTEL_DSN`, and `HOTEL_WS_LISTEN` override ini keys only when non-empty. No database in that result means `/health` does not open MariaDB. Example files have empty secrets. Nothing in `next/` hard-codes a password. The Windows service reads the ini from the executable directory, not from `System32`.
- Layout:

  | Path | Contents |
  |------|----------|
  | `next/server` | HTTP API service |
  | `next/desktopapp` | Qt client code that calls the API |
  | `next/docs` | This note and later migration notes |
  | `next/dbdump` | DDL-only dumps and migration SQL, never production data |

- `next/webport` is deferred until `next/desktopapp` is ready. No web scaffold in this change.
- `Resort/`, `Server/`, `smarthotel/`, and `DB/` are not modified here. The old `Server/` tray program is not this API.

## What this change finishes

This note started as the phase 0 **layout** (tree, dump policy, `next/.gitignore`) and the phase 2 **skeleton** (process, config, `GET /health`, OpenAPI).

`POST /api/v1/sessions` reads `nx_user` and inserts `nx_session` ([nx-schema.md](nx-schema.md)). The password check is MD5 only when `nx_user.password_scheme` is `md5`. The service does not read `users` and does not rewrite the hash. `commands_allowed` is false when the role has no `nx_role_permission` row. No command route consumes that flag yet. `hotel_api_session` from the first session migration is unused.

Not done, on purpose:

- rotating the MariaDB password (the owner does that on the servers; git history still has the old values until an explicit history rewrite)
- Argon2id verification or an in-place hash upgrade
- switching the reception desktop off direct SQL
- a web client

`/health` returns 200 when no DSN is configured (`db.state = skipped`). When a DSN is configured it opens `QMYSQL` with a short timeout and returns 503 if the driver is missing or the server does not answer. The body never includes the DSN or the password.

## Consequences

- Audit text that says "Go", "static binary", or "`backend/`" is historical for the engine choice. Follow this ADR for the engine and the path. Follow the audit for phases, API shape, and the order of moving money.
- The desktop strangler lives in `next/desktopapp`. `hotel-desktop` is the windowed client (connection settings, login, an empty workspace) in Armenian, English, and Russian. `hotel-desktop-stub` still calls `GET /health`. There is no logout route: the client drops the bearer token in memory and `nx_session` stays until it expires. `Resort/` keeps its SQL until a later phase turns a module over behind a flag. Later clients, including `webport`, use the same three languages. The service returns error codes, not sentences.
- Schema files go in `next/dbdump/migrations/` as DDL. The core store is `0002_nx_core.sql`. A production dump does not.
- Hiring and deployment follow Qt 6, not a Go toolchain. The widget-coupling warning from §3.2 is a review rule for every later PR: no `QWidget` in `next/server`.
