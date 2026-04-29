using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Text;

namespace SuperSkillTool
{
    public class PassiveBonusDefinition
    {
        public int SourceSkillId;
        public List<int> TargetSkillIds = new List<int>();
        public string DamagePercent = "";
        public string IgnoreDefensePercent = "";
        public string AttackCount = "";
        public string MobCount = "";

        public bool HasAnyValue()
        {
            return !string.IsNullOrWhiteSpace(DamagePercent)
                || !string.IsNullOrWhiteSpace(IgnoreDefensePercent)
                || !string.IsNullOrWhiteSpace(AttackCount)
                || !string.IsNullOrWhiteSpace(MobCount);
        }

        public bool IsValid()
        {
            return TargetSkillIds != null && TargetSkillIds.Count > 0 && HasAnyValue();
        }
    }

    public class IndependentBuffDefinition
    {
        public bool Enabled = false;
        public int SourceSkillId = 0;
        public string CarrierBuffStat = "";
        public int IconSkillId = 0;
        public string CarrierValue = "1";
        public string ClientBuffDisplayMode = "";
        public string ClientNativeBuffStat = "";
        public string ClientNativeValueField = "";
        public int IndependentNativeDisplaySkillId = 0;
        public string ClientLocalBonusKey = "";
        public string ClientLocalValueField = "";
        public Dictionary<string, string> StatBonuses = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        public bool HasAnyBonus()
        {
            return StatBonuses != null && StatBonuses.Count > 0;
        }

        public bool IsConfigured()
        {
            return Enabled
                || SourceSkillId > 0
                || IconSkillId > 0
                || IndependentNativeDisplaySkillId > 0
                || !string.IsNullOrWhiteSpace(ClientBuffDisplayMode)
                || !string.IsNullOrWhiteSpace(ClientNativeBuffStat)
                || !string.IsNullOrWhiteSpace(ClientNativeValueField)
                || !string.IsNullOrWhiteSpace(ClientLocalBonusKey)
                || !string.IsNullOrWhiteSpace(ClientLocalValueField)
                || HasAnyBonus();
        }
    }

    public class IndependentBuffAutoProfile
    {
        public string PrimaryBonusKey = "";
        public string PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT";
        public string ClientBuffDisplayMode = "overlay";
        public string ClientNativeBuffStat = "";
        public string ClientNativeValueField = "";
        public string ClientLocalBonusKey = "";
        public string ClientLocalValueField = "";
    }

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
        public string PDesc = "";
        public string Ph = "";

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
        public int BehaviorSkillId = 0;        // server behaviorSkillId; 0 = generator resolves donor/proxy fallback
        public int VisibleJobId = 0;           // client-only visibility gate; 0 = no restriction
        public int CloneFromSkillId = 0;       // when set, create by cloning this source skill node first
        public bool PreserveClonedNode = true; // clone donor node as-is unless user explicitly edits structural fields
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
        public bool HideFromSuperSkillWnd = false;
        public bool ShowInNativeWhenLearned = false;
        public bool ShowInSuperWhenLearned = false;
        public bool AllowNativeUpgradeFallback = true;
        public bool InjectToNative = true;
        public bool InjectEnabled = true;      // for native_skill_injections enabled flag
        public int DonorSkillId = 0;
        public int MountItemId = 0;            // for super_skills_server mount override
        public int FlightMountItemId = 0;      // optional flightMountItemId for super_skills_server
        public bool AllowMountedFlight = false; // for super_skills_server allowMountedFlight
        public bool MountedDoubleJumpEnabled = false; // mounted double jump gate
        public int MountedDoubleJumpSkillId = 3101003; // default mounted double jump skill
        public bool MountedDemonJumpEnabled = false; // mounted demon jump gate
        public int MountedDemonJumpSkillId = 30010110; // default mounted demon jump skill
        public string MountResourceMode = "config_only"; // config_only | sync_action | sync_action_and_data
        public int MountSourceItemId = 0;      // donor mount item id for resource sync
        public int MountTamingMobId = 0;       // target tamingMob id when syncing mount data
        public int? MountSpeedOverride = null; // optional override for TamingMob info/speed
        public int? MountJumpOverride = null;  // optional override for TamingMob info/jump
        public int? MountFatigueOverride = null; // optional override for TamingMob info/fatigue
        public double? MountFsOverride = null; // optional override for TamingMob info/fs (float)
        public double? MountSwimOverride = null; // optional override for TamingMob info/swim (float)
        public int SuperSpCarrierSkillId = 0;  // per-skill override; 0 = use default
        public bool ServerEnabled = true;      // for super_skills_server enabled flag

        // ── Config-only super passive / independent buff data ───
        public List<PassiveBonusDefinition> PassiveBonuses = new List<PassiveBonusDefinition>();
        public IndependentBuffDefinition IndependentBuff = new IndependentBuffDefinition();

