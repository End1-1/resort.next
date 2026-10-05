# next/dbdump

DDL for the `nx_` store used by `hotel-api`. The map from the old tables is `next/docs/nx-schema.md`.

| File | Apply? |
|------|--------|
| `migrations/0002_nx_core.sql` | Yes. Creates the `nx_` tables and the voucher catalog. |
| `migrations/0001_hotel_api_session.sql` | No. Superseded by `nx_session`. Skip it on a new database. |
| `seed/nx_user.example.sql` | By hand, after you replace the placeholders. Not part of `migrations/*.sql`. |

```bash
mariadb --default-character-set=utf8mb4 -h HOST -u USER -p DATABASE \
  < next/dbdump/migrations/0002_nx_core.sql
```

Do not commit:

- a dump of a running hotel (`mysqldump`, HeidiSQL export, `.bak`, `.sql.gz`)
- `INSERT` rows for guests, reservations, folios, users, or fiscal devices (the voucher code catalog inside `0002_nx_core.sql` is reference data, not a guest load)
- password hashes (`nx_user.password_hash`, `users.f_password`, or any replacement)
- production host names tied to credentials, or any password

`DB/db.sql` in the working tree is DDL (tables, procedures, the `guests` view). Data rows were removed there without rewriting git history. The old dump is still in history until the owner force-pushes on purpose. Do not put it back.

Hand-written DDL belongs in `migrations/` and may be committed. A file dropped next to this README that looks like a full dump is gitignored (`next/.gitignore`). That ignore rule is not a substitute for reading a file before you add it: `migrations/*.sql` is not ignored, so do not point `mysqldump` at `migrations/`.
