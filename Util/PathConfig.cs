using System;
using System.Collections.Generic;
using System.IO;

namespace SuperSkillTool
{
    /// <summary>
    /// All paths used by the tool. Defaults can be overridden via settings.json.
    /// </summary>
    public static class PathConfig
    {
        // Root directories
        public static string ServerRootDir = @"G:\code\dasheng099";
        public static string GameDataBaseDir = @"G:\code\mxd\Data";

        // Server WZ / XML paths (derived from ServerRootDir)
        public static string ServerWzRoot;
        public static string ServerStringXml;

        // DLL local resource JSON
        public static string DllSkillJsonDir = @"G:\code\c++\SuperSkillWnd\skill";
        public static string DllStringJson;

        public static string DllSkillImgJson(int jobId) =>
            Path.Combine(DllSkillJsonDir, jobId.ToString("D3") + ".img.json");

        // Config JSON files
        public static string SuperSkillsJson;
        public static string CustomSkillRoutesJson;
        public static string NativeSkillInjectionsJson;
        public static string SuperSkillsServerJson;

        // Game data (.img files, derived from GameDataBaseDir)
        public static string GameDataRoot;
        public static string GameStringSkillImg;

        public static string GameSkillImg(int jobId) =>
            Path.Combine(GameDataRoot, jobId + ".img");

        // Mount resources (.img / xml)
        public static string GameCharacterTamingMobRoot;
        public static string GameTamingMobRoot;
        public static string ServerCharacterTamingMobRoot;
        public static string ServerTamingMobRoot;

        // Tool output directory
        public static readonly string ToolRoot =
            @"G:\code\Harepacker-resurrected-master\SuperSkillTool";

        public static string OutputDir;
        public static string BackupRoot;

        // Constants (configurable)
        public static int DefaultCharacterId = 13745;
        public static int DefaultSuperSpCarrierSkillId = 1001038;
        public static int DefaultSuperSpCarrierMaxLevel = 32767;
        private static readonly HashSet<int> KnownCarrierSkillIds = new HashSet<int>();

        // Derived helpers
        public static string ServerSkillXml(int jobId) =>
            Path.Combine(ServerWzRoot, jobId.ToString("D3") + ".img.xml");

        public static string MountActionImgName(int mountItemId)
        {
            if (mountItemId <= 0)
                return "";

            string direct = mountItemId.ToString("D8") + ".img"; // e.g. 1932254 -> 01932254.img
            int suffix = Math.Abs(mountItemId % 10000);
            string legacy = "0190" + suffix.ToString("D4") + ".img";

            try
            {
                if (!string.IsNullOrWhiteSpace(GameCharacterTamingMobRoot) && Directory.Exists(GameCharacterTamingMobRoot))
                {
                    string directPath = Path.Combine(GameCharacterTamingMobRoot, direct);
                    if (File.Exists(directPath))
                        return direct;

                    if (!string.Equals(direct, legacy, StringComparison.OrdinalIgnoreCase))
                    {
                        string legacyPath = Path.Combine(GameCharacterTamingMobRoot, legacy);
                        if (File.Exists(legacyPath))
                            return legacy;
                    }
                }
            }
            catch
            {
            }

            return direct;
        }

        public static string GameMountActionImg(int mountItemId) =>
            Path.Combine(GameCharacterTamingMobRoot, MountActionImgName(mountItemId));

        public static string ServerMountActionXml(int mountItemId) =>
            Path.Combine(ServerCharacterTamingMobRoot, MountActionImgName(mountItemId) + ".xml");

        public static string GameMountDataImg(int tamingMobId) =>
            Path.Combine(GameTamingMobRoot, tamingMobId.ToString("D4") + ".img");

        public static string ServerMountDataXml(int tamingMobId) =>
            Path.Combine(ServerTamingMobRoot, tamingMobId.ToString("D4") + ".img.xml");

        public static int JobIdFromSkillId(int skillId) => skillId / 10000;

        /// <summary>
        /// Recomputes paths that derive from base directories.
        /// Must be called after changing root dirs / dll dir / output dir.
        /// </summary>
        public static void RecomputeDerivedPaths()
        {
            ServerRootDir = NormalizeDirectoryLikePath(ServerRootDir);
            if (string.IsNullOrWhiteSpace(ServerRootDir))
                ServerRootDir = @"G:\code\dasheng099";

            GameDataBaseDir = NormalizeDirectoryLikePath(GameDataBaseDir);
            if (string.IsNullOrWhiteSpace(GameDataBaseDir))
                GameDataBaseDir = @"G:\code\mxd\Data";

            DllSkillJsonDir = NormalizeDirectoryLikePath(DllSkillJsonDir);
            if (string.IsNullOrWhiteSpace(DllSkillJsonDir))
                DllSkillJsonDir = @"G:\code\c++\SuperSkillWnd\skill";

            ServerWzRoot = Path.Combine(ServerRootDir, "wz", "Skill.wz");
            ServerStringXml = Path.Combine(ServerRootDir, "wz", "String.wz", "Skill.img.xml");
            SuperSkillsServerJson = Path.Combine(ServerRootDir, "super_skills_server.json");
            ServerCharacterTamingMobRoot = Path.Combine(ServerRootDir, "wz", "Character.wz", "TamingMob");
            ServerTamingMobRoot = Path.Combine(ServerRootDir, "wz", "TamingMob.wz");

            GameDataRoot = Path.Combine(GameDataBaseDir, "Skill");
            GameStringSkillImg = Path.Combine(GameDataBaseDir, "String", "Skill.img");
            GameCharacterTamingMobRoot = Path.Combine(GameDataBaseDir, "Character", "TamingMob");
            GameTamingMobRoot = Path.Combine(GameDataBaseDir, "TamingMob");

            DllStringJson = Path.Combine(DllSkillJsonDir, "Skill.img.json");
            SuperSkillsJson = Path.Combine(DllSkillJsonDir, "super_skills.json");
            CustomSkillRoutesJson = Path.Combine(DllSkillJsonDir, "custom_skill_routes.json");
            NativeSkillInjectionsJson = Path.Combine(DllSkillJsonDir, "native_skill_injections.json");

            string legacyToolOutput = Path.Combine(ToolRoot, "output");
            if (string.IsNullOrWhiteSpace(OutputDir) || PathEquals(OutputDir, legacyToolOutput))
                OutputDir = Path.Combine(ServerRootDir, "output");

            OutputDir = NormalizeDirectoryLikePath(OutputDir);
            BackupRoot = Path.Combine(OutputDir, "backup");
        }

