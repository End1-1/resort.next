-- Translated names for nx_ dictionary rows. MariaDB 10.11, utf8mb4.
--
-- Does not ALTER 0002_nx_core.sql. Apply that file first.
-- One row per entity and locale (hy, en, ru). owner_table + owner_id point at
-- the row; there is no foreign key because the owner is not one table.
-- Closed codes (room status, reservation status, error codes) do not go here.
-- The client translates those. See next/docs/adr-0002-nx-label.md.
--
-- Re-running is safe: CREATE TABLE IF NOT EXISTS does nothing when the table
-- is already there. It does not repair a table that was created halfway.
-- If the first apply stops in the middle, drop nx_label and run this file again
-- before loading labels.

SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `nx_label` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `owner_table` varchar(64) NOT NULL,
  `owner_id` bigint(20) NOT NULL,
  `locale` char(2) NOT NULL,
  `name` varchar(255) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_label_owner` (`owner_table`, `owner_id`, `locale`),
  KEY `ix_nx_label_lookup` (`owner_table`, `locale`, `owner_id`),
  CONSTRAINT `ck_nx_label_table` CHECK (char_length(`owner_table`) > 0),
  CONSTRAINT `ck_nx_label_locale` CHECK (`locale` IN ('hy', 'en', 'ru')),
  CONSTRAINT `ck_nx_label_name` CHECK (char_length(`name`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
