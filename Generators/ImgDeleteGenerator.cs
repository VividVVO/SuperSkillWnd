using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using MapleLib.WzLib;
using MapleLib.WzLib.Serializer;
using MapleLib.WzLib.Util;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    /// <summary>
    /// Removes skill nodes from game .img files and String/Skill.img.
    /// Backs up before modifying. Only removes nodes that have the _superSkill marker.
    /// </summary>
    public static class ImgDeleteGenerator
    {
        public static void Delete(List<SkillDefinition> skills, bool dryRun)
        {
            DeleteFromSkillImgs(skills, dryRun);
            DeleteFromStringImg(skills, dryRun);
        }

        private static void DeleteFromSkillImgs(List<SkillDefinition> skills, bool dryRun)
        {
            var byJob = new Dictionary<int, List<SkillDefinition>>();
            foreach (var sd in skills)
            {
                if (!byJob.ContainsKey(sd.JobId))
                    byJob[sd.JobId] = new List<SkillDefinition>();
                byJob[sd.JobId].Add(sd);
            }

            foreach (var kv in byJob)
            {
                int jobId = kv.Key;
                var list = kv.Value;
                string imgPath = PathConfig.GameSkillImg(jobId);

                Console.WriteLine($"\n[ImgDelete] Processing {imgPath}");

                if (!File.Exists(imgPath))
                {
                    Console.WriteLine($"  [skip] .img file not found: {imgPath}");
                    continue;
                }

                if (dryRun)
                {
                    foreach (var sd in list)
                        Console.WriteLine($"  [dry-run] Would remove skill {sd.SkillId} from {jobId}.img");
                    continue;
                }

                BackupHelper.Backup(imgPath);

                WzImage wzImg;
                FileStream fs;
                WzMapleVersion skillVersion;
                try
                {
                    skillVersion = WzImageVersionHelper.DetectVersionForSkillImg(imgPath);
                    fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                    wzImg = new WzImage(jobId + ".img", fs, skillVersion);
                    if (!wzImg.ParseImage(true))
                    {
                        fs.Dispose(); wzImg.Dispose();
                        Console.WriteLine($"  [error] Failed to parse {imgPath}");
                        continue;
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  [error] Failed to open {imgPath}: {ex.Message}");
                    continue;
                }

                var skillTop = wzImg.GetFromPath("skill") as WzSubProperty;
                if (skillTop == null)
                {
                    Console.WriteLine($"  [skip] No 'skill' node in {jobId}.img");
                    fs.Dispose(); wzImg.Dispose();
                    continue;
                }

                int count = 0;
                foreach (var sd in list)
                {
                    string idStr = sd.SkillId.ToString();
                    var existing = skillTop[idStr];
                    if (existing == null)
                    {
                        Console.WriteLine($"  [skip] Skill {idStr} not found in {jobId}.img");
                        continue;
                    }
                    if (!HasSuperSkillMarker(existing))
                    {
                        Console.WriteLine($"  [skip] Skill {idStr} exists but has no _superSkill marker.");
                        continue;
                    }

                    skillTop.WzProperties.Remove(existing);
                    try { existing.Dispose(); } catch { }
                    count++;
                    Console.WriteLine($"  [removed] Skill {idStr} ({sd.Name})");
                }

                if (count == 0)
                {
                    fs.Dispose(); wzImg.Dispose();
                    continue;
                }

                wzImg.Changed = true;

                try
                {
                    byte[] iv = WzTool.GetIvByMapleVersion(skillVersion);
                    var serializer = new WzImgSerializer(iv);
                    byte[] imgBytes = serializer.SerializeImage(wzImg);
                    fs.Dispose();
                    wzImg.Dispose();
                    ImgWriteGenerator.WriteWithRetry(imgPath, imgBytes);
                    Console.WriteLine($"  [saved] {imgPath} ({count} skills removed)");
                }
                catch (IOException ex)
                {
                    Console.WriteLine($"  [warn] Failed to save {imgPath}: {ex.Message}");
                    Console.WriteLine("  [hint] 请确认没有其他程序（如游戏客户端、Harepacker）正在使用此文件");
                    try { fs.Dispose(); } catch { }
                    try { wzImg.Dispose(); } catch { }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  [error] Failed to save {imgPath}: {ex.Message}");
                    Console.WriteLine("  [hint] 请确认没有其他程序（如游戏客户端、Harepacker）正在使用此文件");
                    try { fs.Dispose(); } catch { }
                    try { wzImg.Dispose(); } catch { }
                }
            }
        }

        private static void DeleteFromStringImg(List<SkillDefinition> skills, bool dryRun)
        {
            string imgPath = PathConfig.GameStringSkillImg;
            Console.WriteLine($"\n[ImgDelete-String] Processing {imgPath}");

            if (!File.Exists(imgPath))
            {
                Console.WriteLine($"  [skip] String Skill.img not found: {imgPath}");
                return;
            }

            if (dryRun)
            {
                foreach (var sd in skills)
                    Console.WriteLine($"  [dry-run] Would remove string entry {sd.SkillId} ({sd.Name})");
                return;
            }

            BackupHelper.Backup(imgPath);

            WzImage wzImg;
            FileStream fs;
            WzMapleVersion stringVersion;
            try
            {
                stringVersion = WzImageVersionHelper.DetectVersionForStringImg(imgPath);
                fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                wzImg = new WzImage("Skill.img", fs, stringVersion);
                if (!wzImg.ParseImage(true))
                {
                    fs.Dispose(); wzImg.Dispose();
                    Console.WriteLine($"  [error] Failed to parse {imgPath}");
                    return;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [error] Failed to open {imgPath}: {ex.Message}");
                return;
            }

            int count = 0;
            foreach (var sd in skills)
            {
                string idStr = sd.SkillId.ToString();
                var existing = wzImg[idStr];
                if (existing == null)
                {
                    Console.WriteLine($"  [skip] String entry {idStr} not found");
                    continue;
                }
                if (!HasSuperSkillMarker(existing))
                {
                    Console.WriteLine($"  [skip] String entry {idStr} exists but has no _superSkill marker.");
                    continue;
                }

                wzImg.WzProperties.Remove(existing);
                try { existing.Dispose(); } catch { }
                count++;
                Console.WriteLine($"  [removed] String entry {idStr} ({sd.Name})");
            }

            if (count == 0)
            {
                fs.Dispose(); wzImg.Dispose();
                return;
            }

            wzImg.Changed = true;

            try
            {
                byte[] iv = WzTool.GetIvByMapleVersion(stringVersion);
                var serializer = new WzImgSerializer(iv);
                byte[] imgBytes = serializer.SerializeImage(wzImg);
                fs.Dispose();
                wzImg.Dispose();
                ImgWriteGenerator.WriteWithRetry(imgPath, imgBytes);
                Console.WriteLine($"  [saved] {imgPath} ({count} string entries removed)");
            }
            catch (IOException ex)
            {
                Console.WriteLine($"  [warn] Failed to save {imgPath}: {ex.Message}");
                Console.WriteLine("  [hint] 请确认没有其他程序（如游戏客户端、Harepacker）正在使用此文件");
                try { fs.Dispose(); } catch { }
                try { wzImg.Dispose(); } catch { }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [error] Failed to save {imgPath}: {ex.Message}");
                Console.WriteLine("  [hint] 请确认没有其他程序（如游戏客户端、Harepacker）正在使用此文件");
                try { fs.Dispose(); } catch { }
                try { wzImg.Dispose(); } catch { }
            }
        }

        private static bool HasSuperSkillMarker(WzImageProperty node)
        {
            if (node == null) return false;
            var marker = node["_superSkill"];
            if (marker is WzIntProperty ip) return ip.Value == 1;
            if (marker is WzStringProperty sp && int.TryParse(sp.Value, out int sv)) return sv == 1;
            return marker != null;
        }
    }
}

