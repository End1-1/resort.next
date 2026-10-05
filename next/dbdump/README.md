# next/dbdump

Place for **DDL and migration SQL** once schema work starts (audit phase 8, and the small migration files the service will apply before that).

Do not commit:

- a dump of a running hotel (`mysqldump`, HeidiSQL export, `.bak`, `.sql.gz`)
- `INSERT` rows for guests, reservations, folios, users, or fiscal devices
- password hashes (`users.f_password` or any replacement)
- production host names tied to credentials, or any password

`DB/db.sql` in the working tree is DDL (tables, procedures, the `guests` view). Data rows were removed there without rewriting git history. The old dump is still in history until the owner force-pushes on purpose. Do not put it back.

Hand-written DDL belongs in `migrations/` and may be committed. A file dropped next to this README that looks like a full dump is gitignored (`next/.gitignore`). That ignore rule is not a substitute for reading a file before you add it: `migrations/*.sql` is not ignored, so do not point `mysqldump` at `migrations/`.
