using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Text;

namespace SuperSkillTool
{
    /// <summary>
    /// Data model for a single skill to be added.
    /// Populated from the input JSON, then merged with template defaults.
    /// </summary>
    public class SkillDefinition
    {
        // ── Identity ───────────────────────────────────────────
        public int SkillId;
        public string Name = "";
        public string Desc = "";

        // ── Type / template ────────────────────────────────────
        public string Type = "active_melee";   // template key
        public string Tab = "active";          // active | buff | passive

        // ── Levels ─────────────────────────────────────────────
        public int MaxLevel = 20;
        public int SuperSpCost = 1;

        // ── Icon paths (relative to tool root) ─────────────────
        public string Icon = "";
        public string IconMouseOver = "";
        public string IconDisabled = "";

        // ── Icon base64 data (from .img, runtime-only, not serialized) ──
        public string IconBase64 = "";
        public string IconMouseOverBase64 = "";
        public string IconDisabledBase64 = "";

        // ── Combat / routing ───────────────────────────────────
        public string ReleaseType = "";        // close_range | ranged_attack | magic_attack | special_move
        public string ReleaseClass = "native_classifier_proxy";
        public bool BorrowDonorVisual = false;
        public int ProxySkillId = 0;
        public int VisualSkillId = 0;          // 0 = use own visual; >0 = borrow effect from this skillId
        public int CloneFromSkillId = 0;       // when set, create by cloning this source skill node first
        public string Action = "";             // swingO1 | shoot1 | alert2

        // ── Info node ──────────────────────────────────────────
        public int InfoType = 1;               // 1=active, 10=buff(?), 50=passive

        // ── Common parameters (formula strings) ────────────────
        public Dictionary<string, string> Common = new Dictionary<string, string>();

        // ── Level-mode data (for newbie_level type) ────────────
        /// <summary>
        /// key = level number (1,2,3...), value = dict of param -> value
        /// Only used when Type == "newbie_level".
        /// </summary>
        public Dictionary<int, Dictionary<string, string>> Levels;

        // ── Source tracking (cached, not persisted to JSON) ──────
        /// <summary>
        /// "新技能" / "超级技能" / "原版技能" — set once when added to list.
        /// </summary>
        public string SourceLabel = "新技能";
        /// <summary>
        /// true = skill existed in .img at the time it was added to the list.
        /// </summary>
        public bool ExistsInImg = false;

        // ── Flags ──────────────────────────────────────────────
        public bool HideFromNativeSkillWnd = true;
        public bool ShowInNativeWhenLearned = false;
        public bool ShowInSuperWhenLearned = false;
        public bool AllowNativeUpgradeFallback = true;
        public bool InjectToNative = true;
        public bool InjectEnabled = true;      // for native_skill_injections enabled flag
        public int DonorSkillId = 0;
        public int MountItemId = 0;            // for super_skills_server mount override
        public string MountResourceMode = "config_only"; // config_only | sync_action | sync_action_and_data
        public int MountSourceItemId = 0;      // donor mount item id for resource sync
        public int MountTamingMobId = 0;       // target tamingMob id when syncing mount data
        public int? MountSpeedOverride = null; // optional override for TamingMob info/speed
        public int? MountJumpOverride = null;  // optional override for TamingMob info/jump
        public int? MountFatigueOverride = null; // optional override for TamingMob info/fatigue
        public int SuperSpCarrierSkillId = 0;  // per-skill override; 0 = use default
        public bool ServerEnabled = true;      // for super_skills_server enabled flag

        // ── Prerequisite skills ────────────────────────────────
        public Dictionary<int, int> RequiredSkills = new Dictionary<int, int>(); // skillId -> requiredLevel

        // ── hLevel descriptions ────────────────────────────────
        public Dictionary<string, string> HLevels = new Dictionary<string, string>();

        // ── Cached data (serialized to JSON for persistence) ──────
        /// <summary>Effect frames from .img load.</summary>
        public List<WzEffectFrame> CachedEffects;
        public Dictionary<string, List<WzEffectFrame>> CachedEffectsByNode;
        /// <summary>Node tree from .img load.</summary>
        public WzNodeInfo CachedTree;

