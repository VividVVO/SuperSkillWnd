using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using MapleLib.WzLib;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    /// <summary>
    /// Reads MapleStory binary .img files via MapleLib and extracts skill data.
    /// Caches opened WzImage instances by jobId for reuse within the session.
    /// </summary>
    public class WzImgLoader : IDisposable
    {
        private readonly Dictionary<int, WzImage> _imgCache = new Dictionary<int, WzImage>();
        private readonly Dictionary<int, FileStream> _streamCache = new Dictionary<int, FileStream>();
        private WzImage _stringImg;
        private FileStream _stringFs;

        /// <summary>
        /// Load all skill data for the given skillId from the corresponding .img file.
        /// </summary>
        public WzSkillData LoadSkill(int skillId)
        {
            int jobId = skillId / 10000;
            var wzImg = GetOrLoadImg(jobId);

            string skillPath = "skill/" + skillId.ToString();
            var skillNode = wzImg.GetFromPath(skillPath);
            if (skillNode == null)
                throw new KeyNotFoundException($"Skill {skillId} not found in {jobId}.img (path: {skillPath})");

            var data = ExtractSkillData(skillNode, skillId, jobId);

            // Also load string data (name/desc/h) from String/Skill.img
            LoadStringData(data);

            return data;
        }

        /// <summary>
        /// Load only visual resources (icons, effect) for a proxy skill.
        /// </summary>
        public WzSkillData LoadSkillVisuals(int proxySkillId)
        {
            // Same extraction, caller decides what to use
            return LoadSkill(proxySkillId);
        }

        /// <summary>
        /// List all skill IDs found in the given jobId .img file.
        /// </summary>
        public List<int> ListSkillIds(int jobId)
        {
            var wzImg = GetOrLoadImg(jobId);
            var result = new List<int>();

            var skillTop = wzImg.GetFromPath("skill");
            if (skillTop?.WzProperties == null) return result;

            foreach (var child in skillTop.WzProperties)
            {
                if (int.TryParse(child.Name, out int sid))
                    result.Add(sid);
            }
            result.Sort();
            return result;
        }

        /// <summary>
        /// Check if a .img file exists for the given jobId.
        /// </summary>
        public bool ImgExists(int jobId)
        {
            string path = PathConfig.GameSkillImg(jobId);
            return File.Exists(path);
        }

        /// <summary>
        /// Check if a specific skillId already exists in the corresponding .img file.
        /// </summary>
        public bool SkillExistsInImg(int skillId)
        {
            int jobId = skillId / 10000;
            if (!ImgExists(jobId)) return false;
            try
            {
                var wzImg = GetOrLoadImg(jobId);
                string skillPath = "skill/" + skillId.ToString();
                return wzImg.GetFromPath(skillPath) != null;
            }
            catch { return false; }
        }

        /// <summary>
        /// Check if a skill in .img has the _superSkill marker (= added by us).
        /// Returns: true = super skill, false = native/original skill or not found.
        /// </summary>
        public bool IsSuperSkill(int skillId)
        {
            int jobId = skillId / 10000;
            if (!ImgExists(jobId)) return false;
            try
            {
                var wzImg = GetOrLoadImg(jobId);
                var node = wzImg.GetFromPath("skill/" + skillId.ToString());
                if (node == null) return false;
                var marker = (node as MapleLib.WzLib.WzProperties.WzSubProperty)?["_superSkill"];
                return marker != null;
            }
            catch { return false; }
        }

        /// <summary>
        /// Scan all known job .img files and return skills that have the _superSkill marker.
        /// </summary>
        public List<WzSkillData> ScanSuperSkills(int[] jobIds)
        {
            var result = new List<WzSkillData>();
            foreach (int jobId in jobIds)
            {
                if (!ImgExists(jobId)) continue;
                WzImage wzImg;
                try { wzImg = GetOrLoadImg(jobId); }
                catch { continue; }

                var skillTop = wzImg.GetFromPath("skill");
                if (skillTop?.WzProperties == null) continue;

                foreach (var child in skillTop.WzProperties)
                {
                    if (!int.TryParse(child.Name, out int sid)) continue;

                    // Check for our marker
                    var marker = child["_superSkill"];
                    if (marker == null) continue;
                    if (marker is WzIntProperty ip && ip.Value != 1) continue;

                    try
                    {
                        var skillData = ExtractSkillData(child, sid, jobId);
                        LoadStringData(skillData);
                        result.Add(skillData);
                    }
                    catch { }
                }
            }
            return result;
        }

        /// <summary>
        /// Get the cached or freshly loaded String/Skill.img.
        /// </summary>
        public WzImage GetOrLoadStringImg()
        {
            if (_stringImg != null) return _stringImg;

            string imgPath = PathConfig.GameStringSkillImg;
            if (!File.Exists(imgPath))
                throw new FileNotFoundException($"String Skill.img not found: {imgPath}", imgPath);

            WzMapleVersion stringVersion = WzImageVersionHelper.DetectVersionForStringImg(imgPath);
            _stringFs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
            _stringImg = new WzImage("Skill.img", _stringFs, stringVersion);

            if (!_stringImg.ParseImage(true))
            {
                _stringFs.Dispose(); _stringFs = null;
                _stringImg.Dispose(); _stringImg = null;
                throw new InvalidDataException($"Failed to parse {imgPath}");
            }
            return _stringImg;
        }

        /// <summary>
        /// Load name/desc/h levels from String/Skill.img into WzSkillData.
        /// </summary>
        public void LoadStringData(WzSkillData data)
        {
            try
            {
                var strImg = GetOrLoadStringImg();
                var node = strImg[data.SkillId.ToString()];
                if (node == null) return;

                var nameNode = node["name"];
                if (nameNode is WzStringProperty nsp)
                    data.Name = nsp.Value ?? "";

                var descNode = node["desc"];
                if (descNode is WzStringProperty dsp)
                    data.Desc = dsp.Value ?? "";

                // h1, h2, h3... (level descriptions)
                if (node.WzProperties != null)
                {
                    foreach (var child in node.WzProperties)
                    {
                        if (child.Name.StartsWith("h") && child is WzStringProperty hsp)
                            data.HLevels[child.Name] = hsp.Value ?? "";
                    }
                }
            }
            catch { }
        }

        // ── Private Implementation ──────────────────────────────

        /// <summary>
        /// Build a fast index from String/Skill.img: skillId -> (name, desc).
        /// </summary>
        public Dictionary<int, Tuple<string, string>> BuildStringSkillIndex()
        {
            var result = new Dictionary<int, Tuple<string, string>>();
            try
            {
                var strImg = GetOrLoadStringImg();
                if (strImg?.WzProperties == null) return result;

                foreach (var child in strImg.WzProperties)
                {
                    if (!int.TryParse(child.Name, out int skillId) || skillId <= 0)
                        continue;

                    string name = "";
                    string desc = "";
                    if (child is WzSubProperty sub)
                    {
                        if (sub["name"] is WzStringProperty nsp)
                            name = nsp.Value ?? "";
                        if (sub["desc"] is WzStringProperty dsp)
                            desc = dsp.Value ?? "";
                    }

                    result[skillId] = Tuple.Create(name, desc);
                }
            }
            catch
            {
            }
            return result;
        }

        private WzImage GetOrLoadImg(int jobId)
        {
            if (_imgCache.TryGetValue(jobId, out var cached))
                return cached;

            string imgPath = PathConfig.GameSkillImg(jobId);
            if (!File.Exists(imgPath))
                throw new FileNotFoundException($"Skill .img not found: {imgPath}", imgPath);

            WzMapleVersion skillVersion = WzImageVersionHelper.DetectVersionForSkillImg(imgPath);
            var fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
            var wzImg = new WzImage(jobId + ".img", fs, skillVersion);

            bool ok = wzImg.ParseImage(true);
            if (!ok)
            {
                fs.Dispose();
                wzImg.Dispose();
                throw new InvalidDataException($"Failed to parse {imgPath}");
            }

            _streamCache[jobId] = fs;
            _imgCache[jobId] = wzImg;
            return wzImg;
        }

        private WzSkillData ExtractSkillData(WzImageProperty skillNode, int skillId, int jobId)
        {
            var data = new WzSkillData { SkillId = skillId, JobId = jobId };

            // Icons
            data.IconBitmap = SafeGetBitmap(skillNode, "icon");
            data.IconMouseOverBitmap = SafeGetBitmap(skillNode, "iconMouseOver");
            data.IconDisabledBitmap = SafeGetBitmap(skillNode, "iconDisabled");
            data.IconBase64 = BitmapToBase64(data.IconBitmap);
            data.IconMouseOverBase64 = BitmapToBase64(data.IconMouseOverBitmap);
            data.IconDisabledBase64 = BitmapToBase64(data.IconDisabledBitmap);

            // Action
            data.Action = ExtractActionString(skillNode["action"]);

            // Info/type
            var infoType = skillNode.GetFromPath("info/type");
            if (infoType is WzIntProperty itProp)
                data.InfoType = itProp.Value;

            // Level parameters
            var levelNode = skillNode["level"];
            if (levelNode?.WzProperties != null)
                data.LevelParams = ExtractLevelParams(levelNode);

            // Common parameters (formula strings)
            var commonNode = skillNode["common"];
            if (commonNode?.WzProperties != null)
            {
                foreach (var p in commonNode.WzProperties)
                {
                    string val = GetPropertyValueString(p);
                    if (val != null)
                        data.CommonParams[p.Name] = val;
                }
            }

            // Effect frames (effect/effect0/effect1...)
            data.EffectFramesByNode = ExtractEffectFramesByNode(skillNode);
            if (data.EffectFramesByNode != null && data.EffectFramesByNode.Count > 0)
            {
                if (!data.EffectFramesByNode.TryGetValue("effect", out var primaryFrames) || primaryFrames == null)
                {
                    string firstKey = null;
                    foreach (var key in data.EffectFramesByNode.Keys)
                    {
                        if (firstKey == null || CompareEffectNodeName(key, firstKey) < 0)
                            firstKey = key;
                    }
                    if (!string.IsNullOrEmpty(firstKey))
                        data.EffectFramesByNode.TryGetValue(firstKey, out primaryFrames);
                }
                data.EffectFrames = primaryFrames ?? new List<WzEffectFrame>();
            }
            else
            {
                data.EffectFrames = new List<WzEffectFrame>();
            }

            // Node tree for TreeView
            data.RootNode = BuildNodeTree(skillNode, 0);

            return data;
        }

        private string ExtractActionString(WzImageProperty actionNode)
        {
            if (actionNode == null)
                return "";

            if (actionNode is WzStringProperty direct)
                return direct.Value ?? "";

            // Some skills may store action as a direct non-string scalar/UOL.
            var directVal = GetPropertyValueString(actionNode);
            if (!string.IsNullOrEmpty(directVal))
                return directVal.StartsWith("-> ") ? directVal.Substring(3) : directVal;

            if (actionNode.WzProperties == null || actionNode.WzProperties.Count == 0)
                return "";

            // Common case: action/0
            if (actionNode["0"] is WzStringProperty child0)
                return child0.Value ?? "";

            // Fallback: use smallest numeric scalar child (e.g. 1,2,3...)
            string bestNumericVal = null;
            int bestIndex = int.MaxValue;
            foreach (var child in actionNode.WzProperties)
            {
                if (int.TryParse(child.Name, out int idx))
                {
                    string val = GetPropertyValueString(child);
                    if (string.IsNullOrEmpty(val))
                        continue;
                    if (val.StartsWith("-> "))
                        val = val.Substring(3);
                    if (idx < bestIndex)
                    {
                        bestIndex = idx;
                        bestNumericVal = val;
                    }
                }
            }
            if (!string.IsNullOrEmpty(bestNumericVal))
                return bestNumericVal;

            // Last resort: first string child, then generic property string value.
            foreach (var child in actionNode.WzProperties)
            {
                if (child is WzStringProperty sp)
                    return sp.Value ?? "";
            }
            foreach (var child in actionNode.WzProperties)
            {
                string val = GetPropertyValueString(child);
                if (!string.IsNullOrEmpty(val))
                    return val.StartsWith("-> ") ? val.Substring(3) : val;
            }

            return "";
        }

        private Dictionary<int, Dictionary<string, string>> ExtractLevelParams(WzImageProperty levelNode)
        {
            var result = new Dictionary<int, Dictionary<string, string>>();

            foreach (var child in levelNode.WzProperties)
            {
                if (!int.TryParse(child.Name, out int levelNum)) continue;
                if (child.WzProperties == null) continue;

                var pars = new Dictionary<string, string>();
                foreach (var p in child.WzProperties)
                {
                    string val = GetPropertyValueString(p);
                    if (val != null)
                        pars[p.Name] = val;
                }
                if (pars.Count > 0)
                    result[levelNum] = pars;
            }
            return result;
        }

        private List<WzEffectFrame> ExtractEffectFrames(WzImageProperty effectNode)
        {
            var frames = new List<WzEffectFrame>();

            foreach (var child in effectNode.WzProperties)
            {
                if (!int.TryParse(child.Name, out int idx)) continue;

                var resolved = ResolveProperty(child);
                if (resolved is WzCanvasProperty canvas)
                {
                    var frame = new WzEffectFrame { Index = idx };
                    try
                    {
                        var src = canvas.GetBitmap();
                        if (src != null)
                        {
                            frame.Bitmap = CloneBitmapSafe(src);
                            frame.Width = frame.Bitmap.Width;
                            frame.Height = frame.Bitmap.Height;
                        }
                    }
                    catch { }

                    // delay
                    var delayProp = canvas["delay"];
                    if (delayProp is WzIntProperty dp)
                        frame.Delay = dp.Value;
                    else if (delayProp is WzShortProperty dsp)
                        frame.Delay = dsp.Value;
                    else if (delayProp is WzLongProperty dlp)
                        frame.Delay = (int)dlp.Value;
                    else if (delayProp is WzStringProperty dstr && int.TryParse(dstr.Value, out int parsedDelay))
                        frame.Delay = parsedDelay;
                    else
                        frame.Delay = 100;

                    // Preserve all vector anchors in the frame canvas, not just origin.
                    if (canvas.WzProperties != null)
                    {
                        foreach (var p in canvas.WzProperties)
                        {
                            if (TryReadVector(p, out int vx, out int vy))
                                frame.Vectors[p.Name] = new WzFrameVector(vx, vy);
                            else if (!string.Equals(p.Name, "delay", StringComparison.OrdinalIgnoreCase))
                            {
                                string value = GetPropertyValueString(p);
                                if (value != null)
                                    frame.FrameProps[p.Name] = value;
                            }
                        }
                    }

                    frames.Add(frame);
                }
            }

            frames.Sort((a, b) => a.Index.CompareTo(b.Index));
            return frames;
        }

        private Dictionary<string, List<WzEffectFrame>> ExtractEffectFramesByNode(WzImageProperty skillNode)
        {
            var result = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
            if (skillNode?.WzProperties == null)
                return result;

            foreach (var child in skillNode.WzProperties)
            {
                if (!IsEffectNodeName(child?.Name) || child.WzProperties == null)
                    continue;

                var frames = ExtractEffectFrames(child);
                if (frames == null || frames.Count == 0)
                    continue;

                result[child.Name] = frames;
            }

            return result;
        }

        private static bool IsEffectNodeName(string name)
        {
            if (string.IsNullOrWhiteSpace(name))
                return false;
            if (string.Equals(name, "effect", StringComparison.OrdinalIgnoreCase))
                return true;
            if (!name.StartsWith("effect", StringComparison.OrdinalIgnoreCase))
                return false;
            string suffix = name.Substring("effect".Length);
            if (string.IsNullOrEmpty(suffix))
                return false;
            return int.TryParse(suffix, out _);
        }

        private static int CompareEffectNodeName(string a, string b)
        {
            if (string.Equals(a, b, StringComparison.OrdinalIgnoreCase))
                return 0;
            if (string.Equals(a, "effect", StringComparison.OrdinalIgnoreCase))
                return -1;
            if (string.Equals(b, "effect", StringComparison.OrdinalIgnoreCase))
                return 1;

            bool aIndexed = TryParseIndexedEffectName(a, out int aIndex);
            bool bIndexed = TryParseIndexedEffectName(b, out int bIndex);
            if (aIndexed && bIndexed)
                return aIndex.CompareTo(bIndex);
            if (aIndexed) return -1;
            if (bIndexed) return 1;
            return string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
        }

        private static bool TryParseIndexedEffectName(string name, out int index)
        {
            index = -1;
            if (string.IsNullOrWhiteSpace(name) || !name.StartsWith("effect", StringComparison.OrdinalIgnoreCase))
                return false;
            string suffix = name.Substring("effect".Length);
            if (string.IsNullOrEmpty(suffix))
                return false;
            return int.TryParse(suffix, out index);
        }

        private WzNodeInfo BuildNodeTree(WzImageProperty node, int depth)
        {
            var info = new WzNodeInfo
            {
                Name = node.Name ?? "",
                TypeName = node.PropertyType.ToString()
            };

            // Leaf value
            string val = GetPropertyValueString(node);
            if (val != null)
                info.Value = val;

            // Children (limit depth to prevent stack overflow on very deep trees)
            if (depth < 8 && node.WzProperties != null)
            {
                foreach (var child in node.WzProperties)
                {
                    info.Children.Add(BuildNodeTree(child, depth + 1));
                }
            }

            return info;
        }

        // ── Helpers ─────────────────────────────────────────────

        private static Bitmap SafeGetBitmap(WzImageProperty parent, string childName)
        {
            try
            {
                var prop = parent[childName];
                if (prop == null) return null;

                var resolved = ResolveProperty(prop);
                if (resolved is WzCanvasProperty canvas)
                {
                    var src = canvas.GetBitmap();
                    if (src == null) return null;
                    // Deep-copy via MemoryStream to fully detach from WZ stream
                    return CloneBitmapSafe(src);
                }
            }
            catch { }
            return null;
        }

        private static bool TryReadVector(WzImageProperty prop, out int x, out int y)
        {
            x = 0;
            y = 0;
            if (prop == null) return false;

            var resolved = ResolveProperty(prop);
            if (resolved is WzVectorProperty vp)
            {
                x = vp.X?.Value ?? 0;
                y = vp.Y?.Value ?? 0;
                return true;
            }

            // Fallback: treat sub-node with x/y ints as a vector-like entry.
            if (resolved is WzSubProperty sp)
            {
                if (TryReadIntProperty(sp["x"], out x) && TryReadIntProperty(sp["y"], out y))
                    return true;
            }

            return false;
        }

        private static bool TryReadIntProperty(WzImageProperty prop, out int value)
        {
            value = 0;
            if (prop == null) return false;

            if (prop is WzIntProperty ip) { value = ip.Value; return true; }
            if (prop is WzShortProperty sp) { value = sp.Value; return true; }
            if (prop is WzLongProperty lp) { value = (int)lp.Value; return true; }
            if (prop is WzStringProperty str && int.TryParse(str.Value, out int parsed))
            {
                value = parsed;
                return true;
            }
            return false;
        }

        private static WzImageProperty ResolveProperty(WzImageProperty prop)
        {
            if (prop is WzUOLProperty)
                return prop.GetLinkedWzImageProperty() ?? prop;
            return prop;
        }

        private static string GetPropertyValueString(WzImageProperty prop)
        {
            if (prop is WzIntProperty ip) return ip.Value.ToString();
            if (prop is WzStringProperty sp) return sp.Value ?? "";
            if (prop is WzFloatProperty fp) return fp.Value.ToString("G");
            if (prop is WzDoubleProperty dp) return dp.Value.ToString("G");
            if (prop is WzShortProperty shp) return shp.Value.ToString();
            if (prop is WzLongProperty lp) return lp.Value.ToString();
            if (prop is WzUOLProperty uol) return "-> " + (uol.Value ?? "");
            if (prop is WzCanvasProperty) return null; // container, not a value
            if (prop is WzSubProperty) return null;    // container
            return null;
        }

        /// <summary>
        /// Create a fully independent bitmap copy via pixel-level copy.
        /// This ensures the bitmap is completely detached from any WZ stream.
        /// </summary>
        private static Bitmap CloneBitmapSafe(Bitmap src)
        {
            if (src == null) return null;
            var dst = new Bitmap(src.Width, src.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(dst))
            {
                g.DrawImage(src, 0, 0, src.Width, src.Height);
            }
            return dst;
        }

        private static string BitmapToBase64(Bitmap bmp)
        {
            if (bmp == null) return "";
            using (var ms = new MemoryStream())
            {
                bmp.Save(ms, ImageFormat.Png);
                return Convert.ToBase64String(ms.ToArray());
            }
        }

        // ── IDisposable ─────────────────────────────────────────

        /// <summary>
        /// Release all cached .img file handles without destroying the loader.
        /// Call before writing to .img files to avoid file lock conflicts.
        /// </summary>
        public void ClearCache()
        {
            foreach (var kvp in _imgCache)
            {
                try { kvp.Value.Dispose(); } catch { }
            }
            _imgCache.Clear();

            foreach (var kvp in _streamCache)
            {
                try { kvp.Value.Dispose(); } catch { }
            }
            _streamCache.Clear();

            try { _stringImg?.Dispose(); } catch { }
            _stringImg = null;
            try { _stringFs?.Dispose(); } catch { }
            _stringFs = null;
        }

        public void Dispose()
        {
            ClearCache();
        }
    }
}
