-- Who changed a reservation, and when. MariaDB 10.11, utf8mb4.
--
-- Does not ALTER 0002_nx_core.sql or 0003_nx_label.sql. Apply those first.
-- created_by and updated_by store nx_user.id. There is no foreign key: a
-- re-run only adds missing columns, and ADD CONSTRAINT is not idempotent here.
--
-- Re-running is safe. ADD COLUMN IF NOT EXISTS does nothing when the column
-- is already there. It does not change a column that exists with another type.

SET NAMES utf8mb4;

ALTER TABLE `nx_reservation`
  ADD COLUMN IF NOT EXISTS `created_by` bigint(20) DEFAULT NULL,
  ADD COLUMN IF NOT EXISTS `updated_at` datetime DEFAULT NULL,
  ADD COLUMN IF NOT EXISTS `updated_by` bigint(20) DEFAULT NULL;

ALTER TABLE `nx_stay`
  ADD COLUMN IF NOT EXISTS `updated_at` datetime DEFAULT NULL,
  ADD COLUMN IF NOT EXISTS `updated_by` bigint(20) DEFAULT NULL;
