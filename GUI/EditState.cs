using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;

namespace SuperSkillTool
{
    /// <summary>
    /// Manages the temporary editing state for the skill editor form.
    /// Tracks loaded .img data, user overrides (icons, effects), and
    /// whether we're editing an existing list item or creating a new one.
    /// </summary>
    public class EditState
    {
        // Loaded .img data
        public WzSkillData LoadedData;

        // User icon overrides (from drag-drop or file picker)
        public Bitmap IconOverride;
        public Bitmap IconMOOverride;
        public Bitmap IconDisOverride;

        // Edited data
        // Backward-compatible alias to the current selected effect node list.
        public List<WzEffectFrame> EditedEffects;
        public Dictionary<string, List<WzEffectFrame>> EditedEffectsByNode;
        public string SelectedEffectNodeName = "effect";
        public WzNodeInfo EditedTree;
        public Dictionary<int, Dictionary<string, string>> EditedLevelParams;

        // Per-level animation frames editing
        public Dictionary<int, Dictionary<string, List<WzEffectFrame>>> EditedLevelAnimFramesByNode;

        // Current animation editing context: null = top-level (shared), int = specific level
        public int? SelectedAnimLevel;

        // Text fields
        public string EditedH = "";
        public string EditedPDesc = "";
        public string EditedPh = "";

        // List editing state
        /// <summary>
        /// If non-null, we are editing an existing item in the pending list
        /// at this index. null = new skill mode.
        /// </summary>
        public int? EditingListIndex;

        /// <summary>
        /// Clear all editing state for a fresh start.
        /// </summary>
        public void Clear()
        {
            LoadedData?.Dispose();
            LoadedData = null;
            IconOverride?.Dispose();
            IconOverride = null;
            IconMOOverride?.Dispose();
            IconMOOverride = null;
            IconDisOverride?.Dispose();
            IconDisOverride = null;
            EditedEffects = null;
            EditedEffectsByNode = null;
            SelectedEffectNodeName = "effect";
            EditedTree = null;
            EditedLevelParams = null;
            EditedLevelAnimFramesByNode = null;
            SelectedAnimLevel = null;
            EditedH = "";
            EditedPDesc = "";
            EditedPh = "";
            EditingListIndex = null;
        }

        /// <summary>
        /// Load editing state from WzSkillData (after .img load).
        /// Copies level params and effects to editable collections.
        /// </summary>
        public void LoadFromSkillData(WzSkillData data)
        {
            Clear();
            LoadedData = data;

            // Deep-copy level params for editing
            if (data.LevelParams != null)
            {
                EditedLevelParams = new Dictionary<int, Dictionary<string, string>>();
                foreach (var kv in data.LevelParams)
                {
                    var copy = new Dictionary<string, string>();
                    foreach (var p in kv.Value) copy[p.Key] = p.Value;
                    EditedLevelParams[kv.Key] = copy;
                }
            }

            // Copy top-level effect/animation frames by node (bitmap is cloned)
            EditedEffectsByNode = CloneEffectsByNode(data.EffectFramesByNode);
            if ((EditedEffectsByNode == null || EditedEffectsByNode.Count == 0) && data.EffectFrames != null)
            {
                EditedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase)
                {
                    ["effect"] = CloneEffectFrameList(data.EffectFrames) ?? new List<WzEffectFrame>()
                };
            }
            if (EditedEffectsByNode == null)
                EditedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);

            // Copy per-level animation frames
            if (data.LevelAnimFramesByNode != null && data.LevelAnimFramesByNode.Count > 0)
            {
                EditedLevelAnimFramesByNode = CloneLevelAnimFramesByNode(data.LevelAnimFramesByNode);
            }

            // Text fields
            EditedH = data.H ?? "";
            EditedPDesc = data.PDesc ?? "";
            EditedPh = data.Ph ?? "";

            string preferredNode = EditedEffectsByNode.ContainsKey("effect")
                ? "effect"
                : GetFirstEffectNodeName(EditedEffectsByNode);
            SelectedAnimLevel = null;
            SetSelectedEffectNode(preferredNode, createIfMissing: true);

