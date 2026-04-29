using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;

namespace SuperSkillTool
{
    /// <summary>
    /// Data extracted from a .img file for a single skill.
    /// All Bitmap fields are owned by this object and should be disposed via DisposeImages().
    /// </summary>
    public class WzSkillData : IDisposable
    {
        public int SkillId;
        public int JobId;

        // String data (from String/Skill.img)
        public string Name = "";
        public string Desc = "";
        public string PDesc = "";
        public string Ph = "";
        public Dictionary<string, string> HLevels = new Dictionary<string, string>();

        // Icons as bitmaps (for GUI preview) and base64 (for generators)
        public Bitmap IconBitmap;
        public Bitmap IconMouseOverBitmap;
        public Bitmap IconDisabledBitmap;
        public string IconBase64 = "";
        public string IconMouseOverBase64 = "";
        public string IconDisabledBase64 = "";

        // Skill properties
        public string Action = "";
        public int InfoType;

        // Level parameters: level# → (paramName → value)
        public Dictionary<int, Dictionary<string, string>> LevelParams
            = new Dictionary<int, Dictionary<string, string>>();

        // Common parameters (formula strings like "120+10*x")
        public Dictionary<string, string> CommonParams
            = new Dictionary<string, string>();

        // Effect animation frames
        public List<WzEffectFrame> EffectFrames = new List<WzEffectFrame>();
        // Effect animation frames grouped by node name (effect/effect0/effect1/ball/hit\0/prepare/keydown...)
        public Dictionary<string, List<WzEffectFrame>> EffectFramesByNode
            = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);

        // Per-level animation frames: level# → nodeName → frames
        // For skills that store ball/hit/effect/prepare/keydown under each level/ node
        public Dictionary<int, Dictionary<string, List<WzEffectFrame>>> LevelAnimFramesByNode
            = new Dictionary<int, Dictionary<string, List<WzEffectFrame>>>();

        // h template text (contains placeholders like #mpCon, #damage, #dot etc.)
        public string H = "";

        // Lightweight node tree for TreeView display
        public WzNodeInfo RootNode;

