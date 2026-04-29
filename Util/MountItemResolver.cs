using System;
using System.Collections.Generic;
using System.IO;
using MapleLib.WzLib;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    public static class MountItemResolver
    {
        private const bool Gms = false;

        public static int EnsureMountItemIds(
            IEnumerable<SkillDefinition> skills,
            IDictionary<int, int> configuredMounts = null,
            Action<string> log = null)
        {
            if (skills == null)
                return 0;

            var mountMap = configuredMounts != null
                ? new Dictionary<int, int>(configuredMounts)
                : LoadConfiguredMountMap();

            int resolved = 0;
            foreach (var sd in skills)
            {
                if (sd == null || sd.SkillId <= 0)
                    continue;

                if (TryEnsureMountItemId(sd, mountMap, log))
                    resolved++;

                if (sd.MountItemId > 0)
                    mountMap[sd.SkillId] = sd.MountItemId;
            }

            return resolved;
        }

        public static bool TryEnsureMountItemId(
            SkillDefinition sd,
            IDictionary<int, int> configuredMounts = null,
            Action<string> log = null)
        {
            if (sd == null || sd.SkillId <= 0 || sd.MountItemId > 0)
                return false;

            int mountItemId = ResolveConfiguredOrNativeMountItemId(sd, configuredMounts, out int sourceSkillId);
            if (mountItemId <= 0)
                return false;

            sd.MountItemId = mountItemId;
            log?.Invoke(sourceSkillId > 0 && sourceSkillId != sd.SkillId
                ? $"[MountItem] Auto resolved skill {sd.SkillId}: mountItemId={mountItemId} from source skill {sourceSkillId}"
                : $"[MountItem] Auto resolved skill {sd.SkillId}: mountItemId={mountItemId}");
            return true;
        }

        public static int ResolveConfiguredOrNativeMountItemId(
            SkillDefinition sd,
            IDictionary<int, int> configuredMounts = null)
        {
            return ResolveConfiguredOrNativeMountItemId(sd, configuredMounts, out _);
        }

        public static int ResolveConfiguredOrNativeMountItemId(
            SkillDefinition sd,
            IDictionary<int, int> configuredMounts,
            out int sourceSkillId)
        {
            sourceSkillId = 0;
            if (sd == null || sd.SkillId <= 0)
                return 0;
            if (sd.MountItemId > 0)
            {
                sourceSkillId = sd.SkillId;
                return sd.MountItemId;
            }

            foreach (int candidate in EnumerateMountSourceSkillIds(sd))
            {
                int mountItemId = ResolveConfiguredOrNativeMountItemIdForSkillId(candidate, configuredMounts);
                if (mountItemId > 0)
                {
                    sourceSkillId = candidate;
                    return mountItemId;
                }
            }

            return 0;
        }

        public static int ResolveConfiguredOrNativeMountItemIdForSkillId(
            int sourceId,
            IDictionary<int, int> configuredMounts = null)
        {
            if (sourceId <= 0)
                return 0;

            if (configuredMounts != null
                && configuredMounts.TryGetValue(sourceId, out int configuredMountItemId)
                && configuredMountItemId > 0)
            {
                return configuredMountItemId;
            }

            return ResolveNativeMountItemId(sourceId);
        }

        public static Dictionary<int, int> LoadConfiguredMountMap(string path = null)
        {
            string configPath = string.IsNullOrWhiteSpace(path) ? PathConfig.SuperSkillsServerJson : path;
            var result = new Dictionary<int, int>();
            if (string.IsNullOrWhiteSpace(configPath) || !File.Exists(configPath))
                return result;

            try
            {
                string json = TextFileHelper.ReadAllTextAuto(configPath);
                var root = SimpleJson.ParseObject(json);
                return BuildConfiguredMountMap(SimpleJson.GetArray(root, "skills"));
            }
            catch
            {
                return result;
            }
        }

        public static Dictionary<int, int> BuildConfiguredMountMap(List<object> arr)
        {
            var result = new Dictionary<int, int>();
            if (arr == null)
                return result;

            foreach (var item in arr)
            {
                if (!(item is Dictionary<string, object> entry))
                    continue;

                int skillId = SimpleJson.GetInt(entry, "skillId", -1);
                int mountItemId = SimpleJson.GetInt(entry, "mountItemId", 0);
                if (skillId > 0 && mountItemId > 0)
                    result[skillId] = mountItemId;
            }

            return result;
        }

        public static int ResolveNativeMountItemId(int sourceId)
        {
            return ResolveNativeMountItemId(sourceId, depth: 0);
        }

        private static int ResolveNativeMountItemId(int sourceId, int depth)
        {
            if (sourceId <= 0 || depth > 8)
                return 0;

            switch (sourceId)
            {
                case 5221006:
                    return 1932000;
                case 33001001:
                    return 1932015;
                case 35001002:
                case 35120000:
                    return 1932016;
            }

            int jobId = sourceId / 10000;
            if (!IsBeginnerJob(jobId))
            {
                if (jobId == 8000 && sourceId != 80001000)
                {
                    if (TryReadNativeTamingMobFromSkill(sourceId, out int tamingMobId) && tamingMobId > 0)
                        return tamingMobId;

                    int link = GetLinkedMountItem(sourceId);
                    if (link > 0)
                        return link < 10000 ? ResolveNativeMountItemId(link, depth + 1) : link;
                }

                return 0;
            }

            int suffix = sourceId % 10000;
            switch (suffix)
            {
                case 1013:
                case 1046:
                    return 1932001;
                case 1015:
                case 1048:
                    return 1932002;
                case 1016:
                case 1017:
                case 1027:
                    return 1932007;
                case 1018:
                    return 1932003;
                case 1019:
                    return 1932005;
                case 1025:
                    return 1932006;
                case 1028:
                    return 1932008;
                case 1029:
                    return 1932009;
                case 1030:
                    return 1932011;
                case 1031:
                    return 1932010;
                case 1033:
                    return 1932013;
                case 1034:
                    return 1932014;
                case 1035:
                    return 1932012;
                case 1036:
                    return 1932017;
                case 1037:
                    return 1932018;
                case 1038:
                    return 1932019;
                case 1039:
                    return 1932020;
                case 1040:
                    return 1932021;
                case 1042:
                    return 1932022;
                case 1044:
                    return 1932023;
                case 1049:
                    return 1932025;
                case 1050:
                    return 1932004;
                case 1051:
                    return 1932026;
                case 1052:
                    return 1932027;
                case 1053:
                    return 1932028;
                case 1054:
                    return 1932029;
                case 1063:
                    return 1932034;
                case 1064:
                    return 1932035;
                case 1065:
                    return 1932037;
                case 1069:
                    return 1932038;
                case 1070:
                    return 1932039;
                case 1071:
                    return 1932040;
                case 1072:
                    return 1932041;
                case 1084:
                    return 1932043;
                case 1089:
                    return 1932044;
                case 1096:
                    return 1932045;
                case 1101:
                    return 1932046;
                case 1102:
                    return Gms ? 1932061 : 1932047;
                case 1106:
                    return 1932048;
                case 1118:
                    return 1932060;
                case 1115:
                    return 1932052;
                case 1121:
                    return 1932063;
                case 1122:
                    return 1932064;
                case 1123:
                    return 1932065;
                case 1124:
                case 1128:
                    return 1932066;
                case 1129:
                    return 1932071;
                case 1130:
                    return 1932072;
                case 1136:
                    return 1932078;
                case 1138:
                    return 1932080;
                case 1139:
                    return 1932081;
                case 1158:
                    return 1932083;
                default:
                    if (suffix >= 1143 && suffix <= 1157)
                        return 1992000 + suffix - 1143;
                    return 0;
            }
        }

        private static int GetLinkedMountItem(int sourceId)
        {
            int suffix = sourceId % 1000;
            switch (suffix)
            {
                case 1:
                case 24:
                case 25:
                    return 1018;
                case 2:
                case 26:
                    return 1019;
                case 3:
                    return 1025;
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                    return suffix + 1023;
                case 9:
                case 10:
                case 11:
                    return suffix + 1024;
                case 12:
                    return 1042;
                case 13:
                    return 1044;
                case 14:
                    return 1049;
                case 15:
                case 16:
                case 17:
                    return suffix + 1036;
                case 18:
                case 19:
                    return suffix + 1045;
                case 20:
                    return 1072;
                case 21:
                    return 1084;
                case 22:
                    return 1089;
                case 23:
                    return 1106;
                case 29:
                    return 1151;
                case 30:
                case 50:
                    return 1054;
                case 31:
                case 51:
                    return 1069;
                case 32:
                    return 1138;
                case 45:
                case 46:
                case 47:
                case 48:
                case 49:
                    return suffix + 1009;
                case 52:
                    return 1070;
                case 53:
                    return 1071;
                case 54:
                    return 1096;
                case 55:
                    return 1101;
                case 56:
                    return 1102;
                case 58:
                    return 1118;
                case 59:
                    return 1121;
                case 60:
                    return 1122;
                case 61:
                    return 1129;
                case 62:
                    return 1139;
                case 63:
                case 64:
                case 65:
                case 66:
                case 67:
                case 68:
                case 69:
                case 70:
                case 71:
                case 72:
                case 73:
                case 74:
                case 75:
                case 76:
                case 77:
                case 78:
                    return suffix + 1080;
                case 85:
                case 86:
                case 87:
                    return suffix + 928;
                case 88:
                    return 1065;
                case 27:
                    return 1932049;
                case 28:
                    return 1932050;
                case 114:
                    return 1932099;
                default:
                    return 0;
            }
        }

        private static IEnumerable<int> EnumerateMountSourceSkillIds(SkillDefinition sd)
        {
            var seen = new HashSet<int>();
            var yieldIds = new List<int>();

            void Add(int skillId)
            {
                if (skillId > 0 && seen.Add(skillId))
                    yieldIds.Add(skillId);
            }

            Add(sd.SkillId);
            Add(sd.DonorSkillId);
            Add(sd.ProxySkillId);
            Add(sd.VisualSkillId);
            Add(sd.CloneFromSkillId);
            Add(sd.ResolveCloneSourceSkillId());

            foreach (int skillId in yieldIds)
                yield return skillId;
        }

        private static bool IsBeginnerJob(int jobId)
        {
            return jobId == 0
                || jobId == 1
                || jobId == 1000
                || jobId == 2000
                || jobId == 2001
                || jobId == 3000
                || jobId == 3001
                || jobId == 2002;
        }

        private static bool TryReadNativeTamingMobFromSkill(int sourceId, out int tamingMobId)
        {
            tamingMobId = 0;
            try
            {
                int jobId = sourceId / 10000;
                string imgPath = PathConfig.GameSkillImg(jobId);
                if (string.IsNullOrWhiteSpace(imgPath) || !File.Exists(imgPath))
                    return false;

                if (!TryOpenParsedImage(imgPath, out var fs, out var wzImg, out _))
                    return false;

                try
                {
                    foreach (string key in PathConfig.SkillKeyCandidates(sourceId))
                    {
                        string skillPath = "skill/" + key;
                        string[] paths =
                        {
                            skillPath + "/info/tamingMob",
                            skillPath + "/common/tamingMob",
                            skillPath + "/level/1/tamingMob",
                            skillPath + "/tamingMob"
                        };

                        foreach (string path in paths)
                        {
                            int value = ReadIntProperty(wzImg.GetFromPath(path), 0);
                            if (value > 0)
                            {
                                tamingMobId = value;
                                return true;
                            }
                        }
                    }
                }
                finally
                {
                    try { wzImg?.Dispose(); } catch { }
                    try { fs?.Dispose(); } catch { }
                }
            }
            catch
            {
                return false;
            }

            return false;
        }

        private static bool TryOpenParsedImage(string imgPath, out FileStream fs, out WzImage wzImg, out WzMapleVersion version)
        {
            fs = null;
            wzImg = null;
            version = WzMapleVersion.EMS;

            var versions = new List<WzMapleVersion>
            {
                WzImageVersionHelper.DetectVersionForSkillImg(imgPath),
                WzImageVersionHelper.DetectPreferredVersionFromGameData(),
                WzMapleVersion.BMS,
                WzMapleVersion.CLASSIC,
                WzMapleVersion.GMS,
                WzMapleVersion.EMS
            };

            var seen = new HashSet<WzMapleVersion>();
            foreach (var candidate in versions)
            {
                if (!seen.Add(candidate))
                    continue;

                try
                {
                    fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                    wzImg = new WzImage(Path.GetFileName(imgPath), fs, candidate);
                    if (wzImg.ParseImage(true))
                    {
                        version = candidate;
                        return true;
                    }
                }
                catch
                {
                }

                try { wzImg?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
                wzImg = null;
                fs = null;
            }

            return false;
        }

        private static int ReadIntProperty(WzImageProperty prop, int fallback)
        {
            if (prop == null)
                return fallback;

            try
            {
                if (prop is WzUOLProperty)
                    prop = prop.GetLinkedWzImageProperty() ?? prop;
                if (prop is WzIntProperty i32)
                    return i32.Value;
                if (prop is WzShortProperty i16)
                    return i16.Value;
                if (prop is WzLongProperty i64)
                    return (int)i64.Value;
                if (prop is WzStringProperty s && int.TryParse(s.Value, out int parsed))
                    return parsed;
            }
            catch
            {
            }

            return fallback;
        }
    }
}