            // Copy node tree reference (editing modifies in-place)
            EditedTree = data.RootNode;
        }

        /// <summary>
        /// Load editing state from an existing SkillDefinition (for re-editing from list).
        /// </summary>
        public void LoadFromSkillDefinition(SkillDefinition sd, int listIndex)
        {
            Clear();
            EditingListIndex = listIndex;

            // Restore icon overrides from base64 if available
            if (!string.IsNullOrEmpty(sd.IconBase64))
                IconOverride = BitmapFromBase64(sd.IconBase64);
            if (!string.IsNullOrEmpty(sd.IconMouseOverBase64))
                IconMOOverride = BitmapFromBase64(sd.IconMouseOverBase64);
            if (!string.IsNullOrEmpty(sd.IconDisabledBase64))
                IconDisOverride = BitmapFromBase64(sd.IconDisabledBase64);

            // Restore EditedLevelParams from sd.Common and sd.Levels
            EditedLevelParams = new Dictionary<int, Dictionary<string, string>>();
            if (sd.Common != null && sd.Common.Count > 0)
                EditedLevelParams[0] = new Dictionary<string, string>(sd.Common);
            if (sd.Levels != null)
            {
                foreach (var lv in sd.Levels)
                    EditedLevelParams[lv.Key] = new Dictionary<string, string>(lv.Value);
            }

            // Restore a stub LoadedData so BuildSkillFromForm can access all .img-sourced fields
            LoadedData = new WzSkillData
            {
                SkillId = sd.SkillId,
                JobId = sd.JobId,
                Name = sd.Name ?? "",
                Desc = sd.Desc ?? "",
                PDesc = sd.PDesc ?? "",
                Ph = sd.Ph ?? "",
                H = sd.H ?? "",
                Action = sd.Action ?? "",
                InfoType = sd.InfoType,
                CommonParams = sd.Common != null ? new Dictionary<string, string>(sd.Common) : null
            };
            if (sd.Levels != null && sd.Levels.Count > 0)
            {
                foreach (var lv in sd.Levels)
                    LoadedData.LevelParams[lv.Key] = new Dictionary<string, string>(lv.Value);
            }
            if (sd.HLevels != null && sd.HLevels.Count > 0)
            {
                foreach (var kv in sd.HLevels)
                    LoadedData.HLevels[kv.Key] = kv.Value;
            }

            // Restore text editing fields
            EditedH = sd.H ?? "";
            EditedPDesc = sd.PDesc ?? "";
            EditedPh = sd.Ph ?? "";

            // Restore cached effects and tree from SkillDefinition (session-only data)
            EditedEffectsByNode = CloneEffectsByNode(sd.CachedEffectsByNode);
            if ((EditedEffectsByNode == null || EditedEffectsByNode.Count == 0)
                && sd.CachedEffects != null && sd.CachedEffects.Count > 0)
            {
                EditedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase)
                {
                    ["effect"] = CloneEffectFrameList(sd.CachedEffects) ?? new List<WzEffectFrame>()
                };
            }
            if (EditedEffectsByNode == null)
                EditedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);

            // Restore per-level animation frames
            if (sd.LevelAnimFramesByNode != null && sd.LevelAnimFramesByNode.Count > 0)
                EditedLevelAnimFramesByNode = CloneLevelAnimFramesByNode(sd.LevelAnimFramesByNode);

            string preferredNode = EditedEffectsByNode.ContainsKey("effect")
                ? "effect"
                : GetFirstEffectNodeName(EditedEffectsByNode);
            SelectedAnimLevel = null;
            SetSelectedEffectNode(preferredNode, createIfMissing: true);

            if (sd.CachedTree != null)
                EditedTree = sd.CachedTree;
        }

        public static string NormalizeEffectNodeName(string nodeName)
        {
            if (string.IsNullOrWhiteSpace(nodeName))
                return "effect";
            return nodeName.Trim();
        }

        public void SetSelectedEffectNode(string nodeName, bool createIfMissing)
        {
            if (EditedEffectsByNode == null)
                EditedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);

            string normalized = NormalizeEffectNodeName(nodeName);
            if (createIfMissing && !EditedEffectsByNode.ContainsKey(normalized))
                EditedEffectsByNode[normalized] = new List<WzEffectFrame>();

            if (!EditedEffectsByNode.ContainsKey(normalized))
            {
                normalized = GetFirstEffectNodeName(EditedEffectsByNode) ?? "effect";
                if (createIfMissing && !EditedEffectsByNode.ContainsKey(normalized))
                    EditedEffectsByNode[normalized] = new List<WzEffectFrame>();
            }

            SelectedEffectNodeName = normalized;
            EditedEffects = EditedEffectsByNode.TryGetValue(normalized, out var list) ? list : null;
        }

        public List<WzEffectFrame> GetEffectFrames(string nodeName, bool createIfMissing)
        {
            var activeDict = GetActiveAnimDict();
            if (activeDict == null)
            {
                if (createIfMissing)
                {
                    // Ensure the dict exists for the current level
                    if (SelectedAnimLevel != null)
                    {
                        if (EditedLevelAnimFramesByNode == null)
                            EditedLevelAnimFramesByNode = new Dictionary<int, Dictionary<string, List<WzEffectFrame>>>();
                        activeDict = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
                        EditedLevelAnimFramesByNode[SelectedAnimLevel.Value] = activeDict;
                    }
                    else
                    {
                        if (EditedEffectsByNode == null)
                            EditedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
                        activeDict = EditedEffectsByNode;
                    }
                }
                else
                {
                    return null;
                }
            }

            string normalized = NormalizeEffectNodeName(nodeName);
            if (!activeDict.TryGetValue(normalized, out var list) && createIfMissing)
            {
                list = new List<WzEffectFrame>();
                activeDict[normalized] = list;
            }

            if (string.Equals(SelectedEffectNodeName, normalized, StringComparison.OrdinalIgnoreCase))
                EditedEffects = list;

            return list;
        }

        public List<WzEffectFrame> GetSelectedEffectFrames(bool createIfMissing)
        {
            return GetEffectFrames(SelectedEffectNodeName, createIfMissing);
        }

        public List<string> GetEffectNodeNames()
        {
            var activeDict = GetActiveAnimDict();
            var result = new List<string>();
            if (activeDict == null || activeDict.Count == 0)
                return result;

            foreach (var kv in activeDict)
            {
                if (string.IsNullOrWhiteSpace(kv.Key))
                    continue;
                result.Add(kv.Key);
            }
            return result;
        }

        private static string GetFirstEffectNodeName(Dictionary<string, List<WzEffectFrame>> map)
        {
            if (map == null || map.Count == 0)
                return null;
            foreach (var kv in map)
            {
                if (!string.IsNullOrWhiteSpace(kv.Key))
                    return kv.Key;
            }
            return null;
        }

        public static List<WzEffectFrame> CloneEffectFrameList(List<WzEffectFrame> source)
        {
            if (source == null) return null;
            var result = new List<WzEffectFrame>();
            foreach (var frame in source)
            {
                var cloned = WzEffectFrame.CloneShallowBitmap(frame);
                if (cloned != null)
                    result.Add(cloned);
            }
            return result;
        }

        public static Dictionary<string, List<WzEffectFrame>> CloneEffectsByNode(
            Dictionary<string, List<WzEffectFrame>> source)
        {
            if (source == null || source.Count == 0)
                return null;

            var result = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
            foreach (var kv in source)
            {
                if (string.IsNullOrWhiteSpace(kv.Key))
                    continue;
                var list = CloneEffectFrameList(kv.Value);
                if (list == null || list.Count == 0)
                    continue;
                result[NormalizeEffectNodeName(kv.Key)] = list;
            }
            return result.Count > 0 ? result : null;
        }

        public static Dictionary<int, Dictionary<string, List<WzEffectFrame>>> CloneLevelAnimFramesByNode(
            Dictionary<int, Dictionary<string, List<WzEffectFrame>>> source)
        {
            if (source == null || source.Count == 0)
                return null;

            var result = new Dictionary<int, Dictionary<string, List<WzEffectFrame>>>();
            foreach (var levelKv in source)
            {
                if (levelKv.Value == null || levelKv.Value.Count == 0) continue;
                var clonedNodes = CloneEffectsByNode(levelKv.Value);
                if (clonedNodes != null && clonedNodes.Count > 0)
                    result[levelKv.Key] = clonedNodes;
            }
            return result.Count > 0 ? result : null;
        }

        /// <summary>
        /// Switch the animation editing context to a specific level (or top-level if null).
        /// Updates EditedEffectsByNode to point at the correct dict, then refreshes the selected node.
        /// </summary>
        public void SetSelectedAnimLevel(int? level, string preferredNodeName = null)
        {
            SelectedAnimLevel = level;

            if (level == null)
            {
                // Top-level mode: EditedEffectsByNode is the top-level dict (already set)
                // No action needed - EditedEffectsByNode stays as-is
            }
            else
            {
                // Per-level mode: use EditedLevelAnimFramesByNode[level]
                if (EditedLevelAnimFramesByNode == null)
                    EditedLevelAnimFramesByNode = new Dictionary<int, Dictionary<string, List<WzEffectFrame>>>();

                if (!EditedLevelAnimFramesByNode.TryGetValue(level.Value, out var levelNodes) || levelNodes == null)
                {
                    levelNodes = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
                    EditedLevelAnimFramesByNode[level.Value] = levelNodes;
                }
            }

            string nodeName = preferredNodeName ?? SelectedEffectNodeName;
            var activeDict = GetActiveAnimDict();
            if (activeDict != null && activeDict.Count > 0 && !activeDict.ContainsKey(nodeName))
                nodeName = GetFirstEffectNodeName(activeDict) ?? "effect";

            SetSelectedEffectNodeForActiveLevel(nodeName, createIfMissing: false);
        }

        /// <summary>
        /// Get the animation frame dict for the current level context.
        /// Returns top-level EditedEffectsByNode or per-level dict.
        /// </summary>
        public Dictionary<string, List<WzEffectFrame>> GetActiveAnimDict()
        {
            if (SelectedAnimLevel == null)
                return EditedEffectsByNode;

            if (EditedLevelAnimFramesByNode != null
                && EditedLevelAnimFramesByNode.TryGetValue(SelectedAnimLevel.Value, out var levelNodes))
                return levelNodes;

            return null;
        }

        /// <summary>
        /// Set the selected effect/anim node within the current level context.
        /// </summary>
        public void SetSelectedEffectNodeForActiveLevel(string nodeName, bool createIfMissing)
        {
            var activeDict = GetActiveAnimDict();
            if (activeDict == null)
            {
                if (SelectedAnimLevel != null && createIfMissing)
                {
                    if (EditedLevelAnimFramesByNode == null)
                        EditedLevelAnimFramesByNode = new Dictionary<int, Dictionary<string, List<WzEffectFrame>>>();
                    activeDict = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
                    EditedLevelAnimFramesByNode[SelectedAnimLevel.Value] = activeDict;
                }
                else
                {
                    EditedEffects = null;
                    SelectedEffectNodeName = NormalizeEffectNodeName(nodeName);
                    return;
                }
            }

            string normalized = NormalizeEffectNodeName(nodeName);
            if (createIfMissing && !activeDict.ContainsKey(normalized))
                activeDict[normalized] = new List<WzEffectFrame>();

            if (!activeDict.ContainsKey(normalized))
            {
                normalized = GetFirstEffectNodeName(activeDict) ?? "effect";
                if (createIfMissing && !activeDict.ContainsKey(normalized))
                    activeDict[normalized] = new List<WzEffectFrame>();
            }

            SelectedEffectNodeName = normalized;
            EditedEffects = activeDict.TryGetValue(normalized, out var list) ? list : null;
        }

        /// <summary>
        /// Get all node names from the current active level context.
        /// </summary>
        public List<string> GetActiveAnimNodeNames()
        {
            var activeDict = GetActiveAnimDict();
            var result = new List<string>();
            if (activeDict == null || activeDict.Count == 0)
                return result;
            foreach (var kv in activeDict)
            {
                if (!string.IsNullOrWhiteSpace(kv.Key))
                    result.Add(kv.Key);
            }
            return result;
        }

        /// <summary>
        /// Get the available level numbers that have animation frames.
        /// </summary>
        public List<int> GetLevelsWithAnimFrames()
        {
            var result = new List<int>();
            if (EditedLevelAnimFramesByNode != null)
            {
                foreach (var kv in EditedLevelAnimFramesByNode)
                {
                    if (kv.Value != null && kv.Value.Count > 0)
                        result.Add(kv.Key);
                }
            }
            result.Sort();
            return result;
        }

        /// <summary>
        /// Delete an entire animation node from the current level context.
        /// </summary>
        public bool DeleteAnimNode(string nodeName)
        {
            var activeDict = GetActiveAnimDict();
            if (activeDict == null) return false;

            string normalized = NormalizeEffectNodeName(nodeName);
            if (!activeDict.Remove(normalized)) return false;

            // If we deleted the selected node, switch to another
            if (string.Equals(SelectedEffectNodeName, normalized, StringComparison.OrdinalIgnoreCase))
            {
                string fallback = GetFirstEffectNodeName(activeDict) ?? "effect";
                SetSelectedEffectNodeForActiveLevel(fallback, createIfMissing: false);
            }
            return true;
        }

        /// <summary>
        /// Add a new animation node to the current level context.
        /// </summary>
        public void AddAnimNode(string nodeName)
        {
            var activeDict = GetActiveAnimDict();
            if (activeDict == null)
            {
                if (SelectedAnimLevel != null)
                {
                    if (EditedLevelAnimFramesByNode == null)
                        EditedLevelAnimFramesByNode = new Dictionary<int, Dictionary<string, List<WzEffectFrame>>>();
                    activeDict = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
                    EditedLevelAnimFramesByNode[SelectedAnimLevel.Value] = activeDict;
                }
                else
                {
                    if (EditedEffectsByNode == null)
                        EditedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
                    activeDict = EditedEffectsByNode;
                }
            }

            string normalized = NormalizeEffectNodeName(nodeName);
            if (!activeDict.ContainsKey(normalized))
                activeDict[normalized] = new List<WzEffectFrame>();

            SetSelectedEffectNodeForActiveLevel(normalized, createIfMissing: false);
        }

        /// <summary>
        /// Get the effective icon bitmap (user override > loaded .img > null).
        /// </summary>
        public Bitmap GetEffectiveIcon()
        {
            if (IconOverride != null) return IconOverride;
            if (LoadedData?.IconBitmap != null) return LoadedData.IconBitmap;
            return null;
        }

        public Bitmap GetEffectiveIconMO()
        {
            if (IconMOOverride != null) return IconMOOverride;
            if (LoadedData?.IconMouseOverBitmap != null) return LoadedData.IconMouseOverBitmap;
            return null;
        }

        public Bitmap GetEffectiveIconDis()
        {
            if (IconDisOverride != null) return IconDisOverride;
            if (LoadedData?.IconDisabledBitmap != null) return LoadedData.IconDisabledBitmap;
            return null;
        }

        /// <summary>
        /// Get base64 for the effective icon (for saving to SkillDefinition).
        /// </summary>
        public string GetEffectiveIconBase64()
        {
            if (IconOverride != null) return BitmapToBase64(IconOverride);
            if (LoadedData != null && !string.IsNullOrEmpty(LoadedData.IconBase64))
                return LoadedData.IconBase64;
            return "";
        }

        public string GetEffectiveIconMOBase64()
        {
            if (IconMOOverride != null) return BitmapToBase64(IconMOOverride);
            if (LoadedData != null && !string.IsNullOrEmpty(LoadedData.IconMouseOverBase64))
                return LoadedData.IconMouseOverBase64;
            return "";
        }

        public string GetEffectiveIconDisBase64()
        {
            if (IconDisOverride != null) return BitmapToBase64(IconDisOverride);
            if (LoadedData != null && !string.IsNullOrEmpty(LoadedData.IconDisabledBase64))
                return LoadedData.IconDisabledBase64;
            return "";
        }

        // Helpers

        public static string BitmapToBase64(Bitmap bmp)
        {
            if (bmp == null) return "";
            try
            {
                using (var ms = new MemoryStream())
                {
                    bmp.Save(ms, ImageFormat.Png);
                    return Convert.ToBase64String(ms.ToArray());
                }
            }
            catch { return ""; }
        }

        public static Bitmap BitmapFromBase64(string base64)
        {
            if (string.IsNullOrEmpty(base64)) return null;
            try
            {
                byte[] bytes = Convert.FromBase64String(base64);
                // Must NOT use 'using' on MemoryStream when Bitmap(stream) is used,
                // because Bitmap keeps a reference to the stream.
                // Instead, deep-copy via DrawImage to fully detach.
                var ms = new MemoryStream(bytes);
                var temp = new Bitmap(ms);
                var result = new Bitmap(temp.Width, temp.Height, PixelFormat.Format32bppArgb);
                using (var g = Graphics.FromImage(result))
                {
                    g.DrawImage(temp, 0, 0, temp.Width, temp.Height);
                }
                temp.Dispose();
                ms.Dispose();
                return result;
            }
            catch { return null; }
        }
    }
}
