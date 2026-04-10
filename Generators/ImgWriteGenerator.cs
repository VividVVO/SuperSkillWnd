using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Threading;
using MapleLib.Helpers;
using MapleLib.WzLib;
using MapleLib.WzLib.Serializer;
using MapleLib.WzLib.Util;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    /// <summary>
    /// Writes skill nodes directly into game .img files using MapleLib.
    /// Also writes name/desc/h into String/Skill.img.
    /// Each skill gets a _superSkill=1 marker so we can identify our additions later.
    /// If a skill already exists, it is removed first (update = remove + re-add).
    /// Backs up the original .img before overwriting.
    /// </summary>
        public static class ImgWriteGenerator
        {
            public static void Generate(List<SkillDefinition> skills, bool dryRun)
            {
                // Keep IMG writes compatible with legacy/pre-BB client surface formats.
                ImageFormatDetector.UsePreBigBangImageFormats = true;

                GenerateSkillImgs(skills, dryRun);
                GenerateStringImg(skills, dryRun);
            }

        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        //  1. Skill .img files (icons, action, info, common)
        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

        private static void GenerateSkillImgs(List<SkillDefinition> skills, bool dryRun)
        {
            var byJob = new Dictionary<int, List<SkillDefinition>>();
            foreach (var sd in skills)
            {
                if (!byJob.ContainsKey(sd.JobId))
                    byJob[sd.JobId] = new List<SkillDefinition>();
                byJob[sd.JobId].Add(sd);
            }

            int carrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            var staleCarrierIds = CarrierSkillHelper.GetStaleCarrierIds();
            if (carrierId > 0)
            {
                int carrierJobId = carrierId / 10000;
                if (!byJob.ContainsKey(carrierJobId))
                    byJob[carrierJobId] = new List<SkillDefinition>();
            }
            foreach (int staleCarrierId in staleCarrierIds)
            {
                int staleJobId = staleCarrierId / 10000;
                if (!byJob.ContainsKey(staleJobId))
                    byJob[staleJobId] = new List<SkillDefinition>();
            }

            foreach (var kv in byJob)
            {
                int jobId = kv.Key;
                var list = kv.Value;
                string imgPath = PathConfig.GameSkillImg(jobId);

                Console.WriteLine($"\n[ImgWrite] Processing {imgPath}");

                if (!File.Exists(imgPath))
                {
                    if (dryRun)
                    {
                        Console.WriteLine($"  [dry-run] Would create missing {jobId}.img");
                        continue;
                    }

                    if (TryCreateEmptySkillImg(jobId, imgPath, out string createErr))
                    {
                        Console.WriteLine($"  [created] {jobId}.img created (empty skill root)");
                    }
                    else
                    {
                        Console.WriteLine($"  [error] Failed to create {jobId}.img: {createErr}");
                        continue;
                    }
                }

                if (dryRun)
                {
                    if (carrierId > 0 && carrierId / 10000 == jobId)
                        Console.WriteLine($"  [dry-run] Would ensure carrier skill {carrierId} in {jobId}.img");

                    foreach (var sd in list)
                        Console.WriteLine($"  [dry-run] Would write skill {sd.SkillId} into {jobId}.img");
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

                // Find or create "skill" sub-node
                WzSubProperty skillTop = null;
                var existingSkill = wzImg.GetFromPath("skill");
                if (existingSkill is WzSubProperty sp)
                    skillTop = sp;
                else if (existingSkill == null)
                {
                    skillTop = new WzSubProperty("skill");
                    wzImg.AddProperty(skillTop);
                }

                if (skillTop == null)
                {
                    Console.WriteLine($"  [error] Cannot find or create 'skill' node in {jobId}.img");
                    fs.Dispose(); wzImg.Dispose();
                    continue;
                }

                int count = 0;
                count += RemoveStaleCarrierSkillNodes(skillTop, jobId, staleCarrierIds, dryRun);

                if (carrierId > 0 && carrierId / 10000 == jobId)
                {
                    string carrierKey = carrierId.ToString();
                    var existingCarrier = skillTop[carrierKey];
                    if (existingCarrier is WzSubProperty carrierSub)
                    {
                        if (IsCarrierSkillNodeReady(carrierSub))
                        {
                            Console.WriteLine($"  [skip] Carrier skill {carrierKey} already exists");
                        }
                        else
                        {
                            EnsureCarrierSkillNode(carrierSub);
                            count++;
                            Console.WriteLine($"  [write] Ensure carrier skill {carrierKey}");
                        }
                    }
                    else if (existingCarrier != null)
                    {
                        RemoveProperty(skillTop, carrierKey);
                        skillTop.AddProperty(BuildCarrierSkillNode(carrierId));
                        count++;
                        Console.WriteLine($"  [replaced] Carrier skill {carrierKey}");
                    }
                    else
                    {
                        skillTop.AddProperty(BuildCarrierSkillNode(carrierId));
                        count++;
                        Console.WriteLine($"  [added] Carrier skill {carrierKey}");
                    }
                }

                foreach (var sd in list)
                {
                    string idStr = sd.SkillId.ToString();

                    var existing = skillTop[idStr];
                    if (existing is WzSubProperty existingSub)
                    {
                        // Merge mode: update fields on existing node, preserve everything else
                        MergeSkillNode(existingSub, sd);
                        count++;
                        Console.WriteLine($"  [merged] Skill {idStr} ({sd.Name})");
                    }
                    else if (existing != null)
                    {
                        // Existing but not a SubProperty, replace
                        RemoveProperty(skillTop, idStr);
                        bool cloned;
                        var skillNode = TryCloneSkillNodeFromSource(sd, jobId, skillTop, out cloned) ?? BuildSkillNode(sd);
                        skillTop.AddProperty(skillNode);
                        count++;
                        Console.WriteLine(cloned
                            ? $"  [replaced+cloned] Skill {idStr} ({sd.Name})"
                            : $"  [replaced] Skill {idStr} ({sd.Name})");
                    }
                    else
                    {
                        // New skill
                        bool cloned;
                        var skillNode = TryCloneSkillNodeFromSource(sd, jobId, skillTop, out cloned) ?? BuildSkillNode(sd);
                        skillTop.AddProperty(skillNode);
                        count++;
                        Console.WriteLine(cloned
                            ? $"  [added+cloned] Skill {idStr} ({sd.Name})"
                            : $"  [added] Skill {idStr} ({sd.Name})");
                    }
                }

                if (count == 0)
                {
                    fs.Dispose(); wzImg.Dispose();
                    continue;
                }

                wzImg.Changed = true;

                try
                {
                    // Serialize to memory FIRST (while source stream is still open),
                    // then close the source stream, then write the bytes to disk.
                    byte[] iv = WzTool.GetIvByMapleVersion(skillVersion);
                    var serializer = new WzImgSerializer(iv);
                    byte[] imgBytes = serializer.SerializeImage(wzImg);

                    // Now safe to close the source stream
                    fs.Dispose();
                    wzImg.Dispose();

                    WriteWithRetry(imgPath, imgBytes);
                    Console.WriteLine($"  [saved] {imgPath} ({count} skills written)");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  [error] Failed to save {imgPath}: {ex.Message}");
                    if (ex is IOException)
                        Console.WriteLine("  [hint] 请确认没有其他程序（如游戏客户端、Harepacker）正在使用此文件");
                    try { fs.Dispose(); } catch { }
                    try { wzImg.Dispose(); } catch { }
                }
            }
        }

        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        //  2. String/Skill.img (name, desc, h levels)
        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

        private static void GenerateStringImg(List<SkillDefinition> skills, bool dryRun)
        {
            string imgPath = PathConfig.GameStringSkillImg;
            Console.WriteLine($"\n[ImgWrite-String] Processing {imgPath}");

            if (!File.Exists(imgPath))
            {
                Console.WriteLine($"  [warn] String Skill.img not found: {imgPath}");
                return;
            }

            if (dryRun)
            {
                int dryCarrierId = PathConfig.DefaultSuperSpCarrierSkillId;
                if (dryCarrierId > 0)
                    Console.WriteLine($"  [dry-run] Would ensure carrier string entry {dryCarrierId}");

                foreach (var sd in skills)
                    Console.WriteLine($"  [dry-run] Would write string data for {sd.SkillId} ({sd.Name})");
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
            var staleCarrierIds = CarrierSkillHelper.GetStaleCarrierIds();
            count += RemoveStaleCarrierStringNodes(wzImg, staleCarrierIds, dryRun);

            int carrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            if (carrierId > 0)
            {
                string carrierKey = carrierId.ToString();
                var existingCarrier = wzImg[carrierKey];
                if (existingCarrier is WzSubProperty carrierSub)
                {
                    if (IsCarrierStringNodeReady(carrierSub))
                    {
                        Console.WriteLine($"  [skip] Carrier string entry {carrierKey} already exists");
                    }
                    else
                    {
                        EnsureCarrierStringNode(carrierSub);
                        count++;
                        Console.WriteLine($"  [write] Ensure carrier string entry {carrierKey}");
                    }
                }
                else if (existingCarrier != null)
                {
                    RemovePropertyFromImage(wzImg, carrierKey);
                    wzImg.AddProperty(BuildCarrierStringNode(carrierId));
                    count++;
                    Console.WriteLine($"  [replaced] Carrier string entry {carrierKey}");
                }
                else
                {
                    wzImg.AddProperty(BuildCarrierStringNode(carrierId));
                    count++;
                    Console.WriteLine($"  [added] Carrier string entry {carrierKey}");
                }
            }

            foreach (var sd in skills)
            {
                string idStr = sd.SkillId.ToString();

                var existing = wzImg[idStr];
                if (existing is WzSubProperty existingSub)
                {
                    // Merge mode: update string fields, preserve others
                    MergeStringNode(existingSub, sd);
                    count++;
                    Console.WriteLine($"  [merged] String entry {idStr} ({sd.Name})");
                }
                else if (existing != null)
                {
                    RemovePropertyFromImage(wzImg, idStr);
                    bool cloned;
                    var strNode = TryCloneStringNodeFromSource(sd, wzImg, out cloned) ?? BuildStringNode(sd);
                    wzImg.AddProperty(strNode);
                    count++;
                    Console.WriteLine(cloned
                        ? $"  [replaced+cloned] String entry {idStr} ({sd.Name})"
                        : $"  [replaced] String entry {idStr} ({sd.Name})");
                }
                else
                {
                    bool cloned;
                    var strNode = TryCloneStringNodeFromSource(sd, wzImg, out cloned) ?? BuildStringNode(sd);
                    wzImg.AddProperty(strNode);
                    count++;
                    Console.WriteLine(cloned
                        ? $"  [added+cloned] String entry {idStr} ({sd.Name})"
                        : $"  [added] String entry {idStr} ({sd.Name})");
                }
            }

            if (count == 0)
            {
                fs.Dispose(); wzImg.Dispose();
                return;
            }

            wzImg.Changed = true;

            try
            {
                // Serialize to memory FIRST (while source stream is still open),
                // then close the source stream, then write the bytes to disk.
                byte[] iv = WzTool.GetIvByMapleVersion(stringVersion);
                var serializer = new WzImgSerializer(iv);
                byte[] imgBytes = serializer.SerializeImage(wzImg);

                // Now safe to close the source stream
                fs.Dispose();
                wzImg.Dispose();

                WriteWithRetry(imgPath, imgBytes);
                Console.WriteLine($"  [saved] {imgPath} ({count} string entries written)");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [error] Failed to save {imgPath}: {ex.Message}");
                if (ex is IOException)
                    Console.WriteLine("  [hint] 请确认没有其他程序（如游戏客户端、Harepacker）正在使用此文件");
                try { fs.Dispose(); } catch { }
                try { wzImg.Dispose(); } catch { }
            }
        }

        private static int RemoveStaleCarrierSkillNodes(
            WzSubProperty skillTop,
            int jobId,
            HashSet<int> staleCarrierIds,
            bool dryRun)
        {
            if (skillTop == null || staleCarrierIds == null || staleCarrierIds.Count == 0)
                return 0;

            int removed = 0;
            foreach (int staleCarrierId in staleCarrierIds)
            {
                if (staleCarrierId / 10000 != jobId)
                    continue;

                string staleKey = staleCarrierId.ToString();
                if (skillTop[staleKey] == null)
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale carrier skill {staleKey}");
                }
                else
                {
                    RemoveProperty(skillTop, staleKey);
                    Console.WriteLine($"  [cleanup] Removed stale carrier skill {staleKey}");
                }
            }

            return removed;
        }

        private static int RemoveStaleCarrierStringNodes(
            WzImage wzImg,
            HashSet<int> staleCarrierIds,
            bool dryRun)
        {
            if (wzImg == null || staleCarrierIds == null || staleCarrierIds.Count == 0)
                return 0;

            int removed = 0;
            foreach (int staleCarrierId in staleCarrierIds)
            {
                string staleKey = staleCarrierId.ToString();
                if (wzImg[staleKey] == null)
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale carrier string entry {staleKey}");
                }
                else
                {
                    RemovePropertyFromImage(wzImg, staleKey);
                    Console.WriteLine($"  [cleanup] Removed stale carrier string entry {staleKey}");
                }
            }

            return removed;
        }

        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        //  Node builders
        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

        private static WzSubProperty BuildSkillNode(SkillDefinition sd)
        {
            var node = new WzSubProperty(sd.SkillId.ToString());

            // _superSkill marker
            node.AddProperty(new WzIntProperty("_superSkill", 1));

            // icon
            var iconCanvas = BuildCanvasFromBase64(sd.IconBase64, "icon");
            if (iconCanvas != null) node.AddProperty(iconCanvas);

            var moCanvas = BuildCanvasFromBase64(sd.IconMouseOverBase64, "iconMouseOver");
            if (moCanvas != null) node.AddProperty(moCanvas);

            var disCanvas = BuildCanvasFromBase64(sd.IconDisabledBase64, "iconDisabled");
            if (disCanvas != null) node.AddProperty(disCanvas);

            // action
            var actionProp = BuildActionProperty(sd.Action);
            if (actionProp != null)
                node.AddProperty(actionProp);

            // info sub-node
            var info = new WzSubProperty("info");
            info.AddProperty(new WzIntProperty("type", sd.InfoType));
            node.AddProperty(info);

            // common params:
            // only write when caller provided common fields.
            if (sd.Common != null && sd.Common.Count > 0)
            {
                var common = new WzSubProperty("common");
                common.AddProperty(new WzIntProperty("maxLevel", Math.Max(1, sd.MaxLevel)));
                foreach (var kv in sd.Common)
                {
                    if (string.Equals(kv.Key, "maxLevel", StringComparison.OrdinalIgnoreCase))
                        continue;
                    common.AddProperty(new WzStringProperty(kv.Key, kv.Value));
                }
                node.AddProperty(common);
            }

            // level sub-nodes (per-level data)
            if (sd.Levels != null && sd.Levels.Count > 0)
            {
                var levelNode = new WzSubProperty("level");
                foreach (var lv in sd.Levels)
                {
                    var lvSub = new WzSubProperty(lv.Key.ToString());
                    foreach (var p in lv.Value)
                    {
                        // Try to write as int if possible, otherwise string
                        int intVal;
                        if (int.TryParse(p.Value, out intVal))
                            lvSub.AddProperty(new WzIntProperty(p.Key, intVal));
                        else
                            lvSub.AddProperty(new WzStringProperty(p.Key, p.Value));
                    }
                    levelNode.AddProperty(lvSub);
                }
                node.AddProperty(levelNode);
            }

            // effect frames (effect/effect0... + repeat/repeat0...)
            var effectMap = GetEffectFramesByNode(sd);
            if (effectMap != null && effectMap.Count > 0)
            {
                foreach (var key in GetSortedEffectNodeNames(effectMap))
                {
                    if (!effectMap.TryGetValue(key, out var frames) || frames == null || frames.Count == 0)
                        continue;
                    var effectNode = BuildEffectNode(frames, key);
                    if (effectNode != null)
                        node.AddProperty(effectNode);
                }
            }

            return node;
        }

        private static WzSubProperty BuildStringNode(SkillDefinition sd)
        {
            var node = new WzSubProperty(sd.SkillId.ToString());

            // _superSkill marker
            node.AddProperty(new WzIntProperty("_superSkill", 1));

            // name
            if (!string.IsNullOrEmpty(sd.Name))
                node.AddProperty(new WzStringProperty("name", sd.Name));

            // desc
            if (!string.IsNullOrEmpty(sd.Desc))
                node.AddProperty(new WzStringProperty("desc", sd.Desc));

            // h levels
            if (sd.HLevels != null)
            {
                foreach (var kv in sd.HLevels)
                    node.AddProperty(new WzStringProperty(kv.Key, kv.Value));
            }

            return node;
        }

        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        //  Helpers
        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        //  Merge: update specific fields on existing nodes
        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

        /// <summary>
        /// Clone full source skill node so unknown/custom fields are preserved.
        /// </summary>
        private static WzSubProperty TryCloneSkillNodeFromSource(
            SkillDefinition sd,
            int targetJobId,
            WzSubProperty targetSkillTop,
            out bool cloned)
        {
            cloned = false;
            int sourceId = sd.ResolveCloneSourceSkillId();
            if (sourceId <= 0 || sourceId == sd.SkillId)
                return null;

            WzSubProperty sourceNode = null;
            int sourceJobId = sourceId / 10000;

            if (sourceJobId == targetJobId)
            {
                sourceNode = targetSkillTop[sourceId.ToString()] as WzSubProperty;
            }
            else
            {
                string sourceImgPath = PathConfig.GameSkillImg(sourceJobId);
                if (File.Exists(sourceImgPath))
                {
                    try
                    {
                        using (var sourceFs = new FileStream(sourceImgPath, FileMode.Open, FileAccess.Read, FileShare.Read))
                        {
                            WzMapleVersion sourceVersion = WzImageVersionHelper.DetectVersionForSkillImg(sourceImgPath);
                            var sourceImg = new WzImage(sourceJobId + ".img", sourceFs, sourceVersion);
                            if (sourceImg.ParseImage(true))
                            {
                                sourceNode = sourceImg.GetFromPath("skill/" + sourceId) as WzSubProperty;
                                if (sourceNode != null)
                                    sourceNode = (WzSubProperty)sourceNode.DeepClone();
                            }
                            sourceImg.Dispose();
                        }
                    }
                    catch
                    {
                        sourceNode = null;
                    }
                }
            }

            if (sourceNode == null)
                return null;

            WzSubProperty clone = (sourceJobId == targetJobId)
                ? (WzSubProperty)sourceNode.DeepClone()
                : sourceNode;
            clone.Name = sd.SkillId.ToString();
            MergeSkillNode(clone, sd);
            cloned = true;
            return clone;
        }

        /// <summary>
        /// Clone full source String/Skill.img entry so unknown/custom fields are preserved.
        /// </summary>
        private static WzSubProperty TryCloneStringNodeFromSource(
            SkillDefinition sd,
            WzImage stringImg,
            out bool cloned)
        {
            cloned = false;
            int sourceId = sd.ResolveCloneSourceSkillId();
            if (sourceId <= 0 || sourceId == sd.SkillId)
                return null;

            var sourceNode = stringImg[sourceId.ToString()] as WzSubProperty;
            if (sourceNode == null)
                return null;

            var clone = (WzSubProperty)sourceNode.DeepClone();
            clone.Name = sd.SkillId.ToString();
            MergeStringNode(clone, sd);
            cloned = true;
            return clone;
        }

        /// <summary>
        /// Merge our fields into an existing skill node without destroying level/effect/etc.
        /// Only updates: _superSkill, icon, iconMouseOver, iconDisabled, action, level, effect*.
        /// NOTE: To avoid accidental mutation of native skills, merge path no longer writes info/type and common/maxLevel.
        /// </summary>
        private static void MergeSkillNode(WzSubProperty node, SkillDefinition sd)
        {
            // _superSkill marker
            ReplaceOrAddProperty(node, new WzIntProperty("_superSkill", 1));

            // icons (only if we have data)
            if (!string.IsNullOrEmpty(sd.IconBase64))
            {
                var canvas = BuildCanvasFromBase64(sd.IconBase64, "icon");
                if (canvas != null) ReplaceOrAddProperty(node, canvas);
            }
            if (!string.IsNullOrEmpty(sd.IconMouseOverBase64))
            {
                var canvas = BuildCanvasFromBase64(sd.IconMouseOverBase64, "iconMouseOver");
                if (canvas != null) ReplaceOrAddProperty(node, canvas);
            }
            if (!string.IsNullOrEmpty(sd.IconDisabledBase64))
            {
                var canvas = BuildCanvasFromBase64(sd.IconDisabledBase64, "iconDisabled");
                if (canvas != null) ReplaceOrAddProperty(node, canvas);
            }

            // action
            if (!string.IsNullOrEmpty(sd.Action))
                MergeActionProperty(node, sd.Action);

            // Intentionally do NOT touch info/type or common/maxLevel in merge mode.
            // Existing native values should be preserved as-is.

            // level sub-nodes (merge per-level data, preserve existing levels we don't have)
            if (sd.Levels != null && sd.Levels.Count > 0)
            {
                var levelNode = node["level"] as WzSubProperty;
                if (levelNode == null)
                {
                    levelNode = new WzSubProperty("level");
                    node.AddProperty(levelNode);
                }
                foreach (var lv in sd.Levels)
                {
                    string lvName = lv.Key.ToString();
                    var existingLv = levelNode[lvName] as WzSubProperty;
                    if (existingLv == null)
                    {
                        existingLv = new WzSubProperty(lvName);
                        levelNode.AddProperty(existingLv);
                    }
                    foreach (var p in lv.Value)
                    {
                        int intVal;
                        if (int.TryParse(p.Value, out intVal))
                            ReplaceOrAddProperty(existingLv, new WzIntProperty(p.Key, intVal));
                        else
                            ReplaceOrAddProperty(existingLv, new WzStringProperty(p.Key, p.Value));
                    }
                }
            }

            // effect frames (replace all effect* nodes when we have cached effect data)
            var effectMap = GetEffectFramesByNode(sd);
            if (effectMap != null && effectMap.Count > 0)
            {
                RemoveEffectNodes(node);
                foreach (var key in GetSortedEffectNodeNames(effectMap))
                {
                    if (!effectMap.TryGetValue(key, out var frames) || frames == null || frames.Count == 0)
                        continue;
                    var effectNode = BuildEffectNode(frames, key);
                    if (effectNode != null)
                        node.AddProperty(effectNode);
                }
            }
        }

        /// <summary>
        /// Merge string fields into an existing string node.
        /// Only updates: _superSkill, name, desc, h levels.
        /// </summary>
        private static void MergeStringNode(WzSubProperty node, SkillDefinition sd)
        {
            ReplaceOrAddProperty(node, new WzIntProperty("_superSkill", 1));

            if (!string.IsNullOrEmpty(sd.Name))
                ReplaceOrAddProperty(node, new WzStringProperty("name", sd.Name));
            if (!string.IsNullOrEmpty(sd.Desc))
                ReplaceOrAddProperty(node, new WzStringProperty("desc", sd.Desc));
            // Super skill write-out should not keep/add pdesc/ph by default.
            RemoveProperty(node, "pdesc");
            RemoveProperty(node, "ph");
            if (sd.HLevels != null)
            {
                foreach (var kv in sd.HLevels)
                    ReplaceOrAddProperty(node, new WzStringProperty(kv.Key, kv.Value));
            }
        }

        private static WzSubProperty BuildCarrierSkillNode(int carrierId)
        {
            var node = new WzSubProperty(carrierId.ToString());
            EnsureCarrierSkillNode(node);
            return node;
        }

        private static void EnsureCarrierSkillNode(WzSubProperty node)
        {
            if (node == null)
                return;

            ReplaceOrAddProperty(node, new WzIntProperty("_superSkill", 1));
            RemoveProperty(node, "action");

            var info = node["info"] as WzSubProperty;
            if (info == null)
            {
                info = new WzSubProperty("info");
                node.AddProperty(info);
            }
            ReplaceOrAddProperty(info, new WzIntProperty("type", 50));

            var common = node["common"] as WzSubProperty;
            if (common == null)
            {
                common = new WzSubProperty("common");
                node.AddProperty(common);
            }
            int carrierMaxLevel = Math.Max(1, PathConfig.DefaultSuperSpCarrierMaxLevel);
            ReplaceOrAddProperty(common, new WzIntProperty("maxLevel", carrierMaxLevel));
        }

        private static bool IsCarrierSkillNodeReady(WzSubProperty node)
        {
            if (node == null)
                return false;

            var info = node["info"] as WzSubProperty;
            var common = node["common"] as WzSubProperty;
            int carrierMaxLevel = Math.Max(1, PathConfig.DefaultSuperSpCarrierMaxLevel);
            return HasIntValue(info, "type", 50)
                && HasIntValue(common, "maxLevel", carrierMaxLevel);
        }

        private static WzSubProperty BuildCarrierStringNode(int carrierId)
        {
            var node = new WzSubProperty(carrierId.ToString());
            EnsureCarrierStringNode(node);
            return node;
        }

        private static void EnsureCarrierStringNode(WzSubProperty node)
        {
            if (node == null)
                return;

            ReplaceOrAddProperty(node, new WzIntProperty("_superSkill", 1));
            ReplaceOrAddProperty(node, new WzStringProperty("name", "Super SP"));
        }

        private static bool IsCarrierStringNodeReady(WzSubProperty node)
        {
            if (node == null)
                return false;

            var name = node["name"] as WzStringProperty;
            return name != null && !string.IsNullOrEmpty(name.Value);
        }

        private static bool HasIntValue(WzSubProperty parent, string childName, int expected)
        {
            if (parent == null || string.IsNullOrEmpty(childName))
                return false;

            var child = parent[childName];
            if (child is WzIntProperty ip)
                return ip.Value == expected;
            if (child is WzShortProperty sp)
                return sp.Value == expected;
            if (child is WzLongProperty lp)
                return lp.Value == expected;
            if (child is WzStringProperty str && int.TryParse(str.Value, out int parsed))
                return parsed == expected;
            return false;
        }

        /// <summary>
        /// Replace a child property by name, or add it if it doesn't exist.
        /// </summary>
        private static void ReplaceOrAddProperty(WzSubProperty parent, WzImageProperty newProp)
        {
            if (parent == null || newProp == null)
                return;
            if (parent.WzProperties != null)
            {
                var old = parent[newProp.Name];
                if (old != null)
                {
                    parent.WzProperties.Remove(old);
                    try { old.Dispose(); } catch { }
                }
            }
            parent.AddProperty(newProp);
        }

        /// <summary>
        /// Replace a child property on canvas by name, or add it if it doesn't exist.
        /// </summary>
        private static void ReplaceOrAddProperty(WzCanvasProperty parent, WzImageProperty newProp)
        {
            if (parent == null || newProp == null)
                return;

            if (parent.WzProperties != null)
            {
                var old = parent[newProp.Name];
                if (old != null)
                {
                    parent.WzProperties.Remove(old);
                    try { old.Dispose(); } catch { }
                }
            }
            parent.AddProperty(newProp);
        }

        private static WzImageProperty BuildActionProperty(string action)
        {
            if (string.IsNullOrEmpty(action)) return null;

            // Most v83/v95 style skills use action/0.
            var actionNode = new WzSubProperty("action");
            actionNode.AddProperty(new WzStringProperty("0", action));
            return actionNode;
        }

        private static void MergeActionProperty(WzSubProperty node, string action)
        {
            var existingAction = node["action"];
            if (existingAction == null)
            {
                var newAction = BuildActionProperty(action);
                if (newAction != null) node.AddProperty(newAction);
                return;
            }

            if (existingAction is WzStringProperty)
            {
                var old = existingAction as WzStringProperty;
                if (string.Equals(old.Value ?? "", action ?? "", StringComparison.Ordinal))
                    return;
                ReplaceOrAddProperty(node, new WzStringProperty("action", action));
                return;
            }

            if (existingAction is WzSubProperty actionSub)
            {
                // Prefer replacing child "0"; otherwise replace the smallest numeric string child.
                WzStringProperty target = actionSub["0"] as WzStringProperty;
                if (target == null && actionSub.WzProperties != null)
                {
                    int bestIdx = int.MaxValue;
                    foreach (var child in actionSub.WzProperties)
                    {
                        if (child is WzStringProperty sp && int.TryParse(sp.Name, out int idx) && idx < bestIdx)
                        {
                            bestIdx = idx;
                            target = sp;
                        }
                    }
                }
                // Non-numeric style fallback: preserve existing first string child name.
                if (target == null && actionSub.WzProperties != null)
                {
                    foreach (var child in actionSub.WzProperties)
                    {
                        if (child is WzStringProperty only)
                        {
                            target = only;
                            break;
                        }
                    }
                }

                string childName = target?.Name ?? "0";
                if (target != null && string.Equals(target.Value ?? "", action ?? "", StringComparison.Ordinal))
                    return;
                ReplaceOrAddProperty(actionSub, new WzStringProperty(childName, action));
                return;
            }

            // Unknown action node type: rebuild to a safe default action/0 format.
            RemoveProperty(node, "action");
            var rebuilt = BuildActionProperty(action);
            if (rebuilt != null) node.AddProperty(rebuilt);
        }

        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        //  Helpers
        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

        private static Dictionary<string, List<WzEffectFrame>> GetEffectFramesByNode(SkillDefinition sd)
        {
            if (sd == null)
                return null;

            var result = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
            if (sd.CachedEffectsByNode != null && sd.CachedEffectsByNode.Count > 0)
            {
                foreach (var kv in sd.CachedEffectsByNode)
                {
                    if (string.IsNullOrWhiteSpace(kv.Key) || kv.Value == null || kv.Value.Count == 0)
                        continue;
                    result[kv.Key] = kv.Value;
                }
            }

            if (result.Count == 0 && sd.CachedEffects != null && sd.CachedEffects.Count > 0)
            {
                result["effect"] = sd.CachedEffects;
            }

            return result.Count > 0 ? result : null;
        }

        private static List<string> GetSortedEffectNodeNames(Dictionary<string, List<WzEffectFrame>> map)
        {
            var keys = new List<string>();
            if (map == null || map.Count == 0)
                return keys;

            foreach (var kv in map)
            {
                if (string.IsNullOrWhiteSpace(kv.Key) || kv.Value == null || kv.Value.Count == 0)
                    continue;
                keys.Add(kv.Key.Trim());
            }
            keys.Sort(CompareEffectNodeNames);
            return keys;
        }

        private static void RemoveEffectNodes(WzSubProperty node)
        {
            if (node?.WzProperties == null || node.WzProperties.Count == 0)
                return;

            var removeList = new List<WzImageProperty>();
            foreach (var child in node.WzProperties)
            {
                if (child != null && IsEffectNodeName(child.Name))
                    removeList.Add(child);
            }

            foreach (var item in removeList)
            {
                node.WzProperties.Remove(item);
                try { item.Dispose(); } catch { }
            }
        }

        private static bool IsEffectNodeName(string name)
        {
            if (string.IsNullOrWhiteSpace(name))
                return false;
            if (string.Equals(name, "effect", StringComparison.OrdinalIgnoreCase)
                || string.Equals(name, "repeat", StringComparison.OrdinalIgnoreCase))
                return true;
            return TryParseIndexedFrameNodeName(name, "effect", out _)
                || TryParseIndexedFrameNodeName(name, "repeat", out _);
        }

        private static int CompareEffectNodeNames(string a, string b)
        {
            if (string.Equals(a, b, StringComparison.OrdinalIgnoreCase))
                return 0;

            int aRank = GetFrameNodeSortRank(a, out int aIndex);
            int bRank = GetFrameNodeSortRank(b, out int bIndex);
            if (aRank != bRank)
                return aRank.CompareTo(bRank);
            if (aRank == 1 || aRank == 3)
                return aIndex.CompareTo(bIndex);

            return string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
        }

        private static int GetFrameNodeSortRank(string name, out int index)
        {
            index = int.MaxValue;
            if (string.Equals(name, "effect", StringComparison.OrdinalIgnoreCase))
                return 0;
            if (TryParseIndexedFrameNodeName(name, "effect", out index))
                return 1;
            if (string.Equals(name, "repeat", StringComparison.OrdinalIgnoreCase))
                return 2;
            if (TryParseIndexedFrameNodeName(name, "repeat", out index))
                return 3;
            return 4;
        }

        private static bool TryParseIndexedFrameNodeName(string name, string prefix, out int index)
        {
            index = -1;
            if (string.IsNullOrWhiteSpace(name) || string.IsNullOrWhiteSpace(prefix))
                return false;
            if (!name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                return false;
            string suffix = name.Substring(prefix.Length);
            if (string.IsNullOrEmpty(suffix))
                return false;
            return int.TryParse(suffix, out index);
        }

        private static WzCanvasProperty BuildCanvasFromBase64(string base64, string name)
        {
            if (string.IsNullOrEmpty(base64)) return null;

            Bitmap bmp;
            try
            {
                byte[] bytes = Convert.FromBase64String(base64);
                using (var ms = new MemoryStream(bytes))
                    bmp = new Bitmap(ms);
            }
            catch { return null; }

            var legacyReady = EnsureLegacyAlphaFor4444(bmp);
            if (!ReferenceEquals(legacyReady, bmp))
                bmp.Dispose();
            bmp = legacyReady;

            var canvas = new WzCanvasProperty(name);
            var pngProp = new WzPngProperty();
            pngProp.PNG = bmp;
            canvas.PngProperty = pngProp;
            canvas.AddProperty(new WzVectorProperty("origin", 0, bmp.Height));

            bmp.Dispose();
            return canvas;
        }

        /// <summary>
        /// Build an effect sub-node (effect/effect0/effect1...) from cached effect frames.
        /// Each frame becomes a numbered Canvas child (0, 1, 2...) with delay property.
        /// </summary>
        private static WzSubProperty BuildEffectNode(List<WzEffectFrame> frames, string nodeName)
        {
            if (frames == null || frames.Count == 0) return null;
            var effectNode = new WzSubProperty(string.IsNullOrWhiteSpace(nodeName) ? "effect" : nodeName);
            foreach (var ef in frames)
            {
                if (ef.Bitmap == null) continue;
                try
                {
                    var canvas = new WzCanvasProperty(ef.Index.ToString());
                    var pngProp = new WzPngProperty();
                    var frameBmp = new Bitmap(ef.Bitmap); // copy so original is not disturbed
                    var legacyReady = EnsureLegacyAlphaFor4444(frameBmp);
                    if (!ReferenceEquals(legacyReady, frameBmp))
                        frameBmp.Dispose();
                    frameBmp = legacyReady;

                    pngProp.PNG = frameBmp;
                    canvas.PngProperty = pngProp;
                    canvas.AddProperty(new WzIntProperty("delay", ef.Delay > 0 ? ef.Delay : 100));
                    AddEffectFrameVectors(canvas, ef);
                    AddEffectFrameProps(canvas, ef);
                    effectNode.AddProperty(canvas);

                    frameBmp.Dispose();
                }
                catch { }
            }
            return effectNode.WzProperties != null && effectNode.WzProperties.Count > 0 ? effectNode : null;
        }

        private static void AddEffectFrameVectors(WzCanvasProperty canvas, WzEffectFrame ef)
        {
            bool hasOrigin = false;

            if (ef?.Vectors != null && ef.Vectors.Count > 0)
            {
                // Keep common anchors in a stable order first.
                string[] preferred = new[] { "origin", "head", "vector", "border", "crosshair" };
                var written = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

                foreach (var key in preferred)
                {
                    if (TryAddVector(canvas, ef.Vectors, key))
                    {
                        written.Add(key);
                        if (string.Equals(key, "origin", StringComparison.OrdinalIgnoreCase))
                            hasOrigin = true;
                    }
                }

                foreach (var kv in ef.Vectors)
                {
                    if (string.IsNullOrEmpty(kv.Key) || kv.Value == null) continue;
                    if (written.Contains(kv.Key)) continue;

                    canvas.AddProperty(new WzVectorProperty(kv.Key, kv.Value.X, kv.Value.Y));
                    if (string.Equals(kv.Key, "origin", StringComparison.OrdinalIgnoreCase))
                        hasOrigin = true;
                }
            }

            if (!hasOrigin)
            {
                int originY = (ef != null && ef.Height > 0)
                    ? ef.Height
                    : (ef?.Bitmap?.Height ?? 0);
                canvas.AddProperty(new WzVectorProperty("origin", 0, originY));
            }
        }

        private static bool TryAddVector(WzCanvasProperty canvas, Dictionary<string, WzFrameVector> vectors, string key)
        {
            if (canvas == null || vectors == null || string.IsNullOrEmpty(key))
                return false;

            foreach (var kv in vectors)
            {
                if (!string.Equals(kv.Key, key, StringComparison.OrdinalIgnoreCase) || kv.Value == null)
                    continue;

                canvas.AddProperty(new WzVectorProperty(kv.Key, kv.Value.X, kv.Value.Y));
                return true;
            }
            return false;
        }

        private static void AddEffectFrameProps(WzCanvasProperty canvas, WzEffectFrame ef)
        {
            if (canvas == null || ef?.FrameProps == null || ef.FrameProps.Count == 0)
                return;

            foreach (var kv in ef.FrameProps)
            {
                if (string.IsNullOrEmpty(kv.Key)) continue;
                if (string.Equals(kv.Key, "delay", StringComparison.OrdinalIgnoreCase)) continue;
                if (string.Equals(kv.Key, "origin", StringComparison.OrdinalIgnoreCase)) continue;
                if (string.Equals(kv.Key, "head", StringComparison.OrdinalIgnoreCase)) continue;
                if (string.Equals(kv.Key, "vector", StringComparison.OrdinalIgnoreCase)) continue;
                if (string.Equals(kv.Key, "border", StringComparison.OrdinalIgnoreCase)) continue;
                if (string.Equals(kv.Key, "crosshair", StringComparison.OrdinalIgnoreCase)) continue;
                if (ef.Vectors != null && ef.Vectors.ContainsKey(kv.Key)) continue;

                var prop = BuildScalarProperty(kv.Key, kv.Value);
                if (prop != null)
                    canvas.AddProperty(prop);
            }
        }

        private static WzImageProperty BuildScalarProperty(string name, string value)
        {
            if (string.IsNullOrEmpty(name))
                return null;

            if (int.TryParse(value, out int iv))
                return new WzIntProperty(name, iv);
            if (long.TryParse(value, out long lv))
                return new WzLongProperty(name, lv);
            if (float.TryParse(value, out float fv))
                return new WzFloatProperty(name, fv);
            if (double.TryParse(value, out double dv))
                return new WzDoubleProperty(name, dv);
            return new WzStringProperty(name, value ?? "");
        }

        /// <summary>
        /// For legacy clients, avoid binary-alpha-only inputs that tend to get encoded as BGRA5551.
        /// Injecting a near-transparent pixel keeps visual output unchanged while favoring BGRA4444.
        /// </summary>
        private static Bitmap EnsureLegacyAlphaFor4444(Bitmap source)
        {
            if (source == null)
                return source;

            int candidateX = -1;
            int candidateY = -1;
            bool hasAlpha = false;
            bool hasPartialAlpha = false;

            for (int y = 0; y < source.Height; y++)
            {
                for (int x = 0; x < source.Width; x++)
                {
                    var c = source.GetPixel(x, y);
                    if (c.A < 255)
                    {
                        hasAlpha = true;
                        if (c.A > 0)
                        {
                            hasPartialAlpha = true;
                            break;
                        }
                        if (candidateX < 0)
                        {
                            candidateX = x;
                            candidateY = y;
                        }
                    }
                }

                if (hasPartialAlpha)
                    break;
            }

            if (!hasAlpha || hasPartialAlpha || candidateX < 0)
                return source;

            var clone = new Bitmap(source);
            var px = clone.GetPixel(candidateX, candidateY);
            clone.SetPixel(candidateX, candidateY, Color.FromArgb(1, px.R, px.G, px.B));
            return clone;
        }

        /// <summary>
        /// Remove a named child from a WzSubProperty by rebuilding without it.
        /// </summary>
        private static void RemoveProperty(WzSubProperty parent, string childName)
        {
            if (parent.WzProperties == null) return;
            var toRemove = parent[childName];
            if (toRemove != null)
            {
                parent.WzProperties.Remove(toRemove);
                try { toRemove.Dispose(); } catch { }
            }
        }

        /// <summary>
        /// Remove a named child from a WzImage.
        /// </summary>
        private static void RemovePropertyFromImage(WzImage img, string childName)
        {
            if (img.WzProperties == null) return;
            var toRemove = img[childName];
            if (toRemove != null)
            {
                img.WzProperties.Remove(toRemove);
                try { toRemove.Dispose(); } catch { }
            }
        }

        /// <summary>
        /// Write bytes to disk with retry on IOException (file lock).
        /// Retries up to 3 times with increasing delays.
        /// Falls back to writing a .new file next to the target if all retries fail.
        /// </summary>
        internal static void WriteWithRetry(string path, byte[] data)
        {
            string dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);

            const int maxRetries = 3;
            for (int attempt = 1; attempt <= maxRetries; attempt++)
            {
                try
                {
                    File.WriteAllBytes(path, data);
                    return; // success
                }
                catch (IOException) when (attempt < maxRetries)
                {
                    Console.WriteLine($"  [retry] 文件被占用，等待后重试 ({attempt}/{maxRetries})...");
                    Thread.Sleep(attempt * 500);
                }
                catch (IOException)
                {
                    // All retries exhausted 鈥?write to a .new file so data is not lost
                    string fallback = path + ".new";
                    File.WriteAllBytes(fallback, data);
                    Console.WriteLine($"  [fallback] 已保存到 {fallback}，请手动替换原文件");
                    throw; // re-throw so caller logs [error]
                }
            }
        }

        private static bool TryCreateEmptySkillImg(int jobId, string imgPath, out string error)
        {
            error = "";
            WzImage newImage = null;
            try
            {
                newImage = new WzImage(jobId + ".img");
                newImage.AddProperty(new WzSubProperty("skill"));
                newImage.Changed = true;

                WzMapleVersion preferredVersion = WzImageVersionHelper.DetectPreferredVersionFromGameData();
                byte[] iv = WzTool.GetIvByMapleVersion(preferredVersion);
                var serializer = new WzImgSerializer(iv);
                byte[] bytes = serializer.SerializeImage(newImage);
                WriteWithRetry(imgPath, bytes);
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
            finally
            {
                try { newImage?.Dispose(); } catch { }
            }
        }
    }
}

