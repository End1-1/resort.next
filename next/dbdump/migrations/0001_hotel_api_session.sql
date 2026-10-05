-- hotel-api session store. The service also runs this CREATE on login
-- (CREATE TABLE IF NOT EXISTS) so a missing table is not a separate step.
-- Apply it yourself if the MariaDB account cannot CREATE.
--
-- Passwords stay in users.f_password as a 32-char MD5 hex, the same check as
-- Resort/login.cpp (`f_password = MD5(:password)`) and smarthotel/user.php.
-- Do not replace that column with Argon2id:
--   * it is varchar(32), and an Argon2id string does not fit;
--   * the desktop still logs in with MD5, so an in-place rewrite locks the shift out.
-- Later, add a nullable users.f_password_argon2 text column, fill it on a
-- successful MD5 login, and leave f_password unchanged until the desktop
-- stops reading it. hotel-api does not write that column in this build
-- because it does not link an Argon2 implementation.
--
-- f_created_at and f_expires_at are UTC, written by the service.
-- f_token_hash is hex SHA-256 of the bearer token. The token itself is not stored.
-- f_revoked_at is reserved for end-of-day. No revoke route is exposed yet.

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