        // ── Derived ────────────────────────────────────────────
        public int JobId => SkillId / 10000;

        /// <summary>
        /// Resolve the preferred donor/source skill to clone visual/resource nodes from.
        /// Explicit cloneFromSkillId wins. Otherwise, visual-donor style routes can
        /// implicitly borrow from visualSkillId / proxySkillId / donorSkillId.
        /// </summary>
        public int ResolveCloneSourceSkillId()
        {
            if (CloneFromSkillId > 0 && CloneFromSkillId != SkillId)
                return CloneFromSkillId;

            if (VisualSkillId > 0 && VisualSkillId != SkillId)
                return VisualSkillId;

            if (!BorrowDonorVisual)
                return 0;

            if (ProxySkillId > 0 && ProxySkillId != SkillId)
                return ProxySkillId;

            if (DonorSkillId > 0 && DonorSkillId != SkillId)
                return DonorSkillId;

            return 0;
        }

        /// <summary>
        /// Serialize this definition back to a JSON-compatible dictionary.
        /// </summary>
        public Dictionary<string, object> ToJsonDict()
        {
            NormalizeTextFields();

            var d = new Dictionary<string, object>();
            d["skillId"] = (long)SkillId;
            d["name"] = Name;
            if (!string.IsNullOrEmpty(Desc)) d["desc"] = Desc;
            d["type"] = Type;
            d["tab"] = Tab;
            d["maxLevel"] = (long)MaxLevel;
            d["superSpCost"] = (long)SuperSpCost;
            if (!string.IsNullOrEmpty(Icon)) d["icon"] = Icon;
            if (!string.IsNullOrEmpty(IconMouseOver)) d["iconMouseOver"] = IconMouseOver;
            if (!string.IsNullOrEmpty(IconDisabled)) d["iconDisabled"] = IconDisabled;
            if (!string.IsNullOrEmpty(ReleaseType)) d["releaseType"] = ReleaseType;
            if (!string.IsNullOrEmpty(ReleaseClass)) d["releaseClass"] = ReleaseClass;
            if (BorrowDonorVisual) d["borrowDonorVisual"] = true;
            if (ProxySkillId > 0) d["proxySkillId"] = (long)ProxySkillId;
            if (VisualSkillId > 0) d["visualSkillId"] = (long)VisualSkillId;
            if (CloneFromSkillId > 0) d["cloneFromSkillId"] = (long)CloneFromSkillId;
            if (!string.IsNullOrEmpty(Action)) d["action"] = Action;
            if (InfoType > 0) d["infoType"] = (long)InfoType;
            if (!string.IsNullOrEmpty(IconBase64)) d["iconBase64"] = IconBase64;
            if (!string.IsNullOrEmpty(IconMouseOverBase64)) d["iconMouseOverBase64"] = IconMouseOverBase64;
            if (!string.IsNullOrEmpty(IconDisabledBase64)) d["iconDisabledBase64"] = IconDisabledBase64;
            d["hideFromNativeSkillWnd"] = HideFromNativeSkillWnd;
            d["showInNativeWhenLearned"] = ShowInNativeWhenLearned;
            d["showInSuperWhenLearned"] = ShowInSuperWhenLearned;
            d["allowNativeUpgradeFallback"] = AllowNativeUpgradeFallback;
            d["injectToNative"] = InjectToNative;
            if (InjectToNative) d["injectEnabled"] = InjectEnabled;
            if (DonorSkillId > 0) d["donorSkillId"] = (long)DonorSkillId;
            if (MountItemId > 0) d["mountItemId"] = (long)MountItemId;
            if (!string.IsNullOrEmpty(MountResourceMode) && !string.Equals(MountResourceMode, "config_only", StringComparison.OrdinalIgnoreCase))
                d["mountResourceMode"] = MountResourceMode;
            if (MountSourceItemId > 0) d["mountSourceItemId"] = (long)MountSourceItemId;
            if (MountTamingMobId > 0) d["mountTamingMobId"] = (long)MountTamingMobId;
            if (MountSpeedOverride.HasValue) d["mountSpeed"] = (long)MountSpeedOverride.Value;
            if (MountJumpOverride.HasValue) d["mountJump"] = (long)MountJumpOverride.Value;
            if (MountFatigueOverride.HasValue) d["mountFatigue"] = (long)MountFatigueOverride.Value;
            if (SuperSpCarrierSkillId > 0) d["superSpCarrierSkillId"] = (long)SuperSpCarrierSkillId;
            if (!ServerEnabled) d["serverEnabled"] = false;
            if (Common.Count > 0)
            {
                var c = new Dictionary<string, object>();
                foreach (var kv in Common) c[kv.Key] = kv.Value;
                d["common"] = c;
            }
            if (RequiredSkills.Count > 0)
            {
                var r = new Dictionary<string, object>();
                foreach (var kv in RequiredSkills) r[kv.Key.ToString()] = (long)kv.Value;
                d["req"] = r;
            }
            if (HLevels.Count > 0)
            {
                var h = new Dictionary<string, object>();
                foreach (var kv in HLevels) h[kv.Key] = kv.Value;
                d["hLevels"] = h;
            }
            if (Levels != null && Levels.Count > 0)
            {
                var lvDict = new Dictionary<string, object>();
                foreach (var lv in Levels)
                {
                    var paramDict = new Dictionary<string, object>();
                    foreach (var p in lv.Value) paramDict[p.Key] = p.Value;
                    lvDict[lv.Key.ToString()] = paramDict;
                }
                d["levels"] = lvDict;
            }
            Dictionary<string, List<WzEffectFrame>> effectMapForSave = CachedEffectsByNode;
            if ((effectMapForSave == null || effectMapForSave.Count == 0) && CachedEffects != null && CachedEffects.Count > 0)
            {
                effectMapForSave = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase)
                {
                    ["effect"] = CachedEffects
                };
            }
            var serializedByNode = SerializeEffectFramesByNode(effectMapForSave);
            if (serializedByNode != null && serializedByNode.Count > 0)
            {
                d["cachedEffectsByNode"] = serializedByNode;
                if (serializedByNode.TryGetValue("effect", out object effectArrObj) && effectArrObj is List<object>)
                {
                    d["cachedEffects"] = effectArrObj;
                }
                else
                {
                    foreach (var kv in serializedByNode)
                    {
                        if (kv.Value is List<object>)
                        {
                            d["cachedEffects"] = kv.Value;
                            break;
                        }
                    }
                }
            }
            else
            {
                var frames = SerializeEffectFrames(CachedEffects);
                if (frames != null && frames.Count > 0)
                    d["cachedEffects"] = frames;
            }
            if (CachedTree != null)
                d["cachedTree"] = SerializeNodeTree(CachedTree);
            return d;
        }

