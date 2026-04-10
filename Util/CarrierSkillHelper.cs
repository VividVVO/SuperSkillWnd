using System;
using System.Collections.Generic;
using System.IO;

namespace SuperSkillTool
{
    internal static class CarrierSkillHelper
    {
        private const int LegacyCarrierSkillId = 1001038;

        public static HashSet<int> GetStaleCarrierIds()
        {
            var stale = new HashSet<int>();
            int currentCarrierId = PathConfig.DefaultSuperSpCarrierSkillId;

            PathConfig.RegisterCarrierSkillId(currentCarrierId);
            AddIfStale(stale, LegacyCarrierSkillId, currentCarrierId);
            AddIfStale(stale, ReadDefaultCarrierId(PathConfig.SuperSkillsJson), currentCarrierId);
            AddIfStale(stale, ReadDefaultCarrierId(PathConfig.SuperSkillsServerJson), currentCarrierId);
            foreach (int knownCarrierId in PathConfig.GetKnownCarrierSkillIds())
                AddIfStale(stale, knownCarrierId, currentCarrierId);
            foreach (int discoveredCarrierId in DiscoverCarrierIdsFromDllJson())
                AddIfStale(stale, discoveredCarrierId, currentCarrierId);

            return stale;
        }

        public static HashSet<int> GetCarrierIdsPresentInDllJson()
        {
            var discovered = new HashSet<int>();
            foreach (int carrierId in DiscoverCarrierIdsFromDllJson())
            {
                if (carrierId > 0)
                    discovered.Add(carrierId);
            }
            return discovered;
        }

        public static int ResolveCarrierForWrite(SkillDefinition sd, HashSet<int> staleCarrierIds)
        {
            int defaultCarrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            int skillCarrierId = sd != null ? sd.SuperSpCarrierSkillId : 0;

            if (skillCarrierId <= 0)
                return defaultCarrierId;

            if (defaultCarrierId > 0 && skillCarrierId == defaultCarrierId)
                return defaultCarrierId;

            if (defaultCarrierId > 0 && staleCarrierIds != null && staleCarrierIds.Contains(skillCarrierId))
                return defaultCarrierId;

            return skillCarrierId;
        }

        public static bool IsStaleCarrierId(int carrierId, HashSet<int> staleCarrierIds)
        {
            return carrierId > 0 && staleCarrierIds != null && staleCarrierIds.Contains(carrierId);
        }

        private static void AddIfStale(HashSet<int> stale, int candidateCarrierId, int currentCarrierId)
        {
            if (candidateCarrierId > 0 && candidateCarrierId != currentCarrierId)
                stale.Add(candidateCarrierId);
        }

        private static int ReadDefaultCarrierId(string path)
        {
            if (string.IsNullOrEmpty(path) || !File.Exists(path))
                return 0;

            try
            {
                string json = TextFileHelper.ReadAllTextAuto(path);
                var root = SimpleJson.ParseObject(json);
                return SimpleJson.GetInt(root, "defaultSuperSpCarrierSkillId", 0);
            }
            catch
            {
                return 0;
            }
        }

        private static IEnumerable<int> DiscoverCarrierIdsFromDllJson()
        {
            var result = new HashSet<int>();
            DiscoverFromSkillImgJson(result);
            DiscoverFromStringImgJson(result);
            return result;
        }

        private static void DiscoverFromSkillImgJson(HashSet<int> result)
        {
            if (result == null)
                return;

            string dir = PathConfig.DllSkillJsonDir;
            if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir))
                return;

            string[] files;
            try
            {
                files = Directory.GetFiles(dir, "*.img.json", SearchOption.TopDirectoryOnly);
            }
            catch
            {
                return;
            }

            foreach (string file in files)
            {
                if (string.Equals(Path.GetFileName(file), "Skill.img.json", StringComparison.OrdinalIgnoreCase))
                    continue;

                try
                {
                    var root = SimpleJson.ParseObject(TextFileHelper.ReadAllTextAuto(file));
                    var skillRoot = SimpleJson.GetObject(root, "skill");
                    if (skillRoot == null)
                        continue;

                    foreach (var kv in skillRoot)
                    {
                        if (!int.TryParse(kv.Key, out int skillId) || skillId <= 0)
                            continue;
                        if (!(kv.Value is Dictionary<string, object> entry))
                            continue;

                        if (!IsLikelyCarrierSkillEntry(entry))
                            continue;

                        result.Add(skillId);
                        PathConfig.RegisterCarrierSkillId(skillId);
                    }
                }
                catch
                {
                }
            }
        }

        private static void DiscoverFromStringImgJson(HashSet<int> result)
        {
            if (result == null)
                return;

            string file = PathConfig.DllStringJson;
            if (string.IsNullOrEmpty(file) || !File.Exists(file))
                return;

            try
            {
                var root = SimpleJson.ParseObject(TextFileHelper.ReadAllTextAuto(file));
                foreach (var kv in root)
                {
                    if (!int.TryParse(kv.Key, out int skillId) || skillId <= 0)
                        continue;
                    if (!(kv.Value is Dictionary<string, object> entry))
                        continue;

                    if (!IsLikelyCarrierStringEntry(entry))
                        continue;

                    result.Add(skillId);
                    PathConfig.RegisterCarrierSkillId(skillId);
                }
            }
            catch
            {
            }
        }

        private static bool IsLikelyCarrierSkillEntry(Dictionary<string, object> entry)
        {
            if (entry == null || !entry.ContainsKey("_superSkill"))
                return false;

            int type = ReadChildInt(entry, "info", "type", 0);
            int maxLevel = ReadChildInt(entry, "common", "maxLevel", 0);
            return type == 50 && maxLevel == 30;
        }

        private static bool IsLikelyCarrierStringEntry(Dictionary<string, object> entry)
        {
            if (entry == null || !entry.ContainsKey("_superSkill"))
                return false;

            string name = ReadChildString(entry, "name");
            return string.Equals(name, "Super SP", StringComparison.OrdinalIgnoreCase);
        }

        private static int ReadChildInt(
            Dictionary<string, object> entry,
            string childName,
            string keyName,
            int fallback)
        {
            var child = SimpleJson.GetObject(entry, childName);
            if (child == null || !child.TryGetValue(keyName, out object raw))
                return fallback;

            if (raw is int i)
                return i;
            if (raw is long l)
                return (int)l;
            if (raw is double d)
                return (int)d;
            if (raw is string s && int.TryParse(s, out int parsed))
                return parsed;

            if (raw is Dictionary<string, object> prop)
            {
                if (!prop.TryGetValue("_value", out object pv))
                    return fallback;

                if (pv is int pi)
                    return pi;
                if (pv is long pl)
                    return (int)pl;
                if (pv is double pd)
                    return (int)pd;
                if (pv is string ps && int.TryParse(ps, out int pparsed))
                    return pparsed;
            }

            return fallback;
        }

        private static string ReadChildString(Dictionary<string, object> entry, string childName)
        {
            if (entry == null || !entry.TryGetValue(childName, out object raw))
                return "";

            if (raw is string direct)
                return direct;

            if (raw is Dictionary<string, object> prop)
            {
                if (prop.TryGetValue("_value", out object pv) && pv is string sv)
                    return sv;
            }

            return "";
        }
    }
}
