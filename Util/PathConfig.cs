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
        private static readonly string DefaultToolRoot = ResolveDefaultToolRoot();
        private static readonly string DefaultServerRoot = ResolveDefaultServerRoot();
        private static readonly string DefaultGameDataBase = ResolveDefaultGameDataBase();

        // Root directories
        public static string ServerRootDir = DefaultServerRoot;
        public static string GameDataBaseDir = DefaultGameDataBase;

        // Server WZ / XML paths (derived from ServerRootDir)
        public static string ServerWzRoot;
        public static string ServerStringXml;

        // DLL local resource JSON
        public static string DllSkillJsonDir;
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

        public static string SkillImgName(int jobId)
        {
            if (jobId < 0)
                return "";

            string plain = jobId + ".img";
            string d3 = jobId.ToString("D3") + ".img";
            string d4 = jobId.ToString("D4") + ".img";
            string preferred = jobId < 1000 ? d3 : plain;

            try
            {
                if (!string.IsNullOrWhiteSpace(GameDataRoot) && Directory.Exists(GameDataRoot))
                {
                    string preferredPath = Path.Combine(GameDataRoot, preferred);
                    if (File.Exists(preferredPath))
                        return preferred;

                    if (!string.Equals(d3, preferred, StringComparison.OrdinalIgnoreCase))
                    {
                        string d3Path = Path.Combine(GameDataRoot, d3);
                        if (File.Exists(d3Path))
                            return d3;
                    }

                    if (!string.Equals(plain, preferred, StringComparison.OrdinalIgnoreCase)
                        && !string.Equals(plain, d3, StringComparison.OrdinalIgnoreCase))
                    {
                        string plainPath = Path.Combine(GameDataRoot, plain);
                        if (File.Exists(plainPath))
                            return plain;
                    }

                    if (!string.Equals(d4, preferred, StringComparison.OrdinalIgnoreCase)
                        && !string.Equals(d4, plain, StringComparison.OrdinalIgnoreCase)
                        && !string.Equals(d4, d3, StringComparison.OrdinalIgnoreCase))
                    {
                        string d4Path = Path.Combine(GameDataRoot, d4);
                        if (File.Exists(d4Path))
                            return d4;
                    }
                }
            }
            catch
            {
            }

            return preferred;
        }

        public static string GameSkillImg(int jobId) =>
            Path.Combine(GameDataRoot, SkillImgName(jobId));

        public static string SkillKey(int skillId)
        {
            if (skillId <= 0)
                return "";
            return skillId <= 9999999 ? skillId.ToString("D7") : skillId.ToString();
        }

        public static IEnumerable<string> SkillKeyCandidates(int skillId)
        {
            if (skillId <= 0)
                yield break;

            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            string canonical = SkillKey(skillId);
            if (!string.IsNullOrEmpty(canonical) && seen.Add(canonical))
                yield return canonical;

            string raw = skillId.ToString();
            if (seen.Add(raw))
                yield return raw;

            for (int width = 2; width <= 8; width++)
            {
                string padded = skillId.ToString("D" + width);
                if (seen.Add(padded))
                    yield return padded;
            }
        }

        // Mount resources (.img / xml)
        public static string GameCharacterTamingMobRoot;
        public static string GameTamingMobRoot;
        public static string ServerCharacterTamingMobRoot;
        public static string ServerTamingMobRoot;

        // Tool output directory
        public static readonly string ToolRoot = DefaultToolRoot;

        /// <summary>
        /// Directory where tool configuration data files are stored
        /// (pending_skills.json, custom_mount_ids.json, etc.).
        /// Defaults to ToolRoot (current working directory).
        /// </summary>
        public static string ConfigDataDir = DefaultToolRoot;

        public static string OutputDir;
        public static string BackupRoot;

        // Constants (configurable)
        public static int DefaultCharacterId = 13745;
        public static int DefaultSuperSpCarrierSkillId = 1001038;
        public static int DefaultSuperSpCarrierMaxLevel = 999;
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
            ConfigDataDir = NormalizeDirectoryLikePath(ConfigDataDir);
            if (string.IsNullOrWhiteSpace(ConfigDataDir))
                ConfigDataDir = ToolRoot;

            ServerRootDir = NormalizeDirectoryLikePath(ServerRootDir);
            if (string.IsNullOrWhiteSpace(ServerRootDir))
                ServerRootDir = DefaultServerRoot;

            GameDataBaseDir = NormalizeDirectoryLikePath(GameDataBaseDir);
            if (string.IsNullOrWhiteSpace(GameDataBaseDir))
                GameDataBaseDir = DefaultGameDataBase;

            DllSkillJsonDir = Path.Combine(GameDataBaseDir, "Plugins", "SS", "Skill");

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

        private static string ResolveDefaultToolRoot()
        {
            string discovered = "";
            try
            {
                string cwd = NormalizeDirectoryLikePath(Environment.CurrentDirectory);
                if (IsUsableToolRoot(cwd) && HasToolRootMarker(cwd))
                    return cwd;
            }
            catch
            {
            }

            try
            {
                string baseDir = NormalizeDirectoryLikePath(AppContext.BaseDirectory);
                discovered = FindToolRootFrom(baseDir);
                if (!string.IsNullOrWhiteSpace(discovered))
                    return discovered;
                if (IsUsableToolRoot(baseDir))
                    return baseDir;
            }
            catch
            {
            }

            try
            {
                string cwd = NormalizeDirectoryLikePath(Environment.CurrentDirectory);
                if (IsUsableToolRoot(cwd))
                    return cwd;
            }
            catch
            {
            }

            return ".";
        }

        private static string FindToolRootFrom(string startDir)
        {
            string current = NormalizeDirectoryLikePath(startDir);
            if (string.IsNullOrWhiteSpace(current))
                return "";

            for (int i = 0; i < 8; i++)
            {
                if (IsUsableToolRoot(current) && HasToolRootMarker(current))
                    return current;

                DirectoryInfo parent;
                try
                {
                    parent = Directory.GetParent(current);
                }
                catch
                {
                    break;
                }

                if (parent == null)
                    break;
                current = NormalizeDirectoryLikePath(parent.FullName);
            }

            return "";
        }

        private static bool HasToolRootMarker(string dir)
        {
            if (string.IsNullOrWhiteSpace(dir))
                return false;

            try
            {
                return File.Exists(Path.Combine(dir, "settings.json"))
                    || File.Exists(Path.Combine(dir, "SuperSkillTool.csproj"))
                    || File.Exists(Path.Combine(dir, "pending_skills.json"));
            }
            catch
            {
                return false;
            }
        }

        private static bool IsUsableToolRoot(string dir)
        {
            if (string.IsNullOrWhiteSpace(dir))
                return false;
            return !IsWindowsSystemDirectory(dir);
        }

        private static bool IsWindowsSystemDirectory(string dir)
        {
            string normalized = NormalizeDirectoryLikePath(dir);
            if (string.IsNullOrWhiteSpace(normalized))
                return false;

            try
            {
                string windows = NormalizeDirectoryLikePath(Environment.GetFolderPath(Environment.SpecialFolder.Windows));
                if (string.IsNullOrWhiteSpace(windows))
                    return false;

                return PathEquals(normalized, windows)
                    || normalized.StartsWith(windows + "\\System32", StringComparison.OrdinalIgnoreCase)
                    || normalized.StartsWith(windows + "\\SysWOW64", StringComparison.OrdinalIgnoreCase);
            }
            catch
            {
                return false;
            }
        }

        private static string ResolveDefaultServerRoot()
        {
            try
            {
                string candidate = NormalizeDirectoryLikePath(Path.Combine(DefaultToolRoot, "..", "dasheng099"));
                if (!string.IsNullOrWhiteSpace(candidate) && Directory.Exists(candidate))
                    return candidate;
            }
            catch
            {
            }
            return DefaultToolRoot;
        }

        private static string ResolveDefaultGameDataBase()
        {
            try
            {
                string candidate = NormalizeDirectoryLikePath(Path.Combine(DefaultToolRoot, "Data"));
                if (!string.IsNullOrWhiteSpace(candidate) && Directory.Exists(candidate))
                    return candidate;
            }
            catch
            {
            }
            return Path.Combine(DefaultToolRoot, "Data");
        }

        // DllSkillJsonDir is now always derived from GameDataBaseDir:
        // <GameDataBaseDir>\Plugins\SS\Skill
    }
}