        // ── Serialization helpers for WzNodeInfo / WzEffectFrame ──

        private static Dictionary<string, object> SerializeNodeTree(WzNodeInfo node)
        {
            if (node == null) return null;
            var d = new Dictionary<string, object>();
            d["name"] = node.Name ?? "";
            d["typeName"] = node.TypeName ?? "";
            if (!string.IsNullOrEmpty(node.Value)) d["value"] = node.Value;
            if (node.Children != null && node.Children.Count > 0)
            {
                var children = new List<object>();
                foreach (var child in node.Children)
                    children.Add(SerializeNodeTree(child));
                d["children"] = children;
            }
            return d;
        }

        private static WzNodeInfo DeserializeNodeTree(Dictionary<string, object> obj)
        {
            if (obj == null) return null;
            var node = new WzNodeInfo();
            node.Name = SimpleJson.GetString(obj, "name");
            node.TypeName = SimpleJson.GetString(obj, "typeName");
            node.Value = SimpleJson.GetString(obj, "value");
            var childArr = SimpleJson.GetArray(obj, "children");
            if (childArr != null)
            {
                foreach (var item in childArr)
                {
                    if (item is Dictionary<string, object> childObj)
                        node.Children.Add(DeserializeNodeTree(childObj));
                }
            }
            return node;
        }

