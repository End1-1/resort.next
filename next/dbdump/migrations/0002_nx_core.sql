-- Greenfield store for next/hotel-api. MariaDB 10.11, utf8mb4.
--
-- Old tables (users, f_reservation, m_register, ...) stay where they are.
-- This file does not ALTER or DROP them and does not add foreign keys toward them.
-- hotel-api reads nx_user and writes nx_session. It does not read users or
-- hotel_api_session (0001). 0001 is not required before this file.
--
-- Re-running is safe for the voucher catalog (ON DUPLICATE KEY UPDATE).
-- CREATE TABLE IF NOT EXISTS does not repair a table that was created and then
-- failed halfway through its constraints. If a first apply stops in the middle,
-- drop the nx_* tables that appeared and run this file again before loading data.
--
-- Login seed is not in this file. See next/dbdump/seed/nx_user.example.sql.
-- No guest rows, no password hashes, no fiscal secrets.
--
-- nx_posting is append-only by convention: a reversal is a new row with
-- amount_sign = -1 and reverses_posting_id set. There is no trigger and no
-- folio route in hotel-api yet.
-- Two stays may still overlap on one room. MariaDB has no exclusion constraint;
-- the service will enforce that later.
-- Do not put fiscal or channel passwords in nx_setting.

SET NAMES utf8mb4;

