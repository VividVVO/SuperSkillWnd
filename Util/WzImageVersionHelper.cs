using System;
using System.Collections.Generic;
using System.Drawing;
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

        /// <summary>
        /// Allows external code (e.g. WzImgLoader fallback) to update the cached version
        /// for a given .img path after successful spot-check decoding.
        /// </summary>
        public static void UpdateCacheForSkillImg(string imgPath, WzMapleVersion version)
        {
            UpdateCacheEntry(imgPath, ImageKind.Skill, version);
        }

        public static void UpdateCacheForStringImg(string imgPath, WzMapleVersion version)
        {
            UpdateCacheEntry(imgPath, ImageKind.StringSkill, version);
        }

        private static void UpdateCacheEntry(string imgPath, ImageKind kind, WzMapleVersion version)
        {
            if (string.IsNullOrEmpty(imgPath)) return;
            string fullPath;
            try { fullPath = Path.GetFullPath(imgPath); }
            catch { fullPath = imgPath; }
            string cacheKey = kind + "|" + fullPath;
            lock (SyncRoot)
            {
                Cache[cacheKey] = version;
            }
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
                fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
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

            // Bonus: try to decode an icon bitmap to verify image data is correct
            // (wrong WZ key can parse structure fine but produce black/garbage images)
            score += ScoreImageDecodeQuality(skill);

            return score;
        }

        /// <summary>
        /// Try to decode a few icon bitmaps from skill children.
        /// Returns a bonus score: +500 if at least one non-blank image decodes,
        /// 0 if no icons found, -200 if all decoded icons are blank.
        /// </summary>
        private static int ScoreImageDecodeQuality(WzSubProperty skill)
        {
            int tested = 0;
            int nonBlank = 0;
            foreach (WzImageProperty child in skill.WzProperties)
            {
                if (tested >= 5) break;
                WzSubProperty sub = child as WzSubProperty;
                if (sub == null) continue;

                WzCanvasProperty icon = sub["icon"] as WzCanvasProperty;
                if (icon?.PngProperty == null) continue;

                tested++;
                try
                {
                    Bitmap bmp = icon.PngProperty.GetImage(false);
                    if (bmp != null && !IsBitmapBlank(bmp))
                        nonBlank++;
                    bmp?.Dispose();
                }
                catch { }
            }
            if (tested == 0) return 0;
            return nonBlank > 0 ? 500 : -200;
        }

        private static bool IsBitmapBlank(Bitmap bmp)
        {
            if (bmp == null || bmp.Width == 0 || bmp.Height == 0) return true;
            int w = bmp.Width, h = bmp.Height;
            int[] xs = { 0, w / 2, Math.Min(w - 1, w) };
            int[] ys = { 0, h / 2, Math.Min(h - 1, h) };
            foreach (int x in xs)
                foreach (int y in ys)
                    if (x >= 0 && x < w && y >= 0 && y < h)
                    {
                        var c = bmp.GetPixel(x, y);
                        if (c.A > 0 && (c.R > 0 || c.G > 0 || c.B > 0))
                            return false;
                    }
            return true;
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
