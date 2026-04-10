using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace SuperSkillTool
{
    /// <summary>
    /// Manages persistent settings stored in settings.json.
    /// All PathConfig values can be overridden at runtime.
    /// </summary>
    public static class SettingsManager
    {
        private static readonly string SettingsPath =
            Path.Combine(PathConfig.ToolRoot, "settings.json");

        /// <summary>
        /// Load settings from settings.json if it exists.
        /// Overrides PathConfig values.
        /// </summary>
        public static void Load()
        {
            if (!File.Exists(SettingsPath)) return;

            try
            {
                string json = TextFileHelper.ReadAllTextAuto(SettingsPath);
                var obj = SimpleJson.ParseObject(json);

                // Legacy keys (for migration)
                string legacyServerWzRoot = SimpleJson.GetString(obj, "serverWzRoot", "");
                string legacyServerStringXml = SimpleJson.GetString(obj, "serverStringXml", "");
                string legacySuperSkillsServerJson = SimpleJson.GetString(obj, "superSkillsServerJson", "");
                string legacyServerCharacterTamingMobRoot = SimpleJson.GetString(obj, "serverCharacterTamingMobRoot", "");
                string legacyServerTamingMobRoot = SimpleJson.GetString(obj, "serverTamingMobRoot", "");

                string legacyGameDataRoot = SimpleJson.GetString(obj, "gameDataRoot", "");
                string legacyGameCharacterTamingMobRoot = SimpleJson.GetString(obj, "gameCharacterTamingMobRoot", "");
                string legacyGameTamingMobRoot = SimpleJson.GetString(obj, "gameTamingMobRoot", "");

                // New simplified root keys
                string serverRoot = SimpleJson.GetString(obj, "serverRootDir",
                    SimpleJson.GetString(obj, "serverRoot", ""));
                if (string.IsNullOrWhiteSpace(serverRoot))
                {
                    serverRoot = PathConfig.GuessServerRootFromLegacyPaths(
                        legacyServerWzRoot,
                        legacySuperSkillsServerJson,
                        legacyServerStringXml,
                        legacyServerCharacterTamingMobRoot,
                        legacyServerTamingMobRoot);
                }
                if (!string.IsNullOrWhiteSpace(serverRoot))
                    PathConfig.ServerRootDir = serverRoot;

                string gameDataBase = SimpleJson.GetString(obj, "gameDataBaseDir",
                    SimpleJson.GetString(obj, "gameDataDir", ""));
                if (string.IsNullOrWhiteSpace(gameDataBase))
                {
                    gameDataBase = PathConfig.GuessGameDataBaseFromLegacyPaths(
                        legacyGameDataRoot,
                        legacyGameCharacterTamingMobRoot,
                        legacyGameTamingMobRoot,
                        SimpleJson.GetString(obj, "gameStringSkillImg", ""));
                }
                if (!string.IsNullOrWhiteSpace(gameDataBase))
                    PathConfig.GameDataBaseDir = gameDataBase;

                PathConfig.DllSkillJsonDir = SimpleJson.GetString(obj, "dllSkillJsonDir", PathConfig.DllSkillJsonDir);
                PathConfig.OutputDir = SimpleJson.GetString(obj, "outputDir", PathConfig.OutputDir);

                PathConfig.DefaultCharacterId = SimpleJson.GetInt(obj, "defaultCharacterId", PathConfig.DefaultCharacterId);
                PathConfig.DefaultSuperSpCarrierSkillId = SimpleJson.GetInt(obj, "defaultSuperSpCarrierSkillId", PathConfig.DefaultSuperSpCarrierSkillId);
                PathConfig.DefaultSuperSpCarrierMaxLevel = Math.Max(1, SimpleJson.GetInt(obj, "defaultSuperSpCarrierMaxLevel", PathConfig.DefaultSuperSpCarrierMaxLevel));
                PathConfig.RegisterCarrierSkillId(PathConfig.DefaultSuperSpCarrierSkillId);
                PathConfig.RegisterCarrierSkillIds(ReadIntArray(obj, "knownCarrierSkillIds"));
                PathConfig.RegisterCarrierSkillIds(ReadIntArray(obj, "carrierSkillHistory"));

                // Recompute all derived paths from simplified roots
                PathConfig.RecomputeDerivedPaths();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [warn] Failed to load settings.json: {ex.Message}");
            }
        }

        /// <summary>
        /// Save current PathConfig values to settings.json.
        /// </summary>
        public static void Save()
        {
            var obj = new Dictionary<string, object>();

            // New simplified keys
            obj["serverRootDir"] = PathConfig.ServerRootDir;
            obj["gameDataBaseDir"] = PathConfig.GameDataBaseDir;
            obj["dllSkillJsonDir"] = PathConfig.DllSkillJsonDir;
            obj["outputDir"] = PathConfig.OutputDir;

            // Compatibility keys (derived)
            obj["serverWzRoot"] = PathConfig.ServerWzRoot;
            obj["serverStringXml"] = PathConfig.ServerStringXml;
            obj["superSkillsServerJson"] = PathConfig.SuperSkillsServerJson;
            obj["gameDataRoot"] = PathConfig.GameDataRoot;
            obj["gameCharacterTamingMobRoot"] = PathConfig.GameCharacterTamingMobRoot;
            obj["gameTamingMobRoot"] = PathConfig.GameTamingMobRoot;
            obj["serverCharacterTamingMobRoot"] = PathConfig.ServerCharacterTamingMobRoot;
            obj["serverTamingMobRoot"] = PathConfig.ServerTamingMobRoot;

            obj["defaultCharacterId"] = (long)PathConfig.DefaultCharacterId;
            obj["defaultSuperSpCarrierSkillId"] = (long)PathConfig.DefaultSuperSpCarrierSkillId;
            obj["defaultSuperSpCarrierMaxLevel"] = (long)Math.Max(1, PathConfig.DefaultSuperSpCarrierMaxLevel);
            PathConfig.RegisterCarrierSkillId(PathConfig.DefaultSuperSpCarrierSkillId);
            var knownCarrierIds = new List<int>(PathConfig.GetKnownCarrierSkillIds());
            knownCarrierIds.Sort();
            var knownCarrierArray = new List<object>(knownCarrierIds.Count);
            foreach (int carrierId in knownCarrierIds)
                knownCarrierArray.Add((long)carrierId);
            obj["knownCarrierSkillIds"] = knownCarrierArray;

            string json = SimpleJson.Serialize(obj);
            File.WriteAllText(SettingsPath, json, new UTF8Encoding(false));
        }

        public static string GetSettingsPath() => SettingsPath;

        private static IEnumerable<int> ReadIntArray(Dictionary<string, object> obj, string key)
        {
            var result = new List<int>();
            var arr = SimpleJson.GetArray(obj, key);
            if (arr == null)
                return result;

            foreach (object item in arr)
            {
                switch (item)
                {
                    case int i when i > 0:
                        result.Add(i);
                        break;
                    case long l when l > 0 && l <= int.MaxValue:
                        result.Add((int)l);
                        break;
                    case double d when d > 0 && d <= int.MaxValue:
                        result.Add((int)d);
                        break;
                    case string s when int.TryParse(s, out int parsed) && parsed > 0:
                        result.Add(parsed);
                        break;
                }
            }

            return result;
        }
    }
}