        private static List<WzEffectFrame> DeserializeEffectFrames(List<object> arr)
        {
            if (arr == null || arr.Count == 0) return null;
            var frames = new List<WzEffectFrame>();
            foreach (var item in arr)
            {
                if (!(item is Dictionary<string, object> fd)) continue;
                var ef = new WzEffectFrame();
                ef.Index = SimpleJson.GetInt(fd, "index");
                ef.Width = SimpleJson.GetInt(fd, "width");
                ef.Height = SimpleJson.GetInt(fd, "height");
                ef.Delay = SimpleJson.GetInt(fd, "delay", 100);
                var vectorsObj = SimpleJson.GetObject(fd, "vectors");
                if (vectorsObj != null)
                {
                    foreach (var kv in vectorsObj)
                    {
                        if (string.IsNullOrEmpty(kv.Key)) continue;
                        if (!(kv.Value is Dictionary<string, object> vecObj)) continue;
                        int x = SimpleJson.GetInt(vecObj, "x", 0);
                        int y = SimpleJson.GetInt(vecObj, "y", 0);
                        ef.Vectors[kv.Key] = new WzFrameVector(x, y);
                    }
                }
                var framePropsObj = SimpleJson.GetObject(fd, "frameProps") ?? SimpleJson.GetObject(fd, "props");
                if (framePropsObj != null)
                {
                    foreach (var kv in framePropsObj)
                    {
                        if (string.IsNullOrEmpty(kv.Key)) continue;
                        if (kv.Value == null) continue;
                        ef.FrameProps[kv.Key] = kv.Value.ToString();
                    }
                }
                string b64 = SimpleJson.GetString(fd, "bitmapBase64");
                if (!string.IsNullOrEmpty(b64))
                {
                    try
                    {
                        byte[] bytes = Convert.FromBase64String(b64);
                        var ms = new MemoryStream(bytes);
                        var temp = new Bitmap(ms);
                        var bmp = new Bitmap(temp.Width, temp.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
                        using (var g = System.Drawing.Graphics.FromImage(bmp))
                        {
                            g.DrawImage(temp, 0, 0, temp.Width, temp.Height);
                        }
                        temp.Dispose();
                        ms.Dispose();
                        ef.Bitmap = bmp;
                    }
                    catch { }
                }
                frames.Add(ef);
            }
            return frames.Count > 0 ? frames : null;
        }

        private static List<object> SerializeEffectFrames(List<WzEffectFrame> frames)
        {
            if (frames == null || frames.Count == 0) return null;

            var result = new List<object>();
            foreach (var ef in frames)
            {
                if (ef == null) continue;

                var fd = new Dictionary<string, object>();
                fd["index"] = (long)ef.Index;
                fd["width"] = (long)ef.Width;
                fd["height"] = (long)ef.Height;
                fd["delay"] = (long)ef.Delay;
                if (ef.Vectors != null && ef.Vectors.Count > 0)
                {
                    var vectors = new Dictionary<string, object>();
                    foreach (var vk in ef.Vectors)
                    {
                        if (string.IsNullOrEmpty(vk.Key) || vk.Value == null) continue;
                        var vec = new Dictionary<string, object>();
                        vec["x"] = (long)vk.Value.X;
                        vec["y"] = (long)vk.Value.Y;
                        vectors[vk.Key] = vec;
                    }
                    if (vectors.Count > 0)
                        fd["vectors"] = vectors;
                }
                if (ef.FrameProps != null && ef.FrameProps.Count > 0)
                {
                    var props = new Dictionary<string, object>();
                    foreach (var pk in ef.FrameProps)
                    {
                        if (string.IsNullOrEmpty(pk.Key)) continue;
                        props[pk.Key] = pk.Value ?? "";
                    }
                    if (props.Count > 0)
                        fd["frameProps"] = props;
                }
                if (ef.Bitmap != null)
                {
                    try
                    {
                        using (var ms = new MemoryStream())
                        {
                            ef.Bitmap.Save(ms, ImageFormat.Png);
                            fd["bitmapBase64"] = Convert.ToBase64String(ms.ToArray());
                        }
                    }
                    catch { }
                }
                result.Add(fd);
            }
            return result.Count > 0 ? result : null;
        }

        private static Dictionary<string, object> SerializeEffectFramesByNode(Dictionary<string, List<WzEffectFrame>> map)
        {
            if (map == null || map.Count == 0) return null;

            var normalizedMap = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
            var keys = new List<string>();
            foreach (var kv in map)
            {
                if (string.IsNullOrWhiteSpace(kv.Key) || kv.Value == null || kv.Value.Count == 0)
                    continue;
                string key = NormalizeEffectNodeName(kv.Key);
                normalizedMap[key] = kv.Value;
                bool exists = false;
                foreach (string existing in keys)
                {
                    if (string.Equals(existing, key, StringComparison.OrdinalIgnoreCase))
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                    keys.Add(key);
            }
            if (keys.Count == 0) return null;
            keys.Sort(CompareEffectNodeName);

            var result = new Dictionary<string, object>(StringComparer.OrdinalIgnoreCase);
            foreach (string key in keys)
            {
                if (!normalizedMap.TryGetValue(key, out var frames) || frames == null || frames.Count == 0)
                    continue;
                var arr = SerializeEffectFrames(frames);
                if (arr != null && arr.Count > 0)
                    result[key] = arr;
            }
            return result.Count > 0 ? result : null;
        }

        private static Dictionary<string, List<WzEffectFrame>> DeserializeEffectFramesByNode(Dictionary<string, object> obj)
        {
            if (obj == null || obj.Count == 0) return null;

            var result = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
            foreach (var kv in obj)
            {
                if (string.IsNullOrWhiteSpace(kv.Key) || !(kv.Value is List<object> arr))
                    continue;
                var frames = DeserializeEffectFrames(arr);
                if (frames == null || frames.Count == 0)
                    continue;
                result[NormalizeEffectNodeName(kv.Key)] = frames;
            }
            return result.Count > 0 ? result : null;
        }

        private static string NormalizeEffectNodeName(string name)
        {
            if (string.IsNullOrWhiteSpace(name))
                return "effect";
            return name.Trim();
        }

        private static int CompareEffectNodeName(string a, string b)
        {
            string left = NormalizeEffectNodeName(a);
            string right = NormalizeEffectNodeName(b);
            if (string.Equals(left, right, StringComparison.OrdinalIgnoreCase))
                return 0;
            if (string.Equals(left, "effect", StringComparison.OrdinalIgnoreCase))
                return -1;
            if (string.Equals(right, "effect", StringComparison.OrdinalIgnoreCase))
                return 1;

            bool leftIndexed = TryParseIndexedEffectName(left, out int leftIndex);
            bool rightIndexed = TryParseIndexedEffectName(right, out int rightIndex);
            if (leftIndexed && rightIndexed)
                return leftIndex.CompareTo(rightIndex);
            if (leftIndexed) return -1;
            if (rightIndexed) return 1;

            return string.Compare(left, right, StringComparison.OrdinalIgnoreCase);
        }

        private static bool TryParseIndexedEffectName(string name, out int index)
        {
            index = -1;
            if (string.IsNullOrWhiteSpace(name))
                return false;
            if (!name.StartsWith("effect", StringComparison.OrdinalIgnoreCase))
                return false;
            string suffix = name.Substring("effect".Length);
            if (string.IsNullOrEmpty(suffix))
                return false;
            return int.TryParse(suffix, out index);
        }

        /// <summary>
        /// Serialize a list of skills to JSON string.
        /// </summary>
        public static string SerializeList(List<SkillDefinition> skills)
        {
            var arr = new List<object>();
            foreach (var sd in skills) arr.Add(sd.ToJsonDict());
            var root = new Dictionary<string, object>();
            root["skills"] = arr;
            return SimpleJson.Serialize(root);
        }

        /// <summary>
        /// Parse a single skill definition from a JSON dictionary.
        /// </summary>
        public static SkillDefinition FromJson(Dictionary<string, object> obj)
        {
            var sd = new SkillDefinition();
            sd.SkillId = SimpleJson.GetInt(obj, "skillId");
            sd.Name = SimpleJson.GetString(obj, "name");
            sd.Desc = SimpleJson.GetString(obj, "desc");
            sd.Type = SimpleJson.GetString(obj, "type", "active_melee");
            sd.Tab = SimpleJson.GetString(obj, "tab", "active");
            sd.MaxLevel = SimpleJson.GetInt(obj, "maxLevel", 0); // 0 means "use template default"
            sd.SuperSpCost = SimpleJson.GetInt(obj, "superSpCost", 1);
            sd.Icon = SimpleJson.GetString(obj, "icon");
            sd.IconMouseOver = SimpleJson.GetString(obj, "iconMouseOver");
            sd.IconDisabled = SimpleJson.GetString(obj, "iconDisabled");
            sd.ReleaseType = SimpleJson.GetString(obj, "releaseType");
            sd.ReleaseClass = SimpleJson.GetString(obj, "releaseClass", "native_classifier_proxy");
            sd.BorrowDonorVisual = SimpleJson.GetBool(obj, "borrowDonorVisual", false);
            sd.ProxySkillId = SimpleJson.GetInt(obj, "proxySkillId");
            sd.VisualSkillId = SimpleJson.GetInt(obj, "visualSkillId");
            sd.CloneFromSkillId = SimpleJson.GetInt(obj, "cloneFromSkillId");
            sd.Action = SimpleJson.GetString(obj, "action");
            sd.InfoType = SimpleJson.GetInt(obj, "infoType", 0); // 0 means "use template default"
            sd.HideFromNativeSkillWnd = SimpleJson.GetBool(obj, "hideFromNativeSkillWnd", true);
            sd.ShowInNativeWhenLearned = SimpleJson.GetBool(obj, "showInNativeWhenLearned", false);
            sd.ShowInSuperWhenLearned = SimpleJson.GetBool(obj, "showInSuperWhenLearned", false);
            sd.AllowNativeUpgradeFallback = SimpleJson.GetBool(obj, "allowNativeUpgradeFallback", true);
            sd.InjectToNative = SimpleJson.GetBool(obj, "injectToNative", true);
            sd.InjectEnabled = SimpleJson.GetBool(obj, "injectEnabled", true);
            sd.DonorSkillId = SimpleJson.GetInt(obj, "donorSkillId");
            sd.MountItemId = SimpleJson.GetInt(obj, "mountItemId", 0);
            sd.MountResourceMode = SimpleJson.GetString(obj, "mountResourceMode", "config_only");
            if (string.IsNullOrWhiteSpace(sd.MountResourceMode))
                sd.MountResourceMode = "config_only";
            sd.MountSourceItemId = SimpleJson.GetInt(obj, "mountSourceItemId", 0);
            sd.MountTamingMobId = SimpleJson.GetInt(obj, "mountTamingMobId", 0);
            if (obj.ContainsKey("mountSpeed"))
                sd.MountSpeedOverride = SimpleJson.GetInt(obj, "mountSpeed", 0);
            if (obj.ContainsKey("mountJump"))
                sd.MountJumpOverride = SimpleJson.GetInt(obj, "mountJump", 0);
            if (obj.ContainsKey("mountFatigue"))
                sd.MountFatigueOverride = SimpleJson.GetInt(obj, "mountFatigue", 0);
            sd.SuperSpCarrierSkillId = SimpleJson.GetInt(obj, "superSpCarrierSkillId", 0);
            sd.ServerEnabled = SimpleJson.GetBool(
                obj, "serverEnabled",
                SimpleJson.GetBool(obj, "enabled", true));
            sd.IconBase64 = SimpleJson.GetString(obj, "iconBase64");
            sd.IconMouseOverBase64 = SimpleJson.GetString(obj, "iconMouseOverBase64");
            sd.IconDisabledBase64 = SimpleJson.GetString(obj, "iconDisabledBase64");

            // req (prerequisite skills)
            var reqObj = SimpleJson.GetObject(obj, "req");
            if (reqObj != null)
            {
                foreach (var kv in reqObj)
                {
                    int reqId;
                    if (int.TryParse(kv.Key, out reqId))
                    {
                        int reqLv = 1;
                        if (kv.Value is long l) reqLv = (int)l;
                        else if (kv.Value is double d) reqLv = (int)d;
                        else if (kv.Value is string sv && int.TryParse(sv, out int parsed)) reqLv = parsed;
                        sd.RequiredSkills[reqId] = reqLv;
                    }
                }
            }

            // common
            var commonObj = SimpleJson.GetObject(obj, "common");
            if (commonObj != null)
            {
                foreach (var kv in commonObj)
                {
                    if (kv.Value is string s)
                        sd.Common[kv.Key] = s;
                    else if (kv.Value != null)
                        sd.Common[kv.Key] = kv.Value.ToString();
                }
            }

            // hLevels
            var hObj = SimpleJson.GetObject(obj, "hLevels");
            if (hObj != null)
            {
                foreach (var kv in hObj)
                {
                    if (kv.Value is string s)
                        sd.HLevels[kv.Key] = s;
                }
            }

            // levels (per-level data)
            var levelsObj = SimpleJson.GetObject(obj, "levels");
            if (levelsObj != null)
            {
                sd.Levels = new Dictionary<int, Dictionary<string, string>>();
                foreach (var lvKv in levelsObj)
                {
                    int lv;
                    if (!int.TryParse(lvKv.Key, out lv)) continue;
                    var paramObj = lvKv.Value as Dictionary<string, object>;
                    if (paramObj == null) continue;
                    var pDict = new Dictionary<string, string>();
                    foreach (var p in paramObj)
                    {
                        if (p.Value is string s) pDict[p.Key] = s;
                        else if (p.Value != null) pDict[p.Key] = p.Value.ToString();
                    }
                    sd.Levels[lv] = pDict;
                }
            }

            // cachedEffects (persisted effect frames with bitmap base64)
            var effectsArr = SimpleJson.GetArray(obj, "cachedEffects");
            if (effectsArr != null)
                sd.CachedEffects = DeserializeEffectFrames(effectsArr);

            // cachedEffectsByNode (persisted multi-effect-node frames)
            var effectsByNodeObj = SimpleJson.GetObject(obj, "cachedEffectsByNode");
            if (effectsByNodeObj != null)
                sd.CachedEffectsByNode = DeserializeEffectFramesByNode(effectsByNodeObj);

            if ((sd.CachedEffectsByNode == null || sd.CachedEffectsByNode.Count == 0)
                && sd.CachedEffects != null && sd.CachedEffects.Count > 0)
            {
                sd.CachedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase)
                {
                    ["effect"] = sd.CachedEffects
                };
            }
            if ((sd.CachedEffects == null || sd.CachedEffects.Count == 0)
                && sd.CachedEffectsByNode != null && sd.CachedEffectsByNode.Count > 0)
            {
                if (!sd.CachedEffectsByNode.TryGetValue("effect", out var primaryFrames) || primaryFrames == null || primaryFrames.Count == 0)
                {
                    foreach (var kv in sd.CachedEffectsByNode)
                    {
                        if (kv.Value != null && kv.Value.Count > 0)
                        {
                            primaryFrames = kv.Value;
                            break;
                        }
                    }
                }
                sd.CachedEffects = primaryFrames;
            }

            // cachedTree (persisted node tree)
            var treeObj = SimpleJson.GetObject(obj, "cachedTree");
            if (treeObj != null)
                sd.CachedTree = DeserializeNodeTree(treeObj);

            sd.NormalizeTextFields();
            return sd;
        }

        /// <summary>
        /// Normalize user-facing strings so JSON/XML/.img write-out stays consistent.
        /// </summary>
        public void NormalizeTextFields()
        {
            Name = NormalizeSkillText(Name, keepSlashN: false);
            Desc = NormalizeSkillText(Desc, keepSlashN: true);
            if (HLevels != null && HLevels.Count > 0)
            {
                var keys = new List<string>(HLevels.Keys);
                foreach (string key in keys)
                    HLevels[key] = NormalizeSkillText(HLevels[key], keepSlashN: true);
            }
        }

        private static string NormalizeSkillText(string value, bool keepSlashN)
        {
            if (string.IsNullOrEmpty(value))
                return "";

            string normalized = value
                .Replace("\r\n", "\n")
                .Replace('\r', '\n');

            // In WZ String text, "\n" is the expected line break marker.
            if (keepSlashN && normalized.IndexOf('\n') >= 0)
                normalized = normalized.Replace("\n", "\\n");

            if (!keepSlashN || normalized.IndexOf('\\') < 0)
                return normalized.Trim();

            // Auto-heal malformed escapes like "\使用" -> "\n使用".
            var sb = new StringBuilder(normalized.Length + 8);
            for (int i = 0; i < normalized.Length; i++)
            {
                char ch = normalized[i];
                if (ch == '\\' && i + 1 < normalized.Length)
                {
                    char next = normalized[i + 1];
                    if (!IsValidEscapeLead(next) && IsCjk(next))
                    {
                        sb.Append('\\');
                        sb.Append('n');
                        continue;
                    }
                }
                sb.Append(ch);
            }

            return sb.ToString().Trim();
        }

        private static bool IsValidEscapeLead(char c)
        {
            switch (c)
            {
                case '\\':
                case '"':
                case '/':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                case 'u':
                    return true;
                default:
                    return false;
            }
        }

        private static bool IsCjk(char c)
        {
            return (c >= '\u3400' && c <= '\u9FFF')
                || (c >= '\uF900' && c <= '\uFAFF');
        }

        /// <summary>
        /// Applies template defaults for fields that were not set by the user.
        /// </summary>
        public void ApplyTemplate(SkillTemplate tpl)
        {
            bool donorClone = ResolveCloneSourceSkillId() > 0;

            if (!donorClone && InfoType == 0) InfoType = tpl.InfoType;
            if (MaxLevel == 0) MaxLevel = tpl.MaxLevel;
            if (!donorClone && string.IsNullOrEmpty(Action)) Action = tpl.Action;
            if (string.IsNullOrEmpty(ReleaseType)) ReleaseType = tpl.PacketRoute;
            if (ProxySkillId == 0) ProxySkillId = tpl.ProxySkillId;

            if (!donorClone)
            {
                // Merge common: template provides defaults, user overrides
                var merged = new Dictionary<string, string>(tpl.DefaultCommon);
                foreach (var kv in Common)
                    merged[kv.Key] = kv.Value;
                Common = merged;
            }

            // For donor clones, preserve the cloned source node instead of
            // backfilling template action/info/common/levels from melee defaults.
            if (!donorClone && Type == "newbie_level" && Levels == null)
            {
                Levels = tpl.BuildDefaultLevels();
            }
        }

        /// <summary>
        /// Loads all skill definitions from a config JSON file.
        /// </summary>
        public static List<SkillDefinition> LoadFromFile(string configPath)
        {
            string json = TextFileHelper.ReadAllTextAuto(configPath);
            var root = SimpleJson.ParseObject(json);
            var skillsArr = SimpleJson.GetArray(root, "skills");
            if (skillsArr == null)
                throw new Exception("Config JSON must have a \"skills\" array.");

            var result = new List<SkillDefinition>();
            if (skillsArr.Count == 0) return result;
            foreach (var item in skillsArr)
            {
                if (item is Dictionary<string, object> obj)
                {
                    var sd = FromJson(obj);
                    var tpl = SkillTemplate.Get(sd.Type);
                    sd.ApplyTemplate(tpl);
                    result.Add(sd);
                }
            }
            return result;
        }
    }
}
