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
        }
    }
}
