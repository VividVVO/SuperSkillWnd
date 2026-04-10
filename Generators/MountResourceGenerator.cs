using System;
using System.Collections.Generic;
using System.IO;
using MapleLib.WzLib;
using MapleLib.WzLib.Serializer;
using MapleLib.WzLib.Util;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    /// <summary>
    /// Synchronizes mount resources referenced by skills:
    /// - Character/TamingMob/0190xxxx.img (mount action/animation)
    /// - TamingMob/000x.img (mount speed/jump/fatigue data)
    /// Optionally mirrors synced .img files to server XML folders.
    /// </summary>
    public static class MountResourceGenerator
    {
        private const string ModeConfigOnly = "config_only";
        private const string ModeSyncAction = "sync_action";
        private const string ModeSyncActionAndData = "sync_action_and_data";

        public static void Generate(List<SkillDefinition> skills, bool dryRun)
        {
            if (skills == null || skills.Count == 0)
                return;

            var serverMountMap = LoadServerMountMap();
            foreach (var sd in skills)
            {
                if (sd == null || sd.MountItemId <= 0)
                    continue;

                string mode = NormalizeMode(sd.MountResourceMode);
                if (string.Equals(mode, ModeConfigOnly, StringComparison.Ordinal))
                    continue;

                try
                {
                    ProcessSkillMount(sd, mode, serverMountMap, dryRun);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  [error] Mount sync failed for skill {sd.SkillId}, mountItemId={sd.MountItemId}: {ex.Message}");
                }
            }
        }

        private static void ProcessSkillMount(
            SkillDefinition sd,
            string mode,
            Dictionary<int, int> serverMountMap,
            bool dryRun)
        {
            int targetMountItemId = sd.MountItemId;
            int sourceMountItemId = ResolveSourceMountItemId(sd, serverMountMap);
            bool syncData = string.Equals(mode, ModeSyncActionAndData, StringComparison.Ordinal);

            string targetActionPath = PathConfig.GameMountActionImg(targetMountItemId);
            string sourceActionPath = sourceMountItemId > 0 ? PathConfig.GameMountActionImg(sourceMountItemId) : "";
            string actionName = PathConfig.MountActionImgName(targetMountItemId);

            Console.WriteLine($"\n[MountSync] Processing mountItemId={targetMountItemId} (skill={sd.SkillId}, mode={mode})");

            bool actionChanged = false;
            if (!File.Exists(targetActionPath))
            {
                if (sourceMountItemId <= 0)
                {
                    Console.WriteLine($"  [warn] Missing {actionName}, and no mountSourceItemId/donor source could be resolved.");
                    return;
                }

                if (!File.Exists(sourceActionPath))
                {
                    Console.WriteLine($"  [error] Source mount action missing: {sourceActionPath}");
                    return;
                }

                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would create {targetActionPath} from {sourceActionPath}");
                    actionChanged = true;
                }
                else
                {
                    EnsureDirectoryForFile(targetActionPath);
                    File.Copy(sourceActionPath, targetActionPath, overwrite: true);
                    actionChanged = true;
                    Console.WriteLine($"  [created] {targetActionPath} <- {sourceActionPath}");
                }
            }
            else
            {
                Console.WriteLine($"  [skip] Mount action exists: {targetActionPath}");
            }

            int targetDataId = 0;
            bool dataChanged = false;
            string targetDataPath = "";
            if (syncData)
            {
                int sourceDataId = 0;
                if (sourceMountItemId > 0 && File.Exists(sourceActionPath))
                    sourceDataId = ReadTamingMobId(sourceActionPath, fallback: 0);

                targetDataId = ResolveTargetTamingMobId(sd, targetActionPath, sourceDataId);
                if (targetDataId <= 0)
                {
                    Console.WriteLine("  [warn] Cannot resolve target tamingMob id. Skip data sync.");
                }
                else
                {
                    targetDataPath = PathConfig.GameMountDataImg(targetDataId);

                    if (!dryRun && File.Exists(targetActionPath))
                    {
                        if (SetActionTamingMobId(targetActionPath, targetDataId, out bool actionUpdated))
                        {
                            actionChanged = actionChanged || actionUpdated;
                            if (actionUpdated)
                                Console.WriteLine($"  [write] Set action info/tamingMob = {targetDataId}");
                        }
                    }
                    else if (dryRun && File.Exists(targetActionPath))
                    {
                        int current = ReadTamingMobId(targetActionPath, fallback: 0);
                        if (current != targetDataId)
                            Console.WriteLine($"  [dry-run] Would set action info/tamingMob: {current} -> {targetDataId}");
                    }

                    if (!File.Exists(targetDataPath))
                    {
                        if (sourceDataId <= 0)
                        {
                            Console.WriteLine($"  [error] Missing target data {targetDataPath}, and source tamingMob id is unknown.");
                        }
                        else
                        {
                            string sourceDataPath = PathConfig.GameMountDataImg(sourceDataId);
                            if (!File.Exists(sourceDataPath))
                            {
                                Console.WriteLine($"  [error] Source mount data missing: {sourceDataPath}");
                            }
                            else if (dryRun)
                            {
                                Console.WriteLine($"  [dry-run] Would create {targetDataPath} from {sourceDataPath}");
                                dataChanged = true;
                            }
                            else
                            {
                                EnsureDirectoryForFile(targetDataPath);
                                File.Copy(sourceDataPath, targetDataPath, overwrite: true);
                                dataChanged = true;
                                Console.WriteLine($"  [created] {targetDataPath} <- {sourceDataPath}");
                            }
                        }
                    }
                    else
                    {
                        Console.WriteLine($"  [skip] Mount data exists: {targetDataPath}");
                    }

                    if (HasDataOverrides(sd) && targetDataId > 0)
                    {
                        if (dryRun)
                        {
                            Console.WriteLine($"  [dry-run] Would apply data overrides to {targetDataPath}: speed={FormatOpt(sd.MountSpeedOverride)}, jump={FormatOpt(sd.MountJumpOverride)}, fatigue={FormatOpt(sd.MountFatigueOverride)}");
                            dataChanged = true;
                        }
                        else if (File.Exists(targetDataPath))
                        {
                            if (ApplyDataOverrides(targetDataPath, sd, out bool updated))
                            {
                                dataChanged = dataChanged || updated;
                                if (updated)
                                {
                                    Console.WriteLine($"  [write] Updated mount data info (speed/jump/fatigue) for {targetDataPath}");
                                }
                            }
                        }
                    }
                }
            }

            // Mirror to server xml if needed (missing or changed).
            string actionXmlPath = PathConfig.ServerMountActionXml(targetMountItemId);
            bool actionNeedSyncXml = actionChanged || !File.Exists(actionXmlPath);
            EnsureXmlFromImg(targetActionPath, actionXmlPath, actionNeedSyncXml, dryRun, "MountActionXml");

            if (syncData && targetDataId > 0)
            {
                string dataXmlPath = PathConfig.ServerMountDataXml(targetDataId);
                bool dataNeedSyncXml = dataChanged || !File.Exists(dataXmlPath);
                EnsureXmlFromImg(targetDataPath, dataXmlPath, dataNeedSyncXml, dryRun, "MountDataXml");
            }
        }

        private static Dictionary<int, int> LoadServerMountMap()
        {
            var result = new Dictionary<int, int>();
            string path = PathConfig.SuperSkillsServerJson;
            if (!File.Exists(path))
                return result;

            try
            {
                string json = TextFileHelper.ReadAllTextAuto(path);
                var root = SimpleJson.ParseObject(json);
                var arr = SimpleJson.GetArray(root, "skills");
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
            }
            catch
            {
            }

            return result;
        }

        private static int ResolveSourceMountItemId(SkillDefinition sd, Dictionary<int, int> serverMountMap)
        {
            if (sd == null)
                return 0;

            if (sd.MountSourceItemId > 0)
                return sd.MountSourceItemId;

            if (sd.DonorSkillId > 0 && serverMountMap != null
                && serverMountMap.TryGetValue(sd.DonorSkillId, out int donorMountItemId)
                && donorMountItemId > 0)
            {
                return donorMountItemId;
            }

            if (serverMountMap != null
                && serverMountMap.TryGetValue(sd.SkillId, out int existingMountItemId)
                && existingMountItemId > 0
                && existingMountItemId != sd.MountItemId)
            {
                return existingMountItemId;
            }

            return 0;
        }

        private static int ResolveTargetTamingMobId(SkillDefinition sd, string targetActionPath, int sourceDataId)
        {
            if (sd != null && sd.MountTamingMobId > 0)
                return sd.MountTamingMobId;

            if (!string.IsNullOrEmpty(targetActionPath) && File.Exists(targetActionPath))
            {
                int fromAction = ReadTamingMobId(targetActionPath, fallback: 0);
                if (fromAction > 0)
                    return fromAction;
            }

            if (sourceDataId > 0)
                return sourceDataId;

            return 0;
        }

        private static bool HasDataOverrides(SkillDefinition sd)
        {
            if (sd == null)
                return false;
            return sd.MountSpeedOverride.HasValue
                || sd.MountJumpOverride.HasValue
                || sd.MountFatigueOverride.HasValue;
        }

        private static string FormatOpt(int? value)
        {
            return value.HasValue ? value.Value.ToString() : "(保持)";
        }

        private static int ReadTamingMobId(string actionImgPath, int fallback)
        {
            if (!TryOpenParsedImage(actionImgPath, out var fs, out var wzImg, out _, out _))
                return fallback;

            try
            {
                var prop = wzImg.GetFromPath("info/tamingMob");
                int value = ReadIntProperty(prop, fallback);
                return value > 0 ? value : fallback;
            }
            finally
            {
                try { wzImg?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
            }
        }

        private static bool SetActionTamingMobId(string actionImgPath, int targetDataId, out bool changed)
        {
            changed = false;
            if (targetDataId <= 0)
                return true;

            if (!TryOpenParsedImage(actionImgPath, out var fs, out var wzImg, out var version, out string err))
            {
                Console.WriteLine($"  [error] Failed to parse action img for tamingMob update: {err}");
                return false;
            }

            try
            {
                WzSubProperty info = wzImg["info"] as WzSubProperty;
                if (info == null)
                {
                    info = new WzSubProperty("info");
                    wzImg.AddProperty(info);
                }

                int current = ReadIntProperty(info["tamingMob"], 0);
                if (current == targetDataId)
                    return true;

                ReplaceOrAddProperty(info, new WzIntProperty("tamingMob", targetDataId));
                wzImg.Changed = true;

                byte[] iv = WzTool.GetIvByMapleVersion(version);
                var serializer = new WzImgSerializer(iv);
                byte[] bytes = serializer.SerializeImage(wzImg);

                BackupHelper.Backup(actionImgPath);
                try { fs.Dispose(); } catch { }
                try { wzImg.Dispose(); } catch { }
                ImgWriteGenerator.WriteWithRetry(actionImgPath, bytes);
                changed = true;
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [error] Failed to update action tamingMob: {ex.Message}");
                return false;
            }
            finally
            {
                try { wzImg?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
            }
        }

        private static bool ApplyDataOverrides(string dataImgPath, SkillDefinition sd, out bool changed)
        {
            changed = false;
            if (sd == null || !HasDataOverrides(sd))
                return true;

            if (!TryOpenParsedImage(dataImgPath, out var fs, out var wzImg, out var version, out string err))
            {
                Console.WriteLine($"  [error] Failed to parse mount data img: {err}");
                return false;
            }

            try
            {
                WzSubProperty info = wzImg["info"] as WzSubProperty;
                if (info == null)
                {
                    info = new WzSubProperty("info");
                    wzImg.AddProperty(info);
                }

                bool localChanged = false;
                localChanged |= TrySetIntProperty(info, "speed", sd.MountSpeedOverride);
                localChanged |= TrySetIntProperty(info, "jump", sd.MountJumpOverride);
                localChanged |= TrySetIntProperty(info, "fatigue", sd.MountFatigueOverride);

                if (!localChanged)
                    return true;

                wzImg.Changed = true;
                byte[] iv = WzTool.GetIvByMapleVersion(version);
                var serializer = new WzImgSerializer(iv);
                byte[] bytes = serializer.SerializeImage(wzImg);

                BackupHelper.Backup(dataImgPath);
                try { fs.Dispose(); } catch { }
                try { wzImg.Dispose(); } catch { }
                ImgWriteGenerator.WriteWithRetry(dataImgPath, bytes);
                changed = true;
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [error] Failed to write mount data overrides: {ex.Message}");
                return false;
            }
            finally
            {
                try { wzImg?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
            }
        }

        private static bool TrySetIntProperty(WzSubProperty parent, string name, int? value)
        {
            if (parent == null || string.IsNullOrWhiteSpace(name) || !value.HasValue)
                return false;

            int current = ReadIntProperty(parent[name], int.MinValue);
            if (current == value.Value)
                return false;

            ReplaceOrAddProperty(parent, new WzIntProperty(name, value.Value));
            return true;
        }

        private static int ReadIntProperty(WzImageProperty prop, int fallback)
        {
            if (prop == null)
                return fallback;
            try
            {
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

        private static void ReplaceOrAddProperty(WzSubProperty parent, WzImageProperty newProp)
        {
            if (parent == null || newProp == null)
                return;

            var old = parent[newProp.Name];
            if (old != null)
            {
                parent.WzProperties.Remove(old);
                try { old.Dispose(); } catch { }
            }
            parent.AddProperty(newProp);
        }

        private static void EnsureXmlFromImg(string imgPath, string xmlPath, bool forceSync, bool dryRun, string label)
        {
            if (string.IsNullOrWhiteSpace(imgPath) || string.IsNullOrWhiteSpace(xmlPath))
                return;

            if (!File.Exists(imgPath))
            {
                Console.WriteLine($"  [warn] {label} skipped: game img not found: {imgPath}");
                return;
            }

            if (!forceSync)
                return;

            if (dryRun)
            {
                Console.WriteLine(File.Exists(xmlPath)
                    ? $"  [dry-run] Would sync XML from {imgPath}"
                    : $"  [dry-run] Would create XML from {imgPath}");
                return;
            }

            try
            {
                EnsureDirectoryForFile(xmlPath);
                if (File.Exists(xmlPath))
                    BackupHelper.Backup(xmlPath);

                if (!TryOpenParsedImage(imgPath, out var fs, out var wzImg, out _, out string err))
                {
                    Console.WriteLine($"  [error] {label} parse failed: {err}");
                    return;
                }

                try
                {
                    var serializer = new WzClassicXmlSerializer(0, LineBreak.None, false);
                    serializer.SerializeImage(wzImg, xmlPath);
                    Console.WriteLine($"  [saved] {xmlPath}");
                }
                finally
                {
                    try { wzImg?.Dispose(); } catch { }
                    try { fs?.Dispose(); } catch { }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [error] Failed to sync xml {xmlPath}: {ex.Message}");
            }
        }

        private static bool TryOpenParsedImage(
            string imgPath,
            out FileStream fs,
            out WzImage wzImg,
            out WzMapleVersion version,
            out string error)
        {
            fs = null;
            wzImg = null;
            version = WzMapleVersion.EMS;
            error = "";

            var versions = new List<WzMapleVersion>
            {
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
                    fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                    wzImg = new WzImage(Path.GetFileName(imgPath), fs, candidate);
                    if (wzImg.ParseImage(true))
                    {
                        version = candidate;
                        return true;
                    }
                }
                catch (Exception ex)
                {
                    error = ex.Message;
                }

                try { wzImg?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
                wzImg = null;
                fs = null;
            }

            if (string.IsNullOrEmpty(error))
                error = "Unsupported/invalid .img format";
            return false;
        }

        private static void EnsureDirectoryForFile(string filePath)
        {
            string dir = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);
        }

        private static string NormalizeMode(string mode)
        {
            string normalized = (mode ?? "").Trim().ToLowerInvariant();
            switch (normalized)
            {
                case ModeSyncAction:
                case "action":
                case "clone_action":
                    return ModeSyncAction;
                case ModeSyncActionAndData:
                case "action_data":
                case "clone_action_and_data":
                case "sync_all":
                    return ModeSyncActionAndData;
                default:
                    return ModeConfigOnly;
            }
        }
    }
}
