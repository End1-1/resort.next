# next/docs

Notes for the migration into `next/`. The audit stays the plan.

A new agent starts at [HANDOFF.md](HANDOFF.md) (Russian). It is the self-contained handoff: owner decisions, what is already merged, what is in flight, and the next backlog.

| Document | What it decides |
|----------|-----------------|
| [HANDOFF.md](HANDOFF.md) | Handoff for a new agent. Russian. |
| [docs/audit/README.md](../../docs/audit/README.md) | How to read the audit |
| [docs/audit/03-celevaya-arhitektura.md](../../docs/audit/03-celevaya-arhitektura.md) | Target shape: one writer, REST `/api/v1`, sessions, WebSocket hints |
| [docs/audit/05-dorozhnaya-karta.md](../../docs/audit/05-dorozhnaya-karta.md) | Phases 0 through 10 |

[adr-0001-qt-cpp-server.md](adr-0001-qt-cpp-server.md) records the owner override: the service is Qt/C++ with CMake, not the Go process suggested in audit §3.2. Where the audit says `backend/` or Go, read `next/server`.

[nx-schema.md](nx-schema.md) records the later owner override on the database: new `nx_` tables, not expand/contract that keeps `f_reservation` / `m_register` as the store for `next/`. [adr-0002-nx-label.md](adr-0002-nx-label.md) is the translation table for dictionary names. [ws-events.md](ws-events.md) is the WebSocket frame schema (Russian). [rack-chart.md](rack-chart.md) is the legacy rack feature list and what the desktop chart implements (Russian).

Every client — `hotel-desktop` now, `webport` later — is Armenian (`hy`), English (`en`), and Russian (`ru`). The server stays language-neutral: JSON `error` is a code, and the client maps it to a sentence. Dictionary names are `nx_label` ([adr-0002-nx-label.md](adr-0002-nx-label.md)). Closed codes stay on the client.

[server-config.md](server-config.md) describes `hotel-api.ini`: where the file is read from, each key, and the startup errors. Written in Russian.
