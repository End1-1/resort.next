-- Optimistic concurrency for room dictionaries. MariaDB 10.11, utf8mb4.
--
-- Does not ALTER 0002_nx_core.sql, 0003_nx_label.sql, or 0004_nx_audit.sql.
-- Apply those first. version starts at 0. A PATCH sends the version it read;
-- the server increments it. A stale version is 409 version_conflict.
--
-- Re-running is safe. ADD COLUMN IF NOT EXISTS does nothing when the column
-- is already there. It does not change a column that exists with another type.

SET NAMES utf8mb4;

ALTER TABLE `nx_room_type`
  ADD COLUMN IF NOT EXISTS `version` int(11) NOT NULL DEFAULT 0;

ALTER TABLE `nx_building`
  ADD COLUMN IF NOT EXISTS `version` int(11) NOT NULL DEFAULT 0;

ALTER TABLE `nx_room`
  ADD COLUMN IF NOT EXISTS `version` int(11) NOT NULL DEFAULT 0;