        public void Dispose()
        {
            IconBitmap?.Dispose(); IconBitmap = null;
            IconMouseOverBitmap?.Dispose(); IconMouseOverBitmap = null;
            IconDisabledBitmap?.Dispose(); IconDisabledBitmap = null;
            var disposedFrames = new HashSet<WzEffectFrame>();
            if (EffectFramesByNode != null)
            {
                foreach (var kv in EffectFramesByNode)
                {
                    if (kv.Value == null) continue;
                    foreach (var f in kv.Value)
                    {
                        if (f == null || !disposedFrames.Add(f)) continue;
                        f.Bitmap?.Dispose();
                    }
                    kv.Value.Clear();
                }
                EffectFramesByNode.Clear();
            }
            if (LevelAnimFramesByNode != null)
            {
                foreach (var levelKv in LevelAnimFramesByNode)
                {
                    if (levelKv.Value == null) continue;
                    foreach (var nodeKv in levelKv.Value)
                    {
                        if (nodeKv.Value == null) continue;
                        foreach (var f in nodeKv.Value)
                        {
                            if (f == null || !disposedFrames.Add(f)) continue;
                            f.Bitmap?.Dispose();
                        }
                        nodeKv.Value.Clear();
                    }
                    levelKv.Value.Clear();
                }
                LevelAnimFramesByNode.Clear();
            }
            if (EffectFrames != null)
            {
                foreach (var f in EffectFrames)
                {
                    if (f == null || !disposedFrames.Add(f)) continue;
                    f.Bitmap?.Dispose();
                }
                EffectFrames.Clear();
            }
        }
    }

    /// <summary>
    /// A single animation frame from an effect node.
    /// </summary>
    public class WzEffectFrame
    {
        public int Index;
        public int Width;
        public int Height;
        public int Delay;    // ms
        public Bitmap Bitmap; // may be null if extraction failed
        // Extra vector anchors under each effect frame canvas (origin/head/vector/border/crosshair...)
        public Dictionary<string, WzFrameVector> Vectors = new Dictionary<string, WzFrameVector>(StringComparer.OrdinalIgnoreCase);
        // Extra scalar properties under each effect frame canvas (e.g. z, lt/rb-like ints, custom flags...).
        public Dictionary<string, string> FrameProps = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        public static Dictionary<string, WzFrameVector> CloneVectors(Dictionary<string, WzFrameVector> source)
        {
            var result = new Dictionary<string, WzFrameVector>(StringComparer.OrdinalIgnoreCase);
            if (source == null) return result;

            foreach (var kv in source)
            {
                if (string.IsNullOrEmpty(kv.Key) || kv.Value == null) continue;
                result[kv.Key] = new WzFrameVector(kv.Value.X, kv.Value.Y);
            }
            return result;
        }

        public static Dictionary<string, string> CloneFrameProps(Dictionary<string, string> source)
        {
            var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (source == null) return result;

            foreach (var kv in source)
            {
                if (string.IsNullOrEmpty(kv.Key)) continue;
                result[kv.Key] = kv.Value ?? "";
            }
            return result;
        }

        public static WzEffectFrame CloneShallowBitmap(WzEffectFrame source)
        {
            if (source == null) return null;

            Bitmap clonedBitmap = CloneBitmap(source.Bitmap);
            int width = source.Width;
            int height = source.Height;

            if (clonedBitmap != null)
            {
                width = clonedBitmap.Width;
                height = clonedBitmap.Height;
            }
            else if ((width <= 0 || height <= 0) && TryGetBitmapSize(source.Bitmap, out int w, out int h))
            {
                if (width <= 0) width = w;
                if (height <= 0) height = h;
            }

            return new WzEffectFrame
            {
                Index = source.Index,
                Width = width,
                Height = height,
                Delay = source.Delay,
                Bitmap = clonedBitmap,
                Vectors = CloneVectors(source.Vectors),
                FrameProps = CloneFrameProps(source.FrameProps)
            };
        }

        private static bool TryGetBitmapSize(Bitmap source, out int width, out int height)
        {
            width = 0;
            height = 0;
            if (source == null) return false;

            try
            {
                width = source.Width;
                height = source.Height;
                return width > 0 && height > 0;
            }
            catch
            {
                return false;
            }
        }

        private static Bitmap CloneBitmap(Bitmap source)
        {
            if (!TryGetBitmapSize(source, out int width, out int height))
                return null;

            try
            {
                var cloned = new Bitmap(width, height, PixelFormat.Format32bppArgb);
                using (Graphics g = Graphics.FromImage(cloned))
                {
                    g.DrawImage(source, 0, 0, width, height);
                }
                return cloned;
            }
            catch
            {
                try
                {
                    return (Bitmap)source.Clone();
                }
                catch
                {
                    return null;
                }
            }
        }
    }

    /// <summary>
    /// Lightweight 2D vector used by effect frame anchors.
    /// </summary>
    public class WzFrameVector
    {
        public int X;
        public int Y;

        public WzFrameVector() { }

        public WzFrameVector(int x, int y)
        {
            X = x;
            Y = y;
        }
    }

    /// <summary>
    /// Lightweight recursive tree node for display in TreeView.
    /// Does not hold any WzLib references.
    /// </summary>
    public class WzNodeInfo
    {
        public string Name = "";
        public string TypeName = "";  // "SubProperty", "Canvas", "Int", "String", etc.
        public string Value = "";     // display value for leaf nodes, empty for containers
        public List<WzNodeInfo> Children = new List<WzNodeInfo>();

        // Optional runtime-only canvas payload for full WZ tree editing.
        // Skill JSON persistence intentionally ignores these fields.
        public int CanvasWidth;
        public int CanvasHeight;
        public int CanvasPngFormat;
        public byte[] CanvasCompressedBytes;
        public Bitmap CanvasBitmap;
    }
}
