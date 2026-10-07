-- Fictional rooms for a local nx_ database. Not a hotel dump.
-- No guests, no passwords, no folio lines in the part that runs.
--
-- One database is one client. nx_property exists so a client can keep several
-- hotels in that database; normally there is exactly one property row. This
-- file does not insert a property and does not create NXDEMO. It writes the
-- demo building, room types, and rooms onto the property that is already there,
-- so the signed-in admin sees them.
--
-- Apply first:
--   next/dbdump/migrations/0002_nx_core.sql
--   next/dbdump/migrations/0003_nx_label.sql
-- The room editor also needs 0005_nx_dictionary_version.sql (version columns).
-- This seed does not require 0005: version defaults to 0 when the column exists,
-- and the INSERT lists no version column so it also runs before 0005.
--
-- Target property. Leave NULL to use the row with the smallest id (the usual
-- single hotel). Set a code only when this database has more than one property,
-- and only to a code that already exists:
--
--   SET @nx_property_code = 'main';
--
-- Re-running updates the same building, type, and room codes on that property.
-- It does not delete rows you added by hand, and it does not touch another
-- property. If the SELECT at the bottom is empty, create the property first
-- (see nx_user.example.sql) or set @nx_property_code to a code that exists.

SET NAMES utf8mb4;

SET @nx_property_code = NULL;

SET @nx_property_id = (
  SELECT p.id
  FROM nx_property p
  WHERE @nx_property_code IS NULL OR p.code = @nx_property_code
  ORDER BY p.id
  LIMIT 1
);

INSERT INTO `nx_building` (`property_id`, `code`, `name`)
SELECT p.id, 'MAIN', 'Main building'
FROM `nx_property` p
WHERE p.id = @nx_property_id
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

INSERT INTO `nx_room_type` (`property_id`, `code`, `name`)
SELECT p.id, v.code, v.name
FROM `nx_property` p
JOIN (
  SELECT 'STD' AS code, 'Standard' AS name
  UNION ALL SELECT 'DLX', 'Deluxe'
  UNION ALL SELECT 'SGL', 'Single'
) v
WHERE p.id = @nx_property_id
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
WHERE p.id = @nx_property_id AND b.code = 'MAIN'
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
WHERE p.id = @nx_property_id
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
WHERE p.id = @nx_property_id
ON DUPLICATE KEY UPDATE
  `room_type_id` = VALUES(`room_type_id`),
  `building_id` = VALUES(`building_id`),
  `floor` = VALUES(`floor`),
  `status_code` = VALUES(`status_code`);

-- Shows which property received the rows. Empty means there was nothing to
-- attach to: insert nx_property first, or set @nx_property_code above.
SELECT p.id, p.code, p.name
FROM `nx_property` p
WHERE p.id = @nx_property_id;

-- Optional fictional stay so the rack (шахматка) is not empty.
-- Uncomment the block after the rooms above exist. Still no passwords and no
-- folio. The guest is not a real person. Re-running the block does not insert
-- a second stay for the same demo remark. Needs 0002; 0004 columns are nullable
-- and are not listed here.
--
-- INSERT INTO `nx_guest` (`property_id`, `first_name`, `last_name`)
-- SELECT p.id, 'Anna', 'Example'
-- FROM `nx_property` p
-- WHERE p.id = @nx_property_id
--   AND NOT EXISTS (
--     SELECT 1 FROM `nx_guest` g
--     WHERE g.property_id = p.id AND g.first_name = 'Anna' AND g.last_name = 'Example'
--   );
--
-- INSERT INTO `nx_reservation` (`property_id`, `status_code`, `remarks`, `created_at`)
-- SELECT p.id, 'confirmed', 'demo-seed', UTC_TIMESTAMP()
-- FROM `nx_property` p
-- WHERE p.id = @nx_property_id
--   AND NOT EXISTS (
--     SELECT 1 FROM `nx_reservation` r
--     WHERE r.property_id = p.id AND r.remarks = 'demo-seed'
--   );
--
-- INSERT INTO `nx_stay` (
--   `reservation_id`, `room_id`, `arrival`, `departure`, `state_code`, `adults`, `children`, `version`
-- )
-- SELECT r.id, room.id, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_ADD(CURDATE(), INTERVAL 2 DAY),
--        'in_house', 1, 0, 0
-- FROM `nx_reservation` r
-- JOIN `nx_property` p ON p.id = r.property_id
-- JOIN `nx_room` room ON room.property_id = p.id AND room.code = '201'
-- WHERE p.id = @nx_property_id AND r.remarks = 'demo-seed'
--   AND NOT EXISTS (SELECT 1 FROM `nx_stay` s WHERE s.reservation_id = r.id);
--
-- INSERT INTO `nx_stay_guest` (`stay_id`, `guest_id`, `is_primary`)
-- SELECT s.id, g.id, 1
-- FROM `nx_stay` s
-- JOIN `nx_reservation` r ON r.id = s.reservation_id
-- JOIN `nx_guest` g ON g.property_id = r.property_id
--   AND g.first_name = 'Anna' AND g.last_name = 'Example'
-- WHERE r.property_id = @nx_property_id AND r.remarks = 'demo-seed'
--   AND NOT EXISTS (
--     SELECT 1 FROM `nx_stay_guest` sg WHERE sg.stay_id = s.id AND sg.guest_id = g.id
--   );
