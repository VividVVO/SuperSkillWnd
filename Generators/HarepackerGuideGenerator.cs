using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace SuperSkillTool
{
    /// <summary>
    /// Generates a Harepacker import guide for client-side .img editing.
    /// Output: output/harepacker_guide.txt
    /// </summary>
    public static class HarepackerGuideGenerator
    {
        public static void Generate(List<SkillDefinition> skills, bool dryRun)
        {
            string outPath = Path.Combine(PathConfig.OutputDir, "harepacker_guide.txt");
            Console.WriteLine($"\n[HarepackerGuide] Generating {outPath}");

            if (dryRun)
            {
                Console.WriteLine("  [dry-run] Would generate Harepacker guide");
                return;
            }

            EnsureDir(outPath);

            var sb = new StringBuilder();
            sb.AppendLine("================================================================");
            sb.AppendLine("  Harepacker Import Guide - Client .img Editing");
            sb.AppendLine($"  Generated: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
            sb.AppendLine("================================================================");
            sb.AppendLine();
            sb.AppendLine("This guide explains how to add the super skills to the client");
            sb.AppendLine("Skill.wz using Harepacker-resurrected.");
            sb.AppendLine();
            sb.AppendLine("NOTE: If the skills are only used through the overlay system");
            sb.AppendLine("(hideFromNativeSkillWnd=true), client .img editing is optional.");
            sb.AppendLine("The overlay reads from local JSON files instead.");
            sb.AppendLine();

            // Group by job
            var byJob = new Dictionary<int, List<SkillDefinition>>();
            foreach (var sd in skills)
            {
                if (!byJob.ContainsKey(sd.JobId))
                    byJob[sd.JobId] = new List<SkillDefinition>();
                byJob[sd.JobId].Add(sd);
            }

            int step = 1;

            sb.AppendLine("================================================================");
            sb.AppendLine("  STEP-BY-STEP INSTRUCTIONS");
            sb.AppendLine("================================================================");
            sb.AppendLine();

            sb.AppendLine($"{step++}. Open Harepacker-resurrected");
            sb.AppendLine($"   File -> Open -> Select your client's Skill.wz");
            sb.AppendLine();

            foreach (var kv in byJob)
            {
                int jobId = kv.Key;
                var list = kv.Value;

                sb.AppendLine($"{step++}. Navigate to Skill.wz / {jobId:D3}.img / skill");
                sb.AppendLine();

                foreach (var sd in list)
                {
                    sb.AppendLine($"   --- Adding Skill: {sd.Name} (ID: {sd.SkillId}) ---");
                    sb.AppendLine();
                    sb.AppendLine($"   a) Right-click on 'skill' -> Add -> SubProperty");
                    sb.AppendLine($"      Name: {sd.SkillId}");
                    sb.AppendLine();
                    sb.AppendLine($"   b) Under {sd.SkillId}, add three Canvas nodes:");
                    sb.AppendLine($"      - icon       (32x32 PNG, origin: 0,32)");
                    sb.AppendLine($"      - iconMouseOver (32x32 PNG, origin: 0,32)");
                    sb.AppendLine($"      - iconDisabled  (32x32 PNG, origin: 0,32)");
                    sb.AppendLine();
                    sb.AppendLine($"      For each canvas:");
                    sb.AppendLine($"        Right-click -> Add -> Canvas");
                    sb.AppendLine($"        Set name, then right-click canvas -> Change Image");
                    sb.AppendLine($"        Select your 32x32 icon PNG");
                    sb.AppendLine($"        Add a Vector child named 'origin' with x=0, y=32");
                    sb.AppendLine();

                    if (sd.Type == "newbie_level" && sd.Levels != null)
                    {
                        sb.AppendLine($"   c) Add SubProperty 'level' under {sd.SkillId}:");
                        foreach (var lvKv in sd.Levels)
                        {
                            sb.AppendLine($"      - SubProperty '{lvKv.Key}':");
                            foreach (var p in lvKv.Value)
                                sb.AppendLine($"        IntProperty '{p.Key}' = {p.Value}");
                        }
                        sb.AppendLine();
                    }
                    else
                    {
                        sb.AppendLine($"   c) Add SubProperty 'common' under {sd.SkillId}:");
                        sb.AppendLine($"      - IntProperty 'maxLevel' = {sd.MaxLevel}");
                        foreach (var ckv in sd.Common)
                        {
                            if (ckv.Key == "maxLevel") continue;
                            if (int.TryParse(ckv.Value, out int _))
                                sb.AppendLine($"      - IntProperty '{ckv.Key}' = {ckv.Value}");
                            else
                                sb.AppendLine($"      - StringProperty '{ckv.Key}' = {ckv.Value}");
                        }
                        sb.AppendLine();
                    }

                    sb.AppendLine($"   d) Add SubProperty 'info' under {sd.SkillId}:");
                    sb.AppendLine($"      - IntProperty 'type' = {sd.InfoType}");
                    sb.AppendLine();

                    if (!string.IsNullOrEmpty(sd.Action) && sd.InfoType != 50)
                    {
                        sb.AppendLine($"   e) Add SubProperty 'action' under {sd.SkillId}:");
                        sb.AppendLine($"      - StringProperty '0' = {sd.Action}");
                        sb.AppendLine();
                    }

                    sb.AppendLine();
                }
            }

            // String.wz
            sb.AppendLine($"{step++}. Now open String.wz -> Skill.img");
            sb.AppendLine();
            foreach (var sd in skills)
            {
                sb.AppendLine($"   Add SubProperty '{sd.SkillId}':");
                sb.AppendLine($"     - StringProperty 'name' = {sd.Name}");
                if (!string.IsNullOrEmpty(sd.Desc))
                    sb.AppendLine($"     - StringProperty 'desc' = {sd.Desc}");
                foreach (var kv in sd.HLevels)
                    sb.AppendLine($"     - StringProperty '{kv.Key}' = {kv.Value}");
                sb.AppendLine();
            }

            sb.AppendLine($"{step++}. Save all changes in Harepacker");
            sb.AppendLine($"   File -> Save");
            sb.AppendLine();

            sb.AppendLine($"{step++}. Test in client");
            sb.AppendLine($"   Launch the game and verify skill icons appear correctly.");
            sb.AppendLine();

            sb.AppendLine("================================================================");
            sb.AppendLine("  ICON FILES");
            sb.AppendLine("================================================================");
            sb.AppendLine();
            sb.AppendLine("Prepare 32x32 PNG icons for each skill:");
            foreach (var sd in skills)
            {
                string iconStatus = string.IsNullOrEmpty(sd.Icon) ? "(no icon specified - using placeholder)" : sd.Icon;
                sb.AppendLine($"  {sd.SkillId}: {iconStatus}");
            }
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
