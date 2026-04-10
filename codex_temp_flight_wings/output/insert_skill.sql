-- ============================================================
-- Super Skill Tool - Auto-generated SQL
-- Generated: 2026-04-10 11:18:12
-- Character ID: 13745
-- ============================================================

-- === Super SP Carrier Skill (1001999) ===
-- Total SP needed for all skills below: 20
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 1001999, 20, 20, -1)
ON DUPLICATE KEY UPDATE skilllevel=20, masterlevel=20, expiration=-1;

-- Super Skill:  (10001901)
-- Type: active_melee, MaxLevel: 20
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 10001901, 1, 20, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=20, expiration=-1;

-- ============================================================
-- ROLLBACK: Delete the above skills (uncomment if needed)
-- ============================================================
-- DELETE FROM skills WHERE characterid=13745 AND skillid=10001901;

