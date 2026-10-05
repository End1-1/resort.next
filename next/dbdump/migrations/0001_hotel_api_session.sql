-- Superseded for hotel-api by nx_session in 0002_nx_core.sql.
-- The service no longer creates, reads, or writes hotel_api_session.
-- A new database can skip this file. A database that already applied it can
-- leave the unused table in place; this file does not drop it.
--
-- Historical note (no longer the login path): passwords were checked in
-- users.f_password as MD5. Login now uses nx_user.password_hash with
-- password_scheme 'md5'. See next/dbdump/seed/nx_user.example.sql.

CREATE TABLE IF NOT EXISTS `hotel_api_session` (
  `f_id` bigint(20) NOT NULL AUTO_INCREMENT,
  `f_token_hash` char(64) NOT NULL,
  `f_user` int(11) NOT NULL,
  `f_group` int(11) DEFAULT NULL,
  `f_commands_allowed` tinyint(1) NOT NULL DEFAULT 0,
  `f_created_at` datetime NOT NULL,
  `f_expires_at` datetime NOT NULL,
  `f_revoked_at` datetime DEFAULT NULL,
  PRIMARY KEY (`f_id`),
  UNIQUE KEY `uq_hotel_api_session_token` (`f_token_hash`),
  KEY `ix_hotel_api_session_user` (`f_user`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
