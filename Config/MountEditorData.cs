using System;
using System.Collections.Generic;

namespace SuperSkillTool
{
    /// <summary>
    /// Editable mount resource data for one mountItemId.
    /// </summary>
    public sealed class MountEditorData : IDisposable
    {
        public int MountItemId;
        public int TamingMobId;
        public string ActionImgPath = "";
        public string DataImgPath = "";

        public Dictionary<string, string> ActionInfo = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, string> DataInfo = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, List<WzEffectFrame>> ActionFramesByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
        public HashSet<string> RemovedActionNodes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        public WzNodeInfo ActionTree;
        public WzNodeInfo DataTree;

        public void Dispose()
        {
            var seen = new HashSet<WzEffectFrame>();
            if (ActionFramesByNode != null)
            {
                foreach (var kv in ActionFramesByNode)
                {
                    if (kv.Value == null) continue;
                    foreach (var frame in kv.Value)
                    {
                        if (frame == null || !seen.Add(frame)) continue;
                        try { frame.Bitmap?.Dispose(); } catch { }
                    }
                    kv.Value.Clear();
                }
                ActionFramesByNode.Clear();
            }
            RemovedActionNodes?.Clear();
            ActionInfo?.Clear();
            DataInfo?.Clear();
            DisposeNodeTree(ActionTree);
            DisposeNodeTree(DataTree);
            ActionTree = null;
            DataTree = null;
        }

        private static void DisposeNodeTree(WzNodeInfo node)
        {
            if (node == null)
                return;

            try { node.CanvasBitmap?.Dispose(); } catch { }
            node.CanvasBitmap = null;
            if (node.Children == null)
                return;

            foreach (var child in node.Children)
                DisposeNodeTree(child);
        }
    }
}