        static PathConfig()
        {
            OutputDir = Path.Combine(ServerRootDir, "output");
            RecomputeDerivedPaths();
            RegisterCarrierSkillId(DefaultSuperSpCarrierSkillId);
        }

        public static string GuessServerRootFromLegacyPaths(
            string serverWzRoot,
            string superSkillsServerJson,
            string serverStringXml,
            string serverCharacterTamingMobRoot,
            string serverTamingMobRoot)
        {
            string fromSuperSkillJson = NormalizeDirectoryLikePath(Path.GetDirectoryName((superSkillsServerJson ?? "").Trim()));
            if (!string.IsNullOrWhiteSpace(fromSuperSkillJson))
                return fromSuperSkillJson;

            string fromSkillWz = TrimKnownSuffix(serverWzRoot, @"\wz\Skill.wz");
            if (!string.IsNullOrWhiteSpace(fromSkillWz))
                return fromSkillWz;

            string fromStringXml = TrimKnownSuffix(serverStringXml, @"\wz\String.wz\Skill.img.xml");
            if (!string.IsNullOrWhiteSpace(fromStringXml))
                return fromStringXml;

            string fromCharTm = TrimKnownSuffix(serverCharacterTamingMobRoot, @"\wz\Character.wz\TamingMob");
            if (!string.IsNullOrWhiteSpace(fromCharTm))
                return fromCharTm;

            string fromTm = TrimKnownSuffix(serverTamingMobRoot, @"\wz\TamingMob.wz");
            if (!string.IsNullOrWhiteSpace(fromTm))
                return fromTm;

            return "";
        }

        public static string GuessGameDataBaseFromLegacyPaths(
            string gameDataRoot,
            string gameCharacterTamingMobRoot,
            string gameTamingMobRoot,
            string gameStringSkillImg)
        {
            string fromSkill = TrimKnownSuffix(gameDataRoot, @"\Skill");
            if (!string.IsNullOrWhiteSpace(fromSkill))
                return fromSkill;

            string fromCharTm = TrimKnownSuffix(gameCharacterTamingMobRoot, @"\Character\TamingMob");
            if (!string.IsNullOrWhiteSpace(fromCharTm))
                return fromCharTm;

            string fromTm = TrimKnownSuffix(gameTamingMobRoot, @"\TamingMob");
            if (!string.IsNullOrWhiteSpace(fromTm))
                return fromTm;

            string fromString = TrimKnownSuffix(gameStringSkillImg, @"\String\Skill.img");
            if (!string.IsNullOrWhiteSpace(fromString))
                return fromString;

            return "";
        }

        public static void RegisterCarrierSkillId(int carrierSkillId)
        {
            if (carrierSkillId > 0)
                KnownCarrierSkillIds.Add(carrierSkillId);
        }

        public static void RegisterCarrierSkillIds(IEnumerable<int> carrierSkillIds)
        {
            if (carrierSkillIds == null)
                return;

            foreach (int carrierSkillId in carrierSkillIds)
                RegisterCarrierSkillId(carrierSkillId);
        }

        public static HashSet<int> GetKnownCarrierSkillIds()
        {
            return new HashSet<int>(KnownCarrierSkillIds);
        }

        private static string NormalizeDirectoryLikePath(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
                return "";

            string normalized = path.Trim().Trim('"').Replace('/', '\\');
            return normalized.TrimEnd('\\');
        }

        private static string TrimKnownSuffix(string input, string suffix)
        {
            string normalizedInput = NormalizeDirectoryLikePath(input);
            string normalizedSuffix = NormalizeDirectoryLikePath(suffix);
            if (string.IsNullOrWhiteSpace(normalizedInput) || string.IsNullOrWhiteSpace(normalizedSuffix))
                return "";

            if (!normalizedInput.EndsWith(normalizedSuffix, StringComparison.OrdinalIgnoreCase))
                return "";

            string head = normalizedInput.Substring(0, normalizedInput.Length - normalizedSuffix.Length);
            return NormalizeDirectoryLikePath(head);
        }

        private static bool PathEquals(string a, string b)
        {
            return string.Equals(
                NormalizeDirectoryLikePath(a),
                NormalizeDirectoryLikePath(b),
                StringComparison.OrdinalIgnoreCase);
        }
    }
}
