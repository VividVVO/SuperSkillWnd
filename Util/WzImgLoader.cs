using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Reflection;
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
        private readonly Dictionary<int, int> _skillJobHintCache = new Dictionary<int, int>();
        private WzImage _stringImg;
        private FileStream _stringFs;

        /// <summary>
        /// Load all skill data for the given skillId from the corresponding .img file.
        /// </summary>
        public WzSkillData LoadSkill(int skillId, int? preferredJobId = null)
        {
            if (!TryResolveSkillJobId(skillId, preferredJobId, out int jobId))
            {
                string skillPath2 = "skill/" + PathConfig.SkillKey(skillId);
                throw new KeyNotFoundException(
                    $"Skill {skillId} not found in Data/Skill/*.img (path: {skillPath2})");
            }

            var wzImg = GetOrLoadImg(jobId);

            var skillNode = FindSkillNodeById(wzImg, skillId, out _);
            if (skillNode == null)
            {
                string skillPath = "skill/" + PathConfig.SkillKey(skillId);
                throw new KeyNotFoundException(
                    $"Skill {skillId} not found in {PathConfig.SkillImgName(jobId)} (path: {skillPath})");
            }

            var data = ExtractSkillData(skillNode, skillId, jobId);

            // Also load string data (name/desc/pdesc/ph/h) from String/Skill.img
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
            return TryResolveSkillJobId(skillId, null, out _);
        }

        /// <summary>
        /// Check if a skill in .img has the _superSkill marker (= added by us).
        /// Returns: true = super skill, false = native/original skill or not found.
        /// </summary>
        public bool IsSuperSkill(int skillId)
        {
            if (!TryResolveSkillJobId(skillId, null, out int jobId))
                return false;

            try
            {
                var wzImg = GetOrLoadImg(jobId);
                var node = FindSkillNodeById(wzImg, skillId, out _);
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
            _stringFs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
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
        /// Load name/desc/pdesc/ph/h levels from String/Skill.img into WzSkillData.
        /// </summary>
        public void LoadStringData(WzSkillData data)
        {
            try
            {
                var strImg = GetOrLoadStringImg();
                var node = FindStringEntryBySkillId(strImg, data.SkillId);
                if (node == null) return;

                var nameNode = node["name"];
                if (nameNode is WzStringProperty nsp)
                    data.Name = nsp.Value ?? "";

                var descNode = node["desc"];
                if (descNode is WzStringProperty dsp)
                    data.Desc = dsp.Value ?? "";

                var pdescNode = node["pdesc"];
                if (pdescNode is WzStringProperty psp)
                    data.PDesc = psp.Value ?? "";

                var phNode = node["ph"];
                if (phNode is WzStringProperty php)
                    data.Ph = php.Value ?? "";

                // h (template text) and h1, h2, h3... (level descriptions)
                if (node.WzProperties != null)
                {
                    foreach (var child in node.WzProperties)
                    {
                        if (child is WzStringProperty hsp && child.Name.StartsWith("h"))
                        {
                            if (child.Name == "h")
                                data.H = hsp.Value ?? "";
                            else
                                data.HLevels[child.Name] = hsp.Value ?? "";
                        }
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
            var fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            var wzImg = new WzImage(Path.GetFileName(imgPath), fs, skillVersion);

            bool ok = wzImg.ParseImage(true);
            if (!ok)
            {
                fs.Dispose();
                wzImg.Dispose();
                throw new InvalidDataException($"Failed to parse {imgPath}");
            }

            // Spot-check: try to decode one canvas bitmap to verify the WZ key is correct.
            // Wrong key can parse structure fine but produce all-black pixel data.
            if (!SpotCheckAnyCanvasDecodes(wzImg))
            {
                Console.WriteLine($"[WzImgLoader] {Path.GetFileName(imgPath)} decoded blank with {skillVersion}, trying other versions...");
                WzMapleVersion[] fallbacks = { WzMapleVersion.BMS, WzMapleVersion.CLASSIC, WzMapleVersion.GMS, WzMapleVersion.EMS };
                foreach (var alt in fallbacks)
                {
                    if (alt == skillVersion) continue;
                    FileStream altFs = null;
                    WzImage altImg = null;
                    try
                    {
                        altFs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                        altImg = new WzImage(Path.GetFileName(imgPath), altFs, alt);
                        if (!altImg.ParseImage(true))
                        {
                            altImg.Dispose(); altFs.Dispose();
                            continue;
                        }
                        if (SpotCheckAnyCanvasDecodes(altImg))
                        {
                            Console.WriteLine($"[WzImgLoader] {jobId}.img works with {alt}, switching.");
                            WzImageVersionHelper.UpdateCacheForSkillImg(imgPath, alt);
                            wzImg.Dispose(); fs.Dispose();
                            wzImg = altImg; fs = altFs;
                            skillVersion = alt;
                            break;
                        }
                        altImg.Dispose(); altFs.Dispose();
                    }
                    catch
                    {
                        try { altImg?.Dispose(); } catch { }
                        try { altFs?.Dispose(); } catch { }
                    }
                }
            }

            _streamCache[jobId] = fs;
            _imgCache[jobId] = wzImg;
            return wzImg;
        }

        /// <summary>
        /// Quick spot-check: find any canvas property in the image and try to decode it.
        /// Returns true if at least one non-blank bitmap is found.
        /// </summary>
        private static bool SpotCheckAnyCanvasDecodes(WzImage img)
        {
            if (img?.WzProperties == null) return false;

            // Walk skill node children looking for icon or any canvas
            var skill = img.GetFromPath("skill") as WzSubProperty;
            if (skill?.WzProperties == null) return false;

            int tested = 0;
            foreach (WzImageProperty child in skill.WzProperties)
            {
                if (tested >= 8) break;
                WzSubProperty sub = child as WzSubProperty;
                if (sub == null) continue;

                // Try icon first
                WzCanvasProperty canvas = sub["icon"] as WzCanvasProperty;
                if (canvas == null)
                {
                    // Try finding any canvas in common animation nodes
                    foreach (var nodeName in new[] { "effect", "hit", "ball", "prepare" })
                    {
                        var animNode = sub[nodeName];
                        if (animNode == null) continue;
                        canvas = FindFirstCanvas(animNode);
                        if (canvas != null) break;
                    }
                }
                if (canvas == null) continue;

                // Skip canvases that use _inlink/_outlink — they cannot be used to verify
                // the WZ decryption key since their pixel data lives elsewhere
                string inlink = (canvas[WzCanvasProperty.InlinkPropertyName] as WzStringProperty)?.Value;
                string outlink = (canvas[WzCanvasProperty.OutlinkPropertyName] as WzStringProperty)?.Value;
                if (!string.IsNullOrEmpty(inlink) || !string.IsNullOrEmpty(outlink))
                    continue;

                tested++;
                try
                {
                    Bitmap bmp = null;
                    try { bmp = canvas.GetLinkedWzCanvasBitmap(); } catch { }
                    if (bmp == null || IsBitmapBlank(bmp))
                    {
                        try { bmp = canvas.GetBitmap(); } catch { }
                    }
                    if (bmp != null && !IsBitmapBlank(bmp))
                    {
                        bmp.Dispose();
                        return true;
                    }
                    bmp?.Dispose();
                }
                catch { }
            }
            return false;
        }

        /// <summary>
        /// Recursively find the first WzCanvasProperty in a node tree (max depth 3).
        /// Prefers canvases without _inlink/_outlink so they can be used for version checking.
        /// </summary>
        private static WzCanvasProperty FindFirstCanvas(WzImageProperty node, int depth = 0, bool preferNonLinked = false)
        {
            if (node == null || depth > 3) return null;
            if (node is WzCanvasProperty c)
            {
                if (!preferNonLinked) return c;
                // Check if this canvas has links
                string inl = (c[WzCanvasProperty.InlinkPropertyName] as WzStringProperty)?.Value;
                string outl = (c[WzCanvasProperty.OutlinkPropertyName] as WzStringProperty)?.Value;
                if (string.IsNullOrEmpty(inl) && string.IsNullOrEmpty(outl))
                    return c;
                // Has links — continue searching for a non-linked one
            }
            if (node.WzProperties == null) return null;
            WzCanvasProperty fallback = (node is WzCanvasProperty fc) ? fc : null;
            foreach (var child in node.WzProperties)
            {
                if (child is WzCanvasProperty cc)
                {
                    if (!preferNonLinked) return cc;
                    string inl = (cc[WzCanvasProperty.InlinkPropertyName] as WzStringProperty)?.Value;
                    string outl = (cc[WzCanvasProperty.OutlinkPropertyName] as WzStringProperty)?.Value;
                    if (string.IsNullOrEmpty(inl) && string.IsNullOrEmpty(outl))
                        return cc;
                    if (fallback == null) fallback = cc;
                    continue;
                }
                var found = FindFirstCanvas(child, depth + 1, preferNonLinked);
                if (found != null) return found;
            }
            return fallback;
        }

        private bool TryResolveSkillJobId(int skillId, int? preferredJobId, out int resolvedJobId)
        {
            resolvedJobId = -1;
            if (skillId <= 0)
                return false;

            if (preferredJobId.HasValue && TrySkillExistsInJob(preferredJobId.Value, skillId))
            {
                resolvedJobId = preferredJobId.Value;
                _skillJobHintCache[skillId] = resolvedJobId;
                return true;
            }

            bool hasCachedJob = _skillJobHintCache.TryGetValue(skillId, out int cachedJobId);
            if (hasCachedJob && TrySkillExistsInJob(cachedJobId, skillId))
            {
                resolvedJobId = cachedJobId;
                return true;
            }

            int derivedJobId = skillId / 10000;
            if (TrySkillExistsInJob(derivedJobId, skillId))
            {
                resolvedJobId = derivedJobId;
                _skillJobHintCache[skillId] = resolvedJobId;
                return true;
            }

            List<int> allJobIds = EnumerateJobIdsFromGameSkillRoot();
            foreach (int candidate in allJobIds)
            {
                if (candidate == derivedJobId)
                    continue;
                if (preferredJobId.HasValue && candidate == preferredJobId.Value)
                    continue;
                if (hasCachedJob && candidate == cachedJobId)
                    continue;

                if (!TrySkillExistsInJob(candidate, skillId))
                    continue;

                resolvedJobId = candidate;
                _skillJobHintCache[skillId] = resolvedJobId;
                return true;
            }

            return false;
        }

        private bool TrySkillExistsInJob(int jobId, int skillId)
        {
            if (jobId < 0 || !ImgExists(jobId))
                return false;

            try
            {
                var wzImg = GetOrLoadImg(jobId);
                return FindSkillNodeById(wzImg, skillId, out _) != null;
            }
            catch
            {
                return false;
            }
        }

        private static IEnumerable<string> BuildIdNameCandidates(int id)
        {
            foreach (string key in PathConfig.SkillKeyCandidates(id))
                yield return key;
        }

        private static WzImageProperty FindSkillNodeById(WzImage wzImg, int skillId, out string matchedKey)
        {
            matchedKey = null;
            if (wzImg == null || skillId <= 0)
                return null;

            foreach (string key in BuildIdNameCandidates(skillId))
            {
                var node = wzImg.GetFromPath("skill/" + key);
                if (node != null)
                {
                    matchedKey = key;
                    return node;
                }
            }

            var skillTop = wzImg.GetFromPath("skill");
            if (skillTop?.WzProperties != null)
            {
                foreach (var child in skillTop.WzProperties)
                {
                    if (child == null || string.IsNullOrWhiteSpace(child.Name))
                        continue;
                    if (int.TryParse(child.Name, out int parsed) && parsed == skillId)
                    {
                        matchedKey = child.Name;
                        return child;
                    }
                }
            }

            return null;
        }

        private static WzImageProperty FindStringEntryBySkillId(WzImage strImg, int skillId)
        {
            if (strImg == null || skillId <= 0)
                return null;

            foreach (string key in BuildIdNameCandidates(skillId))
            {
                var node = strImg[key];
                if (node != null)
                    return node;
            }

            if (strImg.WzProperties != null)
            {
                foreach (var child in strImg.WzProperties)
                {
                    if (child == null || string.IsNullOrWhiteSpace(child.Name))
                        continue;
                    if (int.TryParse(child.Name, out int parsed) && parsed == skillId)
                        return child;
                }
            }

            return null;
        }

        private static List<int> EnumerateJobIdsFromGameSkillRoot()
        {
            var result = new List<int>();
            string root = PathConfig.GameDataRoot;
            if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
                return result;

            try
            {
                string[] files = Directory.GetFiles(root, "*.img", SearchOption.TopDirectoryOnly);
                foreach (string file in files)
                {
                    string name = Path.GetFileNameWithoutExtension(file);
                    if (int.TryParse(name, out int jobId) && jobId >= 0)
                        result.Add(jobId);
                }
                result.Sort();
            }
            catch
            {
            }

            return result;
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
            {
                data.LevelParams = ExtractLevelParams(levelNode);

                // Also extract per-level animation frames (ball/hit/effect/prepare/keydown/repeat)
                foreach (var levelChild in levelNode.WzProperties)
                {
                    if (!int.TryParse(levelChild.Name, out int levelNum)) continue;
                    if (levelChild.WzProperties == null) continue;

                    // Debug: log what children exist under this level
                    var childNames = new List<string>();
                    foreach (var c in levelChild.WzProperties)
                        childNames.Add(c?.Name ?? "null");
                    Console.WriteLine($"[ExtractLevel] level/{levelNum} children: [{string.Join(", ", childNames)}]");

                    var levelAnimNodes = ExtractEffectFramesByNode(levelChild);
                    Console.WriteLine($"[ExtractLevel] level/{levelNum} animNodes found: {levelAnimNodes?.Count ?? 0}");
                    if (levelAnimNodes != null)
                    {
                        foreach (var kv in levelAnimNodes)
                            Console.WriteLine($"[ExtractLevel]   node='{kv.Key}' frames={kv.Value?.Count ?? 0}");
                    }
                    if (levelAnimNodes != null && levelAnimNodes.Count > 0)
                        data.LevelAnimFramesByNode[levelNum] = levelAnimNodes;
                }
            }

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

            // Effect frames (effect/effect0... + repeat/repeat0...)
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
            if (effectNode == null)
                return frames;

            var resolvedNode = ResolveProperty(effectNode);
            if (TryBuildFrameFromNode(resolvedNode, TryParseFrameIndex(effectNode.Name), out var directFrame))
            {
                if (directFrame != null)
                    frames.Add(directFrame);
                return frames;
            }

            if (resolvedNode?.WzProperties == null)
                return frames;

            foreach (var child in resolvedNode.WzProperties)
            {
                if (child == null)
                    continue;

                int idx = TryParseFrameIndex(child.Name);
                if (idx < 0)
                    continue;

                if (TryBuildFrameFromNode(child, idx, out var frame) && frame != null)
                    frames.Add(frame);
            }

            // Some data stores frames one level deeper, fallback to nested discovery.
            if (frames.Count == 0)
            {
                foreach (var child in resolvedNode.WzProperties)
                {
                    if (child == null || string.Equals(child.Name, "info", StringComparison.OrdinalIgnoreCase))
                        continue;

                    var nested = ExtractEffectFrames(child);
                    if (nested.Count > 0)
                    {
                        frames.AddRange(nested);
                        break;
                    }
                }
            }

            if (frames.Count > 1)
            {
                for (int i = 0; i < frames.Count; i++)
                    frames[i].Index = i;
            }
            frames.Sort((a, b) => a.Index.CompareTo(b.Index));
            return frames;
        }

        private static int TryParseFrameIndex(string name)
        {
            if (int.TryParse((name ?? "").Trim(), out int idx))
                return idx;
            return -1;
        }

        private bool TryBuildFrameFromNode(WzImageProperty sourceNode, int index, out WzEffectFrame frame)
        {
            frame = null;
            if (sourceNode == null)
                return false;

            var resolved = ResolveProperty(sourceNode);
            WzCanvasProperty canvas = null;
            WzImageProperty metaRoot = resolved;

            if (resolved is WzCanvasProperty directCanvas)
            {
                canvas = directCanvas;
            }
            else if (resolved is WzSubProperty sub)
            {
                var namedCanvas = ResolveProperty(sub["canvas"]) as WzCanvasProperty;
                if (namedCanvas != null)
                {
                    canvas = namedCanvas;
                }
                else if (sub.WzProperties != null)
                {
                    // Count how many numbered canvas children exist.
                    // If there are multiple, this is a frame CONTAINER (not a single frame).
                    int numberedCanvasCount = 0;
                    WzCanvasProperty firstCanvas = null;
                    foreach (var child in sub.WzProperties)
                    {
                        var resolvedChild = ResolveProperty(child);
                        if (resolvedChild is WzCanvasProperty childCanvas && int.TryParse(child.Name, out _))
                        {
                            numberedCanvasCount++;
                            if (firstCanvas == null) firstCanvas = childCanvas;
                            if (numberedCanvasCount >= 2) break;
                        }
                    }
                    if (numberedCanvasCount >= 2)
                    {
                        // Multiple numbered canvases → this is a frame container, not a single frame
                        canvas = null; // will return false
                    }
                    else if (firstCanvas != null)
                    {
                        canvas = firstCanvas;
                    }
                    else
                    {
                        // Try any canvas child (non-numbered, e.g. wrapped single frame)
                        foreach (var child in sub.WzProperties)
                        {
                            var resolvedChild = ResolveProperty(child);
                            if (resolvedChild is WzCanvasProperty childCanvas)
                            {
                                canvas = childCanvas;
                                break;
                            }
                        }
                    }
                }
            }

            if (canvas == null)
                return false;

            frame = new WzEffectFrame
            {
                Index = index >= 0 ? index : 0,
                Delay = ReadDelay(metaRoot, 100)
            };
            if (frame.Delay <= 0)
                frame.Delay = ReadDelay(canvas, 100);

            TryFillFrameBitmap(canvas, frame);
            CopyFrameMeta(metaRoot, frame, skipCanvasChildren: true);
            if (!object.ReferenceEquals(metaRoot, canvas))
                CopyFrameMeta(canvas, frame, skipCanvasChildren: false);

            return true;
        }

        private static int ReadDelay(WzImageProperty prop, int fallback)
        {
            if (prop == null)
                return fallback;

            WzImageProperty delay = null;
            if (prop is WzCanvasProperty canvas)
                delay = canvas["delay"];
            else if (prop is WzSubProperty sub)
                delay = sub["delay"];

            if (delay is WzIntProperty dp) return dp.Value;
            if (delay is WzShortProperty dsp) return dsp.Value;
            if (delay is WzLongProperty dlp) return (int)dlp.Value;
            if (delay is WzStringProperty ds && int.TryParse(ds.Value, out int parsed)) return parsed;
            return fallback;
        }

        private static void TryFillFrameBitmap(WzCanvasProperty canvas, WzEffectFrame frame)
        {
            if (canvas == null || frame == null)
                return;

            try
            {
                Bitmap src = null;

                // Diagnostic: log canvas info
                string canvasPath = canvas.Name ?? "?";
                try
                {
                    var p = canvas.Parent;
                    int depth = 0;
                    while (p != null && depth < 8)
                    {
                        canvasPath = (p.Name ?? "?") + "/" + canvasPath;
                        p = p.Parent;
                        depth++;
                    }
                }
                catch { }

                var png = canvas.PngProperty;
                int pngW = 0, pngH = 0;
                string pngFmt = "null";
                bool hasWzReader = false;
                if (png != null)
                {
                    pngW = png.Width;
                    pngH = png.Height;
                    pngFmt = png.Format.ToString();
                    // Check if wzReader is available (needed for listWz XOR decryption)
                    try
                    {
                        var readerField = typeof(WzPngProperty).GetField("wzReader",
                            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
                        hasWzReader = readerField?.GetValue(png) != null;
                    }
                    catch { }
                }

                // 1. Check for _inlink/_outlink FIRST and try manual resolution
                string inlink = (canvas[WzCanvasProperty.InlinkPropertyName] as WzStringProperty)?.Value;
                string outlink = (canvas[WzCanvasProperty.OutlinkPropertyName] as WzStringProperty)?.Value;

                if (!string.IsNullOrEmpty(inlink))
                {
                    src = TryResolveInlinkBitmap(canvas, inlink);
                    Console.WriteLine($"[FillBmp] {canvasPath} _inlink={inlink} result={(src != null ? $"{src.Width}x{src.Height} blank={IsBitmapBlank(src)}" : "null")}");
                }
                else if (!string.IsNullOrEmpty(outlink))
                {
                    src = TryResolveOutlinkBitmap(outlink);
                    Console.WriteLine($"[FillBmp] {canvasPath} _outlink={outlink} result={(src != null ? $"{src.Width}x{src.Height} blank={IsBitmapBlank(src)}" : "null")}");
                }

                // 2. Standard path: GetLinkedWzCanvasBitmap
                if (src == null || IsBitmapBlank(src))
                {
                    try { src = canvas.GetLinkedWzCanvasBitmap(); }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"[FillBmp] {canvasPath} GetLinkedWzCanvasBitmap threw: {ex.GetType().Name}: {ex.Message}");
                    }
                    if (src != null)
                        Console.WriteLine($"[FillBmp] {canvasPath} GetLinkedWzCanvasBitmap => {src.Width}x{src.Height} blank={IsBitmapBlank(src)}");
                }
                if (src == null || IsBitmapBlank(src))
                {
                    try { src = canvas.GetBitmap(); }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"[FillBmp] {canvasPath} GetBitmap threw: {ex.GetType().Name}: {ex.Message}");
                    }
                    if (src != null)
                        Console.WriteLine($"[FillBmp] {canvasPath} GetBitmap => {src.Width}x{src.Height} blank={IsBitmapBlank(src)}");
                }
                // 3. Last resort: PngProperty.GetImage(true) directly
                if ((src == null || IsBitmapBlank(src)) && png != null)
                {
                    Console.WriteLine($"[FillBmp] {canvasPath} trying PngProperty.GetImage(true) fmt={pngFmt} {pngW}x{pngH} wzReader={hasWzReader}");
                    try { src = png.GetImage(true); }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"[FillBmp] {canvasPath} PngProperty.GetImage threw: {ex.GetType().Name}: {ex.Message}");
                    }
                    if (src != null)
                        Console.WriteLine($"[FillBmp] {canvasPath} PngProperty.GetImage => {src.Width}x{src.Height} blank={IsBitmapBlank(src)}");
                }

                if (src == null)
                {
                    Console.WriteLine($"[FillBmp] {canvasPath} FAILED: all attempts returned null. png={png != null} fmt={pngFmt} {pngW}x{pngH} wzReader={hasWzReader}");
                }
                else if (IsBitmapBlank(src))
                {
                    Console.WriteLine($"[FillBmp] {canvasPath} BLANK: got {src.Width}x{src.Height} but all sampled pixels are black/transparent. fmt={pngFmt} wzReader={hasWzReader}");
                }

                if (src != null)
                {
                    frame.Bitmap = CloneBitmapSafe(src);
                    frame.Width = frame.Bitmap.Width;
                    frame.Height = frame.Bitmap.Height;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[FillBmp] outer exception: {ex.GetType().Name}: {ex.Message}");
            }
        }

        /// <summary>
        /// Resolve an _inlink path within the same WzImage.
        /// </summary>
        private static Bitmap TryResolveInlinkBitmap(WzCanvasProperty canvas, string inlinkPath)
        {
            try
            {
                // Walk up to the nearest WzImage
                WzObject current = canvas.Parent;
                while (current != null && !(current is WzImage))
                    current = current.Parent;
                if (current is WzImage wzImage)
                {
                    var resolved = wzImage.GetFromPath(inlinkPath);
                    if (resolved is WzCanvasProperty target)
                    {
                        Bitmap bmp = null;
                        try { bmp = target.GetBitmap(); } catch { }
                        if (bmp != null && !IsBitmapBlank(bmp))
                            return bmp;
                        // Try PngProperty directly
                        if (target.PngProperty != null)
                        {
                            try { bmp = target.PngProperty.GetImage(false); } catch { }
                            if (bmp != null && !IsBitmapBlank(bmp))
                                return bmp;
                        }
                    }
                }
            }
            catch { }
            return null;
        }

        /// <summary>
        /// Resolve an _outlink path by loading the target .img file.
        /// Outlink format: "Category/XXXX.img/path/to/canvas"  (e.g. "Skill/1000.img/skill/10001000/hit/0/0")
        /// Or short form: "Skill/10001000/hit/0/0" (auto-detect .img from Skill subdirectory)
        /// </summary>
        private static Bitmap TryResolveOutlinkBitmap(string outlinkPath)
        {
            try
            {
                // Parse the outlink path to find which .img file and internal path
                // Common format: "Skill/XXXX.img/internal/path"
                // Also seen: "Skill/path" without explicit .img

                if (string.IsNullOrEmpty(outlinkPath))
                    return null;

                string[] parts = outlinkPath.Split('/');
                if (parts.Length < 3)
                    return null;

                string category = parts[0]; // e.g. "Skill"

                // Find the .img part
                int imgPartIdx = -1;
                for (int i = 1; i < parts.Length; i++)
                {
                    if (parts[i].EndsWith(".img", StringComparison.OrdinalIgnoreCase))
                    {
                        imgPartIdx = i;
                        break;
                    }
                }

                string imgFileName;
                string internalPath;

                if (imgPartIdx >= 0)
                {
                    // Explicit .img reference: "Skill/1000.img/skill/10001000/hit/0/0"
                    imgFileName = parts[imgPartIdx]; // "1000.img"
                    internalPath = string.Join("/", parts, imgPartIdx + 1, parts.Length - imgPartIdx - 1);
                }
                else
                {
                    // No explicit .img — try to infer from path
                    // e.g. "Skill/10001000/hit/0/0" → jobId=1000, path="skill/10001000/hit/0/0"
                    if (!string.Equals(category, "Skill", StringComparison.OrdinalIgnoreCase))
                        return null;
                    // parts[1] should be the skill ID
                    if (!int.TryParse(parts[1], out int skillId))
                        return null;
                    int jobId = skillId / 10000;
                    imgFileName = PathConfig.SkillImgName(jobId);
                    string rest = parts.Length > 2
                        ? "/" + string.Join("/", parts, 2, parts.Length - 2)
                        : "";
                    internalPath = "skill/" + PathConfig.SkillKey(skillId) + rest;
                }

                // Build full path to .img file
                string imgDir = null;
                if (string.Equals(category, "Skill", StringComparison.OrdinalIgnoreCase))
                    imgDir = PathConfig.GameDataRoot != null ? Path.Combine(PathConfig.GameDataRoot, "Skill") : null;

                if (imgDir == null || !Directory.Exists(imgDir))
                    return null;

                string fullImgPath = Path.Combine(imgDir, imgFileName);
                if (!File.Exists(fullImgPath))
                    return null;

                // Load and parse the target .img
                WzMapleVersion version = WzImageVersionHelper.DetectVersionForSkillImg(fullImgPath);
                using (var fs = new FileStream(fullImgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                {
                    var tempImg = new WzImage(imgFileName, fs, version);
                    try
                    {
                        if (!tempImg.ParseImage(true))
                            return null;

                        var resolved = tempImg.GetFromPath(internalPath);
                        if (resolved is WzCanvasProperty target)
                        {
                            Bitmap bmp = null;
                            try { bmp = target.GetBitmap(); } catch { }
                            if (bmp == null || IsBitmapBlank(bmp))
                            {
                                if (target.PngProperty != null)
                                    try { bmp = target.PngProperty.GetImage(false); } catch { }
                            }
                            // Must clone before disposing the temp image
                            if (bmp != null && !IsBitmapBlank(bmp))
                                return CloneBitmapSafe(bmp);
                        }
                    }
                    finally
                    {
                        try { tempImg.Dispose(); } catch { }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[WzImgLoader] _outlink resolve failed for '{outlinkPath}': {ex.Message}");
            }
            return null;
        }

        /// <summary>
        /// Quick check: sample a few pixels to see if bitmap is completely blank (black/transparent).
        /// Returns true if all sampled pixels are (0,0,0,0) or (0,0,0,255).
        /// </summary>
        private static bool IsBitmapBlank(Bitmap bmp)
        {
            if (bmp == null || bmp.Width == 0 || bmp.Height == 0)
                return true;
            // Sample up to 9 evenly spaced pixels
            int w = bmp.Width, h = bmp.Height;
            int[] xs = { 0, w / 2, w - 1 };
            int[] ys = { 0, h / 2, h - 1 };
            foreach (int x in xs)
            {
                foreach (int y in ys)
                {
                    if (x >= 0 && x < w && y >= 0 && y < h)
                    {
                        var c = bmp.GetPixel(x, y);
                        if (c.A > 0 && (c.R > 0 || c.G > 0 || c.B > 0))
                            return false; // found a non-black, non-transparent pixel
                    }
                }
            }
            return true;
        }

        private void CopyFrameMeta(WzImageProperty source, WzEffectFrame frame, bool skipCanvasChildren)
        {
            if (source?.WzProperties == null || frame == null)
                return;

            foreach (var p in source.WzProperties)
            {
                if (p == null)
                    continue;
                if (string.Equals(p.Name, "delay", StringComparison.OrdinalIgnoreCase))
                    continue;
                if (skipCanvasChildren && p is WzCanvasProperty)
                    continue;

                if (TryReadVector(p, out int vx, out int vy))
                {
                    frame.Vectors[p.Name] = new WzFrameVector(vx, vy);
                    continue;
                }

                string value = GetPropertyValueString(p);
                if (value != null)
                    frame.FrameProps[p.Name] = value;
            }
        }

        private Dictionary<string, List<WzEffectFrame>> ExtractEffectFramesByNode(WzImageProperty skillNode)
        {
            var result = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
            if (skillNode?.WzProperties == null)
                return result;

            foreach (var child in skillNode.WzProperties)
            {
                bool isAnim = IsAnimNodeName(child?.Name);
                if (!isAnim)
                    continue;

                Console.WriteLine($"[ExtractByNode] found anim child '{child.Name}' type={child.GetType().Name} under '{skillNode.Name}'");
                ExtractAnimNode(child, child.Name, result);
            }

            return result;
        }

        /// <summary>
        /// Generically extract an animation node.
        /// First checks if the node contains sub-groups (numbered SubProperty children)
        /// or direct frames (numbered Canvas children). Handles both patterns for all node types.
        /// For sub-groups, stores as "nodeName/0", "nodeName/1" etc.
        /// </summary>
        private void ExtractAnimNode(WzImageProperty node, string nodeName, Dictionary<string, List<WzEffectFrame>> result)
        {
            if (node?.WzProperties == null)
            {
                Console.WriteLine($"[ExtractAnimNode] '{nodeName}' node has no WzProperties, skipping");
                return;
            }

            // Detect whether this node uses sub-groups or direct frames.
            bool hasSubGroups = false;
            bool hasDirectFrames = false;

            foreach (var child in node.WzProperties)
            {
                if (child == null) continue;
                if (!int.TryParse(child.Name, out int idx) || idx < 0)
                    continue;

                var resolved = ResolveProperty(child);
                if (resolved is WzCanvasProperty)
                {
                    hasDirectFrames = true;
                    break;
                }
                else if (resolved is WzSubProperty sub && sub.WzProperties != null && sub.WzProperties.Count > 0)
                {
                    foreach (var grandchild in sub.WzProperties)
                    {
                        if (grandchild == null) continue;
                        if (!int.TryParse(grandchild.Name, out _)) continue;
                        var resolvedGC = ResolveProperty(grandchild);
                        if (resolvedGC is WzCanvasProperty)
                        {
                            hasSubGroups = true;
                            break;
                        }
                    }
                    if (hasSubGroups) break;
                }
            }

            Console.WriteLine($"[ExtractAnimNode] '{nodeName}' hasSubGroups={hasSubGroups} hasDirectFrames={hasDirectFrames}");

            if (hasSubGroups && !hasDirectFrames)
            {
                bool foundAny = false;
                foreach (var child in node.WzProperties)
                {
                    if (child == null) continue;
                    if (!int.TryParse(child.Name, out int groupIdx) || groupIdx < 0)
                        continue;

                    var frames = ExtractEffectFrames(child);
                    Console.WriteLine($"[ExtractAnimNode] subgroup '{nodeName}/{groupIdx}' frames={frames?.Count ?? 0}");
                    if (frames != null && frames.Count > 0)
                    {
                        result[nodeName + "/" + groupIdx] = frames;
                        foundAny = true;
                    }
                }
                if (!foundAny)
                {
                    var directFrames = ExtractEffectFrames(node);
                    if (directFrames != null && directFrames.Count > 0)
                        result[nodeName] = directFrames;
                }
            }
            else
            {
                var frames = ExtractEffectFrames(node);
                Console.WriteLine($"[ExtractAnimNode] direct '{nodeName}' frames={frames?.Count ?? 0}");
                if (frames != null && frames.Count > 0)
                    result[nodeName] = frames;
            }
        }

        private static readonly string[] AnimNodeExactNames = new[]
        {
            "effect", "repeat", "ball", "hit", "prepare", "keydown",
            "keydownend", "affected", "mob", "special", "screen", "tile", "finish"
        };
        private static readonly string[] AnimNodeIndexablePrefixes = new[]
        {
            "effect", "repeat", "ball", "keydown", "affected", "mob", "special", "tile", "finish"
        };

        private static bool IsAnimNodeName(string name)
        {
            if (string.IsNullOrWhiteSpace(name))
                return false;
            foreach (var exact in AnimNodeExactNames)
            {
                if (string.Equals(name, exact, StringComparison.OrdinalIgnoreCase))
                    return true;
            }
            foreach (var prefix in AnimNodeIndexablePrefixes)
            {
                if (TryParseIndexedFrameNodeName(name, prefix, out _))
                    return true;
            }
            return false;
        }

        // Keep old name as alias for backward compat within this file
        private static bool IsEffectNodeName(string name) => IsAnimNodeName(name);

        private static int CompareEffectNodeName(string a, string b)
        {
            if (string.Equals(a, b, StringComparison.OrdinalIgnoreCase))
                return 0;

            int aRank = GetFrameNodeSortRank(a, out int aIndex);
            int bRank = GetFrameNodeSortRank(b, out int bIndex);
            if (aRank != bRank)
                return aRank.CompareTo(bRank);
            if (aIndex != int.MaxValue || bIndex != int.MaxValue)
                return aIndex.CompareTo(bIndex);
            return string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
        }

        private static readonly (string baseName, int baseRank, bool canIndex)[] FrameNodeSortTable = new[]
        {
            ("effect",     0, true),
            ("repeat",     2, true),
            ("ball",       4, true),
            ("hit",        6, false),   // hit/0, hit/1 handled specially
            ("prepare",    8, false),
            ("keydown",   10, true),
            ("keydownend",12, false),
            ("affected",  14, true),
            ("mob",       16, true),
            ("special",   18, true),
            ("screen",    20, false),
            ("tile",      22, true),
            ("finish",    24, true),
        };

        private static int GetFrameNodeSortRank(string name, out int index)
        {
            index = int.MaxValue;
            if (string.IsNullOrWhiteSpace(name))
                return 99;

            // hit/N special case
            if (name.StartsWith("hit/", StringComparison.OrdinalIgnoreCase)
                && int.TryParse(name.Substring(4), out index))
                return 6;
            if (string.Equals(name, "hit", StringComparison.OrdinalIgnoreCase))
            {
                index = -1;
                return 6;
            }

            foreach (var (baseName, baseRank, canIndex) in FrameNodeSortTable)
            {
                if (baseName == "hit") continue; // already handled
                if (string.Equals(name, baseName, StringComparison.OrdinalIgnoreCase))
                    return baseRank;
                if (canIndex && TryParseIndexedFrameNodeName(name, baseName, out index))
                    return baseRank + 1;
            }
            return 99;
        }

        private static bool TryParseIndexedFrameNodeName(string name, string prefix, out int index)
        {
            index = -1;
            if (string.IsNullOrWhiteSpace(name) || string.IsNullOrWhiteSpace(prefix))
                return false;
            if (!name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                return false;
            string suffix = name.Substring(prefix.Length);
            if (string.IsNullOrEmpty(suffix))
                return false;
            return int.TryParse(suffix, out index);
        }

        private WzNodeInfo BuildNodeTree(WzImageProperty node, int depth)
        {
            var info = new WzNodeInfo
            {
                Name = node.Name ?? "",
                TypeName = node.PropertyType.ToString(),
                Children = new List<WzNodeInfo>()
            };

            // Leaf value
            string val = GetPropertyValueString(node);
            if (val != null)
                info.Value = val;

            if (node is WzVectorProperty vector)
                info.Value = vector.X.Value + "," + vector.Y.Value;

            if (node is WzCanvasProperty canvas && canvas.PngProperty != null)
            {
                info.CanvasWidth = canvas.PngProperty.Width;
                info.CanvasHeight = canvas.PngProperty.Height;
                info.CanvasPngFormat = (int)canvas.PngProperty.Format;
                info.Value = info.CanvasWidth + "x" + info.CanvasHeight;
                try
                {
                    byte[] bytes = canvas.PngProperty.GetCompressedBytes(true);
                    if (bytes != null)
                        info.CanvasCompressedBytes = (byte[])bytes.Clone();
                }
                catch
                {
                }

                try
                {
                    Bitmap bmp = canvas.GetBitmap();
                    if (bmp != null)
                        info.CanvasBitmap = CloneBitmapSafe(bmp);
                }
                catch
                {
                }
            }

            // Children (limit depth to prevent stack overflow on very deep trees)
            if (depth < 32 && node.WzProperties != null)
            {
                foreach (var child in node.WzProperties)
                {
                    if (child != null)
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
                    Bitmap src = null;

                    // Check _inlink/_outlink first (standalone .img has no WzDirectory parent)
                    string inlink = (canvas[WzCanvasProperty.InlinkPropertyName] as WzStringProperty)?.Value;
                    string outlink = (canvas[WzCanvasProperty.OutlinkPropertyName] as WzStringProperty)?.Value;
                    if (!string.IsNullOrEmpty(inlink))
                        src = TryResolveInlinkBitmap(canvas, inlink);
                    else if (!string.IsNullOrEmpty(outlink))
                        src = TryResolveOutlinkBitmap(outlink);

                    if (src == null || IsBitmapBlank(src))
                        try { src = canvas.GetLinkedWzCanvasBitmap(); } catch { }
                    if (src == null || IsBitmapBlank(src))
                        try { src = canvas.GetBitmap(); } catch { }
                    if (src == null) return null;
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
            _skillJobHintCache.Clear();

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
