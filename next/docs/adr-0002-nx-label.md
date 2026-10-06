# ADR 0002: Dictionary names in `nx_label`

**Status:** accepted (2026-10-06, while implementing read-only room dictionaries).

**Follows:** the recommendation in [nx-schema.md](nx-schema.md) (“Имена справочников на трёх языках”). That note asked not to change `0002_nx_core.sql`. This ADR adds `0003_nx_label.sql` only.

## Context

`hotel-desktop` shows Armenian, English, and Russian. The server does not build sentences: JSON `error` stays a code, and the client translates it.

`0002` stores one `name` column on `nx_room_type`, `nx_building`, `nx_role`, `nx_permission`, `nx_voucher`, and `nx_property`. One column cannot be the Armenian, English, and Russian name at once. Adding `name_hy` / `name_en` / `name_ru` on every table would copy the same shape into each dictionary and still miss a later locale.

Room status is not a table. `nx_room.status_code` is a closed set (`vacant_ready`, `occupied`, `vacant_dirty`, `out_of_order`, `house_use`, `complimentary`, `out_of_inventory`). The same is true of reservation statuses and of API error codes. Those strings are identifiers. The client already translates identifiers.

## Decision

Translated display names live in one table:

```text
nx_label
  owner_table   varchar(64)   -- nx_room_type, nx_building, …
  owner_id      bigint
  locale        char(2)       -- hy, en, ru
  name          varchar(255)
  UNIQUE (owner_table, owner_id, locale)
```

There is no foreign key. `owner_id` does not point at a single parent table. The service only writes `owner_table` values it knows.

`code` stays the language-neutral key.

When a read endpoint is asked for a name it resolves, in order:

1. `nx_label` for the requested locale
2. `nx_label` for `ru`
3. the single `name` column already on the `0002` row (the untranslated default)
4. `code`

The client sends the locale as `?lang=hy|en|ru` or as `Accept-Language`. An unknown tag falls through. If neither header nor query names hy, en, or ru, the server uses `ru`.

The server does not translate closed codes. `GET /api/v1/room-statuses` returns `code` only. `hotel-desktop` maps that code with `tr()`, the same way it maps `session_expired`.

Guest `first_name` / `last_name`, free `remarks`, and status codes are not labels.

`0002_nx_core.sql` is not edited. Databases that already applied it gain `nx_label` from `0003_nx_label.sql`. The file is `CREATE TABLE IF NOT EXISTS` and is safe to run again. It does not repair a half-created table.

## Consequences

- A new dictionary adds rows, not columns.
- A room type with no `nx_label` rows still has a name: the old `name` column, or the code if that column is empty.
- The desktop room list shows the name the server already chose. It does not pick among three columns.
- Roles, permissions, and vouchers can use the same table later without another shape. This ADR does not add HTTP for them.
