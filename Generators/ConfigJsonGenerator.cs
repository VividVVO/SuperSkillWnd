using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace SuperSkillTool
{
    /// <summary>
    /// Updates config JSON files using safe per-skill upsert/remove semantics.
    /// Existing unrelated entries and non-array root fields are preserved.
    /// </summary>
    public static class ConfigJsonGenerator
    {
        private static readonly Encoding Utf8NoBom = new UTF8Encoding(false);

        public static void Remove(List<SkillDefinition> skills, bool dryRun)
        {
            if (skills == null || skills.Count == 0) return;

            RemoveFromArrayFile(PathConfig.SuperSkillsJson, "skills", skills, dryRun);
            RemoveFromArrayFile(PathConfig.CustomSkillRoutesJson, "routes", skills, dryRun);
            RemoveFromArrayFile(PathConfig.NativeSkillInjectionsJson, "skills", skills, dryRun);
            RemoveFromArrayFile(PathConfig.SuperSkillsServerJson, "skills", skills, dryRun);
        }

        public static void Generate(List<SkillDefinition> skills, bool dryRun)
        {
            GenerateSuperSkills(skills, dryRun);
            GenerateCustomRoutes(skills, dryRun);
            GenerateNativeInjections(skills, dryRun);
            GenerateSuperSkillsServer(skills, dryRun);
        }

        private static void GenerateSuperSkills(List<SkillDefinition> skills, bool dryRun)
        {
            string path = PathConfig.SuperSkillsJson;
            Console.WriteLine($"\n[ConfigJson] Processing {path}");

            var root = LoadOrCreate(path);
            int previousCarrierId = SimpleJson.GetInt(root, "defaultSuperSpCarrierSkillId", 0);
            int defaultCarrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            var staleCarrierIds = CarrierSkillHelper.GetStaleCarrierIds();
            if (previousCarrierId > 0 && previousCarrierId != defaultCarrierId)
                staleCarrierIds.Add(previousCarrierId);

            var carrierSkillIdsToPurge = new HashSet<int>(staleCarrierIds);
            if (defaultCarrierId > 0)
                carrierSkillIdsToPurge.Add(defaultCarrierId);

            if (defaultCarrierId > 0)
            {
                root["defaultSuperSpCarrierSkillId"] = (long)defaultCarrierId;
                if (dryRun)
                    Console.WriteLine($"  [dry-run] Would set defaultSuperSpCarrierSkillId = {defaultCarrierId}");
                else
                    Console.WriteLine($"  [write] Set defaultSuperSpCarrierSkillId = {defaultCarrierId}");
            }

            var arr = new List<object>();
            root["skills"] = arr;
            Console.WriteLine(dryRun
                ? $"  [dry-run] Would fully overwrite skills array ({skills.Count} entries)"
                : $"  [write] Fully overwrite skills array ({skills.Count} entries)");

            foreach (var sd in skills)
            {
                if (carrierSkillIdsToPurge.Contains(sd.SkillId))
                {
                    Console.WriteLine($"  [skip] Ignore carrier skill entry {sd.SkillId} in skills[]");
                    continue;
                }

                var entry = BuildSuperSkillsEntry(sd, staleCarrierIds);
                arr.Add(entry);

                if (dryRun)
                    Console.WriteLine($"  [dry-run] Would overwrite {sd.SkillId}");
                else
                    Console.WriteLine($"  [write] Overwrite {sd.SkillId}");
            }

            // Ensure carrier skill is hidden in both native and super skill windows.
            // This is written to super_skills.json root.hiddenSkills.
            int carrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            if (carrierId > 0)
            {
                var hiddenArr = EnsureArray(root, "hiddenSkills");
                var carrierIdsToHide = CarrierSkillHelper.GetCarrierIdsPresentInDllJson();
                carrierIdsToHide.Add(carrierId);

                var carrierIdsToPrune = new HashSet<int>(staleCarrierIds);
                carrierIdsToPrune.Add(carrierId);

                int prunedHidden = RemoveHiddenCarrierEntriesNotInSet(hiddenArr, carrierIdsToPrune, carrierIdsToHide, dryRun);
                if (prunedHidden > 0)
                {
                    Console.WriteLine(dryRun
                        ? $"  [dry-run] Would remove {prunedHidden} stale hidden carrier entrie(s)"
                        : $"  [write] Removed {prunedHidden} stale hidden carrier entrie(s)");
                }

                int ensuredHidden = 0;
                foreach (int hideCarrierId in carrierIdsToHide)
                {
                    var hiddenEntry = BuildHiddenCarrierEntry(hideCarrierId);
                    ReplaceBySkillId(hiddenArr, hideCarrierId, hiddenEntry);
                    ensuredHidden++;
                }

                if (dryRun)
                    Console.WriteLine($"  [dry-run] Would ensure {ensuredHidden} hidden carrier entrie(s)");
                else
                    Console.WriteLine($"  [write] Ensure {ensuredHidden} hidden carrier entrie(s)");
            }

            if (!dryRun) SaveJson(path, root);
        }

        private static void GenerateCustomRoutes(List<SkillDefinition> skills, bool dryRun)
        {
            string path = PathConfig.CustomSkillRoutesJson;
            Console.WriteLine($"\n[ConfigJson] Processing {path}");

            var root = LoadOrCreate(path);
            var existingArr = EnsureArray(root, "routes");
            var existingBySkillId = BuildSkillIdEntryMap(existingArr);
            var arr = new List<object>();
            root["routes"] = arr;
            Console.WriteLine(dryRun
                ? $"  [dry-run] Would fully overwrite routes array ({skills.Count} entries)"
                : $"  [write] Fully overwrite routes array ({skills.Count} entries)");

            foreach (var sd in skills)
            {
                if (sd.InfoType == 50 || string.IsNullOrEmpty(sd.ReleaseType))
                {
                    Console.WriteLine($"  [skip] {sd.SkillId} is passive/route-empty, no route needed.");
                    continue;
                }

                existingBySkillId.TryGetValue(sd.SkillId, out var existingEntry);
                var entry = BuildRouteEntry(sd, existingEntry);
                arr.Add(entry);

                if (dryRun)
                    Console.WriteLine($"  [dry-run] Would overwrite route for {sd.SkillId}");
                else
                    Console.WriteLine($"  [write] Overwrite route for {sd.SkillId}");
            }

            if (!dryRun) SaveJson(path, root);
        }

        private static void GenerateNativeInjections(List<SkillDefinition> skills, bool dryRun)
        {
            string path = PathConfig.NativeSkillInjectionsJson;
            Console.WriteLine($"\n[ConfigJson] Processing {path}");

            var root = LoadOrCreate(path);
            var arr = new List<object>();
            root["skills"] = arr;
            Console.WriteLine(dryRun
                ? $"  [dry-run] Would fully overwrite injections array ({skills.Count} entries)"
                : $"  [write] Fully overwrite injections array ({skills.Count} entries)");
            bool wroteAny = false;

            foreach (var sd in skills)
            {
                if (!sd.InjectToNative)
                {
                    continue;
                }

                int donorId = sd.DonorSkillId > 0
                    ? sd.DonorSkillId
                    : (sd.ProxySkillId > 0 ? sd.ProxySkillId : 0);
                if (donorId <= 0)
                {
                    Console.WriteLine($"  [skip] {sd.SkillId} injection needs donor/proxy skill.");
                    continue;
                }

                var entry = BuildInjectionEntry(sd, donorId);
                arr.Add(entry);
                wroteAny = true;

                if (dryRun)
                    Console.WriteLine($"  [dry-run] Would overwrite injection for {sd.SkillId}");
                else
                    Console.WriteLine($"  [write] Overwrite injection for {sd.SkillId}");
            }

            if (!wroteAny)
                Console.WriteLine("  [skip] No skills need native injection.");

            if (!dryRun) SaveJson(path, root);
        }

        private static void GenerateSuperSkillsServer(List<SkillDefinition> skills, bool dryRun)
        {
            string path = PathConfig.SuperSkillsServerJson;
            Console.WriteLine($"\n[ConfigJson] Processing {path}");

            var root = LoadOrCreate(path);
            var existingArr = SimpleJson.GetArray(root, "skills");
            var existingBySkillId = BuildSkillIdEntryMap(existingArr);
            int previousCarrierId = SimpleJson.GetInt(root, "defaultSuperSpCarrierSkillId", 0);
            int defaultCarrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            var staleCarrierIds = CarrierSkillHelper.GetStaleCarrierIds();
            if (previousCarrierId > 0 && previousCarrierId != defaultCarrierId)
                staleCarrierIds.Add(previousCarrierId);

            if (defaultCarrierId > 0)
            {
                root["defaultSuperSpCarrierSkillId"] = (long)defaultCarrierId;
                if (dryRun)
                    Console.WriteLine($"  [dry-run] Would set defaultSuperSpCarrierSkillId = {defaultCarrierId}");
                else
                    Console.WriteLine($"  [write] Set defaultSuperSpCarrierSkillId = {defaultCarrierId}");
            }
            var arr = new List<object>();
            root["skills"] = arr;
            Console.WriteLine(dryRun
                ? $"  [dry-run] Would fully overwrite skills array ({skills.Count} entries)"
                : $"  [write] Fully overwrite skills array ({skills.Count} entries)");

            foreach (var sd in skills)
            {
                if (staleCarrierIds.Contains(sd.SkillId) || sd.SkillId == defaultCarrierId)
                {
                    Console.WriteLine($"  [skip] Ignore carrier skill entry {sd.SkillId} in server skills[]");
                    continue;
                }

                existingBySkillId.TryGetValue(sd.SkillId, out var existingEntry);
                var entry = BuildServerEntry(sd, staleCarrierIds, existingEntry);
                arr.Add(entry);

                if (dryRun)
                    Console.WriteLine($"  [dry-run] Would overwrite server entry for {sd.SkillId}");
                else
                    Console.WriteLine($"  [write] Overwrite server entry for {sd.SkillId}");
            }

            if (!dryRun) SaveJson(path, root);
        }

        private static Dictionary<string, object> BuildSuperSkillsEntry(SkillDefinition sd, HashSet<int> staleCarrierIds)
        {
            var entry = new Dictionary<string, object>();
            entry["skillId"] = (long)sd.SkillId;
            entry["tab"] = sd.Tab ?? "active";
            entry["superSpCost"] = (long)sd.SuperSpCost;
            int carrierId = CarrierSkillHelper.ResolveCarrierForWrite(sd, staleCarrierIds);
            if (carrierId > 0)
                entry["superSpCarrierSkillId"] = (long)carrierId;
            entry["hideFromNativeSkillWnd"] = sd.HideFromNativeSkillWnd;
            entry["showInNativeWhenLearned"] = sd.ShowInNativeWhenLearned;
            entry["showInSuperWhenLearned"] = sd.ShowInSuperWhenLearned;
            entry["allowNativeUpgradeFallback"] = sd.AllowNativeUpgradeFallback;
            if ((sd.Tab ?? "active") == "passive" || sd.InfoType == 50)
                entry["passive"] = true;
            return entry;
        }

        private static Dictionary<string, object> BuildRouteEntry(SkillDefinition sd, Dictionary<string, object> existingEntry)
        {
            var entry = new Dictionary<string, object>();
            entry["skillId"] = (long)sd.SkillId;
            if (sd.ProxySkillId > 0)
                entry["proxySkillId"] = (long)sd.ProxySkillId;
            entry["releaseClass"] = string.IsNullOrEmpty(sd.ReleaseClass)
                ? "native_classifier_proxy"
                : sd.ReleaseClass;
            string packetRoute = sd.ReleaseType ?? "";
            if (string.IsNullOrEmpty(packetRoute) && existingEntry != null)
            {
                packetRoute = SimpleJson.GetString(existingEntry, "packetRoute", packetRoute);
            }
            if (string.Equals(packetRoute, "close_range", StringComparison.OrdinalIgnoreCase) && existingEntry != null)
            {
                string existingRoute = SimpleJson.GetString(existingEntry, "packetRoute", "");
                if (!string.IsNullOrEmpty(existingRoute)
                    && !string.Equals(existingRoute, "close_range", StringComparison.OrdinalIgnoreCase))
                {
                    packetRoute = existingRoute;
                }
            }
            entry["packetRoute"] = packetRoute;
            if (sd.VisualSkillId > 0)
                entry["visualSkillId"] = (long)sd.VisualSkillId;
            if (sd.BorrowDonorVisual)
                entry["borrowDonorVisual"] = true;
            return entry;
        }

        private static Dictionary<string, object> BuildInjectionEntry(SkillDefinition sd, int donorId)
        {
            var entry = new Dictionary<string, object>();
            entry["skillId"] = (long)sd.SkillId;
            entry["donorSkillId"] = (long)donorId;
            entry["enabled"] = sd.InjectEnabled;
            return entry;
        }

        private static Dictionary<string, object> BuildServerEntry(
            SkillDefinition sd,
            HashSet<int> staleCarrierIds,
            Dictionary<string, object> existingEntry)
        {
            int superSpCost = sd.SuperSpCost > 0 ? sd.SuperSpCost : 1;
            int carrierId = CarrierSkillHelper.ResolveCarrierForWrite(sd, staleCarrierIds);
            int behaviorSkillId = ResolveBehaviorSkillId(sd);
            if (behaviorSkillId <= 0 && existingEntry != null)
                behaviorSkillId = SimpleJson.GetInt(existingEntry, "behaviorSkillId", 0);
            int mountItemId = sd.MountItemId;
            if (mountItemId <= 0 && existingEntry != null)
                mountItemId = SimpleJson.GetInt(existingEntry, "mountItemId", 0);
            var entry = BuildServerEntry(sd.SkillId, behaviorSkillId, mountItemId, superSpCost, carrierId, sd.ServerEnabled);
            return entry;
        }

        private static Dictionary<string, object> BuildServerEntry(
            int skillId,
            int behaviorSkillId,
            int mountItemId,
            int superSpCost,
            int carrierId,
            bool enabled)
        {
            var entry = new Dictionary<string, object>();
            entry["skillId"] = (long)skillId;
            if (behaviorSkillId > 0)
                entry["behaviorSkillId"] = (long)behaviorSkillId;
            if (mountItemId > 0)
                entry["mountItemId"] = (long)mountItemId;
            entry["superSpCost"] = (long)(superSpCost > 0 ? superSpCost : 1);
            if (carrierId > 0)
                entry["superSpCarrierSkillId"] = (long)carrierId;
            entry["ignoreJobRequirement"] = true;
            entry["ignoreRequiredSkills"] = true;
            if (!enabled)
                entry["enabled"] = false;
            return entry;
        }

        private static void NormalizeServerEntries(List<object> arr, int defaultCarrierId, bool dryRun)
        {
            if (arr == null) return;

            for (int i = 0; i < arr.Count; i++)
            {
                if (!(arr[i] is Dictionary<string, object> raw))
                    continue;

                int skillId = SimpleJson.GetInt(raw, "skillId", -1);
                if (skillId <= 0)
                    continue;

                int superSpCost = SimpleJson.GetInt(raw, "superSpCost", 1);
                if (superSpCost <= 0) superSpCost = 1;

                int carrierId = SimpleJson.GetInt(raw, "superSpCarrierSkillId", 0);
                if (carrierId <= 0) carrierId = defaultCarrierId;

                int behaviorSkillId = SimpleJson.GetInt(raw, "behaviorSkillId", 0);
                int mountItemId = SimpleJson.GetInt(raw, "mountItemId", 0);
                bool enabled = SimpleJson.GetBool(raw, "enabled", true);
                var normalized = BuildServerEntry(skillId, behaviorSkillId, mountItemId, superSpCost, carrierId, enabled);
                if (DictionaryShallowEquals(raw, normalized))
                    continue;

                arr[i] = normalized;
                if (dryRun)
                    Console.WriteLine($"  [dry-run] Would normalize server entry {skillId}");
                else
                    Console.WriteLine($"  [write] Normalize server entry {skillId}");
            }
        }

        private static bool DictionaryShallowEquals(Dictionary<string, object> a, Dictionary<string, object> b)
        {
            if (ReferenceEquals(a, b)) return true;
            if (a == null || b == null) return false;
            if (a.Count != b.Count) return false;

            foreach (var kv in a)
            {
                if (!b.TryGetValue(kv.Key, out object bv))
                    return false;
                if (!ValueLooseEquals(kv.Value, bv))
                    return false;
            }
            return true;
        }

        private static bool ValueLooseEquals(object a, object b)
        {
            if (ReferenceEquals(a, b)) return true;
            if (a == null || b == null) return false;

            if (a is bool ab && b is bool bb)
                return ab == bb;

            if (IsNumber(a) && IsNumber(b))
                return Convert.ToInt64(a) == Convert.ToInt64(b);

            return string.Equals(a.ToString(), b.ToString(), StringComparison.Ordinal);
        }

        private static bool IsNumber(object value)
        {
            return value is byte
                || value is sbyte
                || value is short
                || value is ushort
                || value is int
                || value is uint
                || value is long
                || value is ulong;
        }

        private static int ResolveBehaviorSkillId(SkillDefinition sd)
        {
            if (sd == null)
                return 0;
            if (sd.DonorSkillId > 0)
                return sd.DonorSkillId;
            if (sd.ProxySkillId > 0)
                return sd.ProxySkillId;
            return 0;
        }

        private static Dictionary<string, object> FindBySkillId(List<object> arr, int skillId)
        {
            if (arr == null || skillId <= 0)
                return null;
            for (int i = 0; i < arr.Count; i++)
            {
                if (!(arr[i] is Dictionary<string, object> entry))
                    continue;
                if (SimpleJson.GetInt(entry, "skillId", -1) == skillId)
                    return entry;
            }
            return null;
        }

        private static Dictionary<int, Dictionary<string, object>> BuildSkillIdEntryMap(List<object> arr)
        {
            var result = new Dictionary<int, Dictionary<string, object>>();
            if (arr == null)
                return result;
            for (int i = 0; i < arr.Count; i++)
            {
                if (!(arr[i] is Dictionary<string, object> entry))
                    continue;
                int skillId = SimpleJson.GetInt(entry, "skillId", -1);
                if (skillId > 0)
                    result[skillId] = entry;
            }
            return result;
        }

        private static void ReplaceBySkillId(List<object> arr, int skillId, Dictionary<string, object> replacement)
        {
            for (int i = 0; i < arr.Count; i++)
            {
                if (!(arr[i] is Dictionary<string, object> entry)) continue;
                if (SimpleJson.GetInt(entry, "skillId", -1) != skillId) continue;
                arr[i] = replacement;
                return;
            }
            arr.Add(replacement);
        }

        private static int RemoveBySkillId(List<object> arr, int skillId, bool dryRun)
        {
            int removed = 0;
            for (int i = arr.Count - 1; i >= 0; i--)
            {
                if (!(arr[i] is Dictionary<string, object> entry)) continue;
                if (SimpleJson.GetInt(entry, "skillId", -1) != skillId) continue;
                removed++;
                if (!dryRun)
                    arr.RemoveAt(i);
            }
            return removed;
        }

        private static int RemoveBySkillIds(List<object> arr, HashSet<int> skillIds, bool dryRun)
        {
            if (arr == null || skillIds == null || skillIds.Count == 0)
                return 0;

            int removed = 0;
            for (int i = arr.Count - 1; i >= 0; i--)
            {
                if (!(arr[i] is Dictionary<string, object> entry))
                    continue;

                int skillId = SimpleJson.GetInt(entry, "skillId", -1);
                if (!skillIds.Contains(skillId))
                    continue;

                removed++;
                if (!dryRun)
                    arr.RemoveAt(i);
            }

            return removed;
        }

        private static int RemoveHiddenCarrierEntriesNotInSet(
            List<object> arr,
            HashSet<int> candidateCarrierIds,
            HashSet<int> keepCarrierIds,
            bool dryRun)
        {
            if (arr == null || candidateCarrierIds == null || candidateCarrierIds.Count == 0)
                return 0;

            int removed = 0;
            for (int i = arr.Count - 1; i >= 0; i--)
            {
                if (!(arr[i] is Dictionary<string, object> entry))
                    continue;

                int skillId = SimpleJson.GetInt(entry, "skillId", -1);
                if (!candidateCarrierIds.Contains(skillId))
                    continue;
                if (keepCarrierIds != null && keepCarrierIds.Contains(skillId))
                    continue;

                removed++;
                if (!dryRun)
                    arr.RemoveAt(i);
            }

            return removed;
        }

        private static Dictionary<string, object> BuildHiddenCarrierEntry(int carrierId)
        {
            var hiddenEntry = new Dictionary<string, object>();
            hiddenEntry["skillId"] = (long)carrierId;
            hiddenEntry["hideFromNativeSkillWnd"] = true;
            hiddenEntry["hideFromSuperSkillWnd"] = true;
            return hiddenEntry;
        }

        private static int RemapCarrierSkillIds(
            List<object> arr,
            HashSet<int> fromCarrierIds,
            int toCarrierId,
            bool dryRun)
        {
            if (arr == null || fromCarrierIds == null || fromCarrierIds.Count == 0 || toCarrierId <= 0)
                return 0;

            int changed = 0;
            for (int i = 0; i < arr.Count; i++)
            {
                if (!(arr[i] is Dictionary<string, object> entry))
                    continue;

                if (!entry.ContainsKey("superSpCarrierSkillId"))
                    continue;

                int carrierId = SimpleJson.GetInt(entry, "superSpCarrierSkillId", 0);
                if (!fromCarrierIds.Contains(carrierId))
                    continue;

                changed++;
                if (!dryRun)
                    entry["superSpCarrierSkillId"] = (long)toCarrierId;
            }

            return changed;
        }

        private static void RemoveFromArrayFile(
            string path,
            string arrayName,
            List<SkillDefinition> skills,
            bool dryRun)
        {
            Console.WriteLine($"\n[ConfigJson-Delete] Processing {path}");
            if (!File.Exists(path))
            {
                Console.WriteLine("  [skip] File not found.");
                return;
            }

            var root = LoadOrCreate(path);
            var arr = EnsureArray(root, arrayName);
            int removed = 0;

            var idSet = new HashSet<int>();
            foreach (var sd in skills) idSet.Add(sd.SkillId);

            for (int i = arr.Count - 1; i >= 0; i--)
            {
                if (!(arr[i] is Dictionary<string, object> entry)) continue;
                int skillId = SimpleJson.GetInt(entry, "skillId", -1);
                if (idSet.Contains(skillId))
                {
                    removed++;
                    if (!dryRun) arr.RemoveAt(i);
                }
            }

            if (removed == 0)
            {
                Console.WriteLine("  [skip] No matching entries.");
                return;
            }

            if (dryRun)
            {
                Console.WriteLine($"  [dry-run] Would remove {removed} entrie(s).");
            }
            else
            {
                SaveJson(path, root);
                Console.WriteLine($"  [saved] {path} ({removed} removed)");
            }
        }

        private static List<object> EnsureArray(Dictionary<string, object> root, string arrayName)
        {
            var arr = SimpleJson.GetArray(root, arrayName);
            if (arr == null)
            {
                arr = new List<object>();
                root[arrayName] = arr;
            }
            return arr;
        }

        private static Dictionary<string, object> LoadOrCreate(string path)
        {
            if (File.Exists(path))
            {
                try
                {
                    string json = TextFileHelper.ReadAllTextAuto(path);
                    return SimpleJson.ParseObject(json);
                }
                catch
                {
                    Console.WriteLine($"  [warn] Failed to parse {path}, will recreate");
                }
            }
            else
            {
                Console.WriteLine($"  [warn] File not found, will create: {path}");
            }
            return new Dictionary<string, object>();
        }

        private static void SaveJson(string path, Dictionary<string, object> root)
        {
            BackupHelper.Backup(path);
            string dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);
            File.WriteAllText(path, SimpleJson.Serialize(root), Utf8NoBom);
            Console.WriteLine($"  [saved] {path}");
        }
    }
}