        // ── Prerequisite skills ────────────────────────────────
        public Dictionary<int, int> RequiredSkills = new Dictionary<int, int>(); // skillId -> requiredLevel

        // ── hLevel descriptions ────────────────────────────────
        public Dictionary<string, string> HLevels = new Dictionary<string, string>();

        // ── h template text (with #mpCon, #damage placeholders) ──
        public string H = "";

        // ── Per-level animation frames (ball/hit/effect/prepare/keydown per level) ──
        public Dictionary<int, Dictionary<string, List<WzEffectFrame>>> LevelAnimFramesByNode;

        // ── Cached data (serialized to JSON for persistence) ──────
        /// <summary>Effect frames from .img load.</summary>
        public List<WzEffectFrame> CachedEffects;
        public Dictionary<string, List<WzEffectFrame>> CachedEffectsByNode;
        /// <summary>
        /// True only when the queued effect cache should be written back to .img
        /// (i.e. user manually edited effect/hit/ball/repeat/levelAnim data).
        /// </summary>
        public bool HasManualEffectOverride = false;
        /// <summary>
        /// True only when the queued node tree should be written back to .img
        /// (i.e. user manually edited scalar/container nodes in the full tree editor).
        /// </summary>
        public bool HasManualTreeOverride = false;
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

        public bool HasLegacyMountMovementPlaceholderOverrides()
        {
            return MountSpeedOverride.HasValue && MountSpeedOverride.Value == 10000
                && MountJumpOverride.HasValue && MountJumpOverride.Value == 10000
                && MountFsOverride.HasValue && Math.Abs(MountFsOverride.Value - 10000.0) < 0.0001
                && MountSwimOverride.HasValue && Math.Abs(MountSwimOverride.Value - 100000.0) < 0.0001;
        }

        public bool ShouldWriteMountSpeedOverride()
        {
            return MountSpeedOverride.HasValue && !HasLegacyMountMovementPlaceholderOverrides();
        }

        public bool ShouldWriteMountJumpOverride()
        {
            return MountJumpOverride.HasValue && !HasLegacyMountMovementPlaceholderOverrides();
        }

        public bool ShouldWriteMountFsOverride()
        {
            return MountFsOverride.HasValue && !HasLegacyMountMovementPlaceholderOverrides();
        }

