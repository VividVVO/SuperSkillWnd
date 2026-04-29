using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Linq;
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
                string imgName = Path.GetFileName(imgPath);

                Console.WriteLine($"\n[ImgWrite] Processing {imgPath}");

                if (!File.Exists(imgPath))
                {
                    if (dryRun)
                    {
                        Console.WriteLine($"  [dry-run] Would create missing {imgName}");
                        continue;
                    }

                    if (TryCreateEmptySkillImg(jobId, imgPath, out string createErr))
                    {
                        Console.WriteLine($"  [created] {Path.GetFileName(imgPath)} created (empty skill root)");
                    }
                    else
                    {
                        Console.WriteLine($"  [error] Failed to create {imgName}: {createErr}");
                        continue;
                    }
                }

                if (dryRun)
                {
                    if (carrierId > 0 && carrierId / 10000 == jobId)
                        Console.WriteLine($"  [dry-run] Would ensure carrier skill {PathConfig.SkillKey(carrierId)} in {imgName}");

                    foreach (var sd in list)
                        Console.WriteLine($"  [dry-run] Would write skill {PathConfig.SkillKey(sd.SkillId)} into {imgName}");
                    continue;
                }

                BackupHelper.Backup(imgPath);

                WzImage wzImg;
                FileStream fs;
                WzMapleVersion skillVersion;
                try
                {
                    skillVersion = WzImageVersionHelper.DetectVersionForSkillImg(imgPath);
                    Console.WriteLine($"  [version] {jobId}.img detected as {skillVersion}");
                    fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                    wzImg = new WzImage(Path.GetFileName(imgPath), fs, skillVersion);
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
                    string carrierKey = PathConfig.SkillKey(carrierId);
                    string existingCarrierKey = FindSkillPropertyKey(skillTop, carrierId);
                    var existingCarrier = existingCarrierKey != null ? skillTop[existingCarrierKey] : null;
                    if (existingCarrier is WzSubProperty carrierSub)
                    {
                        carrierSub.Name = carrierKey;
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
                        RemoveProperty(skillTop, existingCarrierKey);
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
                    string idStr = PathConfig.SkillKey(sd.SkillId);
                    int cloneSourceId = sd.ResolveCloneSourceSkillId();

                    string existingKey = FindSkillPropertyKey(skillTop, sd.SkillId);
                    var existing = existingKey != null ? skillTop[existingKey] : null;
                    if (ShouldProtectNativeSkillWrite(sd, existing))
                    {
                        Console.WriteLine($"  [protect] Skip native skill {idStr} ({sd.Name})");
                        continue;
                    }
                    bool markAsSuperSkill = ShouldMarkAsSuperSkill(sd, existing);
                    bool forceCloneReplace = cloneSourceId > 0 && cloneSourceId != sd.SkillId;
                    bool applyCloneOverlay = !sd.PreserveClonedNode;
                    bool hasCachedTree = sd.HasManualTreeOverride && sd.CachedTree != null;
                    string cloneModeTag = $" [src={cloneSourceId}, mode={(applyCloneOverlay ? "auto" : "raw")}]";

                    if (hasCachedTree)
                    {
                        var treeNode = BuildSkillNode(sd, markAsSuperSkill, existing as WzSubProperty);
                        if (treeNode != null)
                        {
                            if (existing != null)
                                RemoveProperty(skillTop, existingKey);
                            skillTop.AddProperty(treeNode);
                            count++;
                            Console.WriteLine(existing != null
                                ? $"  [replaced+tree] Skill {idStr} ({sd.Name})"
                                : $"  [added+tree] Skill {idStr} ({sd.Name})");
                            continue;
                        }
                    }

                    if (forceCloneReplace)
                    {
                        bool clonedForced;
                        var forcedNode = TryCloneSkillNodeFromSource(sd, jobId, skillTop, out clonedForced, markAsSuperSkill, applyCloneOverlay);
                        if (forcedNode != null)
                        {
                            if (existing != null)
                                RemoveProperty(skillTop, existingKey);
                            skillTop.AddProperty(forcedNode);
                            count++;
                            if (existing != null)
                                Console.WriteLine(clonedForced
                                    ? $"  [replaced+cloned] Skill {idStr} ({sd.Name}){cloneModeTag}"
                                    : $"  [replaced] Skill {idStr} ({sd.Name})");
                            else
                                Console.WriteLine(clonedForced
                                    ? $"  [added+cloned] Skill {idStr} ({sd.Name}){cloneModeTag}"
                                    : $"  [added] Skill {idStr} ({sd.Name})");
                            continue;
                        }
                    }

                    if (existing is WzSubProperty existingSub)
                    {
                        // Merge mode: update fields on existing node, preserve everything else
                        existingSub.Name = idStr;
                        MergeSkillNode(existingSub, sd, markAsSuperSkill);
                        count++;
                        Console.WriteLine($"  [merged] Skill {idStr} ({sd.Name})");
                    }
                    else if (existing != null)
                    {
                        // Existing but not a SubProperty, replace
                        RemoveProperty(skillTop, existingKey);
                        bool cloned;
                        var skillNode = TryCloneSkillNodeFromSource(sd, jobId, skillTop, out cloned, markAsSuperSkill, applyCloneOverlay) ?? BuildSkillNode(sd, markAsSuperSkill);
                        skillTop.AddProperty(skillNode);
                        count++;
                        Console.WriteLine(cloned
                            ? $"  [replaced+cloned] Skill {idStr} ({sd.Name}){cloneModeTag}"
                            : $"  [replaced] Skill {idStr} ({sd.Name})");
                    }
                    else
                    {
                        // New skill
                        bool cloned;
                        var skillNode = TryCloneSkillNodeFromSource(sd, jobId, skillTop, out cloned, markAsSuperSkill, applyCloneOverlay) ?? BuildSkillNode(sd, markAsSuperSkill);
                        skillTop.AddProperty(skillNode);
                        count++;
                        Console.WriteLine(cloned
                            ? $"  [added+cloned] Skill {idStr} ({sd.Name}){cloneModeTag}"
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
                    Console.WriteLine($"  [dry-run] Would ensure carrier string entry {PathConfig.SkillKey(dryCarrierId)}");

                foreach (var sd in skills)
                    Console.WriteLine($"  [dry-run] Would write string data for {PathConfig.SkillKey(sd.SkillId)} ({sd.Name})");
                return;
            }

            BackupHelper.Backup(imgPath);

            WzImage wzImg;
            FileStream fs;
            WzMapleVersion stringVersion;
            try
            {
                stringVersion = WzImageVersionHelper.DetectVersionForStringImg(imgPath);
                Console.WriteLine($"  [version] Skill.img (String) detected as {stringVersion}");
                fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
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
                string carrierKey = PathConfig.SkillKey(carrierId);
                string existingCarrierKey = FindImagePropertyKey(wzImg, carrierId);
                var existingCarrier = existingCarrierKey != null ? wzImg[existingCarrierKey] : null;
                if (existingCarrier is WzSubProperty carrierSub)
                {
                    carrierSub.Name = carrierKey;
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
                    RemovePropertyFromImage(wzImg, existingCarrierKey);
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
                string idStr = PathConfig.SkillKey(sd.SkillId);

                string existingKey = FindImagePropertyKey(wzImg, sd.SkillId);
                var existing = existingKey != null ? wzImg[existingKey] : null;
                if (ShouldProtectNativeStringWrite(sd, existing))
                {
                    Console.WriteLine($"  [protect] Skip native string entry {idStr} ({sd.Name})");
                    continue;
                }
                bool markAsSuperSkill = ShouldMarkAsSuperSkill(sd, existing);
                bool forceCloneReplace = sd.ResolveCloneSourceSkillId() > 0
                    && sd.ResolveCloneSourceSkillId() != sd.SkillId;

                if (forceCloneReplace)
                {
                    bool clonedForced;
                    var forcedNode = TryCloneStringNodeFromSource(sd, wzImg, out clonedForced, markAsSuperSkill);
                    if (forcedNode != null)
                    {
                        if (existing != null)
                            RemovePropertyFromImage(wzImg, existingKey);
                        wzImg.AddProperty(forcedNode);
                        count++;
                        if (existing != null)
                            Console.WriteLine(clonedForced
                                ? $"  [replaced+cloned] String entry {idStr} ({sd.Name})"
                                : $"  [replaced] String entry {idStr} ({sd.Name})");
                        else
                            Console.WriteLine(clonedForced
                                ? $"  [added+cloned] String entry {idStr} ({sd.Name})"
                                : $"  [added] String entry {idStr} ({sd.Name})");
                        continue;
                    }
                }

                if (existing is WzSubProperty existingSub)
                {
                    // Merge mode: update string fields, preserve others
                    existingSub.Name = idStr;
                    MergeStringNode(existingSub, sd, markAsSuperSkill);
                    count++;
                    Console.WriteLine($"  [merged] String entry {idStr} ({sd.Name})");
                }
                else if (existing != null)
                {
                    RemovePropertyFromImage(wzImg, existingKey);
                    bool cloned;
                    var strNode = TryCloneStringNodeFromSource(sd, wzImg, out cloned, markAsSuperSkill) ?? BuildStringNode(sd);
                    wzImg.AddProperty(strNode);
                    count++;
                    Console.WriteLine(cloned
                        ? $"  [replaced+cloned] String entry {idStr} ({sd.Name})"
                        : $"  [replaced] String entry {idStr} ({sd.Name})");
                }
                else
                {
                    bool cloned;
                    var strNode = TryCloneStringNodeFromSource(sd, wzImg, out cloned, markAsSuperSkill) ?? BuildStringNode(sd);
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

                string staleKey = FindSkillPropertyKey(skillTop, staleCarrierId);
                if (staleKey == null)
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale carrier skill {PathConfig.SkillKey(staleCarrierId)}");
                }
                else
                {
                    RemoveProperty(skillTop, staleKey);
                    Console.WriteLine($"  [cleanup] Removed stale carrier skill {PathConfig.SkillKey(staleCarrierId)}");
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
                string staleKey = FindImagePropertyKey(wzImg, staleCarrierId);
                if (staleKey == null)
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale carrier string entry {PathConfig.SkillKey(staleCarrierId)}");
                }
                else
                {
                    RemovePropertyFromImage(wzImg, staleKey);
                    Console.WriteLine($"  [cleanup] Removed stale carrier string entry {PathConfig.SkillKey(staleCarrierId)}");
                }
            }

            return removed;
        }

        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        //  Node builders
        // 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

        private static WzSubProperty BuildSkillNode(SkillDefinition sd, bool markAsSuperSkill, WzSubProperty existingSource = null)
        {
            bool fromCachedTree = sd?.HasManualTreeOverride == true && sd.CachedTree != null;
            var node = BuildSubPropertyFromTree(fromCachedTree ? sd.CachedTree : null, existingSource);
            if (node == null)
            {
                fromCachedTree = false;
                node = new WzSubProperty(PathConfig.SkillKey(sd.SkillId));
            }
            node.Name = PathConfig.SkillKey(sd.SkillId);

            ApplySuperSkillMarker(node, markAsSuperSkill);

            // icon
            var iconCanvas = BuildCanvasFromBase64(sd.IconBase64, "icon");
            if (iconCanvas != null) ReplaceOrAddProperty(node, iconCanvas);

            var moCanvas = BuildCanvasFromBase64(sd.IconMouseOverBase64, "iconMouseOver");
            if (moCanvas != null) ReplaceOrAddProperty(node, moCanvas);

            var disCanvas = BuildCanvasFromBase64(sd.IconDisabledBase64, "iconDisabled");
            if (disCanvas != null) ReplaceOrAddProperty(node, disCanvas);

            // action
            var actionProp = BuildActionProperty(sd.Action);
            if (actionProp != null)
                ReplaceOrAddProperty(node, actionProp);

            // info sub-node
            var info = EnsureSubProperty(node, "info");
            var infoMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["type"] = sd.InfoType.ToString()
            };
            MergeSubPropertyScalars(info, infoMap);

            // common params:
            // only write when caller provided common fields.
            if (sd.Common != null && sd.Common.Count > 0)
            {
                var common = EnsureSubProperty(node, "common");
                var commonMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    ["maxLevel"] = Math.Max(1, sd.MaxLevel).ToString()
                };
                foreach (var kv in sd.Common)
                {
                    if (string.Equals(kv.Key, "maxLevel", StringComparison.OrdinalIgnoreCase))
                        continue;
                    commonMap[kv.Key] = kv.Value;
                }
                OverwriteSubPropertyScalars(common, commonMap);
            }

            // level sub-nodes (per-level data)
            if (sd.Levels != null && sd.Levels.Count > 0)
            {
                var existingLevelNode = node["level"] as WzSubProperty;
                var levelNode = fromCachedTree && existingLevelNode != null
                    ? (WzSubProperty)existingLevelNode.DeepClone()
                    : new WzSubProperty("level");
                levelNode.Name = "level";
                foreach (var lv in sd.Levels)
                {
                    string lvName = lv.Key.ToString();
                    var lvSub = levelNode[lvName] as WzSubProperty;
                    bool isNewLevelSub = lvSub == null;
                    if (lvSub == null)
                        lvSub = new WzSubProperty(lvName);
                    lvSub.Name = lvName;
                    OverwriteSubPropertyScalars(lvSub, lv.Value);

                    // Per-level animation frames (ball/hit/effect/prepare/keydown/repeat)
                    if (sd.LevelAnimFramesByNode != null
                        && sd.LevelAnimFramesByNode.TryGetValue(lv.Key, out var levelAnimNodes)
                        && levelAnimNodes != null && levelAnimNodes.Count > 0)
                    {
                        RemoveEffectNodes(lvSub);
                        foreach (var animKey in GetSortedEffectNodeNames(levelAnimNodes))
                        {
                            if (!levelAnimNodes.TryGetValue(animKey, out var animFrames)
                                || animFrames == null || animFrames.Count == 0)
                                continue;

                            // slash-based sub-group (hit/N, effect/N, ball/N, tile/N, etc.)
                            if (animKey.Contains("/"))
                            {
                                AddSubGroupToParent(lvSub, animKey, animFrames);
                            }
                            else
                            {
                                var animNode = BuildEffectNode(animFrames, animKey);
                                if (animNode != null)
                                    ReplaceOrAddProperty(lvSub, animNode);
                            }
                        }
                    }

                    if (isNewLevelSub)
                        levelNode.AddProperty(lvSub);
                }
                ReplaceOrAddProperty(node, levelNode);
            }

            // effect/animation frames (effect/effect0/repeat/ball/hit/prepare/keydown...)
            var effectMap = GetEffectFramesByNode(sd);
            if (effectMap != null && effectMap.Count > 0)
            {
                foreach (var key in GetSortedEffectNodeNames(effectMap))
                {
                    if (!effectMap.TryGetValue(key, out var frames) || frames == null || frames.Count == 0)
                        continue;

                    // slash-based sub-group (hit/N, effect/N, ball/N, tile/N, etc.)
                    if (key.Contains("/"))
                    {
                        AddSubGroupToParent(node, key, frames);
                    }
                    else
                    {
                        var effectNode = BuildEffectNode(frames, key);
                        if (effectNode != null)
                            ReplaceOrAddProperty(node, effectNode);
                    }
                }
            }

            return node;
        }

        private static WzSubProperty BuildStringNode(SkillDefinition sd)
        {
            var node = new WzSubProperty(PathConfig.SkillKey(sd.SkillId));

            // _superSkill marker
            node.AddProperty(new WzIntProperty("_superSkill", 1));

            // name
            if (!string.IsNullOrEmpty(sd.Name))
                node.AddProperty(new WzStringProperty("name", sd.Name));

            // desc
            if (!string.IsNullOrEmpty(sd.Desc))
                node.AddProperty(new WzStringProperty("desc", sd.Desc));

            // pdesc
            if (!string.IsNullOrEmpty(sd.PDesc))
                node.AddProperty(new WzStringProperty("pdesc", sd.PDesc));

            // ph
            if (!string.IsNullOrEmpty(sd.Ph))
                node.AddProperty(new WzStringProperty("ph", sd.Ph));

            // h template text (contains #mpCon, #damage etc. placeholders)
            if (!string.IsNullOrEmpty(sd.H))
                node.AddProperty(new WzStringProperty("h", sd.H));

            // h levels (h1, h2, h3...)
            if (sd.HLevels != null)
            {
                foreach (var kv in sd.HLevels)
                    node.AddProperty(new WzStringProperty(kv.Key, kv.Value));
            }

            return node;
        }

        private static WzSubProperty BuildSubPropertyFromTree(WzNodeInfo root, WzSubProperty existingSource)
        {
            if (root == null)
                return null;

            var prop = BuildPropertyFromNode(root, existingSource);
            if (prop is WzSubProperty sub)
            {
                sub.Name = string.IsNullOrWhiteSpace(root.Name) ? sub.Name : root.Name;
                return sub;
            }

            try { prop?.Dispose(); } catch { }
            return null;
        }

        private static WzImageProperty BuildPropertyFromNode(WzNodeInfo node, WzImageProperty existingSource)
        {
            if (node == null || string.IsNullOrWhiteSpace(node.Name))
                return null;

            string type = (node.TypeName ?? "").Trim();
            bool isCustom = string.Equals(type, "自定义", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "Custom", StringComparison.OrdinalIgnoreCase);

            if (string.Equals(type, "ImgDir", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "Image", StringComparison.OrdinalIgnoreCase))
            {
                return null;
            }

            if (string.Equals(type, "SubProperty", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "Convex", StringComparison.OrdinalIgnoreCase)
                || (isCustom && node.Children != null && node.Children.Count > 0))
            {
                if (!string.Equals(type, "Convex", StringComparison.OrdinalIgnoreCase)
                    && (node.Children == null || node.Children.Count == 0)
                    && !string.IsNullOrWhiteSpace(node.Value))
                {
                    return BuildTreeScalarProperty(node.Name, node.Value);
                }

                WzImageProperty sub = string.Equals(type, "Convex", StringComparison.OrdinalIgnoreCase)
                    ? new WzConvexProperty(node.Name)
                    : new WzSubProperty(node.Name);
                AddBuiltChildren(sub, node, existingSource);
                return sub;
            }

            if (string.Equals(type, "Canvas", StringComparison.OrdinalIgnoreCase))
            {
                WzCanvasProperty canvas;
                bool hasCanvasPayload = (node.CanvasCompressedBytes != null && node.CanvasCompressedBytes.Length > 0)
                    || node.CanvasBitmap != null
                    || node.CanvasWidth > 0
                    || node.CanvasHeight > 0;

                if (!hasCanvasPayload && existingSource is WzCanvasProperty existingCanvas)
                {
                    canvas = (WzCanvasProperty)existingCanvas.DeepClone();
                    canvas.Name = node.Name;
                    if (node.Children != null && node.Children.Count > 0)
                    {
                        ClearPropertyChildren(canvas);
                        AddBuiltChildren(canvas, node, existingCanvas);
                    }
                    return canvas;
                }

                canvas = new WzCanvasProperty(node.Name)
                {
                    PngProperty = BuildPngProperty(node)
                };
                AddBuiltChildren(canvas, node, existingSource);
                return canvas;
            }

            if (string.Equals(type, "Vector", StringComparison.OrdinalIgnoreCase))
            {
                TryParseVectorValue(node.Value, out int x, out int y);
                return new WzVectorProperty(node.Name, x, y);
            }

            if (string.Equals(type, "UOL", StringComparison.OrdinalIgnoreCase))
            {
                string value = node.Value ?? "";
                if (value.StartsWith("-> ", StringComparison.Ordinal))
                    value = value.Substring(3);
                return new WzUOLProperty(node.Name, value);
            }

            if (string.Equals(type, "Null", StringComparison.OrdinalIgnoreCase))
                return new WzNullProperty(node.Name);
            if (string.Equals(type, "Short", StringComparison.OrdinalIgnoreCase)
                && short.TryParse(node.Value, out short s16))
                return new WzShortProperty(node.Name, s16);
            if (string.Equals(type, "Int", StringComparison.OrdinalIgnoreCase)
                && int.TryParse(node.Value, out int i32))
                return new WzIntProperty(node.Name, i32);
            if (string.Equals(type, "Long", StringComparison.OrdinalIgnoreCase)
                && long.TryParse(node.Value, out long i64))
                return new WzLongProperty(node.Name, i64);
            if (string.Equals(type, "Float", StringComparison.OrdinalIgnoreCase)
                && float.TryParse(node.Value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float f32))
                return new WzFloatProperty(node.Name, f32);
            if (string.Equals(type, "Double", StringComparison.OrdinalIgnoreCase)
                && double.TryParse(node.Value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double f64))
                return new WzDoubleProperty(node.Name, f64);
            if (string.Equals(type, "String", StringComparison.OrdinalIgnoreCase))
                return new WzStringProperty(node.Name, node.Value ?? "");

            if (node.Children != null && node.Children.Count > 0)
            {
                var sub = new WzSubProperty(node.Name);
                AddBuiltChildren(sub, node, existingSource);
                return sub;
            }

            return BuildScalarProperty(node.Name, node.Value ?? "");
        }

        private static WzPngProperty BuildPngProperty(WzNodeInfo node)
        {
            var png = new WzPngProperty();
            if (node.CanvasCompressedBytes != null
                && node.CanvasCompressedBytes.Length > 0
                && node.CanvasWidth > 0
                && node.CanvasHeight > 0
                && node.CanvasPngFormat > 0)
            {
                png.SetCompressedBytes(
                    (byte[])node.CanvasCompressedBytes.Clone(),
                    node.CanvasWidth,
                    node.CanvasHeight,
                    (WzPngFormat)node.CanvasPngFormat);
                return png;
            }

            Bitmap source = node.CanvasBitmap;
            if (source == null)
            {
                ParseCanvasSize(node, out int width, out int height);
                width = Math.Max(width, 1);
                height = Math.Max(height, 1);
                using (var blank = new Bitmap(width, height, PixelFormat.Format32bppArgb))
                    png.SetBitmapBgra4444(blank);
                return png;
            }

            using (var bmp = new Bitmap(source))
                png.SetBitmapBgra4444(bmp);
            return png;
        }

        private static void AddBuiltChildren(WzImageProperty parent, WzNodeInfo node, WzImageProperty existingSource)
        {
            if (parent == null || node?.Children == null)
                return;

            foreach (var child in node.Children)
            {
                var prop = BuildPropertyFromNode(child, FindChildProperty(existingSource, child?.Name));
                if (prop == null)
                    continue;

                switch (parent)
                {
                    case WzSubProperty sub:
                        sub.AddProperty(prop);
                        break;
                    case WzCanvasProperty canvas:
                        canvas.AddProperty(prop);
                        break;
                    case WzConvexProperty convex:
                        convex.AddProperty(prop);
                        break;
                }
            }
        }

        private static WzImageProperty FindChildProperty(WzImageProperty parent, string childName)
        {
            if (parent?.WzProperties == null || string.IsNullOrWhiteSpace(childName))
                return null;

            foreach (var child in parent.WzProperties)
            {
                if (child != null && string.Equals(child.Name, childName, StringComparison.OrdinalIgnoreCase))
                    return child;
            }
            return null;
        }

        private static void ClearPropertyChildren(WzImageProperty parent)
        {
            if (parent?.WzProperties == null || parent.WzProperties.Count == 0)
                return;

            var removeList = new List<WzImageProperty>(parent.WzProperties);
            foreach (var child in removeList)
            {
                parent.WzProperties.Remove(child);
                try { child.Dispose(); } catch { }
            }
        }

        private static WzSubProperty EnsureSubProperty(WzSubProperty parent, string childName)
        {
            if (parent == null || string.IsNullOrWhiteSpace(childName))
                return null;

            if (parent[childName] is WzSubProperty existing)
                return existing;

            var sub = new WzSubProperty(childName);
            ReplaceOrAddProperty(parent, sub);
            return sub;
        }

        private static void OverwriteSubPropertyScalars(WzSubProperty target, Dictionary<string, string> map)
        {
            if (target == null)
                return;

            var desired = map != null
                ? new HashSet<string>(map.Keys.Where(k => !string.IsNullOrWhiteSpace(k)), StringComparer.OrdinalIgnoreCase)
                : new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var remove = new List<WzImageProperty>();
            foreach (var p in target.WzProperties)
            {
                if (p is WzIntProperty || p is WzShortProperty || p is WzLongProperty
                    || p is WzFloatProperty || p is WzDoubleProperty || p is WzStringProperty
                    || p is WzUOLProperty)
                {
                    if (map == null || !desired.Contains(p.Name))
                        remove.Add(p);
                }
            }

            foreach (var p in remove)
            {
                target.WzProperties.Remove(p);
                try { p.Dispose(); } catch { }
            }

            if (map == null)
                return;

            foreach (var kv in map)
            {
                var scalar = BuildScalarProperty(kv.Key, kv.Value ?? "");
                if (scalar != null)
                    ReplaceOrAddProperty(target, scalar);
            }
        }

        private static void MergeSubPropertyScalars(WzSubProperty target, Dictionary<string, string> map)
        {
            if (target == null || map == null)
                return;

            foreach (var kv in map)
            {
                var scalar = BuildScalarProperty(kv.Key, kv.Value ?? "");
                if (scalar != null)
                    ReplaceOrAddProperty(target, scalar);
            }
        }

        private static void MergeMissingSubPropertyScalars(WzSubProperty target, Dictionary<string, string> map)
        {
            if (target == null || map == null)
                return;

            foreach (var kv in map)
            {
                if (string.IsNullOrWhiteSpace(kv.Key) || FindChildProperty(target, kv.Key) != null)
                    continue;

                var scalar = BuildScalarProperty(kv.Key, kv.Value ?? "");
                if (scalar != null)
                    target.AddProperty(scalar);
            }
        }

        private static void ParseCanvasSize(WzNodeInfo node, out int width, out int height)
        {
            width = node?.CanvasWidth ?? 0;
            height = node?.CanvasHeight ?? 0;
            string text = node?.Value ?? "";
            if (width > 0 && height > 0)
                return;

            string[] parts = text.Split(new[] { 'x', 'X', ',', ' ' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length >= 2)
            {
                int.TryParse(parts[0], out width);
                int.TryParse(parts[1], out height);
            }
        }

        private static bool TryParseVectorValue(string value, out int x, out int y)
        {
            x = 0;
            y = 0;
            string[] parts = (value ?? "").Split(new[] { ',', ' ', ';', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length < 2)
                return false;

            bool okX = int.TryParse(parts[0], out x);
            bool okY = int.TryParse(parts[1], out y);
            return okX && okY;
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
            out bool cloned,
            bool markAsSuperSkill,
            bool applyOverlay)
        {
            cloned = false;
            int sourceId = sd.ResolveCloneSourceSkillId();
            if (sourceId <= 0 || sourceId == sd.SkillId)
                return null;

            WzSubProperty sourceNode = null;
            int sourceJobId = sourceId / 10000;

            if (sourceJobId == targetJobId)
            {
                string sourceKey = FindSkillPropertyKey(targetSkillTop, sourceId);
                sourceNode = sourceKey != null ? targetSkillTop[sourceKey] as WzSubProperty : null;
            }
            else
            {
                string sourceImgPath = PathConfig.GameSkillImg(sourceJobId);
                if (File.Exists(sourceImgPath))
                {
                    try
                    {
                        using (var sourceFs = new FileStream(sourceImgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                        {
                            WzMapleVersion sourceVersion = WzImageVersionHelper.DetectVersionForSkillImg(sourceImgPath);
                            var sourceImg = new WzImage(Path.GetFileName(sourceImgPath), sourceFs, sourceVersion);
                            if (sourceImg.ParseImage(true))
                            {
                                sourceNode = FindSkillProperty(sourceImg, sourceId) as WzSubProperty;
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
            bool shouldApplyOverlay = applyOverlay && ShouldApplyCloneOverlay(sourceNode, sd);
            clone.Name = PathConfig.SkillKey(sd.SkillId);
            if (shouldApplyOverlay)
                MergeSkillNode(clone, sd, markAsSuperSkill, overwriteCoreParams: true);
            else
                ApplySuperSkillMarker(clone, markAsSuperSkill);
            cloned = true;
            return clone;
        }

        private static bool ShouldApplyCloneOverlay(WzSubProperty sourceNode, SkillDefinition sd)
        {
            if (sourceNode == null || sd == null)
                return true;

            if (sd.HasManualTreeOverride || sd.HasManualEffectOverride)
                return true;
            if (sd.Levels != null && sd.Levels.Count > 0)
                return true;
            if (HasLevelAnimationOverride(sd))
                return true;

            if (!CanvasMatchesBase64(sourceNode["icon"], sd.IconBase64, "icon"))
                return true;
            if (!CanvasMatchesBase64(sourceNode["iconMouseOver"], sd.IconMouseOverBase64, "iconMouseOver"))
                return true;
            if (!CanvasMatchesBase64(sourceNode["iconDisabled"], sd.IconDisabledBase64, "iconDisabled"))
                return true;

            if (!string.IsNullOrEmpty(sd.Action)
                && !string.Equals(GetActionValue(sourceNode["action"]), sd.Action, StringComparison.Ordinal))
            {
                return true;
            }

            if (sd.InfoType > 0
                && !ScalarPropertyMatches(sourceNode.GetFromPath("info/type"), sd.InfoType.ToString(CultureInfo.InvariantCulture)))
            {
                return true;
            }

            if (sd.MaxLevel > 0
                && !ScalarPropertyMatches(sourceNode.GetFromPath("common/maxLevel"), Math.Max(1, sd.MaxLevel).ToString(CultureInfo.InvariantCulture)))
            {
                return true;
            }

            if (sd.Common != null)
            {
                var common = sourceNode["common"] as WzSubProperty;
                foreach (var kv in sd.Common)
                {
                    if (string.IsNullOrWhiteSpace(kv.Key))
                        continue;
                    if (string.Equals(kv.Key, "maxLevel", StringComparison.OrdinalIgnoreCase))
                        continue;
                    if (!ScalarPropertyMatches(common?[kv.Key], kv.Value ?? ""))
                        return true;
                }
            }

            return false;
        }

        private static bool HasLevelAnimationOverride(SkillDefinition sd)
        {
            if (sd?.LevelAnimFramesByNode == null || sd.LevelAnimFramesByNode.Count == 0)
                return false;

            foreach (var levelKv in sd.LevelAnimFramesByNode)
            {
                if (levelKv.Value == null)
                    continue;
                foreach (var nodeKv in levelKv.Value)
                {
                    if (nodeKv.Value != null && nodeKv.Value.Count > 0)
                        return true;
                }
            }
            return false;
        }

        private static bool CanvasMatchesBase64(WzImageProperty existing, string base64, string name)
        {
            if (string.IsNullOrEmpty(base64))
                return true;

            var existingCanvas = existing as WzCanvasProperty;
            if (existingCanvas == null)
                return false;

            var incoming = BuildCanvasFromBase64(base64, name);
            if (incoming == null)
                return false;

            try
            {
                return AreCanvasPixelsEqual(existingCanvas, incoming);
            }
            finally
            {
                try { incoming.Dispose(); } catch { }
            }
        }

        private static bool ScalarPropertyMatches(WzImageProperty prop, string expected)
        {
            if (prop == null)
                return string.IsNullOrWhiteSpace(expected);
            return string.Equals(
                GetScalarText(prop).Trim(),
                (expected ?? "").Trim(),
                StringComparison.Ordinal);
        }

        private static string GetActionValue(WzImageProperty actionProp)
        {
            if (actionProp is WzStringProperty actionString)
                return actionString.Value ?? "";

            if (actionProp is WzSubProperty actionSub)
            {
                var zero = actionSub["0"] as WzStringProperty;
                if (zero != null)
                    return zero.Value ?? "";

                if (actionSub.WzProperties != null)
                {
                    foreach (var child in actionSub.WzProperties)
                    {
                        if (child is WzStringProperty str)
                            return str.Value ?? "";
                    }
                }
            }

            return "";
        }

        private static string GetScalarText(WzImageProperty prop)
        {
            switch (prop)
            {
                case null:
                    return "";
                case WzIntProperty p:
                    return p.Value.ToString(CultureInfo.InvariantCulture);
                case WzShortProperty p:
                    return p.Value.ToString(CultureInfo.InvariantCulture);
                case WzLongProperty p:
                    return p.Value.ToString(CultureInfo.InvariantCulture);
                case WzFloatProperty p:
                    return p.Value.ToString("G", CultureInfo.InvariantCulture);
                case WzDoubleProperty p:
                    return p.Value.ToString("G", CultureInfo.InvariantCulture);
                case WzStringProperty p:
                    return p.Value ?? "";
                case WzUOLProperty p:
                    return p.Value ?? "";
                case WzVectorProperty p:
                    return string.Format(CultureInfo.InvariantCulture, "{0},{1}", p.X.Value, p.Y.Value);
                default:
                    return "";
            }
        }

        /// <summary>
        /// Clone full source String/Skill.img entry so unknown/custom fields are preserved.
        /// </summary>
        private static WzSubProperty TryCloneStringNodeFromSource(
            SkillDefinition sd,
            WzImage stringImg,
            out bool cloned,
            bool markAsSuperSkill)
        {
            cloned = false;
            int sourceId = sd.ResolveCloneSourceSkillId();
            if (sourceId <= 0 || sourceId == sd.SkillId)
                return null;

            string sourceKey = FindImagePropertyKey(stringImg, sourceId);
            var sourceNode = sourceKey != null ? stringImg[sourceKey] as WzSubProperty : null;
            if (sourceNode == null)
                return null;

            var clone = (WzSubProperty)sourceNode.DeepClone();
            clone.Name = PathConfig.SkillKey(sd.SkillId);
            MergeStringNode(clone, sd, markAsSuperSkill);
            cloned = true;
            return clone;
        }

        /// <summary>
        /// Merge our fields into an existing skill node without destroying level/effect/etc.
        /// Only updates: _superSkill, icon, iconMouseOver, iconDisabled, action, level, effect*.
        /// For donor-clone overlay we may also opt-in to writing info/type and common/maxLevel.
        /// </summary>
        private static void MergeSkillNode(WzSubProperty node, SkillDefinition sd, bool markAsSuperSkill, bool overwriteCoreParams = false)
        {
            // _superSkill marker:
            // - custom/new/super skill: keep marker for safe delete
            // - native overwrite: do not mark, avoid accidental native deletion later
            ApplySuperSkillMarker(node, markAsSuperSkill);

            // icons (only if we have data)
            if (!string.IsNullOrEmpty(sd.IconBase64))
            {
                MergeIconCanvas(node, "icon", sd.IconBase64);
            }
            if (!string.IsNullOrEmpty(sd.IconMouseOverBase64))
            {
                MergeIconCanvas(node, "iconMouseOver", sd.IconMouseOverBase64);
            }
            if (!string.IsNullOrEmpty(sd.IconDisabledBase64))
            {
                MergeIconCanvas(node, "iconDisabled", sd.IconDisabledBase64);
            }

            // action
            if (!string.IsNullOrEmpty(sd.Action))
                MergeActionProperty(node, sd.Action);

            if (overwriteCoreParams)
            {
                if (sd.InfoType > 0)
                {
                    var info = EnsureSubProperty(node, "info");
                    var infoMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                    {
                        ["type"] = sd.InfoType.ToString()
                    };
                    MergeSubPropertyScalars(info, infoMap);
                }

                if (sd.MaxLevel > 0 || (sd.Common != null && sd.Common.Count > 0))
                {
                    var common = EnsureSubProperty(node, "common");
                    var commonMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                    if (sd.MaxLevel > 0)
                        commonMap["maxLevel"] = Math.Max(1, sd.MaxLevel).ToString();
                    if (sd.Common != null)
                    {
                        foreach (var kv in sd.Common)
                        {
                            if (string.Equals(kv.Key, "maxLevel", StringComparison.OrdinalIgnoreCase))
                                continue;
                            commonMap[kv.Key] = kv.Value;
                        }
                    }
                    MergeSubPropertyScalars(common, commonMap);
                }
            }
            else
            {
                // Intentionally do NOT touch info/type or common/maxLevel in native merge mode.
                // Existing native values should be preserved as-is.
            }

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
        /// Only updates: _superSkill, name, desc, h, h levels.
        /// </summary>
        private static void MergeStringNode(WzSubProperty node, SkillDefinition sd, bool markAsSuperSkill)
        {
            if (markAsSuperSkill)
                ReplaceOrAddProperty(node, new WzIntProperty("_superSkill", 1));
            else
                RemoveProperty(node, "_superSkill");

            if (!string.IsNullOrEmpty(sd.Name))
                ReplaceOrAddProperty(node, new WzStringProperty("name", sd.Name));
            if (!string.IsNullOrEmpty(sd.Desc))
                ReplaceOrAddProperty(node, new WzStringProperty("desc", sd.Desc));
            if (!string.IsNullOrEmpty(sd.H))
                ReplaceOrAddProperty(node, new WzStringProperty("h", sd.H));
            else
                RemoveProperty(node, "h");
            // Super skill write-out should not keep/add pdesc/ph by default.
            RemoveProperty(node, "pdesc");
            RemoveProperty(node, "ph");
            RemoveMissingStringLevelDescriptions(node, sd.HLevels);
            if (sd.HLevels != null)
            {
                foreach (var kv in sd.HLevels)
                    ReplaceOrAddProperty(node, new WzStringProperty(kv.Key, kv.Value));
            }
        }

        private static WzSubProperty BuildCarrierSkillNode(int carrierId)
        {
            var node = new WzSubProperty(PathConfig.SkillKey(carrierId));
            EnsureCarrierSkillNode(node);
            return node;
        }

        private static void EnsureCarrierSkillNode(WzSubProperty node)
        {
            if (node == null)
                return;

            PruneCarrierSkillTopLevel(node);
            ReplaceOrAddProperty(node, new WzIntProperty("_superSkill", 1));
            ReplaceOrAddProperty(node, new WzIntProperty("invisible", 1));

            EnsureCarrierCommonTemplate(node);
            EnsureCarrierIconTemplate(node, "icon");
            EnsureCarrierIconTemplate(node, "iconMouseOver");
            EnsureCarrierIconTemplate(node, "iconDisabled");
        }

        private static bool IsCarrierSkillNodeReady(WzSubProperty node)
        {
            if (node == null)
                return false;

            int carrierMaxLevel = Math.Max(1, PathConfig.DefaultSuperSpCarrierMaxLevel);
            var common = node["common"] as WzSubProperty;
            bool hasCommonMaxLevel = common != null && HasIntValue(common, "maxLevel", carrierMaxLevel);
            bool hasIcons = node["icon"] is WzCanvasProperty
                && node["iconMouseOver"] is WzCanvasProperty
                && node["iconDisabled"] is WzCanvasProperty;
            return HasIntValue(node, "_superSkill", 1)
                && HasIntValue(node, "invisible", 1)
                && hasCommonMaxLevel
                && hasIcons;
        }

        private static WzSubProperty BuildCarrierStringNode(int carrierId)
        {
            var node = new WzSubProperty(PathConfig.SkillKey(carrierId));
            EnsureCarrierStringNode(node);
            return node;
        }

        private static void EnsureCarrierStringNode(WzSubProperty node)
        {
            if (node == null)
                return;

            ReplaceOrAddProperty(node, new WzIntProperty("_superSkill", 1));
            ReplaceOrAddProperty(node, new WzStringProperty("name", "Super SP"));
            ReplaceOrAddProperty(node, new WzStringProperty("desc", "超级SP载体技能。"));
            ReplaceOrAddProperty(node, new WzStringProperty("h1", "仅用于承载超级SP，不在技能栏显示。"));
            RemoveProperty(node, "h");
            RemoveProperty(node, "pdesc");
            RemoveProperty(node, "ph");
            RemoveCarrierExtraHLevels(node);
        }

        private static bool IsCarrierStringNodeReady(WzSubProperty node)
        {
            if (node == null)
                return false;

            var name = node["name"] as WzStringProperty;
            var desc = node["desc"] as WzStringProperty;
            var h1 = node["h1"] as WzStringProperty;
            return name != null && !string.IsNullOrEmpty(name.Value)
                && desc != null && !string.IsNullOrEmpty(desc.Value)
                && h1 != null && !string.IsNullOrEmpty(h1.Value);
        }

        private static void EnsureCarrierCommonTemplate(WzSubProperty node)
        {
            if (node == null)
                return;

            int carrierMaxLevel = Math.Max(1, PathConfig.DefaultSuperSpCarrierMaxLevel);

            var common = node["common"] as WzSubProperty;
            if (common == null)
            {
                RemoveProperty(node, "common");
                common = new WzSubProperty("common");
                node.AddProperty(common);
            }

            var remove = new List<WzImageProperty>();
            foreach (var child in common.WzProperties)
            {
                if (child == null)
                    continue;
                if (!string.Equals(child.Name, "maxLevel", StringComparison.OrdinalIgnoreCase))
                    remove.Add(child);
            }
            foreach (var child in remove)
            {
                common.WzProperties.Remove(child);
                try { child.Dispose(); } catch { }
            }

            ReplaceOrAddProperty(common, new WzIntProperty("maxLevel", carrierMaxLevel));
        }

        private static void EnsureCarrierIconTemplate(WzSubProperty node, string iconName)
        {
            if (node == null || string.IsNullOrWhiteSpace(iconName))
                return;

            var canvas = node[iconName] as WzCanvasProperty;
            if (canvas == null)
            {
                RemoveProperty(node, iconName);
                canvas = BuildBlankCarrierIconCanvas(iconName);
                if (canvas != null)
                    node.AddProperty(canvas);
                return;
            }

            ReplaceOrAddProperty(canvas, new WzVectorProperty("origin", 0, 32));
            ReplaceOrAddProperty(canvas, new WzIntProperty("z", 0));
        }

        private static WzCanvasProperty BuildBlankCarrierIconCanvas(string name)
        {
            using (var bmp = new Bitmap(32, 32, PixelFormat.Format32bppArgb))
            {
                using (var g = Graphics.FromImage(bmp))
                    g.Clear(Color.Transparent);

                var legacyReady = EnsureLegacyAlphaFor4444(bmp);
                var encoded = ReferenceEquals(legacyReady, bmp) ? new Bitmap(bmp) : legacyReady;

                var canvas = new WzCanvasProperty(name);
                var pngProp = new WzPngProperty();
                pngProp.SetBitmapBgra4444(encoded);
                canvas.PngProperty = pngProp;
                ReplaceOrAddProperty(canvas, new WzVectorProperty("origin", 0, 32));
                ReplaceOrAddProperty(canvas, new WzIntProperty("z", 0));

                if (!ReferenceEquals(encoded, bmp))
                    encoded.Dispose();
                return canvas;
            }
        }

        private static void PruneCarrierSkillTopLevel(WzSubProperty node)
        {
            if (node?.WzProperties == null)
                return;

            var keep = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
            {
                "_superSkill",
                "icon",
                "iconMouseOver",
                "iconDisabled",
                "common",
                "invisible"
            };

            var remove = new List<WzImageProperty>();
            foreach (var child in node.WzProperties)
            {
                if (child == null || string.IsNullOrEmpty(child.Name))
                    continue;
                if (!keep.Contains(child.Name))
                    remove.Add(child);
            }

            foreach (var child in remove)
            {
                node.WzProperties.Remove(child);
                try { child.Dispose(); } catch { }
            }
        }

        private static void RemoveCarrierExtraHLevels(WzSubProperty node)
        {
            if (node?.WzProperties == null)
                return;

            var remove = new List<WzImageProperty>();
            foreach (var child in node.WzProperties)
            {
                if (child == null || string.IsNullOrEmpty(child.Name))
                    continue;
                if (!child.Name.StartsWith("h", StringComparison.OrdinalIgnoreCase))
                    continue;
                if (string.Equals(child.Name, "h1", StringComparison.OrdinalIgnoreCase))
                    continue;
                if (child.Name.Length > 1 && int.TryParse(child.Name.Substring(1), out _))
                    remove.Add(child);
            }

            foreach (var child in remove)
            {
                node.WzProperties.Remove(child);
                try { child.Dispose(); } catch { }
            }
        }

        private static void RemoveMissingStringLevelDescriptions(WzSubProperty node, Dictionary<string, string> desiredHLevels)
        {
            if (node?.WzProperties == null)
                return;

            var keep = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            if (desiredHLevels != null)
            {
                foreach (var kv in desiredHLevels)
                {
                    string key = (kv.Key ?? "").Trim();
                    if (!string.IsNullOrWhiteSpace(key))
                        keep.Add(key);
                }
            }

            var remove = new List<WzImageProperty>();
            foreach (var child in node.WzProperties)
            {
                if (child == null || string.IsNullOrWhiteSpace(child.Name))
                    continue;
                if (!child.Name.StartsWith("h", StringComparison.OrdinalIgnoreCase))
                    continue;
                if (child.Name.Length <= 1 || !int.TryParse(child.Name.Substring(1), out _))
                    continue;
                if (!keep.Contains(child.Name))
                    remove.Add(child);
            }

            foreach (var child in remove)
            {
                node.WzProperties.Remove(child);
                try { child.Dispose(); } catch { }
            }
        }

        private static bool ShouldMarkAsSuperSkill(SkillDefinition sd, WzImageProperty existingNode)
        {
            if (HasSuperSkillMarker(existingNode))
                return true;
            if (sd == null)
                return true;
            if (string.Equals(sd.SourceLabel ?? "", "超级技能", StringComparison.OrdinalIgnoreCase))
                return true;
            if (!sd.ExistsInImg)
                return true;
            return false;
        }

        private static bool ShouldProtectNativeSkillWrite(SkillDefinition sd, WzImageProperty existingNode)
        {
            if (sd == null || existingNode == null)
                return false;
            if (HasSuperSkillMarker(existingNode))
                return false;
            if (!sd.ExistsInImg)
                return false;
            if (!string.Equals(sd.SourceLabel ?? "", "原生技能", StringComparison.OrdinalIgnoreCase))
                return false;
            if (sd.HasManualTreeOverride && sd.CachedTree != null)
                return false;

            int sourceId = sd.ResolveCloneSourceSkillId();
            if (sourceId > 0 && sourceId != sd.SkillId)
                return false;

            return true;
        }

        private static bool ShouldProtectNativeStringWrite(SkillDefinition sd, WzImageProperty existingNode)
        {
            if (sd == null || existingNode == null)
                return false;
            if (HasSuperSkillMarker(existingNode))
                return false;
            if (!sd.ExistsInImg)
                return false;
            if (!string.Equals(sd.SourceLabel ?? "", "原生技能", StringComparison.OrdinalIgnoreCase))
                return false;

            int sourceId = sd.ResolveCloneSourceSkillId();
            if (sourceId > 0 && sourceId != sd.SkillId)
                return false;

            return true;
        }

        private static void ApplySuperSkillMarker(WzSubProperty node, bool markAsSuperSkill)
        {
            if (node == null)
                return;

            if (markAsSuperSkill)
                ReplaceOrAddProperty(node, new WzIntProperty("_superSkill", 1));
            else
                RemoveProperty(node, "_superSkill");
        }

        private static bool HasSuperSkillMarker(WzImageProperty node)
        {
            if (node == null)
                return false;

            var marker = node["_superSkill"];
            if (marker is WzIntProperty ip)
                return ip.Value == 1;
            if (marker is WzShortProperty sp)
                return sp.Value == 1;
            if (marker is WzLongProperty lp)
                return lp.Value == 1;
            if (marker is WzStringProperty str && int.TryParse(str.Value, out int parsed))
                return parsed == 1;
            return false;
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
                    int index = parent.WzProperties.IndexOf(old);
                    parent.WzProperties.Remove(old);
                    if (index >= 0 && index <= parent.WzProperties.Count)
                        parent.WzProperties.Insert(index, newProp);
                    else
                        parent.AddProperty(newProp);
                    try { old.Dispose(); } catch { }
                    return;
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
                    int index = parent.WzProperties.IndexOf(old);
                    parent.WzProperties.Remove(old);
                    if (index >= 0 && index <= parent.WzProperties.Count)
                        parent.WzProperties.Insert(index, newProp);
                    else
                        parent.AddProperty(newProp);
                    try { old.Dispose(); } catch { }
                    return;
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
            if (sd == null || !sd.HasManualEffectOverride)
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
                if (child != null && IsAnimNodeName(child.Name))
                    removeList.Add(child);
            }

            foreach (var item in removeList)
            {
                node.WzProperties.Remove(item);
                try { item.Dispose(); } catch { }
            }
        }

        private static readonly string[] AnimNodeExactNames = new[]
        {
            "effect", "repeat", "ball", "hit", "prepare", "keydown",
            "keydownend", "affected", "mob", "special", "screen", "tile", "finish"
        };
        private static readonly string[] AnimNodeIndexablePrefixes = new[]
        {
            "effect", "repeat", "ball", "keydown", "affected", "mob", "special", "tile", "finish"
        };

        private static bool IsAnimNodeName(string name)
        {
            if (string.IsNullOrWhiteSpace(name))
                return false;
            foreach (var exact in AnimNodeExactNames)
            {
                if (string.Equals(name, exact, StringComparison.OrdinalIgnoreCase))
                    return true;
            }
            foreach (var prefix in AnimNodeIndexablePrefixes)
            {
                if (TryParseIndexedFrameNodeName(name, prefix, out _))
                    return true;
            }
            return false;
        }

        // Keep old name as alias
        private static bool IsEffectNodeName(string name) => IsAnimNodeName(name);

        private static int CompareEffectNodeNames(string a, string b)
        {
            if (string.Equals(a, b, StringComparison.OrdinalIgnoreCase))
                return 0;

            int aRank = GetFrameNodeSortRank(a, out int aIndex);
            int bRank = GetFrameNodeSortRank(b, out int bIndex);
            if (aRank != bRank)
                return aRank.CompareTo(bRank);
            if (aIndex != int.MaxValue || bIndex != int.MaxValue)
                return aIndex.CompareTo(bIndex);

            return string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
        }

        private static readonly (string baseName, int baseRank, bool canIndex)[] FrameNodeSortTable = new[]
        {
            ("effect",     0, true),
            ("repeat",     2, true),
            ("ball",       4, true),
            ("hit",        6, false),
            ("prepare",    8, false),
            ("keydown",   10, true),
            ("keydownend",12, false),
            ("affected",  14, true),
            ("mob",       16, true),
            ("special",   18, true),
            ("screen",    20, false),
            ("tile",      22, true),
            ("finish",    24, true),
        };

        private static int GetFrameNodeSortRank(string name, out int index)
        {
            index = int.MaxValue;
            if (string.IsNullOrWhiteSpace(name))
                return 99;

            if (name.StartsWith("hit/", StringComparison.OrdinalIgnoreCase)
                && int.TryParse(name.Substring(4), out index))
                return 6;
            if (string.Equals(name, "hit", StringComparison.OrdinalIgnoreCase))
            {
                index = -1;
                return 6;
            }

            foreach (var (baseName, baseRank, canIndex) in FrameNodeSortTable)
            {
                if (baseName == "hit") continue;
                if (string.Equals(name, baseName, StringComparison.OrdinalIgnoreCase))
                    return baseRank;
                if (canIndex && TryParseIndexedFrameNodeName(name, baseName, out index))
                    return baseRank + 1;
            }
            return 99;
        }

        /// <summary>
        /// Add a sub-group node to a parent. "effect/0" -> parent/effect/0/[frames].
        /// Creates the container intermediate node if it doesn't exist yet.
        /// Works for any family: hit/0, effect/0, ball/0, tile/0, etc.
        /// </summary>
        private static void AddSubGroupToParent(WzSubProperty parent, string slashKey, List<WzEffectFrame> frames)
        {
            // slashKey is like "hit/0", "effect/1", "ball/2"
            int slashIdx = slashKey.IndexOf('/');
            if (slashIdx < 0) return; // safety: not a sub-group key

            string containerName = slashKey.Substring(0, slashIdx);
            string groupIndex = slashKey.Substring(slashIdx + 1);
            if (string.IsNullOrEmpty(containerName) || string.IsNullOrEmpty(groupIndex))
                return;

            // Find or create the container node
            var containerNode = parent[containerName] as WzSubProperty;
            if (containerNode == null)
            {
                containerNode = new WzSubProperty(containerName);
                parent.AddProperty(containerNode);
            }

            // Build the group sub-node with frames
            var groupNode = BuildEffectNode(frames, groupIndex);
            if (groupNode != null)
                containerNode.AddProperty(groupNode);
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
            pngProp.SetBitmapBgra4444(bmp);
            canvas.PngProperty = pngProp;
            canvas.AddProperty(new WzVectorProperty("origin", 0, bmp.Height));

            bmp.Dispose();
            return canvas;
        }

        private static void MergeIconCanvas(WzSubProperty node, string iconName, string base64)
        {
            if (node == null || string.IsNullOrWhiteSpace(iconName) || string.IsNullOrEmpty(base64))
                return;

            var incoming = BuildCanvasFromBase64(base64, iconName);
            if (incoming == null)
                return;

            var existing = node[iconName] as WzCanvasProperty;
            if (existing != null)
            {
                if (AreCanvasPixelsEqual(existing, incoming))
                {
                    try { incoming.Dispose(); } catch { }
                    return;
                }

                // Even when rewriting icon bitmap, preserve legacy metadata fields
                // (z/origin/custom scalar flags) from the previous canvas node.
                CopyCanvasMetadata(existing, incoming);
            }

            ReplaceOrAddProperty(node, incoming);
        }

        private static void CopyCanvasMetadata(WzCanvasProperty source, WzCanvasProperty target)
        {
            if (source == null || target == null)
                return;
            if (source.WzProperties == null || source.WzProperties.Count == 0)
                return;

            foreach (var child in source.WzProperties)
            {
                if (child == null || string.IsNullOrWhiteSpace(child.Name))
                    continue;
                if (string.Equals(child.Name, WzCanvasProperty.InlinkPropertyName, StringComparison.OrdinalIgnoreCase)
                    || string.Equals(child.Name, WzCanvasProperty.OutlinkPropertyName, StringComparison.OrdinalIgnoreCase))
                {
                    // Links are tied to source atlas/layout; don't carry across a rewritten bitmap.
                    continue;
                }

                WzImageProperty clone = null;
                try { clone = (WzImageProperty)child.DeepClone(); }
                catch { }
                if (clone == null)
                {
                    clone = CloneScalarProperty(child);
                }
                if (clone == null)
                    continue;

                ReplaceOrAddProperty(target, clone);
            }
        }

        private static WzImageProperty CloneScalarProperty(WzImageProperty prop)
        {
            if (prop is WzIntProperty i32) return new WzIntProperty(prop.Name, i32.Value);
            if (prop is WzShortProperty i16) return new WzShortProperty(prop.Name, i16.Value);
            if (prop is WzLongProperty i64) return new WzLongProperty(prop.Name, i64.Value);
            if (prop is WzFloatProperty f32) return new WzFloatProperty(prop.Name, f32.Value);
            if (prop is WzDoubleProperty f64) return new WzDoubleProperty(prop.Name, f64.Value);
            if (prop is WzStringProperty s) return new WzStringProperty(prop.Name, s.Value ?? "");
            if (prop is WzVectorProperty v) return new WzVectorProperty(prop.Name, v.X?.Value ?? 0, v.Y?.Value ?? 0);
            return null;
        }

        private static bool AreCanvasPixelsEqual(WzCanvasProperty left, WzCanvasProperty right)
        {
            Bitmap leftBmp = null;
            Bitmap rightBmp = null;
            bool disposeLeft = false;
            bool disposeRight = false;

            try
            {
                if (left == null || right == null)
                    return false;

                leftBmp = TryGetCanvasBitmap(left, out disposeLeft);
                rightBmp = TryGetCanvasBitmap(right, out disposeRight);
                if (leftBmp == null || rightBmp == null)
                    return false;
                if (leftBmp.Width != rightBmp.Width || leftBmp.Height != rightBmp.Height)
                    return false;

                for (int y = 0; y < leftBmp.Height; y++)
                {
                    for (int x = 0; x < leftBmp.Width; x++)
                    {
                        if (!ArePixelsEffectivelyEqual(leftBmp.GetPixel(x, y), rightBmp.GetPixel(x, y)))
                            return false;
                    }
                }
                return true;
            }
            catch
            {
                return false;
            }
            finally
            {
                if (disposeLeft && leftBmp != null)
                {
                    try { leftBmp.Dispose(); } catch { }
                }
                if (disposeRight && rightBmp != null)
                {
                    try { rightBmp.Dispose(); } catch { }
                }
            }
        }

        private static bool ArePixelsEffectivelyEqual(Color left, Color right)
        {
            if (left.ToArgb() == right.ToArgb())
                return true;

            // Re-encoding legacy WZ icons may turn fully transparent pixels into
            // alpha=1 pixels. They are visually identical, so keep the original node.
            return left.A <= 1 && right.A <= 1;
        }

        private static Bitmap TryGetCanvasBitmap(WzCanvasProperty canvas, out bool shouldDispose)
        {
            shouldDispose = false;
            if (canvas == null)
                return null;

            try
            {
                Bitmap linked = canvas.GetLinkedWzCanvasBitmap();
                if (linked != null)
                {
                    shouldDispose = true;
                    return linked;
                }
            }
            catch { }

            try
            {
                Bitmap bmp = canvas.GetBitmap();
                if (bmp != null)
                {
                    shouldDispose = true;
                    return bmp;
                }
            }
            catch { }
            return null;
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

                    pngProp.SetBitmapBgra4444(frameBmp);
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

        private static WzImageProperty BuildTreeScalarProperty(string name, string value)
        {
            if (TryParseVectorValue(value, out int x, out int y))
                return new WzVectorProperty(name, x, y);
            return BuildScalarProperty(name, value);
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
            if (parent == null || parent.WzProperties == null || string.IsNullOrEmpty(childName)) return;
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
            if (img == null || img.WzProperties == null || string.IsNullOrEmpty(childName)) return;
            var toRemove = img[childName];
            if (toRemove != null)
            {
                img.WzProperties.Remove(toRemove);
                try { toRemove.Dispose(); } catch { }
            }
        }

        private static string FindSkillPropertyKey(WzSubProperty skillTop, int skillId)
        {
            if (skillTop == null || skillId <= 0)
                return null;

            foreach (string key in PathConfig.SkillKeyCandidates(skillId))
            {
                if (skillTop[key] != null)
                    return key;
            }

            if (skillTop.WzProperties != null)
            {
                foreach (var child in skillTop.WzProperties)
                {
                    if (child == null || string.IsNullOrWhiteSpace(child.Name))
                        continue;
                    if (int.TryParse(child.Name, out int parsed) && parsed == skillId)
                        return child.Name;
                }
            }

            return null;
        }

        private static WzImageProperty FindSkillProperty(WzImage img, int skillId)
        {
            if (img == null || skillId <= 0)
                return null;

            foreach (string key in PathConfig.SkillKeyCandidates(skillId))
            {
                var node = img.GetFromPath("skill/" + key);
                if (node != null)
                    return node;
            }

            var skillTop = img.GetFromPath("skill") as WzSubProperty;
            string foundKey = FindSkillPropertyKey(skillTop, skillId);
            return foundKey != null ? skillTop[foundKey] : null;
        }

        private static string FindImagePropertyKey(WzImage img, int skillId)
        {
            if (img == null || skillId <= 0)
                return null;

            foreach (string key in PathConfig.SkillKeyCandidates(skillId))
            {
                if (img[key] != null)
                    return key;
            }

            if (img.WzProperties != null)
            {
                foreach (var child in img.WzProperties)
                {
                    if (child == null || string.IsNullOrWhiteSpace(child.Name))
                        continue;
                    if (int.TryParse(child.Name, out int parsed) && parsed == skillId)
                        return child.Name;
                }
            }

            return null;
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
                newImage = new WzImage(Path.GetFileName(imgPath));
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

