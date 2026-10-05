# next/docs

Notes for the migration into `next/`. The audit stays the plan:

| Document | What it decides |
|----------|-----------------|
| [docs/audit/README.md](../../docs/audit/README.md) | How to read the audit |
| [docs/audit/03-celevaya-arhitektura.md](../../docs/audit/03-celevaya-arhitektura.md) | Target shape: one writer, REST `/api/v1`, sessions, WebSocket hints |
| [docs/audit/05-dorozhnaya-karta.md](../../docs/audit/05-dorozhnaya-karta.md) | Phases 0 through 10 |

[adr-0001-qt-cpp-server.md](adr-0001-qt-cpp-server.md) records the owner override: the service is Qt/C++ with CMake, not the Go process suggested in audit §3.2. Where the audit says `backend/` or Go, read `next/server`.

[nx-schema.md](nx-schema.md) records the later owner override on the database: new `nx_` tables, not expand/contract that keeps `f_reservation` / `m_register` as the store for `next/`.