        public bool ShouldWriteMountSwimOverride()
        {
            return MountSwimOverride.HasValue && !HasLegacyMountMovementPlaceholderOverrides();
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
            if (BehaviorSkillId > 0) d["behaviorSkillId"] = (long)BehaviorSkillId;
            if (VisibleJobId > 0) d["visibleJobId"] = (long)VisibleJobId;
            if (CloneFromSkillId > 0) d["cloneFromSkillId"] = (long)CloneFromSkillId;
            if (CloneFromSkillId > 0) d["preserveClonedNode"] = PreserveClonedNode;
            if (!string.IsNullOrEmpty(Action)) d["action"] = Action;
            if (InfoType > 0) d["infoType"] = (long)InfoType;
            if (!string.IsNullOrEmpty(IconBase64)) d["iconBase64"] = IconBase64;
            if (!string.IsNullOrEmpty(IconMouseOverBase64)) d["iconMouseOverBase64"] = IconMouseOverBase64;
            if (!string.IsNullOrEmpty(IconDisabledBase64)) d["iconDisabledBase64"] = IconDisabledBase64;
            d["hideFromNativeSkillWnd"] = HideFromNativeSkillWnd;
            d["hideFromSuperSkillWnd"] = HideFromSuperSkillWnd;
            d["showInNativeWhenLearned"] = ShowInNativeWhenLearned;
            d["showInSuperWhenLearned"] = ShowInSuperWhenLearned;
            d["allowNativeUpgradeFallback"] = AllowNativeUpgradeFallback;
            d["injectToNative"] = InjectToNative;
            if (InjectToNative) d["injectEnabled"] = InjectEnabled;
            if (DonorSkillId > 0) d["donorSkillId"] = (long)DonorSkillId;
            if (MountItemId > 0) d["mountItemId"] = (long)MountItemId;
            if (FlightMountItemId > 0) d["flightMountItemId"] = (long)FlightMountItemId;
            d["allowMountedFlight"] = AllowMountedFlight;
            if (MountedDoubleJumpEnabled)
            {
                d["mountedDoubleJumpEnabled"] = true;
                d["mountedDoubleJumpSkillId"] = (long)(MountedDoubleJumpSkillId > 0 ? MountedDoubleJumpSkillId : 3101003);
            }
            if (MountedDemonJumpEnabled)
            {
                d["mountedDemonJumpEnabled"] = true;
                d["mountedDemonJumpSkillId"] = (long)(MountedDemonJumpSkillId > 0 ? MountedDemonJumpSkillId : 30010110);
            }
            if (!string.IsNullOrEmpty(MountResourceMode) && !string.Equals(MountResourceMode, "config_only", StringComparison.OrdinalIgnoreCase))
                d["mountResourceMode"] = MountResourceMode;
            if (MountSourceItemId > 0) d["mountSourceItemId"] = (long)MountSourceItemId;
            if (MountTamingMobId > 0) d["mountTamingMobId"] = (long)MountTamingMobId;
            if (ShouldWriteMountSpeedOverride()) d["mountSpeed"] = (long)MountSpeedOverride.Value;
            if (ShouldWriteMountJumpOverride()) d["mountJump"] = (long)MountJumpOverride.Value;
            if (MountFatigueOverride.HasValue) d["mountFatigue"] = (long)MountFatigueOverride.Value;
            if (ShouldWriteMountFsOverride()) d["mountFs"] = MountFsOverride.Value;
            if (ShouldWriteMountSwimOverride()) d["mountSwim"] = MountSwimOverride.Value;
            if (SuperSpCarrierSkillId > 0) d["superSpCarrierSkillId"] = (long)SuperSpCarrierSkillId;
            if (!ServerEnabled) d["serverEnabled"] = false;
            var passiveBonuses = SerializePassiveBonuses(PassiveBonuses);
            if (passiveBonuses != null && passiveBonuses.Count > 0)
                d["passiveBonuses"] = passiveBonuses;
            var independentBuff = SerializeIndependentBuff(BuildResolvedIndependentBuff());
            if (independentBuff != null && independentBuff.Count > 0)
                d["independentBuff"] = independentBuff;
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
            if (!string.IsNullOrEmpty(H))
                d["h"] = H;
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
            // Per-level animation frames
            if (LevelAnimFramesByNode != null && LevelAnimFramesByNode.Count > 0)
            {
                var levelAnimDict = new Dictionary<string, object>();
                foreach (var levelKv in LevelAnimFramesByNode)
                {
                    if (levelKv.Value == null || levelKv.Value.Count == 0) continue;
                    var serialized = SerializeEffectFramesByNode(levelKv.Value);
                    if (serialized != null && serialized.Count > 0)
                        levelAnimDict[levelKv.Key.ToString()] = serialized;
                }
                if (levelAnimDict.Count > 0)
                    d["levelAnimFramesByNode"] = levelAnimDict;
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
                if (HasManualEffectOverride)
                    d["hasManualEffectOverride"] = true;
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
            {
                if (HasManualTreeOverride)
                    d["hasManualTreeOverride"] = true;
                d["cachedTree"] = SerializeNodeTree(CachedTree);
            }
            return d;
        }

        public static object SerializePassiveValue(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return null;
            string text = value.Trim();
            if (long.TryParse(text, out long number))
                return number;
            return text;
        }

        public static string PassiveValueToString(Dictionary<string, object> obj, string key)
        {
            if (obj == null || string.IsNullOrEmpty(key) || !obj.TryGetValue(key, out object value) || value == null)
                return "";
            if (value is string s)
                return s;
            if (value is long l)
                return l.ToString();
            if (value is int i)
                return i.ToString();
            if (value is double d)
                return d.ToString(System.Globalization.CultureInfo.InvariantCulture);
            return value.ToString();
        }

        public static List<object> SerializePassiveBonuses(List<PassiveBonusDefinition> bonuses)
        {
            if (bonuses == null || bonuses.Count == 0)
                return null;

            var arr = new List<object>();
            foreach (var bonus in bonuses)
            {
                if (bonus == null || !bonus.IsValid())
                    continue;

                var entry = new Dictionary<string, object>();
                if (bonus.SourceSkillId > 0)
                    entry["sourceSkillId"] = (long)bonus.SourceSkillId;

                var targets = new List<object>();
                foreach (int targetSkillId in bonus.TargetSkillIds)
                {
                    if (targetSkillId > 0)
                        targets.Add((long)targetSkillId);
                }
                if (targets.Count == 0)
                    continue;
                entry["psdSkillIds"] = targets;

                PutPassiveValue(entry, "damagePercent", bonus.DamagePercent);
                PutPassiveValue(entry, "ignoreDefensePercent", bonus.IgnoreDefensePercent);
                PutPassiveValue(entry, "attackCount", bonus.AttackCount);
                PutPassiveValue(entry, "mobCount", bonus.MobCount);
                arr.Add(entry);
            }

            return arr.Count > 0 ? arr : null;
        }

        public static Dictionary<string, object> SerializeIndependentBuff(IndependentBuffDefinition buff)
        {
            if (buff == null || !buff.IsConfigured())
                return null;

            var entry = new Dictionary<string, object>();
            entry["enabled"] = buff.Enabled;
            if (buff.SourceSkillId > 0)
                entry["sourceSkillId"] = (long)buff.SourceSkillId;
            if (!string.IsNullOrWhiteSpace(buff.CarrierBuffStat))
                entry["carrierBuffStat"] = buff.CarrierBuffStat.Trim();
            if (buff.IconSkillId > 0)
                entry["iconSkillId"] = (long)buff.IconSkillId;
            PutPassiveValue(entry, "carrierValue", string.IsNullOrWhiteSpace(buff.CarrierValue) ? "1" : buff.CarrierValue);
            if (!string.IsNullOrWhiteSpace(buff.ClientBuffDisplayMode))
                entry["clientBuffDisplayMode"] = buff.ClientBuffDisplayMode.Trim();
            if (!string.IsNullOrWhiteSpace(buff.ClientNativeBuffStat))
                entry["clientNativeBuffStat"] = buff.ClientNativeBuffStat.Trim();
            if (!string.IsNullOrWhiteSpace(buff.ClientNativeValueField))
                entry["clientNativeValueField"] = buff.ClientNativeValueField.Trim();

            if (buff.StatBonuses != null && buff.StatBonuses.Count > 0)
            {
                var statBonuses = new Dictionary<string, object>();
                foreach (var kv in buff.StatBonuses)
                {
                    if (string.IsNullOrWhiteSpace(kv.Key) || string.IsNullOrWhiteSpace(kv.Value))
                        continue;
                    PutPassiveValue(statBonuses, kv.Key.Trim(), kv.Value);
                }
                if (statBonuses.Count > 0)
                    entry["statBonuses"] = statBonuses;
            }

            return entry.Count > 0 ? entry : null;
        }

        private static void PutPassiveValue(Dictionary<string, object> target, string key, string value)
        {
            object serialized = SerializePassiveValue(value);
            if (serialized != null)
                target[key] = serialized;
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

        private static List<PassiveBonusDefinition> DeserializePassiveBonuses(Dictionary<string, object> obj, int defaultSourceSkillId)
        {
            var result = new List<PassiveBonusDefinition>();
            var arr = SimpleJson.GetArray(obj, "passiveBonuses");
            if (arr != null)
            {
                foreach (object item in arr)
                {
                    if (item is Dictionary<string, object> bonusObj)
                    {
                        var bonus = DeserializePassiveBonus(bonusObj, defaultSourceSkillId);
                        if (bonus != null && bonus.IsValid())
                            result.Add(bonus);
                    }
                }
            }

            if (result.Count == 0)
            {
                var legacy = DeserializePassiveBonus(obj, defaultSourceSkillId);
                if (legacy != null && legacy.IsValid())
                    result.Add(legacy);
            }

            return result;
        }

        private static PassiveBonusDefinition DeserializePassiveBonus(Dictionary<string, object> obj, int defaultSourceSkillId)
        {
            if (obj == null)
                return null;

            var bonus = new PassiveBonusDefinition();
            bonus.SourceSkillId = SimpleJson.GetInt(obj, "sourceSkillId", defaultSourceSkillId);
            if (bonus.SourceSkillId <= 0)
                bonus.SourceSkillId = defaultSourceSkillId;

            AppendSkillIds(bonus.TargetSkillIds, obj, "psdSkillIds");
            AppendSkillIds(bonus.TargetSkillIds, obj, "targetSkillIds");
            AppendSkillIds(bonus.TargetSkillIds, obj, "skillIds");
            AppendSkillId(bonus.TargetSkillIds, SimpleJson.GetInt(obj, "psdSkillId", 0));
            AppendSkillId(bonus.TargetSkillIds, SimpleJson.GetInt(obj, "psdSkill", 0));
            AppendSkillId(bonus.TargetSkillIds, SimpleJson.GetInt(obj, "targetSkillId", 0));

            bonus.DamagePercent = FirstPassiveValue(obj, "damagePercent", "damageIncrease");
            bonus.IgnoreDefensePercent = FirstPassiveValue(obj, "ignoreDefensePercent", "ignoreDefenseIncrease");
            bonus.AttackCount = FirstPassiveValue(obj, "attackCount", "hitCount");
            bonus.MobCount = FirstPassiveValue(obj, "mobCount", "targetCount");
            return bonus;
        }

        private static IndependentBuffDefinition DeserializeIndependentBuff(Dictionary<string, object> obj)
        {
            var buffObj = SimpleJson.GetObject(obj, "independentBuff");
            var buff = new IndependentBuffDefinition();
            if (buffObj == null)
                return buff;

            buff.Enabled = SimpleJson.GetBool(buffObj, "enabled", true);
            buff.SourceSkillId = SimpleJson.GetInt(buffObj, "sourceSkillId", 0);
            buff.CarrierBuffStat = SimpleJson.GetString(buffObj, "carrierBuffStat", "");
            buff.IconSkillId = SimpleJson.GetInt(buffObj, "iconSkillId", 0);
            buff.CarrierValue = FirstPassiveValue(buffObj, "carrierValue", "value");
            if (string.IsNullOrWhiteSpace(buff.CarrierValue))
                buff.CarrierValue = "1";
            buff.ClientBuffDisplayMode = SimpleJson.GetString(
                buffObj,
                "clientBuffDisplayMode",
                SimpleJson.GetString(buffObj, "independentDisplayMode", ""));
            buff.ClientNativeBuffStat = SimpleJson.GetString(
                buffObj,
                "clientNativeBuffStat",
                SimpleJson.GetString(buffObj, "nativeBuffStat", ""));
            buff.ClientNativeValueField = SimpleJson.GetString(
                buffObj,
                "clientNativeValueField",
                SimpleJson.GetString(buffObj, "independentNativeValueField", ""));
            buff.IndependentNativeDisplaySkillId = SimpleJson.GetInt(
                buffObj,
                "independentNativeDisplaySkillId",
                SimpleJson.GetInt(buffObj, "iconSkillId", 0));
            buff.ClientLocalBonusKey = SimpleJson.GetString(buffObj, "clientLocalBonusKey", "");
            buff.ClientLocalValueField = SimpleJson.GetString(buffObj, "clientLocalValueField", "");

            var statObj = SimpleJson.GetObject(buffObj, "statBonuses") ?? buffObj;
            foreach (string key in IndependentBuffBonusKeys)
            {
                string value = PassiveValueToString(statObj, key);
                if (!string.IsNullOrWhiteSpace(value))
                    buff.StatBonuses[key] = value;
            }
            return buff;
        }

        public static readonly string[] IndependentBuffBonusKeys = new string[]
        {
            "watk", "matk", "criticalRate", "wdef", "mdef", "acc", "avoid", "speed", "jump",
            "str", "dex", "int", "luk",
            "strPercent", "dexPercent", "intPercent", "lukPercent",
            "maxHp", "maxMp", "maxHpPercent", "maxMpPercent",
            "damagePercent", "bossDamagePercent", "ignoreDefensePercent",
            "criticalMinDamage", "criticalMaxDamage",
            "asr", "ter", "attackSpeedStage"
        };

        public static readonly string[] PassiveValueFieldHints = new string[]
        {
            "damRate", "damagePercent", "damage", "bossDamagePercent", "bdr",
            "ignoreMob", "ignoreDefensePercent", "attackCount", "bulletCount", "mobCount",
            "str", "dex", "int", "luk", "watk", "pad", "matk", "mad", "wdef", "pdd", "mdef", "mdd",
            "acc", "avoid", "eva", "speed", "jump", "criticalRate", "criticalMinDamage", "criticalMaxDamage",
            "asr", "ter", "hp", "mp", "maxHp", "maxMp", "maxHpPercent", "maxMpPercent", "x", "y", "z", "u", "w"
        };

        private static readonly string[] IndependentBuffAutoPriorityKeys = new string[]
        {
            "wdef", "mdef", "watk", "matk",
            "acc", "avoid", "speed", "jump",
            "maxHp", "maxMp",
            "damagePercent", "bossDamagePercent", "ignoreDefensePercent",
            "str", "dex", "int", "luk", "allStat",
            "strPercent", "dexPercent", "intPercent", "lukPercent", "allStatPercent",
            "criticalRate", "criticalMinDamage", "criticalMaxDamage",
            "asr", "ter", "attackSpeedStage"
        };

        public static IndependentBuffDefinition CloneIndependentBuff(IndependentBuffDefinition source)
        {
            if (source == null)
                return new IndependentBuffDefinition();

            return new IndependentBuffDefinition
            {
                Enabled = source.Enabled,
                SourceSkillId = source.SourceSkillId,
                CarrierBuffStat = source.CarrierBuffStat ?? "",
                IconSkillId = source.IconSkillId,
                CarrierValue = source.CarrierValue ?? "",
                ClientBuffDisplayMode = source.ClientBuffDisplayMode ?? "",
                ClientNativeBuffStat = source.ClientNativeBuffStat ?? "",
                ClientNativeValueField = source.ClientNativeValueField ?? "",
                IndependentNativeDisplaySkillId = source.IndependentNativeDisplaySkillId,
                ClientLocalBonusKey = source.ClientLocalBonusKey ?? "",
                ClientLocalValueField = source.ClientLocalValueField ?? "",
                StatBonuses = source.StatBonuses != null
                    ? new Dictionary<string, string>(source.StatBonuses, StringComparer.OrdinalIgnoreCase)
                    : new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            };
        }

        public static string GetPrimaryIndependentBuffBonusKey(Dictionary<string, string> statBonuses)
        {
            if (statBonuses == null || statBonuses.Count == 0)
                return "";

            foreach (string key in IndependentBuffAutoPriorityKeys)
            {
                if (statBonuses.TryGetValue(key, out string value) && !string.IsNullOrWhiteSpace(value))
                    return key;
            }

            foreach (KeyValuePair<string, string> kv in statBonuses)
            {
                if (!string.IsNullOrWhiteSpace(kv.Key) && !string.IsNullOrWhiteSpace(kv.Value))
                    return kv.Key.Trim();
            }

            return "";
        }

        public static IndependentBuffAutoProfile BuildIndependentBuffAutoProfile(IndependentBuffDefinition buff)
        {
            var profile = new IndependentBuffAutoProfile();
            profile.PrimaryBonusKey = GetPrimaryIndependentBuffBonusKey(buff?.StatBonuses);

            switch ((profile.PrimaryBonusKey ?? "").Trim())
            {
                case "wdef":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientNativeBuffStat = "";
                    profile.ClientNativeValueField = "";
                    profile.ClientLocalBonusKey = "wdef";
                    profile.ClientLocalValueField = "pdd";
                    break;
                case "mdef":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT2";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientNativeBuffStat = "";
                    profile.ClientNativeValueField = "";
                    profile.ClientLocalBonusKey = "mdef";
                    profile.ClientLocalValueField = "mdd";
                    break;
                case "watk":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientNativeBuffStat = "";
                    profile.ClientNativeValueField = "";
                    profile.ClientLocalBonusKey = "watk";
                    profile.ClientLocalValueField = "pad";
                    break;
                case "matk":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT2";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientNativeBuffStat = "";
                    profile.ClientNativeValueField = "";
                    profile.ClientLocalBonusKey = "matk";
                    profile.ClientLocalValueField = "mad";
                    break;
                case "acc":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientNativeBuffStat = "";
                    profile.ClientNativeValueField = "";
                    profile.ClientLocalBonusKey = "acc";
                    profile.ClientLocalValueField = "acc";
                    break;
                case "avoid":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT2";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientNativeBuffStat = "";
                    profile.ClientNativeValueField = "";
                    profile.ClientLocalBonusKey = "avoid";
                    profile.ClientLocalValueField = "eva";
                    break;
                case "speed":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientNativeBuffStat = "";
                    profile.ClientNativeValueField = "";
                    profile.ClientLocalBonusKey = "speed";
                    profile.ClientLocalValueField = "speed";
                    break;
                case "jump":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT2";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientNativeBuffStat = "";
                    profile.ClientNativeValueField = "";
                    profile.ClientLocalBonusKey = "jump";
                    profile.ClientLocalValueField = "jump";
                    break;
                case "maxHp":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientLocalBonusKey = "maxHp";
                    profile.ClientLocalValueField = "maxHp";
                    break;
                case "maxMp":
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT2";
                    profile.ClientBuffDisplayMode = "none";
                    profile.ClientLocalBonusKey = "maxMp";
                    profile.ClientLocalValueField = "maxMp";
                    break;
                default:
                    profile.PreferredCarrierBuffStat = "DEFAULT_BUFFSTAT";
                    profile.ClientBuffDisplayMode = "none";
                    break;
            }

            return profile;
        }

        public IndependentBuffDefinition BuildResolvedIndependentBuff()
        {
            if (IndependentBuff == null || !IndependentBuff.IsConfigured())
                return CloneIndependentBuff(IndependentBuff);

            IndependentBuffDefinition resolved = CloneIndependentBuff(IndependentBuff);
            IndependentBuffAutoProfile profile = BuildIndependentBuffAutoProfile(resolved);

            if (string.IsNullOrWhiteSpace(resolved.CarrierBuffStat))
                resolved.CarrierBuffStat = string.IsNullOrWhiteSpace(profile.PreferredCarrierBuffStat) ? "DEFAULT_BUFFSTAT" : profile.PreferredCarrierBuffStat.Trim();
            if (resolved.SourceSkillId <= 0 && SkillId > 0)
                resolved.SourceSkillId = SkillId;
            if (resolved.IconSkillId <= 0 && SkillId > 0)
                resolved.IconSkillId = SkillId;
            if (string.IsNullOrWhiteSpace(resolved.CarrierValue))
                resolved.CarrierValue = "1";
            if (string.IsNullOrWhiteSpace(resolved.ClientBuffDisplayMode))
                resolved.ClientBuffDisplayMode = profile.ClientBuffDisplayMode ?? "";
            if (string.IsNullOrWhiteSpace(resolved.ClientNativeBuffStat))
                resolved.ClientNativeBuffStat = profile.ClientNativeBuffStat ?? "";
            if (string.IsNullOrWhiteSpace(resolved.ClientNativeValueField))
                resolved.ClientNativeValueField = profile.ClientNativeValueField ?? "";
            if (string.IsNullOrWhiteSpace(resolved.ClientLocalBonusKey))
                resolved.ClientLocalBonusKey = profile.ClientLocalBonusKey ?? "";
            if (string.IsNullOrWhiteSpace(resolved.ClientLocalValueField))
                resolved.ClientLocalValueField = profile.ClientLocalValueField ?? "";

            return resolved;
        }

        private static string FirstPassiveValue(Dictionary<string, object> obj, string primaryKey, string alternateKey)
        {
            string value = PassiveValueToString(obj, primaryKey);
            if (string.IsNullOrWhiteSpace(value) && !string.IsNullOrWhiteSpace(alternateKey))
                value = PassiveValueToString(obj, alternateKey);
            return value;
        }

        private static void AppendSkillIds(List<int> target, Dictionary<string, object> obj, string key)
        {
            var arr = SimpleJson.GetArray(obj, key);
            if (arr == null)
                return;
            foreach (object item in arr)
            {
                int skillId = 0;
                if (item is long l)
                    skillId = (int)l;
                else if (item is int i)
                    skillId = i;
                else if (item is double d)
                    skillId = (int)d;
                else if (item is string s)
                    int.TryParse(s, out skillId);
                AppendSkillId(target, skillId);
            }
        }

        private static void AppendSkillId(List<int> target, int skillId)
        {
            if (target == null || skillId <= 0 || target.Contains(skillId))
                return;
            target.Add(skillId);
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

            int leftRank = GetEffectNodeSortRank(left, out int leftIndex);
            int rightRank = GetEffectNodeSortRank(right, out int rightIndex);
            if (leftRank != rightRank)
                return leftRank.CompareTo(rightRank);
            if (leftIndex != int.MaxValue || rightIndex != int.MaxValue)
                return leftIndex.CompareTo(rightIndex);

            return string.Compare(left, right, StringComparison.OrdinalIgnoreCase);
        }

        private static readonly (string baseName, int baseRank, bool canIndex)[] EffectNodeSortTable = new[]
        {
            ("effect",     0, true),
            ("repeat",     2, true),
            ("ball",       4, true),
            ("hit",        6, false),
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

        private static int GetEffectNodeSortRank(string name, out int index)
        {
            index = int.MaxValue;
            if (string.IsNullOrWhiteSpace(name))
                return 99;

            if (name.StartsWith("hit/", StringComparison.OrdinalIgnoreCase)
                && int.TryParse(name.Substring(4), out index))
                return 6;
            if (string.Equals(name, "hit", StringComparison.OrdinalIgnoreCase))
            {
                index = -1;
                return 6;
            }

            foreach (var (baseName, baseRank, canIndex) in EffectNodeSortTable)
            {
                if (baseName == "hit") continue;
                if (string.Equals(name, baseName, StringComparison.OrdinalIgnoreCase))
                    return baseRank;
                if (canIndex && TryParseIndexedEffectName(name, baseName, out index))
                    return baseRank + 1;
            }
            return 99;
        }

        private static bool TryParseIndexedEffectName(string name, string prefix, out int index)
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
            sd.PDesc = SimpleJson.GetString(obj, "pdesc");
            sd.Ph = SimpleJson.GetString(obj, "ph");
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
            sd.BehaviorSkillId = SimpleJson.GetInt(obj, "behaviorSkillId");
            sd.VisibleJobId = SimpleJson.GetInt(obj, "visibleJobId", 0);
            sd.CloneFromSkillId = SimpleJson.GetInt(obj, "cloneFromSkillId");
            if (obj.ContainsKey("preserveClonedNode"))
            {
                sd.PreserveClonedNode = SimpleJson.GetBool(obj, "preserveClonedNode", true);
            }
            else
            {
                // Backward compatibility: old pending_skills.json may miss this field.
                // For donor-clone entries, default to raw clone-preserve to avoid effect re-encode.
                sd.PreserveClonedNode = sd.CloneFromSkillId > 0 && sd.CloneFromSkillId != sd.SkillId;
            }
            sd.Action = SimpleJson.GetString(obj, "action");
            sd.InfoType = SimpleJson.GetInt(obj, "infoType", 0); // 0 means "use template default"
            sd.HideFromNativeSkillWnd = SimpleJson.GetBool(obj, "hideFromNativeSkillWnd", true);
            sd.HideFromSuperSkillWnd = SimpleJson.GetBool(obj, "hideFromSuperSkillWnd", false);
            sd.ShowInNativeWhenLearned = SimpleJson.GetBool(obj, "showInNativeWhenLearned", false);
            sd.ShowInSuperWhenLearned = SimpleJson.GetBool(obj, "showInSuperWhenLearned", false);
            sd.AllowNativeUpgradeFallback = SimpleJson.GetBool(obj, "allowNativeUpgradeFallback", true);
            sd.InjectToNative = SimpleJson.GetBool(obj, "injectToNative", true);
            sd.InjectEnabled = SimpleJson.GetBool(obj, "injectEnabled", true);
            sd.DonorSkillId = SimpleJson.GetInt(obj, "donorSkillId");
            sd.MountItemId = SimpleJson.GetInt(obj, "mountItemId", 0);
            sd.FlightMountItemId = SimpleJson.GetInt(obj, "flightMountItemId", 0);
            sd.AllowMountedFlight = SimpleJson.GetBool(
                obj,
                "allowMountedFlight",
                SimpleJson.GetBool(obj, "grantSoaringOnRide", false));
            sd.MountedDoubleJumpEnabled = SimpleJson.GetBool(obj, "mountedDoubleJumpEnabled", false);
            sd.MountedDoubleJumpSkillId = SimpleJson.GetInt(obj, "mountedDoubleJumpSkillId", 3101003);
            if (sd.MountedDoubleJumpSkillId <= 0)
                sd.MountedDoubleJumpSkillId = 3101003;
            sd.MountedDemonJumpEnabled = SimpleJson.GetBool(obj, "mountedDemonJumpEnabled", false);
            sd.MountedDemonJumpSkillId = SimpleJson.GetInt(obj, "mountedDemonJumpSkillId", 30010110);
            if (sd.MountedDemonJumpSkillId <= 0)
                sd.MountedDemonJumpSkillId = 30010110;
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
            if (obj.ContainsKey("mountFs"))
                sd.MountFsOverride = SimpleJson.GetDouble(obj, "mountFs", 0d);
            if (obj.ContainsKey("mountSwim"))
                sd.MountSwimOverride = SimpleJson.GetDouble(obj, "mountSwim", 0d);
            sd.SuperSpCarrierSkillId = SimpleJson.GetInt(obj, "superSpCarrierSkillId", 0);
            sd.ServerEnabled = SimpleJson.GetBool(
                obj, "serverEnabled",
                SimpleJson.GetBool(obj, "enabled", true));
            sd.PassiveBonuses = DeserializePassiveBonuses(obj, sd.SkillId);
            sd.IndependentBuff = DeserializeIndependentBuff(obj);
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

            // h template text
            sd.H = SimpleJson.GetString(obj, "h");

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

            // levelAnimFramesByNode (per-level animation frames)
            var levelAnimObj = SimpleJson.GetObject(obj, "levelAnimFramesByNode");
            if (levelAnimObj != null)
            {
                sd.LevelAnimFramesByNode = new Dictionary<int, Dictionary<string, List<WzEffectFrame>>>();
                foreach (var lvKv in levelAnimObj)
                {
                    if (!int.TryParse(lvKv.Key, out int lvNum)) continue;
                    if (!(lvKv.Value is Dictionary<string, object> nodeMapObj)) continue;
                    var nodeMap = DeserializeEffectFramesByNode(nodeMapObj);
                    if (nodeMap != null && nodeMap.Count > 0)
                        sd.LevelAnimFramesByNode[lvNum] = nodeMap;
                }
                if (sd.LevelAnimFramesByNode.Count == 0)
                    sd.LevelAnimFramesByNode = null;
            }

            // cachedEffects (persisted effect frames with bitmap base64)
            var effectsArr = SimpleJson.GetArray(obj, "cachedEffects");
            if (effectsArr != null)
                sd.CachedEffects = DeserializeEffectFrames(effectsArr);

            // cachedEffectsByNode (persisted multi-effect-node frames)
            var effectsByNodeObj = SimpleJson.GetObject(obj, "cachedEffectsByNode");
            if (effectsByNodeObj != null)
                sd.CachedEffectsByNode = DeserializeEffectFramesByNode(effectsByNodeObj);
            sd.HasManualEffectOverride = SimpleJson.GetBool(obj, "hasManualEffectOverride", false);

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
            sd.HasManualTreeOverride = SimpleJson.GetBool(obj, "hasManualTreeOverride", false);

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
            PDesc = NormalizeSkillText(PDesc, keepSlashN: true);
            Ph = NormalizeSkillText(Ph, keepSlashN: true);
            H = NormalizeSkillText(H, keepSlashN: true);
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
