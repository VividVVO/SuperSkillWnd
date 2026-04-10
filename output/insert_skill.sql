-- ============================================================
-- Super Skill Tool - Auto-generated SQL
-- Generated: 2026-04-09 14:42:29
-- Character ID: 13745
-- ============================================================

-- === Super SP Carrier Skill (1001999) ===
-- Total SP needed for all skills below: 61
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 1001999, 61, 61, -1)
ON DUPLICATE KEY UPDATE skilllevel=61, masterlevel=61, expiration=-1;

-- Super Skill: 极限防御2 (1301111)
-- Type: buff, MaxLevel: 20
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 1301111, 1, 20, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=20, expiration=-1;

-- Super Skill: 圣甲术2 (1001555)
-- Type: buff, MaxLevel: 15
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 1001555, 1, 15, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=15, expiration=-1;

-- Super Skill: 骑士团战车001 (80001901)
-- Type: newbie_level, MaxLevel: 1
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 80001901, 1, 1, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=1, expiration=-1;

-- Super Skill: 物理训练2 (1300901)
-- Type: passive, MaxLevel: 10
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 1300901, 1, 10, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=10, expiration=-1;

-- Super Skill: 飞翔2 (10001901)
-- Type: newbie_level, MaxLevel: 1
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 10001901, 1, 1, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=1, expiration=-1;

-- Super Skill: 天马2 (20001901)
-- Type: newbie_level, MaxLevel: 10
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 20001901, 1, 10, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=10, expiration=-1;

-- Super Skill: 小龟龟 (20001903)
-- Type: newbie_level, MaxLevel: 1
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 20001903, 1, 1, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=1, expiration=-1;

-- Super Skill: 骑兽技能2 (80001902)
-- Type: newbie_level, MaxLevel: 1
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 80001902, 1, 1, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=1, expiration=-1;

-- Super Skill: 鳄鱼 (80001903)
-- Type: newbie_level, MaxLevel: 1
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 80001903, 1, 1, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=1, expiration=-1;

-- Super Skill: 鳄鱼3 (80001904)
-- Type: newbie_level, MaxLevel: 1
INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)
VALUES (13745, 80001904, 1, 1, -1)
ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel=1, expiration=-1;

-- ============================================================
-- ROLLBACK: Delete the above skills (uncomment if needed)
-- ============================================================
-- DELETE FROM skills WHERE characterid=13745 AND skillid=1301111;
-- DELETE FROM skills WHERE characterid=13745 AND skillid=1001555;
-- DELETE FROM skills WHERE characterid=13745 AND skillid=80001901;
-- DELETE FROM skills WHERE characterid=13745 AND skillid=1300901;
-- DELETE FROM skills WHERE characterid=13745 AND skillid=10001901;
-- DELETE FROM skills WHERE characterid=13745 AND skillid=20001901;
-- DELETE FROM skills WHERE characterid=13745 AND skillid=20001903;
-- DELETE FROM skills WHERE characterid=13745 AND skillid=80001902;
-- DELETE FROM skills WHERE characterid=13745 AND skillid=80001903;
-- DELETE FROM skills WHERE characterid=13745 AND skillid=80001904;

