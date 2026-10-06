-- Fictional rooms for a local nx_ database. Not a hotel dump.
-- No guests, no passwords, no folio lines.
--
-- Apply first:
--   next/dbdump/migrations/0002_nx_core.sql
--   next/dbdump/migrations/0003_nx_label.sql
--
-- Re-running updates the same codes (property NXDEMO). It does not delete
-- rooms you added by hand under other property codes.

SET NAMES utf8mb4;

INSERT INTO `nx_property` (`code`, `name`, `timezone_name`, `currency_code`, `created_at`)
VALUES ('NXDEMO', 'Demo hotel', 'Asia/Yerevan', 'AMD', UTC_TIMESTAMP())
ON DUPLICATE KEY UPDATE
  `name` = VALUES(`name`),
  `timezone_name` = VALUES(`timezone_name`),
  `currency_code` = VALUES(`currency_code`);

INSERT INTO `nx_building` (`property_id`, `code`, `name`)
SELECT p.id, 'MAIN', 'Main building'
FROM `nx_property` p
WHERE p.code = 'NXDEMO'
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

INSERT INTO `nx_room_type` (`property_id`, `code`, `name`)
SELECT p.id, v.code, v.name
FROM `nx_property` p
JOIN (
  SELECT 'STD' AS code, 'Standard' AS name
  UNION ALL SELECT 'DLX', 'Deluxe'
  UNION ALL SELECT 'SGL', 'Single'
) v
WHERE p.code = 'NXDEMO'
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

INSERT INTO `nx_label` (`owner_table`, `owner_id`, `locale`, `name`)
SELECT 'nx_building', b.id, v.locale, v.name
FROM `nx_building` b
JOIN `nx_property` p ON p.id = b.property_id
JOIN (
  SELECT 'hy' AS locale, 'Գլխավոր մասնաշենք' AS name
  UNION ALL SELECT 'en', 'Main building'
  UNION ALL SELECT 'ru', 'Главный корпус'
) v
WHERE p.code = 'NXDEMO' AND b.code = 'MAIN'
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

INSERT INTO `nx_label` (`owner_table`, `owner_id`, `locale`, `name`)
SELECT 'nx_room_type', t.id, v.locale, v.name
FROM `nx_room_type` t
JOIN `nx_property` p ON p.id = t.property_id
JOIN (
  SELECT 'STD' AS code, 'hy' AS locale, 'Ստանդարտ' AS name
  UNION ALL SELECT 'STD', 'en', 'Standard'
  UNION ALL SELECT 'STD', 'ru', 'Стандарт'
  UNION ALL SELECT 'DLX', 'hy', 'Դելյուքս'
  UNION ALL SELECT 'DLX', 'en', 'Deluxe'
  UNION ALL SELECT 'DLX', 'ru', 'Делюкс'
  UNION ALL SELECT 'SGL', 'hy', 'Մեկտեղանի'
  UNION ALL SELECT 'SGL', 'en', 'Single'
  UNION ALL SELECT 'SGL', 'ru', 'Одноместный'
) v ON v.code = t.code
WHERE p.code = 'NXDEMO'
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

INSERT INTO `nx_room` (`property_id`, `room_type_id`, `building_id`, `code`, `floor`, `status_code`)
SELECT p.id, t.id, b.id, v.code, v.floor, v.status_code
FROM `nx_property` p
JOIN `nx_building` b ON b.property_id = p.id AND b.code = 'MAIN'
JOIN (
  SELECT '101' AS code, 'STD' AS type_code, 1 AS floor, 'vacant_ready' AS status_code
  UNION ALL SELECT '102', 'STD', 1, 'vacant_dirty'
  UNION ALL SELECT '103', 'SGL', 1, 'vacant_ready'
  UNION ALL SELECT '201', 'DLX', 2, 'occupied'
  UNION ALL SELECT '202', 'DLX', 2, 'out_of_order'
  UNION ALL SELECT '203', 'STD', 2, 'vacant_ready'
) v
JOIN `nx_room_type` t ON t.property_id = p.id AND t.code = v.type_code
WHERE p.code = 'NXDEMO'
ON DUPLICATE KEY UPDATE
  `room_type_id` = VALUES(`room_type_id`),
  `building_id` = VALUES(`building_id`),
  `floor` = VALUES(`floor`),
  `status_code` = VALUES(`status_code`);