-- Object: one hotel. The old database was the whole property.
CREATE TABLE IF NOT EXISTS `nx_property` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `code` varchar(32) NOT NULL,
  `name` varchar(128) NOT NULL,
  `timezone_name` varchar(64) NOT NULL DEFAULT 'UTC',
  `currency_code` char(3) NOT NULL DEFAULT 'AMD',
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_property_code` (`code`),
  CONSTRAINT `ck_nx_property_code` CHECK (char_length(`code`) > 0),
  CONSTRAINT `ck_nx_property_currency` CHECK (char_length(`currency_code`) = 3)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Building inside the property. Was f_room_building.
CREATE TABLE IF NOT EXISTS `nx_building` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `property_id` bigint(20) NOT NULL,
  `code` varchar(32) NOT NULL,
  `name` varchar(128) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_building_code` (`property_id`, `code`),
  CONSTRAINT `fk_nx_building_property` FOREIGN KEY (`property_id`) REFERENCES `nx_property` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_building_code` CHECK (char_length(`code`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Was users_groups. Rights are named rows, not a bare integer.
CREATE TABLE IF NOT EXISTS `nx_role` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `property_id` bigint(20) NOT NULL,
  `code` varchar(32) NOT NULL,
  `name` varchar(64) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_role_code` (`property_id`, `code`),
  CONSTRAINT `fk_nx_role_property` FOREIGN KEY (`property_id`) REFERENCES `nx_property` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_role_code` CHECK (char_length(`code`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Was the meaning of users_rights_template / numeric f_right.
CREATE TABLE IF NOT EXISTS `nx_permission` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `code` varchar(64) NOT NULL,
  `name` varchar(128) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_permission_code` (`code`),
  CONSTRAINT `ck_nx_permission_code` CHECK (char_length(`code`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Was users_rights where f_flag = 1. One grant per role and permission.
CREATE TABLE IF NOT EXISTS `nx_role_permission` (
  `role_id` bigint(20) NOT NULL,
  `permission_id` bigint(20) NOT NULL,
  PRIMARY KEY (`role_id`, `permission_id`),
  KEY `ix_nx_role_permission_permission` (`permission_id`),
  CONSTRAINT `fk_nx_role_permission_role` FOREIGN KEY (`role_id`) REFERENCES `nx_role` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_role_permission_permission` FOREIGN KEY (`permission_id`) REFERENCES `nx_permission` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Was users. login is unique. password_scheme 'md5' is the only scheme this
-- build checks (32 hex chars, UTF-8, same bytes as the desktop MD5).
-- The hash lives here, not in users.f_password. varchar(255) has room for a
-- later scheme; widening ck_nx_user_scheme is a future migration.
-- legacy_id is an optional copy of users.f_id. It is not a foreign key.
CREATE TABLE IF NOT EXISTS `nx_user` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `property_id` bigint(20) NOT NULL,
  `role_id` bigint(20) DEFAULT NULL,
  `login` varchar(64) NOT NULL,
  `first_name` varchar(64) NOT NULL DEFAULT '',
  `last_name` varchar(64) NOT NULL DEFAULT '',
  `password_hash` varchar(255) NOT NULL,
  `password_scheme` varchar(16) NOT NULL,
  `state` varchar(16) NOT NULL,
  `legacy_id` bigint(20) DEFAULT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_user_login` (`login`),
  UNIQUE KEY `uq_nx_user_legacy` (`legacy_id`),
  KEY `ix_nx_user_property` (`property_id`),
  KEY `ix_nx_user_role` (`role_id`),
  CONSTRAINT `fk_nx_user_property` FOREIGN KEY (`property_id`) REFERENCES `nx_property` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_user_role` FOREIGN KEY (`role_id`) REFERENCES `nx_role` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_user_login` CHECK (char_length(`login`) > 0),
  CONSTRAINT `ck_nx_user_hash` CHECK (char_length(`password_hash`) > 0),
  CONSTRAINT `ck_nx_user_scheme` CHECK (`password_scheme` IN ('md5')),
  CONSTRAINT `ck_nx_user_state` CHECK (`state` IN ('active', 'disabled'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Replaces hotel_api_session, s_user_session, and web_sessions for the API.
-- token_hash is hex SHA-256 of the bearer token. The token is not stored.
-- created_at and expires_at are UTC, written by hotel-api.
-- revoked_at is reserved for end of day. No revoke route yet.
CREATE TABLE IF NOT EXISTS `nx_session` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `token_hash` char(64) NOT NULL,
  `user_id` bigint(20) NOT NULL,
  `role_id` bigint(20) DEFAULT NULL,
  `commands_allowed` tinyint(1) NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL,
  `expires_at` datetime NOT NULL,
  `revoked_at` datetime DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_session_token` (`token_hash`),
  KEY `ix_nx_session_user` (`user_id`),
  KEY `ix_nx_session_role` (`role_id`),
  CONSTRAINT `fk_nx_session_user` FOREIGN KEY (`user_id`) REFERENCES `nx_user` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_session_role` FOREIGN KEY (`role_id`) REFERENCES `nx_role` (`id`) ON DELETE SET NULL ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_session_token` CHECK (char_length(`token_hash`) = 64)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Was f_guests. Passport stays a single optional number until a document table exists.
-- legacy_id is an optional copy of f_guests.f_id, not a foreign key.
CREATE TABLE IF NOT EXISTS `nx_guest` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `property_id` bigint(20) NOT NULL,
  `first_name` varchar(255) NOT NULL DEFAULT '',
  `last_name` varchar(255) NOT NULL DEFAULT '',
  `title` varchar(16) DEFAULT NULL,
  `sex_code` varchar(16) DEFAULT NULL,
  `birth_date` date DEFAULT NULL,
  `nationality_code` varchar(8) DEFAULT NULL,
  `document_number` varchar(64) DEFAULT NULL,
  `phone` varchar(64) DEFAULT NULL,
  `email` varchar(255) DEFAULT NULL,
  `remarks` text DEFAULT NULL,
  `legacy_id` bigint(20) DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_guest_legacy` (`legacy_id`),
  KEY `ix_nx_guest_property` (`property_id`),
  KEY `ix_nx_guest_name` (`property_id`, `last_name`, `first_name`),
  CONSTRAINT `fk_nx_guest_property` FOREIGN KEY (`property_id`) REFERENCES `nx_property` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Was f_room_classes.
CREATE TABLE IF NOT EXISTS `nx_room_type` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `property_id` bigint(20) NOT NULL,
  `code` varchar(32) NOT NULL,
  `name` varchar(128) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_room_type_code` (`property_id`, `code`),
  CONSTRAINT `fk_nx_room_type_property` FOREIGN KEY (`property_id`) REFERENCES `nx_property` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_room_type_code` CHECK (char_length(`code`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Was f_room. status_code is the current housekeeping state, not a stay state.
-- Intended values: vacant_ready, occupied, vacant_dirty, out_of_order,
-- house_use, complimentary, out_of_inventory. Not a check, so a later copy
-- can land an unknown code and fix it in the service.
CREATE TABLE IF NOT EXISTS `nx_room` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `property_id` bigint(20) NOT NULL,
  `room_type_id` bigint(20) NOT NULL,
  `building_id` bigint(20) DEFAULT NULL,
  `code` varchar(32) NOT NULL,
  `floor` smallint(6) DEFAULT NULL,
  `phone` varchar(32) DEFAULT NULL,
  `status_code` varchar(32) NOT NULL DEFAULT 'vacant_ready',
  `do_not_disturb` tinyint(1) NOT NULL DEFAULT 0,
  `legacy_id` bigint(20) DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_room_code` (`property_id`, `code`),
  UNIQUE KEY `uq_nx_room_legacy` (`legacy_id`),
  KEY `ix_nx_room_type` (`room_type_id`),
  KEY `ix_nx_room_building` (`building_id`),
  CONSTRAINT `fk_nx_room_property` FOREIGN KEY (`property_id`) REFERENCES `nx_property` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_room_type` FOREIGN KEY (`room_type_id`) REFERENCES `nx_room_type` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_room_building` FOREIGN KEY (`building_id`) REFERENCES `nx_building` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_room_code` CHECK (char_length(`code`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Header of a booking. Was the commercial/source part of f_reservation.
-- status_code is the reserve status (confirmed, guaranteed, tentative, blocked, canceled).
-- legacy_id is an optional copy of f_reservation.f_id (char 16), not a foreign key.
CREATE TABLE IF NOT EXISTS `nx_reservation` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `property_id` bigint(20) NOT NULL,
  `status_code` varchar(32) NOT NULL,
  `channel_code` varchar(64) DEFAULT NULL,
  `remarks` text DEFAULT NULL,
  `legacy_id` varchar(16) DEFAULT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_reservation_legacy` (`legacy_id`),
  KEY `ix_nx_reservation_property` (`property_id`, `status_code`),
  CONSTRAINT `fk_nx_reservation_property` FOREIGN KEY (`property_id`) REFERENCES `nx_property` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_reservation_status` CHECK (char_length(`status_code`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- One room segment of a reservation. Was f_room + dates + f_state on f_reservation.
-- A group is several stays under one nx_reservation.
-- state_code intended values: reserved, in_house, checked_out, canceled,
-- out_of_order, out_of_inventory.
CREATE TABLE IF NOT EXISTS `nx_stay` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `reservation_id` bigint(20) NOT NULL,
  `room_id` bigint(20) DEFAULT NULL,
  `arrival` date NOT NULL,
  `departure` date NOT NULL,
  `state_code` varchar(32) NOT NULL,
  `adults` int(11) NOT NULL DEFAULT 0,
  `children` int(11) NOT NULL DEFAULT 0,
  `version` int(11) NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `ix_nx_stay_reservation` (`reservation_id`),
  KEY `ix_nx_stay_room_dates` (`room_id`, `arrival`, `departure`),
  CONSTRAINT `fk_nx_stay_reservation` FOREIGN KEY (`reservation_id`) REFERENCES `nx_reservation` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_stay_room` FOREIGN KEY (`room_id`) REFERENCES `nx_room` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_stay_dates` CHECK (`departure` >= `arrival`),
  CONSTRAINT `ck_nx_stay_pax` CHECK (`adults` >= 0 AND `children` >= 0),
  CONSTRAINT `ck_nx_stay_state` CHECK (char_length(`state_code`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Was f_reservation_guests. The guest hangs off a stay; the reservation party
-- is the guests of its stays. One row per stay and guest.
CREATE TABLE IF NOT EXISTS `nx_stay_guest` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `stay_id` bigint(20) NOT NULL,
  `guest_id` bigint(20) NOT NULL,
  `is_primary` tinyint(1) NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_stay_guest` (`stay_id`, `guest_id`),
  KEY `ix_nx_stay_guest_guest` (`guest_id`),
  CONSTRAINT `fk_nx_stay_guest_stay` FOREIGN KEY (`stay_id`) REFERENCES `nx_stay` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_stay_guest_guest` FOREIGN KEY (`guest_id`) REFERENCES `nx_guest` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Was m_vaucher. code is unique, so the old duplicate RR/CM rows cannot return.
-- Flags are a catalog for a later posting service. hotel-api does not post.
CREATE TABLE IF NOT EXISTS `nx_voucher` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `code` varchar(8) NOT NULL,
  `name` varchar(64) NOT NULL,
  `affects_balance` tinyint(1) NOT NULL DEFAULT 1,
  `requires_payment` tinyint(1) NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_voucher_code` (`code`),
  CONSTRAINT `ck_nx_voucher_code` CHECK (char_length(`code`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO `nx_voucher` (`code`, `name`, `affects_balance`, `requires_payment`) VALUES
  ('AV', 'Advance', 1, 1),
  ('RC', 'Rate change', 1, 0),
  ('PS', 'Restaurant', 1, 0),
  ('TR', 'Transfer', 1, 0),
  ('CM', 'Phone call', 1, 0),
  ('CO', 'Checkout', 1, 0),
  ('RM', 'Room charge', 1, 0),
  ('RV', 'Receipt', 1, 1),
  ('CH', 'Post charge', 1, 0),
  ('DS', 'Discount', 1, 0),
  ('RR', 'Reinstate', 1, 0),
  ('RS', 'Reservation', 1, 0),
  ('PE', 'Event', 1, 0),
  ('RF', 'Refund', 1, 1),
  ('IN', 'Invoice', 1, 0),
  ('DR', 'Dish line', 1, 0),
  ('CR', 'City ledger opening', 1, 0),
  ('TC', 'Transfer to city ledger', 1, 1),
  ('CI', 'Check-in', 1, 0),
  ('AT', 'Advance transfer', 1, 0),
  ('MR', 'Room move', 1, 0)
ON DUPLICATE KEY UPDATE
  `name` = VALUES(`name`),
  `affects_balance` = VALUES(`affects_balance`),
  `requires_payment` = VALUES(`requires_payment`);

-- Bill for a stay or a reservation. Balance is the sum of nx_posting, not a column.
-- Was the idea of f_reservation.f_invoice / a folio window, not a copy of m_register.
CREATE TABLE IF NOT EXISTS `nx_folio` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `property_id` bigint(20) NOT NULL,
  `reservation_id` bigint(20) DEFAULT NULL,
  `stay_id` bigint(20) DEFAULT NULL,
  `currency_code` char(3) NOT NULL,
  `opened_on` date DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `ix_nx_folio_property` (`property_id`),
  KEY `ix_nx_folio_reservation` (`reservation_id`),
  KEY `ix_nx_folio_stay` (`stay_id`),
  CONSTRAINT `fk_nx_folio_property` FOREIGN KEY (`property_id`) REFERENCES `nx_property` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_folio_reservation` FOREIGN KEY (`reservation_id`) REFERENCES `nx_reservation` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_folio_stay` FOREIGN KEY (`stay_id`) REFERENCES `nx_stay` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_folio_currency` CHECK (char_length(`currency_code`) = 3)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- One immutable ledger line. Was m_register, narrowed to amount, currency, sign, voucher.
-- guest_name_snapshot is a print snapshot, not the guest key.
-- legacy_id is an optional copy of m_register.f_id, not a foreign key.
CREATE TABLE IF NOT EXISTS `nx_posting` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `folio_id` bigint(20) NOT NULL,
  `voucher_id` bigint(20) NOT NULL,
  `business_date` date NOT NULL,
  `amount` decimal(14,2) NOT NULL,
  `currency_code` char(3) NOT NULL,
  `amount_sign` smallint(6) NOT NULL,
  `payment_method_code` varchar(32) DEFAULT NULL,
  `guest_name_snapshot` varchar(255) DEFAULT NULL,
  `reverses_posting_id` bigint(20) DEFAULT NULL,
  `created_at` datetime NOT NULL,
  `created_by` bigint(20) DEFAULT NULL,
  `legacy_id` varchar(16) DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_posting_legacy` (`legacy_id`),
  KEY `ix_nx_posting_folio` (`folio_id`, `business_date`),
  KEY `ix_nx_posting_voucher` (`voucher_id`),
  KEY `ix_nx_posting_reverses` (`reverses_posting_id`),
  KEY `ix_nx_posting_created_by` (`created_by`),
  CONSTRAINT `fk_nx_posting_folio` FOREIGN KEY (`folio_id`) REFERENCES `nx_folio` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_posting_voucher` FOREIGN KEY (`voucher_id`) REFERENCES `nx_voucher` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_posting_reverses` FOREIGN KEY (`reverses_posting_id`) REFERENCES `nx_posting` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `fk_nx_posting_created_by` FOREIGN KEY (`created_by`) REFERENCES `nx_user` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_posting_amount` CHECK (`amount` >= 0),
  CONSTRAINT `ck_nx_posting_sign` CHECK (`amount_sign` IN (-1, 1)),
  CONSTRAINT `ck_nx_posting_currency` CHECK (char_length(`currency_code`) = 3)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Typed property settings. Was f_global_settings, one row per key.
-- value is operational text. Do not store fiscal passwords or DSN secrets here.
CREATE TABLE IF NOT EXISTS `nx_setting` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `property_id` bigint(20) NOT NULL,
  `setting_key` varchar(64) NOT NULL,
  `setting_value` text DEFAULT NULL,
  `updated_at` datetime DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nx_setting_key` (`property_id`, `setting_key`),
  CONSTRAINT `fk_nx_setting_property` FOREIGN KEY (`property_id`) REFERENCES `nx_property` (`id`) ON DELETE RESTRICT ON UPDATE RESTRICT,
  CONSTRAINT `ck_nx_setting_key` CHECK (char_length(`setting_key`) > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
