using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Text;
using MapleLib.WzLib;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    /// <summary>
    /// Modifies DLL-side local resource JSON files:
    ///   1. {jobId}.img.json  -- skill data under "skill" object
    ///   2. Skill.img.json    -- name/desc/h data at root level
    /// </summary>
    public static class DllJsonGenerator
    {
        private static readonly Encoding Utf8NoBom = new UTF8Encoding(false);
        private static readonly string TransparentIconBase64 = BuildTransparentIconBase64();

        public static void GenerateSkillImgJson(List<SkillDefinition> skills, bool dryRun)
        {
            var targetSkillIds = BuildSkillIdSet(skills);
            var managedSkillIds = LoadManagedSkillIdsForCleanup();

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

            var staleManagedSkillIds = new HashSet<int>(managedSkillIds);
            staleManagedSkillIds.ExceptWith(targetSkillIds);
            staleManagedSkillIds.Remove(carrierId);
            staleManagedSkillIds.ExceptWith(staleCarrierIds);
            foreach (int staleSkillId in staleManagedSkillIds)
            {
                int staleJobId = staleSkillId / 10000;
                if (!byJob.ContainsKey(staleJobId))
                    byJob[staleJobId] = new List<SkillDefinition>();
            }

            foreach (var kv in byJob)
            {
                int jobId = kv.Key;
                var list = kv.Value;
                string jsonPath = PathConfig.DllSkillImgJson(jobId);
                Console.WriteLine($"\n[DllSkillImgJson] Processing {jsonPath}");

                var root = LoadOrCreateJsonObject(jsonPath);
                var skillContainer = GetOrCreateSkillContainer(root);
                bool modified = false;

                int removedStale = RemoveStaleCarrierSkillEntries(skillContainer, jobId, staleCarrierIds, dryRun);
                if (!dryRun && removedStale > 0)
                    modified = true;

                int removedManaged = RemoveManagedSkillEntries(skillContainer, jobId, staleManagedSkillIds, dryRun);
                if (!dryRun && removedManaged > 0)
                    modified = true;

                if (carrierId > 0 && carrierId / 10000 == jobId)
                {
                    string targetKey = FormatSkillKey(carrierId);
                    string existingKey = TryFindExistingSkillKey(skillContainer, carrierId);
                    bool overwrite = !string.IsNullOrEmpty(existingKey);

                    if (dryRun)
                    {
                        Console.WriteLine(overwrite
                            ? $"  [dry-run] Would ensure carrier skill entry {existingKey} -> {targetKey}"
                            : $"  [dry-run] Would add carrier skill entry {targetKey}");
                    }
                    else
                    {
                        Dictionary<string, object> existingEntry = null;
                        if (overwrite
                            && skillContainer.TryGetValue(existingKey, out object existingObj)
                            && existingObj is Dictionary<string, object> existingDict)
                        {
                            existingEntry = existingDict;
                        }

                        skillContainer[targetKey] = BuildCarrierSkillImgEntry(carrierId, existingEntry);
                        if (overwrite && existingKey != targetKey)
                            skillContainer.Remove(existingKey);

                        modified = true;
                        Console.WriteLine(overwrite
                            ? $"  [write] Ensure carrier skill entry {targetKey}"
                            : $"  [added] Carrier skill entry {targetKey}");
                    }
                }

                foreach (var sd in list)
                {
                    string targetKey = FormatSkillKey(sd.SkillId);
                    string existingKey = TryFindExistingSkillKey(skillContainer, sd.SkillId);
                    bool overwrite = !string.IsNullOrEmpty(existingKey);

                    if (dryRun)
                    {
                        Console.WriteLine(overwrite
                            ? $"  [dry-run] Would overwrite skill entry {existingKey} -> {targetKey}"
                            : $"  [dry-run] Would add skill entry {targetKey}");
                        continue;
                    }

                    Dictionary<string, object> existingEntry = null;
                    if (overwrite
                        && skillContainer.TryGetValue(existingKey, out object existingObj)
                        && existingObj is Dictionary<string, object> existingDict)
                    {
                        existingEntry = existingDict;
                    }

                    skillContainer[targetKey] = BuildSkillImgEntry(sd, existingEntry);
                    if (overwrite && existingKey != targetKey)
                        skillContainer.Remove(existingKey);

                    modified = true;
                    Console.WriteLine(overwrite
                        ? $"  [overwrite] Skill entry {targetKey}"
                        : $"  [added] Skill entry {targetKey}");
                }

                if (!dryRun && modified)
                    SaveJson(jsonPath, root);
            }
        }

        public static void GenerateStringImgJson(List<SkillDefinition> skills, bool dryRun)
        {
            string jsonPath = PathConfig.DllStringJson;
            Console.WriteLine($"\n[DllStringJson] Processing {jsonPath}");

            var targetSkillIds = BuildSkillIdSet(skills);
            var managedSkillIds = LoadManagedSkillIdsForCleanup();

            var root = LoadOrCreateJsonObject(jsonPath);
            bool modified = false;

            int carrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            var staleCarrierIds = CarrierSkillHelper.GetStaleCarrierIds();
            var staleManagedSkillIds = new HashSet<int>(managedSkillIds);
            staleManagedSkillIds.ExceptWith(targetSkillIds);
            staleManagedSkillIds.Remove(carrierId);
            staleManagedSkillIds.ExceptWith(staleCarrierIds);

            int removedStale = RemoveStaleCarrierStringEntries(root, staleCarrierIds, dryRun);
            if (!dryRun && removedStale > 0)
                modified = true;

            int removedManaged = RemoveManagedStringEntries(root, staleManagedSkillIds, dryRun);
            if (!dryRun && removedManaged > 0)
                modified = true;

            if (carrierId > 0)
            {
                string targetKey = FormatSkillKey(carrierId);
                string existingKey = TryFindExistingSkillKey(root, carrierId);
                bool overwrite = !string.IsNullOrEmpty(existingKey);

                if (dryRun)
                {
                    Console.WriteLine(overwrite
                        ? $"  [dry-run] Would ensure carrier string entry {existingKey} -> {targetKey}"
                        : $"  [dry-run] Would add carrier string entry {targetKey}");
                }
                else
                {
                    Dictionary<string, object> existingEntry = null;
                    if (overwrite
                        && root.TryGetValue(existingKey, out object existingObj)
                        && existingObj is Dictionary<string, object> existingDict)
                    {
                        existingEntry = existingDict;
                    }

                    root[targetKey] = BuildCarrierStringEntry(carrierId, existingEntry);
                    if (overwrite && existingKey != targetKey)
                        root.Remove(existingKey);

                    modified = true;
                    Console.WriteLine(overwrite
                        ? $"  [write] Ensure carrier string entry {targetKey}"
                        : $"  [added] Carrier string entry {targetKey}");
                }
            }

            foreach (var sd in skills)
            {
                string targetKey = FormatSkillKey(sd.SkillId);
                string existingKey = TryFindExistingSkillKey(root, sd.SkillId);
                bool overwrite = !string.IsNullOrEmpty(existingKey);

                if (dryRun)
                {
                    Console.WriteLine(overwrite
                        ? $"  [dry-run] Would overwrite string entry {existingKey} -> {targetKey}"
                        : $"  [dry-run] Would add string entry {targetKey}");
                    continue;
                }

                root[targetKey] = BuildStringEntry(sd);
                if (overwrite && existingKey != targetKey)
                    root.Remove(existingKey);

                modified = true;
                Console.WriteLine(overwrite
                    ? $"  [overwrite] String entry {targetKey} ({sd.Name})"
                    : $"  [added] String entry {targetKey} ({sd.Name})");
            }

            if (!dryRun && modified)
                SaveJson(jsonPath, root);
        }

        private static int RemoveStaleCarrierSkillEntries(
            Dictionary<string, object> skillContainer,
            int jobId,
            HashSet<int> staleCarrierIds,
            bool dryRun)
        {
            if (skillContainer == null || staleCarrierIds == null || staleCarrierIds.Count == 0)
                return 0;

            int removed = 0;
            foreach (int staleCarrierId in staleCarrierIds)
            {
                if (staleCarrierId / 10000 != jobId)
                    continue;

                string staleKey = TryFindExistingSkillKey(skillContainer, staleCarrierId);
                if (string.IsNullOrEmpty(staleKey))
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale carrier skill entry {staleKey}");
                }
                else
                {
                    skillContainer.Remove(staleKey);
                    Console.WriteLine($"  [cleanup] Removed stale carrier skill entry {staleKey}");
                }
            }

            return removed;
        }

        private static int RemoveStaleCarrierStringEntries(
            Dictionary<string, object> root,
            HashSet<int> staleCarrierIds,
            bool dryRun)
        {
            if (root == null || staleCarrierIds == null || staleCarrierIds.Count == 0)
                return 0;

            int removed = 0;
            foreach (int staleCarrierId in staleCarrierIds)
            {
                string staleKey = TryFindExistingSkillKey(root, staleCarrierId);
                if (string.IsNullOrEmpty(staleKey))
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale carrier string entry {staleKey}");
                }
                else
                {
                    root.Remove(staleKey);
                    Console.WriteLine($"  [cleanup] Removed stale carrier string entry {staleKey}");
                }
            }

            return removed;
        }

        private static int RemoveManagedSkillEntries(
            Dictionary<string, object> skillContainer,
            int jobId,
            HashSet<int> staleManagedSkillIds,
            bool dryRun)
        {
            if (skillContainer == null || staleManagedSkillIds == null || staleManagedSkillIds.Count == 0)
                return 0;

            int removed = 0;
            foreach (int staleSkillId in staleManagedSkillIds)
            {
                if (staleSkillId / 10000 != jobId)
                    continue;

                string staleKey = TryFindExistingSkillKey(skillContainer, staleSkillId);
                if (string.IsNullOrEmpty(staleKey))
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale managed skill entry {staleKey}");
                }
                else
                {
                    skillContainer.Remove(staleKey);
                    Console.WriteLine($"  [cleanup] Removed stale managed skill entry {staleKey}");
                }
            }

            return removed;
        }

        private static int RemoveManagedStringEntries(
            Dictionary<string, object> root,
            HashSet<int> staleManagedSkillIds,
            bool dryRun)
        {
            if (root == null || staleManagedSkillIds == null || staleManagedSkillIds.Count == 0)
                return 0;

            int removed = 0;
            foreach (int staleSkillId in staleManagedSkillIds)
            {
                string staleKey = TryFindExistingSkillKey(root, staleSkillId);
                if (string.IsNullOrEmpty(staleKey))
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale managed string entry {staleKey}");
                }
                else
                {
                    root.Remove(staleKey);
                    Console.WriteLine($"  [cleanup] Removed stale managed string entry {staleKey}");
                }
            }

            return removed;
        }

        public static void RemoveSkillImgJson(List<SkillDefinition> skills, bool dryRun)
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
                string jsonPath = PathConfig.DllSkillImgJson(jobId);

                Console.WriteLine($"\n[DllSkillImgJson-Delete] Processing {jsonPath}");
                if (!File.Exists(jsonPath))
                {
                    Console.WriteLine("  [skip] Not found");
                    continue;
                }

                var root = LoadOrCreateJsonObject(jsonPath);
                var skillContainer = GetOrCreateSkillContainer(root);
                int removed = 0;

                foreach (var sd in list)
                {
                    string existingKey = TryFindExistingSkillKey(skillContainer, sd.SkillId);
                    if (string.IsNullOrEmpty(existingKey))
                    {
                        Console.WriteLine($"  [skip] {FormatSkillKey(sd.SkillId)} not found");
                        continue;
                    }

                    if (dryRun)
                    {
                        Console.WriteLine($"  [dry-run] Would remove {existingKey}");
                        continue;
                    }

                    skillContainer.Remove(existingKey);
                    removed++;
                    Console.WriteLine($"  [removed] {existingKey}");
                }

                if (!dryRun && removed > 0)
                {
                    SaveJson(jsonPath, root);
                    Console.WriteLine($"  [saved] {jsonPath} ({removed} removed)");
                }
            }
        }

        public static void RemoveStringImgJson(List<SkillDefinition> skills, bool dryRun)
        {
            string jsonPath = PathConfig.DllStringJson;
            Console.WriteLine($"\n[DllStringJson-Delete] Processing {jsonPath}");
            if (!File.Exists(jsonPath))
            {
                Console.WriteLine("  [skip] Not found");
                return;
            }

            var root = LoadOrCreateJsonObject(jsonPath);
            int removed = 0;
            foreach (var sd in skills)
            {
                string existingKey = TryFindExistingSkillKey(root, sd.SkillId);
                if (string.IsNullOrEmpty(existingKey))
                {
                    Console.WriteLine($"  [skip] {FormatSkillKey(sd.SkillId)} not found");
                    continue;
                }

                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove {existingKey}");
                    continue;
                }

                root.Remove(existingKey);
                removed++;
                Console.WriteLine($"  [removed] {existingKey}");
            }

            if (!dryRun && removed > 0)
            {
                SaveJson(jsonPath, root);
                Console.WriteLine($"  [saved] {jsonPath} ({removed} removed)");
            }
        }

        private static Dictionary<string, object> BuildSkillImgEntry(SkillDefinition sd, Dictionary<string, object> existingEntry)
        {
            string idStr = FormatSkillKey(sd.SkillId);

            var entry = TryCloneSkillEntryFromSource(sd);
            if (entry == null && existingEntry != null)
                entry = existingEntry;
            if (entry == null)
            {
                entry = new Dictionary<string, object>();
                entry["_dirName"] = idStr;
                entry["_dirType"] = "sub";
            }

            entry["_dirName"] = idStr;
            entry["_dirType"] = "sub";

            OverlayIconEntries(entry, sd);
            OverlayKnownSkillFields(entry, sd);

            return entry;
        }

        private static Dictionary<string, object> BuildCarrierSkillImgEntry(int carrierId, Dictionary<string, object> existingEntry)
        {
            string idStr = FormatSkillKey(carrierId);
            var entry = existingEntry ?? new Dictionary<string, object>();
            PruneCarrierSkillEntry(entry);
            entry["_dirName"] = idStr;
            entry["_dirType"] = "sub";
            entry["_superSkill"] = BuildIntProperty("_superSkill", 1);
            entry["invisible"] = BuildIntProperty("invisible", 1);
            entry["icon"] = BuildCanvasEntry("icon", TransparentIconBase64);
            entry["iconMouseOver"] = BuildCanvasEntry("iconMouseOver", TransparentIconBase64);
            entry["iconDisabled"] = BuildCanvasEntry("iconDisabled", TransparentIconBase64);
            entry["common"] = BuildCarrierCommonEntry();

            entry.Remove("action");
            entry.Remove("info");
            entry.Remove("level");
            entry.Remove("req");
            RemoveEffectEntries(entry);

            return entry;
        }

        private static void OverlayIconEntries(Dictionary<string, object> entry, SkillDefinition sd)
        {
            bool hasIconOverride = !string.IsNullOrEmpty(sd.IconBase64) || !string.IsNullOrEmpty(sd.Icon);
            bool hasMoOverride = !string.IsNullOrEmpty(sd.IconMouseOverBase64) || !string.IsNullOrEmpty(sd.IconMouseOver);
            bool hasDisOverride = !string.IsNullOrEmpty(sd.IconDisabledBase64) || !string.IsNullOrEmpty(sd.IconDisabled);

            if (hasIconOverride || !entry.ContainsKey("icon"))
                entry["icon"] = BuildCanvasEntry("icon", GetIconBase64(sd));

            string moBase64 = GetIconMouseOverBase64(sd);
            if (!string.IsNullOrEmpty(moBase64) && (hasMoOverride || !entry.ContainsKey("iconMouseOver")))
                entry["iconMouseOver"] = BuildCanvasEntry("iconMouseOver", moBase64);

            string disBase64 = GetIconDisabledBase64(sd);
            if (!string.IsNullOrEmpty(disBase64) && (hasDisOverride || !entry.ContainsKey("iconDisabled")))
                entry["iconDisabled"] = BuildCanvasEntry("iconDisabled", disBase64);
        }

        private static void OverlayKnownSkillFields(Dictionary<string, object> entry, SkillDefinition sd)
        {
            if (!string.IsNullOrEmpty(sd.Action) && sd.InfoType != 50)
                MergeActionEntry(entry, sd.Action);
            else if (sd.InfoType == 50 && entry.ContainsKey("action"))
                entry.Remove("action");

            if (sd.InfoType > 0)
            {
                var info = SimpleJson.GetObject(entry, "info") ?? new Dictionary<string, object>();
                info["_dirName"] = "info";
                info["_dirType"] = "sub";
                info["type"] = BuildIntProperty("type", sd.InfoType);
                entry["info"] = info;
            }

            if (sd.Levels != null && sd.Levels.Count > 0)
            {
                entry["level"] = BuildLevelEntry(sd.Levels);
            }
            if (sd.Common != null && sd.Common.Count > 0)
            {
                MergeCommonEntry(entry, sd);
            }

            if (sd.RequiredSkills != null && sd.RequiredSkills.Count > 0)
                entry["req"] = BuildReqEntry(sd.RequiredSkills);

            var effectMap = GetEffectFramesByNode(sd);
            if (effectMap != null && effectMap.Count > 0)
            {
                RemoveEffectEntries(entry);
                foreach (string nodeName in GetSortedEffectNodeNames(effectMap))
                {
                    if (!effectMap.TryGetValue(nodeName, out var frames) || frames == null || frames.Count == 0)
                        continue;
                    var effect = BuildEffectEntry(frames, nodeName);
                    if (effect != null)
                        entry[nodeName] = effect;
                }
            }
        }

        private static Dictionary<string, object> BuildActionEntry(string action)
        {
            var actionNode = new Dictionary<string, object>();
            actionNode["_dirName"] = "action";
            actionNode["_dirType"] = "sub";
            actionNode["0"] = BuildStringProperty("0", action);
            return actionNode;
        }

        private static void MergeActionEntry(Dictionary<string, object> entry, string action)
        {
            if (entry == null)
                return;

            var actionNode = SimpleJson.GetObject(entry, "action");
            if (actionNode == null)
            {
                entry["action"] = BuildActionEntry(action);
                return;
            }

            actionNode["_dirName"] = "action";
            string dirType = SimpleJson.GetString(actionNode, "_dirType");
            if (string.Equals(dirType, "String", StringComparison.OrdinalIgnoreCase))
            {
                actionNode["_dirType"] = "String";
                actionNode["_value"] = action ?? "";
                entry["action"] = actionNode;
                return;
            }

            actionNode["_dirType"] = "sub";

            string targetChildName = "0";
            Dictionary<string, object> targetChild = SimpleJson.GetObject(actionNode, "0");

            if (targetChild == null)
            {
                int bestNumeric = int.MaxValue;
                foreach (var kv in actionNode)
                {
                    if (string.IsNullOrEmpty(kv.Key) || kv.Key[0] == '_')
                        continue;

                    if (!(kv.Value is Dictionary<string, object> childObj))
                        continue;

                    if (!IsStringProperty(childObj))
                        continue;

                    if (int.TryParse(kv.Key, out int idx))
                    {
                        if (idx < bestNumeric)
                        {
                            bestNumeric = idx;
                            targetChildName = kv.Key;
                            targetChild = childObj;
                        }
                    }
                    else if (targetChild == null)
                    {
                        targetChildName = kv.Key;
                        targetChild = childObj;
                    }
                }
            }

            if (targetChild == null)
            {
                actionNode[targetChildName] = BuildStringProperty(targetChildName, action);
            }
            else
            {
                targetChild["_dirName"] = targetChildName;
                targetChild["_dirType"] = "String";
                targetChild["_value"] = action ?? "";
                actionNode[targetChildName] = targetChild;
            }

            entry["action"] = actionNode;
        }

        private static bool IsStringProperty(Dictionary<string, object> obj)
        {
            string type = SimpleJson.GetString(obj, "_dirType");
            return string.Equals(type, "String", StringComparison.OrdinalIgnoreCase);
        }

        private static void MergeCommonEntry(Dictionary<string, object> entry, SkillDefinition sd)
        {
            if (entry == null || sd == null)
                return;

            var common = SimpleJson.GetObject(entry, "common") ?? new Dictionary<string, object>();
            common["_dirName"] = "common";
            common["_dirType"] = "sub";
            common["maxLevel"] = BuildIntProperty("maxLevel", sd.MaxLevel);

            foreach (var kv in sd.Common)
            {
                if (kv.Key == "maxLevel")
                    continue;

                if (int.TryParse(kv.Value, out int iv))
                    common[kv.Key] = BuildIntProperty(kv.Key, iv);
                else
                    common[kv.Key] = BuildStringProperty(kv.Key, kv.Value);
            }
            entry["common"] = common;
        }

        private static Dictionary<string, object> BuildLevelEntry(Dictionary<int, Dictionary<string, string>> levels)
        {
            var levelNode = new Dictionary<string, object>();
            levelNode["_dirName"] = "level";
            levelNode["_dirType"] = "sub";

            foreach (var lv in levels)
            {
                var lvNode = new Dictionary<string, object>();
                string lvName = lv.Key.ToString();
                lvNode["_dirName"] = lvName;
                lvNode["_dirType"] = "sub";

                foreach (var p in lv.Value)
                {
                    if (int.TryParse(p.Value, out int iv))
                        lvNode[p.Key] = BuildIntProperty(p.Key, iv);
                    else
                        lvNode[p.Key] = BuildStringProperty(p.Key, p.Value);
                }

                levelNode[lvName] = lvNode;
            }

            return levelNode;
        }

        private static Dictionary<string, object> BuildReqEntry(Dictionary<int, int> req)
        {
            var reqNode = new Dictionary<string, object>();
            reqNode["_dirName"] = "req";
            reqNode["_dirType"] = "sub";

            foreach (var kv in req)
                reqNode[FormatSkillKey(kv.Key)] = BuildIntProperty(FormatSkillKey(kv.Key), kv.Value);

            return reqNode;
        }

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
                result["effect"] = sd.CachedEffects;

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

        private static void RemoveEffectEntries(Dictionary<string, object> entry)
        {
            if (entry == null || entry.Count == 0)
                return;

            var removeKeys = new List<string>();
            foreach (var kv in entry)
            {
                if (IsEffectNodeName(kv.Key))
                    removeKeys.Add(kv.Key);
            }

            foreach (string key in removeKeys)
                entry.Remove(key);
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

        private static Dictionary<string, object> BuildEffectEntry(List<WzEffectFrame> frames, string nodeName)
        {
            var effectNode = new Dictionary<string, object>();
            effectNode["_dirName"] = string.IsNullOrWhiteSpace(nodeName) ? "effect" : nodeName;
            effectNode["_dirType"] = "sub";

            var sorted = new List<WzEffectFrame>(frames);
            sorted.Sort((a, b) => a.Index.CompareTo(b.Index));

            int added = 0;
            foreach (var ef in sorted)
            {
                if (ef == null)
                    continue;
                try
                {
                    if (ef.Bitmap == null)
                        continue;

                    if (!TryGetBitmapSize(ef.Bitmap, out int bmpWidth, out int bmpHeight))
                    {
                        Console.WriteLine($"  [warn] Skip effect frame {ef.Index}: invalid bitmap size");
                        continue;
                    }

                    string imageBase64 = BitmapToBase64(ef.Bitmap);
                    if (string.IsNullOrEmpty(imageBase64))
                    {
                        Console.WriteLine($"  [warn] Skip effect frame {ef.Index}: bitmap encode failed");
                        continue;
                    }

                    int frameWidth = ef.Width > 0 ? ef.Width : bmpWidth;
                    int frameHeight = ef.Height > 0 ? ef.Height : bmpHeight;
                    if (frameWidth <= 0 || frameHeight <= 0)
                    {
                        Console.WriteLine($"  [warn] Skip effect frame {ef.Index}: width/height invalid");
                        continue;
                    }

                    string frameName = ef.Index.ToString();
                    var frameCanvas = new Dictionary<string, object>();
                    frameCanvas["_dirName"] = frameName;
                    frameCanvas["_dirType"] = "canvas";
                    frameCanvas["_width"] = (long)frameWidth;
                    frameCanvas["_height"] = (long)frameHeight;
                    frameCanvas["_image"] = imageBase64;

                    bool hasOrigin = false;
                    if (ef.Vectors != null && ef.Vectors.Count > 0)
                    {
                        string[] preferred = new[] { "origin", "head", "vector", "border", "crosshair" };
                        var written = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

                        foreach (var key in preferred)
                        {
                            if (TryAddVectorNode(frameCanvas, ef.Vectors, key))
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
                            frameCanvas[kv.Key] = BuildVectorNode(kv.Key, kv.Value.X, kv.Value.Y);
                            if (string.Equals(kv.Key, "origin", StringComparison.OrdinalIgnoreCase))
                                hasOrigin = true;
                        }
                    }
                    if (!hasOrigin)
                        frameCanvas["origin"] = BuildVectorNode("origin", 0, frameHeight);

                    frameCanvas["delay"] = BuildIntProperty("delay", ef.Delay > 0 ? ef.Delay : 100);
                    AddEffectFrameProps(frameCanvas, ef);

                    effectNode[frameName] = frameCanvas;
                    added++;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  [warn] Skip effect frame {ef.Index}: {ex.Message}");
                }
            }

            return added > 0 ? effectNode : null;
        }

        private static Dictionary<string, object> BuildCanvasEntry(string name, string imageBase64)
        {
            var origin = new Dictionary<string, object>();
            origin["_dirName"] = "origin";
            origin["_dirType"] = "vector";
            origin["_x"] = (long)0;
            origin["_y"] = (long)32;

            var canvas = new Dictionary<string, object>();
            canvas["_dirName"] = name;
            canvas["_dirType"] = "canvas";
            canvas["_width"] = (long)32;
            canvas["_height"] = (long)32;
            canvas["_image"] = imageBase64;
            canvas["origin"] = origin;
            canvas["z"] = BuildIntProperty("z", 0);
            return canvas;
        }

        private static Dictionary<string, object> BuildVectorNode(string name, int x, int y)
        {
            var node = new Dictionary<string, object>();
            node["_dirName"] = name;
            node["_dirType"] = "vector";
            node["_x"] = (long)x;
            node["_y"] = (long)y;
            return node;
        }

        private static bool TryAddVectorNode(
            Dictionary<string, object> frameCanvas,
            Dictionary<string, WzFrameVector> vectors,
            string key)
        {
            if (frameCanvas == null || vectors == null || string.IsNullOrEmpty(key))
                return false;

            foreach (var kv in vectors)
            {
                if (!string.Equals(kv.Key, key, StringComparison.OrdinalIgnoreCase) || kv.Value == null)
                    continue;

                frameCanvas[kv.Key] = BuildVectorNode(kv.Key, kv.Value.X, kv.Value.Y);
                return true;
            }
            return false;
        }

        private static void AddEffectFrameProps(Dictionary<string, object> frameCanvas, WzEffectFrame ef)
        {
            if (frameCanvas == null || ef?.FrameProps == null || ef.FrameProps.Count == 0)
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

                if (int.TryParse(kv.Value, out int iv))
                    frameCanvas[kv.Key] = BuildIntProperty(kv.Key, iv);
                else
                    frameCanvas[kv.Key] = BuildStringProperty(kv.Key, kv.Value ?? "");
            }
        }

        private static Dictionary<string, object> BuildStringEntry(SkillDefinition sd)
        {
            string idStr = FormatSkillKey(sd.SkillId);
            var entry = new Dictionary<string, object>();
            entry["_dirName"] = idStr;
            entry["_dirType"] = "sub";

            entry["name"] = BuildStringProperty("name", sd.Name);
            if (!string.IsNullOrEmpty(sd.Desc))
                entry["desc"] = BuildStringProperty("desc", sd.Desc);
            if (!string.IsNullOrEmpty(sd.H))
                entry["h"] = BuildStringProperty("h", sd.H);

            if (sd.HLevels != null)
            {
                foreach (var kv in sd.HLevels)
                    entry[kv.Key] = BuildStringProperty(kv.Key, kv.Value);
            }

            return entry;
        }

        private static Dictionary<string, object> BuildCarrierStringEntry(int carrierId, Dictionary<string, object> existingEntry)
        {
            string idStr = FormatSkillKey(carrierId);
            var entry = existingEntry ?? new Dictionary<string, object>();
            entry["_dirName"] = idStr;
            entry["_dirType"] = "sub";
            entry["_superSkill"] = BuildIntProperty("_superSkill", 1);
            entry["name"] = BuildStringProperty("name", "Super SP");
            entry["desc"] = BuildStringProperty("desc", "超级SP载体技能。");
            entry["h1"] = BuildStringProperty("h1", "仅用于承载超级SP，不在技能栏显示。");
            entry.Remove("h");
            entry.Remove("pdesc");
            entry.Remove("ph");
            RemoveCarrierExtraHLevels(entry);
            return entry;
        }

        private static void PruneCarrierSkillEntry(Dictionary<string, object> entry)
        {
            if (entry == null)
                return;

            var keep = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
            {
                "_dirName",
                "_dirType",
                "_superSkill",
                "icon",
                "iconMouseOver",
                "iconDisabled",
                "common",
                "invisible"
            };

            var remove = new List<string>();
            foreach (var kv in entry)
            {
                if (string.IsNullOrEmpty(kv.Key))
                    continue;
                if (!keep.Contains(kv.Key))
                    remove.Add(kv.Key);
            }
            foreach (string key in remove)
                entry.Remove(key);
        }

        private static void RemoveCarrierExtraHLevels(Dictionary<string, object> entry)
        {
            if (entry == null)
                return;

            var remove = new List<string>();
            foreach (var kv in entry)
            {
                if (string.IsNullOrEmpty(kv.Key))
                    continue;
                if (!kv.Key.StartsWith("h", StringComparison.OrdinalIgnoreCase))
                    continue;
                if (string.Equals(kv.Key, "h1", StringComparison.OrdinalIgnoreCase))
                    continue;
                if (kv.Key.Length > 1 && int.TryParse(kv.Key.Substring(1), out _))
                    remove.Add(kv.Key);
            }
            foreach (string key in remove)
                entry.Remove(key);
        }

        private static Dictionary<string, object> BuildCarrierCommonEntry()
        {
            int maxLevel = Math.Max(1, PathConfig.DefaultSuperSpCarrierMaxLevel);
            var common = new Dictionary<string, object>();
            common["_dirName"] = "common";
            common["_dirType"] = "sub";
            common["maxLevel"] = BuildIntProperty("maxLevel", maxLevel);
            return common;
        }

        private static string BuildTransparentIconBase64()
        {
            using (var bmp = new Bitmap(32, 32, PixelFormat.Format32bppArgb))
            using (var g = Graphics.FromImage(bmp))
            using (var ms = new MemoryStream())
            {
                g.Clear(Color.Transparent);
                bmp.Save(ms, ImageFormat.Png);
                return Convert.ToBase64String(ms.ToArray());
            }
        }

        private static Dictionary<string, object> BuildStringProperty(string name, string value)
        {
            var d = new Dictionary<string, object>();
            d["_dirName"] = name;
            d["_dirType"] = "String";
            d["_value"] = value ?? "";
            return d;
        }

        private static Dictionary<string, object> BuildIntProperty(string name, int value)
        {
            var d = new Dictionary<string, object>();
            d["_dirName"] = name;
            d["_dirType"] = "Int";
            d["_value"] = (long)value;
            return d;
        }

        private static string FormatSkillKey(int skillId)
        {
            return PathConfig.SkillKey(skillId);
        }

        private static string TryFindExistingSkillKey(Dictionary<string, object> container, int skillId)
        {
            if (container == null) return null;

            string d7 = FormatSkillKey(skillId);
            if (container.ContainsKey(d7)) return d7;

            string raw = skillId.ToString();
            if (container.ContainsKey(raw)) return raw;

            foreach (var kv in container)
            {
                if (string.IsNullOrEmpty(kv.Key) || kv.Key[0] == '_')
                    continue;

                if (int.TryParse(kv.Key, out int parsed) && parsed == skillId)
                    return kv.Key;
            }

            return null;
        }

        private static Dictionary<string, object> TryCloneSkillEntryFromSource(SkillDefinition sd)
        {
            int sourceId = sd.ResolveCloneSourceSkillId();
            if (sourceId <= 0 || sourceId == sd.SkillId)
                return null;

            var sourceNode = TryLoadSourceSkillNode(sourceId);
            if (sourceNode == null)
                return null;

            sourceNode.Name = FormatSkillKey(sd.SkillId);
            var cloned = ConvertWzPropertyToJson(sourceNode) as Dictionary<string, object>;
            return cloned;
        }

        private static WzSubProperty TryLoadSourceSkillNode(int sourceSkillId)
        {
            int sourceJobId = sourceSkillId / 10000;
            string sourceImgPath = PathConfig.GameSkillImg(sourceJobId);
            if (!File.Exists(sourceImgPath))
                return null;

            FileStream fs = null;
            WzImage img = null;
            try
            {
                fs = new FileStream(sourceImgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                img = new WzImage(Path.GetFileName(sourceImgPath), fs, WzMapleVersion.EMS);
                if (!img.ParseImage(true))
                    return null;

                WzSubProperty sourceNode = null;
                foreach (string key in PathConfig.SkillKeyCandidates(sourceSkillId))
                {
                    sourceNode = img.GetFromPath("skill/" + key) as WzSubProperty;
                    if (sourceNode != null)
                        break;
                }
                if (sourceNode == null)
                    return null;

                return (WzSubProperty)sourceNode.DeepClone();
            }
            catch
            {
                return null;
            }
            finally
            {
                try { img?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
            }
        }

        private static object ConvertWzPropertyToJson(WzImageProperty prop)
        {
            if (prop == null)
                return null;

            if (prop is WzSubProperty || prop.PropertyType == WzPropertyType.SubProperty || prop.PropertyType == WzPropertyType.Convex)
            {
                var obj = new Dictionary<string, object>();
                obj["_dirName"] = prop.Name ?? "";
                obj["_dirType"] = "sub";

                if (prop.WzProperties != null)
                {
                    foreach (WzImageProperty child in prop.WzProperties)
                    {
                        var childObj = ConvertWzPropertyToJson(child);
                        if (childObj != null)
                            obj[child.Name] = childObj;
                    }
                }

                return obj;
            }

            if (prop is WzCanvasProperty canvas)
            {
                var obj = new Dictionary<string, object>();
                obj["_dirName"] = canvas.Name ?? "";
                obj["_dirType"] = "canvas";

                if (TryGetCanvasSize(canvas, out int width, out int height))
                {
                    obj["_width"] = (long)width;
                    obj["_height"] = (long)height;
                }

                try
                {
                    Bitmap bmp = canvas.GetBitmap();
                    string base64 = BitmapToBase64(bmp);
                    if (!string.IsNullOrEmpty(base64))
                        obj["_image"] = base64;
                }
                catch { }

                if (canvas.WzProperties != null)
                {
                    foreach (WzImageProperty child in canvas.WzProperties)
                    {
                        var childObj = ConvertWzPropertyToJson(child);
                        if (childObj != null)
                            obj[child.Name] = childObj;
                    }
                }

                return obj;
            }

            if (prop is WzVectorProperty vec)
            {
                var obj = new Dictionary<string, object>();
                obj["_dirName"] = vec.Name ?? "";
                obj["_dirType"] = "vector";
                obj["_x"] = (long)vec.X.Value;
                obj["_y"] = (long)vec.Y.Value;
                return obj;
            }

            if (prop is WzIntProperty i32)
                return BuildTypedValueProperty(i32.Name, "Int", (long)i32.Value);
            if (prop is WzShortProperty i16)
                return BuildTypedValueProperty(i16.Name, "Short", (long)i16.Value);
            if (prop is WzLongProperty i64)
                return BuildTypedValueProperty(i64.Name, "Long", i64.Value);
            if (prop is WzFloatProperty f)
                return BuildTypedValueProperty(f.Name, "Float", (double)f.Value);
            if (prop is WzDoubleProperty d)
                return BuildTypedValueProperty(d.Name, "Double", d.Value);
            if (prop is WzStringProperty s)
                return BuildTypedValueProperty(s.Name, "String", s.Value ?? "");
            if (prop is WzUOLProperty u)
                return BuildTypedValueProperty(u.Name, "UOL", u.Value ?? "");

            return null;
        }

        private static Dictionary<string, object> BuildTypedValueProperty(string name, string dirType, object value)
        {
            var obj = new Dictionary<string, object>();
            obj["_dirName"] = name ?? "";
            obj["_dirType"] = dirType;
            obj["_value"] = value;
            return obj;
        }

        private static bool TryGetCanvasSize(WzCanvasProperty canvas, out int width, out int height)
        {
            width = 0;
            height = 0;

            if (canvas == null)
                return false;

            try
            {
                if (canvas.PngProperty != null)
                {
                    width = canvas.PngProperty.Width;
                    height = canvas.PngProperty.Height;
                    if (width > 0 && height > 0)
                        return true;
                }
            }
            catch { }

            try
            {
                Bitmap bmp = canvas.GetBitmap();
                if (bmp != null)
                {
                    width = bmp.Width;
                    height = bmp.Height;
                    if (width > 0 && height > 0)
                        return true;
                }
            }
            catch { }

            return false;
        }

        private static bool TryGetBitmapSize(Bitmap bmp, out int width, out int height)
        {
            width = 0;
            height = 0;
            if (bmp == null)
                return false;

            try
            {
                width = bmp.Width;
                height = bmp.Height;
                return width > 0 && height > 0;
            }
            catch
            {
                return false;
            }
        }

        private static string BitmapToBase64(Bitmap bmp)
        {
            if (bmp == null)
                return "";

            try
            {
                using (var ms = new MemoryStream())
                {
                    bmp.Save(ms, ImageFormat.Png);
                    return Convert.ToBase64String(ms.ToArray());
                }
            }
            catch
            {
                return "";
            }
        }

        private static Dictionary<string, object> GetOrCreateSkillContainer(Dictionary<string, object> root)
        {
            if (root == null) root = new Dictionary<string, object>();

            if (IsSkillContainer(root))
                return root;

            var skill = SimpleJson.GetObject(root, "skill");
            if (skill != null)
            {
                NormalizeSkillContainer(skill);
                return skill;
            }

            var created = new Dictionary<string, object>();
            created["_dirName"] = "skill";
            created["_dirType"] = "sub";
            root["skill"] = created;
            return created;
        }

        private static bool IsSkillContainer(Dictionary<string, object> obj)
        {
            if (obj == null) return false;
            string dirName = SimpleJson.GetString(obj, "_dirName");
            string dirType = SimpleJson.GetString(obj, "_dirType");
            return dirName == "skill" && dirType == "sub";
        }

        private static void NormalizeSkillContainer(Dictionary<string, object> skillContainer)
        {
            if (skillContainer == null) return;
            if (!skillContainer.ContainsKey("_dirName")) skillContainer["_dirName"] = "skill";
            if (!skillContainer.ContainsKey("_dirType")) skillContainer["_dirType"] = "sub";
        }

        private static Dictionary<string, object> LoadOrCreateJsonObject(string path)
        {
            if (!File.Exists(path))
            {
                Console.WriteLine($"  [warn] File not found: {path}");
                return new Dictionary<string, object>();
            }

            try
            {
                string content = TextFileHelper.ReadAllTextAuto(path);
                return SimpleJson.ParseObject(content);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [warn] Parse failed, recreating object: {ex.Message}");
                return new Dictionary<string, object>();
            }
        }

        private static HashSet<int> BuildSkillIdSet(List<SkillDefinition> skills)
        {
            var ids = new HashSet<int>();
            if (skills == null)
                return ids;

            foreach (var sd in skills)
            {
                if (sd != null && sd.SkillId > 0)
                    ids.Add(sd.SkillId);
            }
            return ids;
        }

        private static HashSet<int> LoadManagedSkillIdsForCleanup()
        {
            var ids = new HashSet<int>();
            AppendSkillIdsFromArrayFile(PathConfig.SuperSkillsJson, "skills", ids);
            AppendSkillIdsFromArrayFile(PathConfig.CustomSkillRoutesJson, "routes", ids);
            AppendSkillIdsFromArrayFile(PathConfig.NativeSkillInjectionsJson, "skills", ids);
            return ids;
        }

        private static void AppendSkillIdsFromArrayFile(string path, string arrayName, HashSet<int> ids)
        {
            if (ids == null || string.IsNullOrWhiteSpace(path) || !File.Exists(path))
                return;

            try
            {
                string content = TextFileHelper.ReadAllTextAuto(path);
                var root = SimpleJson.ParseObject(content);
                var arr = SimpleJson.GetArray(root, arrayName);
                if (arr == null)
                    return;

                foreach (var item in arr)
                {
                    if (!(item is Dictionary<string, object> entry))
                        continue;

                    int skillId = SimpleJson.GetInt(entry, "skillId", -1);
                    if (skillId > 0)
                        ids.Add(skillId);
                }
            }
            catch
            {
            }
        }

        private static void SaveJson(string path, Dictionary<string, object> root)
        {
            BackupHelper.Backup(path);
            EnsureDir(path);
            File.WriteAllText(path, SimpleJson.Serialize(root), Utf8NoBom);
            Console.WriteLine($"  [saved] {path}");
        }

        private static string GetIconBase64(SkillDefinition sd)
        {
            if (!string.IsNullOrEmpty(sd.IconBase64)) return sd.IconBase64;
            string base64 = PngHelper.LoadPngBase64(sd.Icon);
            if (!string.IsNullOrEmpty(base64)) return base64;
            return PngHelper.GeneratePlaceholderPngBase64(0x44, 0xBB, 0xFF, 0xFF);
        }

        private static string GetIconMouseOverBase64(SkillDefinition sd)
        {
            if (!string.IsNullOrEmpty(sd.IconMouseOverBase64)) return sd.IconMouseOverBase64;
            string base64 = PngHelper.LoadPngBase64(sd.IconMouseOver);
            return !string.IsNullOrEmpty(base64) ? base64 : "";
        }

        private static string GetIconDisabledBase64(SkillDefinition sd)
        {
            if (!string.IsNullOrEmpty(sd.IconDisabledBase64)) return sd.IconDisabledBase64;
            string base64 = PngHelper.LoadPngBase64(sd.IconDisabled);
            return !string.IsNullOrEmpty(base64) ? base64 : "";
        }

        private static void EnsureDir(string filePath)
        {
            string dir = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);
        }
    }
}
