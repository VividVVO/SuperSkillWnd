using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace SuperSkillTool
{
    /// <summary>
    /// Generates a verification checklist: output/checklist.txt
    /// Lists every file modified/created and what to verify.
    /// </summary>
    public static class ChecklistGenerator
    {
        public static void Generate(List<SkillDefinition> skills, bool dryRun)
        {
            string outPath = Path.Combine(PathConfig.OutputDir, "checklist.txt");
            Console.WriteLine($"\n[ChecklistGenerator] Generating {outPath}");

            if (dryRun)
            {
                Console.WriteLine("  [dry-run] Would generate checklist");
                return;
            }

            EnsureDir(outPath);

            var sb = new StringBuilder();
            sb.AppendLine("================================================================");
            sb.AppendLine("  Super Skill Tool - Verification Checklist");
            sb.AppendLine($"  Generated: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
            sb.AppendLine("================================================================");
            sb.AppendLine();

            // List all skills
            sb.AppendLine("SKILLS ADDED:");
            sb.AppendLine("─────────────────────────────────────────");
            foreach (var sd in skills)
            {
                sb.AppendLine($"  [{sd.SkillId}] {sd.Name}");
                sb.AppendLine($"    Type: {sd.Type}, Tab: {sd.Tab}, MaxLevel: {sd.MaxLevel}");
                sb.AppendLine($"    InfoType: {sd.InfoType}, Action: {sd.Action}");
                if (sd.ProxySkillId > 0)
                    sb.AppendLine($"    ProxySkill: {sd.ProxySkillId}, Route: {sd.ReleaseType}");
                sb.AppendLine();
            }

            // Group jobs
            var jobIds = new HashSet<int>();
            foreach (var sd in skills) jobIds.Add(sd.JobId);

            sb.AppendLine();
            sb.AppendLine("FILES MODIFIED / CREATED:");
            sb.AppendLine("─────────────────────────────────────────");
            sb.AppendLine();

            // Server XML
            sb.AppendLine("[ ] Server Skill XML:");
            foreach (int jobId in jobIds)
                sb.AppendLine($"    [ ] {PathConfig.ServerSkillXml(jobId)}");
            sb.AppendLine();

            // Server String XML
            sb.AppendLine($"[ ] Server String XML:");
            sb.AppendLine($"    [ ] {PathConfig.ServerStringXml}");
            sb.AppendLine();

            // Config JSON
            sb.AppendLine($"[ ] super_skills.json:");
            sb.AppendLine($"    [ ] {PathConfig.SuperSkillsJson}");
            sb.AppendLine();

            sb.AppendLine($"[ ] custom_skill_routes.json:");
            sb.AppendLine($"    [ ] {PathConfig.CustomSkillRoutesJson}");
            sb.AppendLine();

            sb.AppendLine($"[ ] native_skill_injections.json:");
            sb.AppendLine($"    [ ] {PathConfig.NativeSkillInjectionsJson}");
            sb.AppendLine();

            sb.AppendLine($"[ ] super_skills_server.json:");
            sb.AppendLine($"    [ ] {PathConfig.SuperSkillsServerJson}");
            sb.AppendLine();

            // SQL
            sb.AppendLine($"[ ] SQL output:");
            sb.AppendLine($"    [ ] {Path.Combine(PathConfig.OutputDir, "insert_skill.sql")}");
            sb.AppendLine();

            // Verification steps
            sb.AppendLine();
            sb.AppendLine("VERIFICATION STEPS:");
            sb.AppendLine("─────────────────────────────────────────");
            sb.AppendLine();
            sb.AppendLine("1. Server-side:");
            sb.AppendLine("   [ ] Restart server after XML changes");
            sb.AppendLine("   [ ] Run insert_skill.sql on the database");
            sb.AppendLine("   [ ] Verify skill appears in @skill or admin commands");
            sb.AppendLine();
            sb.AppendLine("2. Client-side DLL:");
            sb.AppendLine("   [ ] Rebuild hook.dll (SuperSkillWnd build.bat)");
            sb.AppendLine("   [ ] Verify skill appears in overlay panel");
            sb.AppendLine("   [ ] Verify icon loads correctly");
            sb.AppendLine("   [ ] Verify skill name/desc display");
            sb.AppendLine();
            sb.AppendLine("3. Client-side .img (Harepacker):");
            sb.AppendLine("   [ ] See harepacker_guide.txt for import instructions");
            sb.AppendLine("   [ ] Import skill data into client Skill.wz");
            sb.AppendLine("   [ ] Verify icon displays in native skill window (if applicable)");
            sb.AppendLine();
            sb.AppendLine("4. In-game testing:");
            sb.AppendLine("   [ ] Open super skill panel");
            sb.AppendLine("   [ ] Verify skill is in correct tab");
            sb.AppendLine("   [ ] Test skill level up");
            sb.AppendLine("   [ ] Test skill use (drag to hotkey bar, press key)");
            sb.AppendLine("   [ ] Verify damage / buff / passive effect");
            sb.AppendLine();

            // Backup info
            sb.AppendLine();
            sb.AppendLine("BACKUP LOCATION:");
            sb.AppendLine("─────────────────────────────────────────");
            sb.AppendLine($"  {BackupHelper.GetSessionBackupDir()}");
            sb.AppendLine();

            File.WriteAllText(outPath, sb.ToString(), new UTF8Encoding(false));
            Console.WriteLine($"  [saved] {outPath}");
        }

        private static void EnsureDir(string filePath)
        {
            string dir = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);
        }
    }
}
