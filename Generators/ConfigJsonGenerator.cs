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
            RemoveFromArrayFile(PathConfig.SuperSkillsJson, "hiddenSkills", skills, dryRun);
            RemoveFromArrayFile(PathConfig.CustomSkillRoutesJson, "routes", skills, dryRun);
            RemoveFromArrayFile(PathConfig.NativeSkillInjectionsJson, "skills", skills, dryRun);
            RemoveFromArrayFile(PathConfig.SuperSkillsServerJson, "skills", skills, dryRun);
        }

        public static void Generate(List<SkillDefinition> skills, bool dryRun)
        {
            MountItemResolver.EnsureMountItemIds(skills, log: msg => Console.WriteLine("  " + msg));
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
            EnsureClientConfigReadme(root);
            var previousSkillEntries = SimpleJson.GetArray(root, "skills");
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

            // Reconcile super_skills.json root.hiddenSkills:
            // - per-skill super-window hiding
            // - carrier hiding in both native and super windows
            int carrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            var hiddenArr = EnsureArray(root, "hiddenSkills");
            var carrierIdsToHide = CarrierSkillHelper.GetCarrierIdsPresentInDllJson();
            if (carrierId > 0)
                carrierIdsToHide.Add(carrierId);

            var managedHiddenSkillIds = new HashSet<int>();
            if (previousSkillEntries != null)
            {
                var previousBySkillId = BuildSkillIdEntryMap(previousSkillEntries);
                foreach (var kv in previousBySkillId)
                {
                    if (kv.Key > 0)
                        managedHiddenSkillIds.Add(kv.Key);
                }
            }
            foreach (var sd in skills)
            {
                if (sd != null && sd.SkillId > 0)
                    managedHiddenSkillIds.Add(sd.SkillId);
            }
            if (carrierId > 0)
                managedHiddenSkillIds.Remove(carrierId);
            managedHiddenSkillIds.ExceptWith(staleCarrierIds);
            int removedManagedHidden = RemoveBySkillIds(hiddenArr, managedHiddenSkillIds, dryRun);
            if (removedManagedHidden > 0)
            {
                Console.WriteLine(dryRun
                    ? $"  [dry-run] Would remove {removedManagedHidden} managed hidden skill entrie(s)"
                    : $"  [write] Removed {removedManagedHidden} managed hidden skill entrie(s)");
            }

            if (carrierIdsToHide.Count > 0)
            {
                var carrierIdsToPrune = new HashSet<int>(staleCarrierIds);
                if (carrierId > 0)
                    carrierIdsToPrune.Add(carrierId);

                int prunedHidden = RemoveHiddenCarrierEntriesNotInSet(hiddenArr, carrierIdsToPrune, carrierIdsToHide, dryRun);
                if (prunedHidden > 0)
                {
                    Console.WriteLine(dryRun
                        ? $"  [dry-run] Would remove {prunedHidden} stale hidden carrier entrie(s)"
                        : $"  [write] Removed {prunedHidden} stale hidden carrier entrie(s)");
                }
            }

            int ensuredHidden = 0;
            foreach (int hideCarrierId in carrierIdsToHide)
            {
                var hiddenEntry = BuildHiddenCarrierEntry(hideCarrierId);
                ReplaceBySkillId(hiddenArr, hideCarrierId, hiddenEntry);
                ensuredHidden++;
            }

            foreach (var sd in skills)
            {
                if (sd == null || sd.SkillId <= 0 || !sd.HideFromSuperSkillWnd)
                    continue;

                var hiddenEntry = BuildHiddenSkillEntry(sd.SkillId, hideFromNative: false, hideFromSuper: true);
                ReplaceBySkillId(hiddenArr, sd.SkillId, hiddenEntry);
                ensuredHidden++;
            }

            if (ensuredHidden > 0)
            {
                if (dryRun)
                    Console.WriteLine($"  [dry-run] Would ensure {ensuredHidden} hidden entry(s)");
                else
                    Console.WriteLine($"  [write] Ensure {ensuredHidden} hidden entry(s)");
            }

            if (!dryRun) SaveJson(path, root);
        }

        private static void GenerateCustomRoutes(List<SkillDefinition> skills, bool dryRun)
        {
            string path = PathConfig.CustomSkillRoutesJson;
            Console.WriteLine($"\n[ConfigJson] Processing {path}");

            var root = LoadOrCreate(path);
            EnsureRoutesReadme(root);
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
            EnsureNativeInjectionReadme(root);
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
            EnsureServerConfigReadme(root);
            var existingArr = SimpleJson.GetArray(root, "skills");
            var existingBySkillId = BuildSkillIdEntryMap(existingArr);
            var existingMountBySkillId = MountItemResolver.BuildConfiguredMountMap(existingArr);
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
                var entry = BuildServerEntry(sd, staleCarrierIds, existingEntry, existingMountBySkillId);
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
            if (sd.BehaviorSkillId > 0)
                entry["behaviorSkillId"] = (long)sd.BehaviorSkillId;
            if (sd.VisibleJobId > 0)
                entry["visibleJobId"] = (long)sd.VisibleJobId;
            if (sd.MountItemId > 0)
                entry["mountItemId"] = (long)sd.MountItemId;
            if (sd.MountTamingMobId > 0)
                entry["mountTamingMobId"] = (long)sd.MountTamingMobId;
            if (sd.ShouldWriteMountSpeedOverride())
                entry["mountSpeed"] = (long)sd.MountSpeedOverride.Value;
            if (sd.ShouldWriteMountJumpOverride())
                entry["mountJump"] = (long)sd.MountJumpOverride.Value;
            if (sd.ShouldWriteMountFsOverride())
                entry["mountFs"] = sd.MountFsOverride.Value;
            if (sd.ShouldWriteMountSwimOverride())
                entry["mountSwim"] = sd.MountSwimOverride.Value;
            if (sd.MountedDoubleJumpEnabled)
            {
                entry["mountedDoubleJumpEnabled"] = true;
                entry["mountedDoubleJumpSkillId"] = (long)(sd.MountedDoubleJumpSkillId > 0 ? sd.MountedDoubleJumpSkillId : 3101003);
            }
            if (sd.MountedDemonJumpEnabled)
            {
                entry["mountedDemonJumpEnabled"] = true;
                entry["mountedDemonJumpSkillId"] = (long)(sd.MountedDemonJumpSkillId > 0 ? sd.MountedDemonJumpSkillId : 30010110);
            }
            AddPassiveBonusesIfAny(entry, sd);
            AddIndependentBuffClientHintsIfAny(entry, sd);
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
            if (ShouldForceSpecialMoveRoute(sd, packetRoute))
            {
                packetRoute = "special_move";
            }
            entry["packetRoute"] = packetRoute;
            if (sd.VisualSkillId > 0)
                entry["visualSkillId"] = (long)sd.VisualSkillId;
            if (sd.BorrowDonorVisual)
                entry["borrowDonorVisual"] = true;
            return entry;
        }

        private static bool ShouldForceSpecialMoveRoute(SkillDefinition sd, string packetRoute)
        {
            if (sd == null)
                return false;

            if (!string.IsNullOrEmpty(packetRoute)
                && !string.Equals(packetRoute, "close_range", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            return ContainsSpecialMoveActionSignal(sd.Action);
        }

        private static bool ContainsSpecialMoveActionSignal(string action)
        {
            if (string.IsNullOrWhiteSpace(action))
                return false;

            string[] keywords = { "doublejump", "flashjump", "dashjump", "rocketbooster", "demonfly" };
            foreach (string keyword in keywords)
            {
                if (action.IndexOf(keyword, StringComparison.OrdinalIgnoreCase) >= 0)
                    return true;
            }

            return false;
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
            Dictionary<string, object> existingEntry,
            Dictionary<int, int> existingMountBySkillId)
        {
            int superSpCost = sd.SuperSpCost > 0 ? sd.SuperSpCost : 1;
            int carrierId = CarrierSkillHelper.ResolveCarrierForWrite(sd, staleCarrierIds);
            int behaviorSkillId = ResolveBehaviorSkillId(sd);
            if (behaviorSkillId <= 0 && existingEntry != null)
                behaviorSkillId = SimpleJson.GetInt(existingEntry, "behaviorSkillId", 0);
            int mountItemId = sd.MountItemId;
            if (mountItemId <= 0 && existingEntry != null)
                mountItemId = SimpleJson.GetInt(existingEntry, "mountItemId", 0);
            if (mountItemId <= 0)
                mountItemId = MountItemResolver.ResolveConfiguredOrNativeMountItemId(sd, existingMountBySkillId);
            if (mountItemId > 0 && sd.MountItemId <= 0)
                sd.MountItemId = mountItemId;
            bool allowMountedFlight = sd.AllowMountedFlight;
            bool mountedDoubleJumpEnabled = sd.MountedDoubleJumpEnabled;
            int mountedDoubleJumpSkillId = sd.MountedDoubleJumpSkillId > 0 ? sd.MountedDoubleJumpSkillId : 3101003;
            bool mountedDemonJumpEnabled = sd.MountedDemonJumpEnabled;
            int mountedDemonJumpSkillId = sd.MountedDemonJumpSkillId > 0 ? sd.MountedDemonJumpSkillId : 30010110;
            int flightMountItemId = sd.FlightMountItemId;
            if (flightMountItemId <= 0 && existingEntry != null)
                flightMountItemId = SimpleJson.GetInt(existingEntry, "flightMountItemId", 0);
            var entry = BuildServerEntry(
                sd.SkillId,
                behaviorSkillId,
                mountItemId,
                flightMountItemId,
                allowMountedFlight,
                mountedDoubleJumpEnabled,
                mountedDoubleJumpSkillId,
                mountedDemonJumpEnabled,
                mountedDemonJumpSkillId,
                superSpCost,
                carrierId,
                sd.ServerEnabled);
            AddPassiveBonusesIfAny(entry, sd);
            AddIndependentBuffIfAny(entry, sd);
            return entry;
        }

        private static Dictionary<string, object> BuildServerEntry(
            int skillId,
            int behaviorSkillId,
            int mountItemId,
            int flightMountItemId,
            bool allowMountedFlight,
            bool mountedDoubleJumpEnabled,
            int mountedDoubleJumpSkillId,
            bool mountedDemonJumpEnabled,
            int mountedDemonJumpSkillId,
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
            if (flightMountItemId > 0)
                entry["flightMountItemId"] = (long)flightMountItemId;
            if (mountItemId > 0 || allowMountedFlight)
                entry["allowMountedFlight"] = allowMountedFlight;
            if (mountedDoubleJumpEnabled)
            {
                entry["mountedDoubleJumpEnabled"] = true;
                entry["mountedDoubleJumpSkillId"] = (long)(mountedDoubleJumpSkillId > 0 ? mountedDoubleJumpSkillId : 3101003);
            }
            if (mountedDemonJumpEnabled)
            {
                entry["mountedDemonJumpEnabled"] = true;
                entry["mountedDemonJumpSkillId"] = (long)(mountedDemonJumpSkillId > 0 ? mountedDemonJumpSkillId : 30010110);
            }
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
                int flightMountItemId = SimpleJson.GetInt(raw, "flightMountItemId", 0);
                bool allowMountedFlight = SimpleJson.GetBool(
                    raw,
                    "allowMountedFlight",
                    SimpleJson.GetBool(raw, "grantSoaringOnRide", false));
                bool mountedDoubleJumpEnabled = SimpleJson.GetBool(raw, "mountedDoubleJumpEnabled", false);
                int mountedDoubleJumpSkillId = SimpleJson.GetInt(raw, "mountedDoubleJumpSkillId", 3101003);
                bool mountedDemonJumpEnabled = SimpleJson.GetBool(raw, "mountedDemonJumpEnabled", false);
                int mountedDemonJumpSkillId = SimpleJson.GetInt(raw, "mountedDemonJumpSkillId", 30010110);
                bool enabled = SimpleJson.GetBool(raw, "enabled", true);
                var normalized = BuildServerEntry(
                    skillId,
                    behaviorSkillId,
                    mountItemId,
                    flightMountItemId,
                    allowMountedFlight,
                    mountedDoubleJumpEnabled,
                    mountedDoubleJumpSkillId,
                    mountedDemonJumpEnabled,
                    mountedDemonJumpSkillId,
                    superSpCost,
                    carrierId,
                    enabled);
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
            if (sd.BehaviorSkillId > 0)
                return sd.BehaviorSkillId;
            if (sd.DonorSkillId > 0)
                return sd.DonorSkillId;
            if (sd.ProxySkillId > 0)
                return sd.ProxySkillId;
            return 0;
        }

        private static void AddPassiveBonusesIfAny(Dictionary<string, object> entry, SkillDefinition sd)
        {
            if (entry == null || sd == null)
                return;
            var passiveBonuses = SkillDefinition.SerializePassiveBonuses(sd.PassiveBonuses);
            if (passiveBonuses == null || passiveBonuses.Count == 0)
                return;
            entry["passiveBonuses"] = passiveBonuses;
        }

        private static void AddIndependentBuffIfAny(Dictionary<string, object> entry, SkillDefinition sd)
        {
            if (entry == null || sd == null)
                return;
            var independentBuff = SkillDefinition.SerializeIndependentBuff(sd.BuildResolvedIndependentBuff());
            if (independentBuff == null || independentBuff.Count == 0)
                return;
            entry["independentBuff"] = independentBuff;
        }

        private static void AddIndependentBuffClientHintsIfAny(Dictionary<string, object> entry, SkillDefinition sd)
        {
            if (entry == null || sd == null || sd.IndependentBuff == null || !sd.IndependentBuff.IsConfigured())
                return;
            var resolvedBuff = sd.BuildResolvedIndependentBuff();
            entry["independentBuffEnabled"] = resolvedBuff.Enabled;
            if (resolvedBuff.SourceSkillId > 0)
                entry["independentSourceSkillId"] = resolvedBuff.SourceSkillId;
            if (!string.IsNullOrWhiteSpace(resolvedBuff.CarrierBuffStat))
                entry["independentCarrierBuffStat"] = resolvedBuff.CarrierBuffStat.Trim();
            if (!string.IsNullOrWhiteSpace(resolvedBuff.ClientBuffDisplayMode))
                entry["clientBuffDisplayMode"] = resolvedBuff.ClientBuffDisplayMode.Trim();
            if (!string.IsNullOrWhiteSpace(resolvedBuff.ClientNativeBuffStat))
                entry["clientNativeBuffStat"] = resolvedBuff.ClientNativeBuffStat.Trim();
            else if (!string.IsNullOrWhiteSpace(resolvedBuff.CarrierBuffStat))
                entry["clientNativeBuffStat"] = resolvedBuff.CarrierBuffStat.Trim();
            if (!string.IsNullOrWhiteSpace(resolvedBuff.ClientNativeValueField))
                entry["clientNativeValueField"] = resolvedBuff.ClientNativeValueField.Trim();
            if (resolvedBuff.StatBonuses != null && resolvedBuff.StatBonuses.Count > 0)
            {
                var clientStatBonuses = new Dictionary<string, object>();
                foreach (var kv in resolvedBuff.StatBonuses)
                {
                    if (string.IsNullOrWhiteSpace(kv.Key) || string.IsNullOrWhiteSpace(kv.Value))
                        continue;
                    object serialized = SkillDefinition.SerializePassiveValue(kv.Value);
                    if (serialized != null)
                        clientStatBonuses[kv.Key.Trim()] = serialized;
                }
                if (clientStatBonuses.Count > 0)
                    entry["independentStatBonuses"] = clientStatBonuses;
            }
        }

        private static void EnsureClientConfigReadme(Dictionary<string, object> root)
        {
            EnsureReadme(root, new string[]
            {
                "super_skills.json 由 SuperSkillTool 生成：客户端显示、隐藏、tooltip、本地识别和被动伤害包修正。",
                "visibleJobId 可限制技能只对指定职业显示；mountedDoubleJumpEnabled/mountedDoubleJumpSkillId 用于坐骑二段跳配置（仅写 super_skills.json）；mountedDemonJumpEnabled/mountedDemonJumpSkillId 用于骑宠恶魔跳跃配置；mountTamingMobId/mountSpeed/mountJump/mountFs/mountSwim 用于客户端骑宠数值修正。",
                "passiveBonuses 支持 damagePercent/ignoreDefensePercent/attackCount/mobCount；attackCount/mobCount 需要客户端/服务端扩展配合。",
                "unknown fields beginning with '_' are comments for humans and ignored by runtime parsers."
            });
        }

        private static void EnsureRoutesReadme(Dictionary<string, object> root)
        {
            EnsureReadme(root, new string[]
            {
                "custom_skill_routes.json 由 SuperSkillTool 生成：仅当客户端无法正确识别释放包类型时需要。",
                "packetRoute 请使用 close_range/ranged_attack/magic_attack/special_move/skill_effect/cancel_buff/special_attack/passive_energy。"
            });
        }

        private static void EnsureNativeInjectionReadme(Dictionary<string, object> root)
        {
            EnsureReadme(root, new string[]
            {
                "native_skill_injections.json 由 SuperSkillTool 生成：用于把超级技能注入原生技能窗。",
                "只使用超级技能面板时，可在工具中关闭 InjectToNative。"
            });
        }

        private static void EnsureServerConfigReadme(Dictionary<string, object> root)
        {
            EnsureReadme(root, new string[]
            {
                "super_skills_server.json 由 SuperSkillTool 生成：服务端真实行为、SP、BUFF、骑宠、被动加成配置。",
                "behaviorSkillId 决定真实执行效果；independentBuff 决定独立 BUFF 属性计算；passiveBonuses 决定指定技能被动加成。",
                "unknown fields beginning with '_' are comments for humans and ignored by Jackson parsing。"
            });
        }

        private static void EnsureReadme(Dictionary<string, object> root, string[] lines)
        {
            if (root == null || lines == null || lines.Length == 0)
                return;
            var readme = new List<object>();
            foreach (string line in lines)
            {
                if (!string.IsNullOrWhiteSpace(line))
                    readme.Add(line);
            }
            if (readme.Count > 0)
                root["_readme"] = readme;
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
            return BuildHiddenSkillEntry(carrierId, hideFromNative: true, hideFromSuper: true);
        }

        private static Dictionary<string, object> BuildHiddenSkillEntry(int skillId, bool hideFromNative, bool hideFromSuper)
        {
            var hiddenEntry = new Dictionary<string, object>();
            hiddenEntry["skillId"] = (long)skillId;
            if (hideFromNative)
                hiddenEntry["hideFromNativeSkillWnd"] = true;
            if (hideFromSuper)
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
