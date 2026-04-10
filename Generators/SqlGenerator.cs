using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace SuperSkillTool
{
    /// <summary>
    /// Generates SQL INSERT statements for adding skills to the database.
    /// Output: output/insert_skill.sql
    /// </summary>
    public static class SqlGenerator
    {
        public static void Generate(List<SkillDefinition> skills, bool dryRun)
        {
            string outPath = Path.Combine(PathConfig.OutputDir, "insert_skill.sql");
            Console.WriteLine($"\n[SqlGenerator] Generating {outPath}");

            if (dryRun)
            {
                foreach (var sd in skills)
                    Console.WriteLine($"  [dry-run] Would generate SQL for {sd.SkillId} ({sd.Name})");
                return;
            }

            EnsureDir(outPath);

            var sb = new StringBuilder();
            sb.AppendLine("-- ============================================================");
            sb.AppendLine("-- Super Skill Tool - Auto-generated SQL");
            sb.AppendLine($"-- Generated: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
            sb.AppendLine($"-- Character ID: {PathConfig.DefaultCharacterId}");
            sb.AppendLine("-- ============================================================");
            sb.AppendLine();

            // Carrier skill (Super SP source)
            int totalSpNeeded = 0;
            foreach (var sd in skills) totalSpNeeded += sd.SuperSpCost * sd.MaxLevel;
            sb.AppendLine($"-- === Super SP Carrier Skill ({PathConfig.DefaultSuperSpCarrierSkillId}) ===");
            sb.AppendLine($"-- Total SP needed for all skills below: {totalSpNeeded}");
            sb.AppendLine($"INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)");
            sb.AppendLine($"VALUES ({PathConfig.DefaultCharacterId}, {PathConfig.DefaultSuperSpCarrierSkillId}, {totalSpNeeded}, {totalSpNeeded}, -1)");
            sb.AppendLine($"ON DUPLICATE KEY UPDATE skilllevel={totalSpNeeded}, masterlevel={totalSpNeeded}, expiration=-1;");
            sb.AppendLine();

            foreach (var sd in skills)
            {
                sb.AppendLine($"-- Super Skill: {sd.Name} ({sd.SkillId})");
                sb.AppendLine($"-- Type: {sd.Type}, MaxLevel: {sd.MaxLevel}");
                sb.AppendLine($"INSERT INTO skills (characterid, skillid, skilllevel, masterlevel, expiration)");
                sb.AppendLine($"VALUES ({PathConfig.DefaultCharacterId}, {sd.SkillId}, 1, {sd.MaxLevel}, -1)");
                sb.AppendLine($"ON DUPLICATE KEY UPDATE skilllevel=1, masterlevel={sd.MaxLevel}, expiration=-1;");
                sb.AppendLine();
            }

            // Also generate a block to remove skills if needed
            sb.AppendLine("-- ============================================================");
            sb.AppendLine("-- ROLLBACK: Delete the above skills (uncomment if needed)");
            sb.AppendLine("-- ============================================================");
            foreach (var sd in skills)
            {
                sb.AppendLine($"-- DELETE FROM skills WHERE characterid={PathConfig.DefaultCharacterId} AND skillid={sd.SkillId};");
            }
            sb.AppendLine();

            File.WriteAllText(outPath, sb.ToString(), new UTF8Encoding(false));
            Console.WriteLine($"  [saved] {outPath}");
            Console.WriteLine($"  Generated {skills.Count} INSERT statement(s).");
        }

        private static void EnsureDir(string filePath)
        {
            string dir = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);
        }
    }
}
