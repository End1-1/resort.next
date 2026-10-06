# next/

Strangler-fig home for the new hotel backend and the clients that talk to it.

A new agent should read [docs/HANDOFF.md](docs/HANDOFF.md) first (Russian). It is the handoff for this migration: decisions, current state, and the next backlog.

The running product is still the Qt desktop under `Resort/`: widgets open MariaDB through `DoubleDatabase`. Nothing in this tree replaces that yet. `Resort/`, `Server/`, `smarthotel/`, and `DB/` stay as they are.

Owner decision, which overrides the Go recommendation in `docs/audit/03-celevaya-arhitektura.md` §3.2: the backend is **Qt/C++**, built with **CMake**. One process owns the business rules and is the only future writer to MariaDB. Desktop and the later web client share one HTTP JSON contract (`/api/v1`, OpenAPI). WebSocket carries realtime hints; clients still read and write through HTTP.

| Path | Role |
|------|------|
| `server/` | `hotel-api`. Qt/C++ HTTP service. Linux: foreground process under systemd. Windows: Windows service. |
| `desktopapp/` | Windowed `hotel-desktop` (Qt Widgets) and headless `hotel-desktop-stub`. No `QMYSQL`. |
| `docs/` | Migration notes. They extend `docs/audit/`; they do not replace it. |
| `dbdump/` | DDL and migration SQL only. Core tables are `nx_*` in `dbdump/migrations/0002_nx_core.sql`. No guest data, no password hashes, no production dump. |

`webport/` is not in this tree. The web client starts after `desktopapp/` is actually in use.

Build both targets from here:

```bash
cmake -S next -B /tmp/hotel-next-build -G Ninja
cmake --build /tmp/hotel-next-build
```

Dependencies and run instructions are in `server/README.md` and `desktopapp/README.md`. The phase plan is `docs/audit/05-dorozhnaya-karta.md`. What this scaffold does and does not finish is `docs/adr-0001-qt-cpp-server.md`.
