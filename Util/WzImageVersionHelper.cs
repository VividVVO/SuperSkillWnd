using System;
using System.Collections.Generic;
using System.IO;
using MapleLib.WzLib;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    internal static class WzImageVersionHelper
    {
        private enum ImageKind
        {
            Skill,
            StringSkill
        }

        private static readonly WzMapleVersion[] CandidateVersions = new[]
        {
            WzMapleVersion.BMS,
            WzMapleVersion.CLASSIC,
            WzMapleVersion.GMS,
            WzMapleVersion.EMS
        };

        private static readonly object SyncRoot = new object();
        private static readonly Dictionary<string, WzMapleVersion> Cache =
            new Dictionary<string, WzMapleVersion>(StringComparer.OrdinalIgnoreCase);

        public static WzMapleVersion DetectVersionForSkillImg(string imgPath)
        {
            return DetectVersion(imgPath, ImageKind.Skill);
        }

        public static WzMapleVersion DetectVersionForStringImg(string imgPath)
        {
            return DetectVersion(imgPath, ImageKind.StringSkill);
        }

        public static WzMapleVersion DetectPreferredVersionFromGameData()
        {
            try
            {
                if (File.Exists(PathConfig.GameStringSkillImg))
                    return DetectVersionForStringImg(PathConfig.GameStringSkillImg);

                if (!string.IsNullOrEmpty(PathConfig.GameDataRoot) && Directory.Exists(PathConfig.GameDataRoot))
                {
                    foreach (string path in Directory.EnumerateFiles(PathConfig.GameDataRoot, "*.img"))
                        return DetectVersionForSkillImg(path);
                }
            }
            catch
            {
            }

            return WzMapleVersion.EMS;
        }

        private static WzMapleVersion DetectVersion(string imgPath, ImageKind kind)
        {
            if (string.IsNullOrEmpty(imgPath) || !File.Exists(imgPath))
                return WzMapleVersion.EMS;

            string fullPath;
            try
            {
                fullPath = Path.GetFullPath(imgPath);
            }
            catch
            {
                fullPath = imgPath;
            }

            string cacheKey = kind + "|" + fullPath;

            lock (SyncRoot)
            {
                if (Cache.TryGetValue(cacheKey, out WzMapleVersion cached))
                    return cached;
            }

            int bestScore = int.MinValue;
            WzMapleVersion bestVersion = WzMapleVersion.EMS;

            foreach (WzMapleVersion candidate in CandidateVersions)
            {
                int score = ScoreVersion(fullPath, candidate, kind);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestVersion = candidate;
                }
            }

            if (bestScore < -5000)
                bestVersion = WzMapleVersion.EMS;

            lock (SyncRoot)
            {
                Cache[cacheKey] = bestVersion;
            }

            Console.WriteLine($"[WzVersion] {Path.GetFileName(fullPath)} => {bestVersion} (score={bestScore})");
            return bestVersion;
        }

        private static int ScoreVersion(string imgPath, WzMapleVersion version, ImageKind kind)
        {
            FileStream fs = null;
            WzImage img = null;
            try
            {
                fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                img = new WzImage(Path.GetFileName(imgPath), fs, version);
                if (!img.ParseImage(true))
                    return int.MinValue / 2;

                return kind == ImageKind.StringSkill
                    ? ScoreStringSkillImage(img)
                    : ScoreSkillImage(img);
            }
            catch
            {
                return int.MinValue / 2;
            }
            finally
            {
                try { img?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
            }
        }

        private static int ScoreSkillImage(WzImage img)
        {
            if (img?.WzProperties == null)
                return -10000;

            int score = img.WzProperties.Count > 0 ? 10 : -1000;
            WzSubProperty skill = img.GetFromPath("skill") as WzSubProperty;
            if (skill == null)
                return score - 6000;

            score += 2000;
            if (skill.WzProperties == null || skill.WzProperties.Count == 0)
                return score - 100;

            int scanned = 0;
            int numericChildren = 0;
            int withInfo = 0;
            int withCommon = 0;
            int withAction = 0;

            foreach (WzImageProperty child in skill.WzProperties)
            {
                scanned++;
                if (scanned > 120)
                    break;

                if (int.TryParse(child.Name, out int _))
                    numericChildren++;

                WzSubProperty sub = child as WzSubProperty;
                if (sub == null)
                    continue;

                if (sub["info"] != null)
                    withInfo++;
                if (sub["common"] != null)
                    withCommon++;
                if (sub["action"] != null)
                    withAction++;
            }

            score += numericChildren * 12;
            score += withInfo * 6;
            score += withCommon * 4;
            score += withAction * 2;
            if (numericChildren == 0)
                score -= 1500;

            return score;
        }

        private static int ScoreStringSkillImage(WzImage img)
        {
            if (img?.WzProperties == null)
                return -10000;

            int scanned = 0;
            int numericEntries = 0;
            int withName = 0;
            int withDesc = 0;

            foreach (WzImageProperty child in img.WzProperties)
            {
                scanned++;
                if (scanned > 240)
                    break;

                if (!int.TryParse(child.Name, out int _))
                    continue;

                numericEntries++;
                WzSubProperty sub = child as WzSubProperty;
                if (sub == null)
                    continue;

                if (sub["name"] is WzStringProperty)
                    withName++;
                if (sub["desc"] is WzStringProperty)
                    withDesc++;
            }

            if (numericEntries == 0)
                return -7000;

            int score = 1800;
            score += numericEntries * 10;
            score += withName * 8;
            score += withDesc * 5;
            return score;
        }
    }
}
