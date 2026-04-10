using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using MapleLib.WzLib;
using MapleLib.WzLib.Serializer;
using MapleLib.WzLib.Util;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    internal static class MountEditorService
    {
        public static bool TryReadActionTamingMobIdByMountItem(int mountItemId, out int tamingMobId)
        {
            tamingMobId = 0;
            if (mountItemId <= 0)
                return false;

            string actionImgPath = PathConfig.GameMountActionImg(mountItemId);
            if (!File.Exists(actionImgPath))
                return false;

            int value = ReadActionTamingMobId(actionImgPath, 0);
            if (value <= 0)
                return false;

            tamingMobId = value;
            return true;
        }

        public static int FindNextAvailableTamingMobId(int preferredStart)
        {
            int start = preferredStart > 0 ? preferredStart : 1;
            var used = new HashSet<int>();
            string root = PathConfig.GameTamingMobRoot;

            if (Directory.Exists(root))
            {
                foreach (string file in Directory.EnumerateFiles(root, "*.img", SearchOption.TopDirectoryOnly))
                {
                    string name = Path.GetFileNameWithoutExtension(file);
                    if (int.TryParse(name, out int id) && id > 0)
                        used.Add(id);
                }
            }

            for (int candidate = start; candidate <= 9999; candidate++)
            {
                if (!used.Contains(candidate))
                    return candidate;
            }

            for (int candidate = 1; candidate < start; candidate++)
            {
                if (!used.Contains(candidate))
                    return candidate;
            }

            int fallback = Math.Max(start, 1);
            while (used.Contains(fallback))
                fallback++;
            return fallback;
        }

        public static MountEditorData Load(int mountItemId)
        {
            var data = new MountEditorData
            {
                MountItemId = mountItemId,
                ActionImgPath = PathConfig.GameMountActionImg(mountItemId)
            };

            if (!File.Exists(data.ActionImgPath))
                return data;

            if (!TryOpenParsedImage(data.ActionImgPath, out var actionFs, out var actionImg, out _, out string actionErr))
                throw new InvalidDataException("无法读取坐骑动作文件: " + actionErr);

            try
            {
                foreach (var prop in actionImg.WzProperties)
                {
                    if (prop == null)
                        continue;

                    if (string.Equals(prop.Name, "info", StringComparison.OrdinalIgnoreCase)
                        && prop is WzSubProperty infoSub)
                    {
                        data.ActionInfo = ReadScalarMap(infoSub);
                        continue;
                    }

                    var frames = ExtractFrames(prop);
                    if (frames.Count > 0)
                    {
                        string nodeName = NormalizeNodeName(prop.Name, "default");
                        data.ActionFramesByNode[nodeName] = frames;
                    }
                }

                if (TryReadInt(data.ActionInfo, "tamingMob", out int tamingMobId))
                    data.TamingMobId = tamingMobId;
            }
            finally
            {
                try { actionImg.Dispose(); } catch { }
                try { actionFs.Dispose(); } catch { }
            }

            if (data.TamingMobId > 0)
            {
                data.DataImgPath = PathConfig.GameMountDataImg(data.TamingMobId);
                if (File.Exists(data.DataImgPath)
                    && TryOpenParsedImage(data.DataImgPath, out var dataFs, out var dataImg, out _, out _))
                {
                    try
                    {
                        if (dataImg["info"] is WzSubProperty info)
                            data.DataInfo = ReadScalarMap(info);
                    }
                    finally
                    {
                        try { dataImg.Dispose(); } catch { }
                        try { dataFs.Dispose(); } catch { }
                    }
                }
            }

            return data;
        }

        public static void CloneMount(int sourceMountItemId, int targetMountItemId, int targetTamingMobId, bool cloneData)
        {
            string srcAction = PathConfig.GameMountActionImg(sourceMountItemId);
            if (!File.Exists(srcAction))
                throw new FileNotFoundException("来源坐骑动作文件不存在", srcAction);

            string dstAction = PathConfig.GameMountActionImg(targetMountItemId);
            EnsureDirectoryForFile(dstAction);
            if (File.Exists(dstAction))
                BackupHelper.Backup(dstAction);
            File.Copy(srcAction, dstAction, overwrite: true);

            int sourceTamingMobId = ReadActionTamingMobId(dstAction, 0);
            int finalTamingMobId = targetTamingMobId > 0 ? targetTamingMobId : sourceTamingMobId;
            if (finalTamingMobId > 0)
                SetActionTamingMobId(dstAction, finalTamingMobId);

            if (cloneData && sourceTamingMobId > 0 && finalTamingMobId > 0)
            {
                string srcData = PathConfig.GameMountDataImg(sourceTamingMobId);
                if (File.Exists(srcData))
                {
                    string dstData = PathConfig.GameMountDataImg(finalTamingMobId);
                    string srcFull = Path.GetFullPath(srcData);
                    string dstFull = Path.GetFullPath(dstData);
                    if (string.Equals(srcFull, dstFull, StringComparison.OrdinalIgnoreCase))
                        return;

                    EnsureDirectoryForFile(dstData);
                    if (File.Exists(dstData))
                        BackupHelper.Backup(dstData);
                    File.Copy(srcData, dstData, overwrite: true);
                }
            }
        }

        public static void SaveAction(MountEditorData data)
        {
            if (data == null || data.MountItemId <= 0)
                throw new ArgumentException("mountItemId 无效");

            string imgPath = PathConfig.GameMountActionImg(data.MountItemId);
            EnsureDirectoryForFile(imgPath);

            WzImage img = null;
            FileStream fs = null;
            WzMapleVersion version = WzImageVersionHelper.DetectPreferredVersionFromGameData();
            bool fromExisting = File.Exists(imgPath);

            if (fromExisting)
            {
                if (!TryOpenParsedImage(imgPath, out fs, out img, out version, out string err))
                    throw new InvalidDataException("无法读取坐骑动作文件: " + err);
            }
            else
            {
                img = new WzImage(Path.GetFileName(imgPath));
            }

            try
            {
                var info = img["info"] as WzSubProperty;
                if (info == null)
                {
                    info = new WzSubProperty("info");
                    img.AddProperty(info);
                }

                if (data.TamingMobId > 0)
                    data.ActionInfo["tamingMob"] = data.TamingMobId.ToString();

                OverwriteSubPropertyScalars(info, data.ActionInfo);

                if (data.RemovedActionNodes != null && data.RemovedActionNodes.Count > 0)
                {
                    foreach (string removed in data.RemovedActionNodes)
                        RemoveTopProperty(img, removed);
                    data.RemovedActionNodes.Clear();
                }

                if (data.ActionFramesByNode != null)
                {
                    foreach (var kv in data.ActionFramesByNode)
                    {
                        string nodeName = (kv.Key ?? "").Trim();
                        if (string.IsNullOrEmpty(nodeName))
                            continue;

                        if (kv.Value != null && kv.Value.Count > 0)
                        {
                            bool allBitmapMissing = true;
                            foreach (var frame in kv.Value)
                            {
                                if (frame?.Bitmap != null)
                                {
                                    allBitmapMissing = false;
                                    break;
                                }
                            }
                            if (allBitmapMissing)
                                continue; // avoid accidentally wiping nodes when source textures are undecodable
                        }

                        var node = BuildFrameNode(nodeName, kv.Value);
                        if (node == null)
                            continue;
                        ReplaceTopProperty(img, node);
                    }
                }

                img.Changed = true;
                byte[] iv = WzTool.GetIvByMapleVersion(version);
                var serializer = new WzImgSerializer(iv);
                byte[] bytes = serializer.SerializeImage(img);

                if (fromExisting)
                    BackupHelper.Backup(imgPath);
                try { fs?.Dispose(); } catch { }
                try { img?.Dispose(); } catch { }
                ImgWriteGenerator.WriteWithRetry(imgPath, bytes);
            }
            finally
            {
                try { img?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
            }
        }

        public static void SaveData(MountEditorData data)
        {
            if (data == null || data.TamingMobId <= 0)
                throw new ArgumentException("tamingMobId 无效");

            string imgPath = PathConfig.GameMountDataImg(data.TamingMobId);
            EnsureDirectoryForFile(imgPath);

            WzImage img = null;
            FileStream fs = null;
            WzMapleVersion version = WzImageVersionHelper.DetectPreferredVersionFromGameData();
            bool fromExisting = File.Exists(imgPath);

            if (fromExisting)
            {
                if (!TryOpenParsedImage(imgPath, out fs, out img, out version, out string err))
                    throw new InvalidDataException("无法读取坐骑参数文件: " + err);
            }
            else
            {
                img = new WzImage(Path.GetFileName(imgPath));
            }

            try
            {
                var info = img["info"] as WzSubProperty;
                if (info == null)
                {
                    info = new WzSubProperty("info");
                    img.AddProperty(info);
                }

                OverwriteSubPropertyScalars(info, data.DataInfo);
                img.Changed = true;

                byte[] iv = WzTool.GetIvByMapleVersion(version);
                var serializer = new WzImgSerializer(iv);
                byte[] bytes = serializer.SerializeImage(img);

                if (fromExisting)
                    BackupHelper.Backup(imgPath);
                try { fs?.Dispose(); } catch { }
                try { img?.Dispose(); } catch { }
                ImgWriteGenerator.WriteWithRetry(imgPath, bytes);
            }
            finally
            {
                try { img?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
            }
        }

        public static void SyncXml(MountEditorData data)
        {
            if (data == null || data.MountItemId <= 0)
                return;

            string actionImg = PathConfig.GameMountActionImg(data.MountItemId);
            string actionXml = PathConfig.ServerMountActionXml(data.MountItemId);
            SyncSingleXml(actionImg, actionXml);

            if (data.TamingMobId > 0)
            {
                string dataImg = PathConfig.GameMountDataImg(data.TamingMobId);
                string dataXml = PathConfig.ServerMountDataXml(data.TamingMobId);
                SyncSingleXml(dataImg, dataXml);
            }
        }

        private static void SyncSingleXml(string imgPath, string xmlPath)
        {
            if (!File.Exists(imgPath))
                return;

            EnsureDirectoryForFile(xmlPath);
            if (File.Exists(xmlPath))
                BackupHelper.Backup(xmlPath);

            if (!TryOpenParsedImage(imgPath, out var fs, out var img, out _, out string err))
                throw new InvalidDataException($"同步XML失败，读取{imgPath}错误: {err}");

            try
            {
                var serializer = new WzClassicXmlSerializer(0, LineBreak.None, false);
                serializer.SerializeImage(img, xmlPath);
            }
            finally
            {
                try { img.Dispose(); } catch { }
                try { fs.Dispose(); } catch { }
            }
        }

        private static Dictionary<string, string> ReadScalarMap(WzSubProperty parent)
        {
            var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (parent?.WzProperties == null)
                return map;

            foreach (var p in parent.WzProperties)
            {
                string value = GetPropertyValueString(p);
                if (value != null)
                    map[p.Name] = value;
            }
            return map;
        }

        private static string GetPropertyValueString(WzImageProperty p)
        {
            if (p == null) return null;
            if (p is WzIntProperty i32) return i32.Value.ToString();
            if (p is WzShortProperty i16) return i16.Value.ToString();
            if (p is WzLongProperty i64) return i64.Value.ToString();
            if (p is WzFloatProperty f) return f.Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
            if (p is WzDoubleProperty d) return d.Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
            if (p is WzStringProperty s) return s.Value ?? "";
            if (p is WzUOLProperty u) return u.Value ?? "";
            return null;
        }

        private static string NormalizeNodeName(string name, string fallback)
        {
            string text = (name ?? "").Trim();
            return string.IsNullOrEmpty(text) ? (fallback ?? "default") : text;
        }

        private static List<WzEffectFrame> ExtractFrames(WzImageProperty node)
        {
            var frames = new List<WzEffectFrame>();
            if (node == null)
                return frames;

            var resolvedNode = ResolveProperty(node);
            if (TryBuildFrameFromNode(resolvedNode, TryParseFrameIndex(node.Name), out var directFrame))
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

            if (frames.Count == 0)
            {
                foreach (var child in resolvedNode.WzProperties)
                {
                    if (child == null || string.Equals(child.Name, "info", StringComparison.OrdinalIgnoreCase))
                        continue;

                    var nested = ExtractFrames(child);
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

        private static bool TryBuildFrameFromNode(WzImageProperty sourceNode, int index, out WzEffectFrame frame)
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
                if (sub["canvas"] is WzCanvasProperty namedCanvas)
                {
                    canvas = namedCanvas;
                }
                else if (sub.WzProperties != null)
                {
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

            if (canvas == null)
                return false;

            frame = new WzEffectFrame
            {
                Index = index >= 0 ? index : 0,
                Delay = ReadDelay(metaRoot, 100)
            };

            if (frame.Delay <= 0)
                frame.Delay = ReadDelay(canvas, 100);

            TryFillBitmap(canvas, frame);
            CopyFrameMeta(metaRoot, frame, skipCanvasChildren: true);
            if (!ReferenceEquals(metaRoot, canvas))
                CopyFrameMeta(canvas, frame, skipCanvasChildren: false);

            return true;
        }

        private static int ReadDelay(WzImageProperty prop, int fallback)
        {
            if (prop == null)
                return fallback;

            WzImageProperty delay = null;
            if (prop is WzCanvasProperty c)
                delay = c["delay"];
            else if (prop is WzSubProperty s)
                delay = s["delay"];

            if (delay is WzIntProperty dp) return dp.Value;
            if (delay is WzShortProperty dsp) return dsp.Value;
            if (delay is WzLongProperty dlp) return (int)dlp.Value;
            if (delay is WzStringProperty ds && int.TryParse(ds.Value, out int parsed)) return parsed;
            return fallback;
        }

        private static void TryFillBitmap(WzCanvasProperty canvas, WzEffectFrame frame)
        {
            if (canvas == null || frame == null)
                return;
            try
            {
                var src = canvas.GetBitmap();
                if (src != null)
                {
                    frame.Bitmap = CloneBitmapSafe(src);
                    frame.Width = frame.Bitmap.Width;
                    frame.Height = frame.Bitmap.Height;
                    return;
                }
            }
            catch
            {
            }
        }

        private static void CopyFrameMeta(WzImageProperty source, WzEffectFrame frame, bool skipCanvasChildren)
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

        private static Bitmap CloneBitmapSafe(Bitmap source)
        {
            if (source == null)
                return null;
            var cloned = new Bitmap(source.Width, source.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(cloned))
                g.DrawImage(source, 0, 0, source.Width, source.Height);
            return cloned;
        }

        private static WzImageProperty ResolveProperty(WzImageProperty prop)
        {
            if (prop is WzUOLProperty uol)
            {
                var linked = uol.LinkValue as WzImageProperty;
                if (linked != null)
                    return linked;
            }
            return prop;
        }

        private static bool TryReadVector(WzImageProperty prop, out int x, out int y)
        {
            x = 0;
            y = 0;
            if (prop is WzVectorProperty vec)
            {
                x = vec.X.Value;
                y = vec.Y.Value;
                return true;
            }
            return false;
        }

        private static void OverwriteSubPropertyScalars(WzSubProperty target, Dictionary<string, string> map)
        {
            if (target == null)
                return;

            var remove = new List<WzImageProperty>();
            foreach (var p in target.WzProperties)
            {
                if (p is WzIntProperty || p is WzShortProperty || p is WzLongProperty
                    || p is WzFloatProperty || p is WzDoubleProperty || p is WzStringProperty
                    || p is WzUOLProperty)
                {
                    remove.Add(p);
                }
            }
            foreach (var p in remove)
            {
                target.WzProperties.Remove(p);
                try { p.Dispose(); } catch { }
            }

            if (map == null)
                return;

            foreach (var kv in map)
            {
                if (string.IsNullOrWhiteSpace(kv.Key))
                    continue;
                var scalar = BuildScalarProperty(kv.Key.Trim(), kv.Value ?? "");
                if (scalar != null)
                    target.AddProperty(scalar);
            }
        }

        private static WzImageProperty BuildScalarProperty(string name, string value)
        {
            if (string.IsNullOrWhiteSpace(name))
                return null;

            if (int.TryParse(value, out int i32))
                return new WzIntProperty(name, i32);
            if (long.TryParse(value, out long i64))
                return new WzLongProperty(name, i64);
            if (float.TryParse(value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float f))
                return new WzFloatProperty(name, f);
            if (double.TryParse(value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double d))
                return new WzDoubleProperty(name, d);
            return new WzStringProperty(name, value ?? "");
        }

        private static WzSubProperty BuildFrameNode(string nodeName, List<WzEffectFrame> frames)
        {
            var node = new WzSubProperty(nodeName ?? "");
            if (frames == null)
                return node;

            for (int i = 0; i < frames.Count; i++)
            {
                WzEffectFrame frame = frames[i];
                if (frame == null || frame.Bitmap == null)
                    continue;

                var canvas = new WzCanvasProperty(i.ToString());
                var png = new WzPngProperty { PNG = new Bitmap(frame.Bitmap) };
                canvas.PngProperty = png;
                canvas.AddProperty(new WzIntProperty("delay", frame.Delay > 0 ? frame.Delay : 100));

                if (frame.Vectors != null)
                {
                    foreach (var vk in frame.Vectors)
                    {
                        if (string.IsNullOrWhiteSpace(vk.Key) || vk.Value == null)
                            continue;
                        canvas.AddProperty(new WzVectorProperty(vk.Key, vk.Value.X, vk.Value.Y));
                    }
                }

                if (frame.FrameProps != null)
                {
                    foreach (var pk in frame.FrameProps)
                    {
                        if (string.IsNullOrWhiteSpace(pk.Key))
                            continue;
                        if (string.Equals(pk.Key, "delay", StringComparison.OrdinalIgnoreCase))
                            continue;
                        if (frame.Vectors != null && frame.Vectors.ContainsKey(pk.Key))
                            continue;
                        var prop = BuildScalarProperty(pk.Key, pk.Value ?? "");
                        if (prop != null)
                            canvas.AddProperty(prop);
                    }
                }

                node.AddProperty(canvas);
            }

            return node;
        }

        private static void RemoveTopProperty(WzImage img, string name)
        {
            if (img?.WzProperties == null || string.IsNullOrWhiteSpace(name))
                return;

            var old = img[name];
            if (old != null)
            {
                img.WzProperties.Remove(old);
                try { old.Dispose(); } catch { }
            }
        }

        private static void ReplaceTopProperty(WzImage img, WzImageProperty prop)
        {
            if (img == null || prop == null)
                return;
            RemoveTopProperty(img, prop.Name);
            img.AddProperty(prop);
        }

        private static bool TryReadInt(Dictionary<string, string> map, string key, out int value)
        {
            value = 0;
            if (map == null || string.IsNullOrWhiteSpace(key))
                return false;
            if (!map.TryGetValue(key, out string text))
                return false;
            return int.TryParse(text, out value);
        }

        private static int ReadActionTamingMobId(string actionImgPath, int fallback)
        {
            if (!TryOpenParsedImage(actionImgPath, out var fs, out var img, out _, out _))
                return fallback;
            try
            {
                var prop = img.GetFromPath("info/tamingMob");
                if (prop is WzIntProperty i32) return i32.Value;
                if (prop is WzShortProperty i16) return i16.Value;
                if (prop is WzLongProperty i64) return (int)i64.Value;
                if (prop is WzStringProperty s && int.TryParse(s.Value, out int parsed)) return parsed;
                return fallback;
            }
            finally
            {
                try { img.Dispose(); } catch { }
                try { fs.Dispose(); } catch { }
            }
        }

        private static void SetActionTamingMobId(string actionImgPath, int tamingMobId)
        {
            if (tamingMobId <= 0 || !File.Exists(actionImgPath))
                return;

            if (!TryOpenParsedImage(actionImgPath, out var fs, out var img, out var version, out _))
                return;
            try
            {
                var info = img["info"] as WzSubProperty;
                if (info == null)
                {
                    info = new WzSubProperty("info");
                    img.AddProperty(info);
                }
                ReplaceOrAddSubProperty(info, new WzIntProperty("tamingMob", tamingMobId));
                img.Changed = true;
                byte[] iv = WzTool.GetIvByMapleVersion(version);
                var serializer = new WzImgSerializer(iv);
                byte[] bytes = serializer.SerializeImage(img);
                BackupHelper.Backup(actionImgPath);
                try { fs.Dispose(); } catch { }
                try { img.Dispose(); } catch { }
                ImgWriteGenerator.WriteWithRetry(actionImgPath, bytes);
            }
            finally
            {
                try { img.Dispose(); } catch { }
                try { fs.Dispose(); } catch { }
            }
        }

        private static void ReplaceOrAddSubProperty(WzSubProperty parent, WzImageProperty prop)
        {
            if (parent == null || prop == null)
                return;
            var old = parent[prop.Name];
            if (old != null)
            {
                parent.WzProperties.Remove(old);
                try { old.Dispose(); } catch { }
            }
            parent.AddProperty(prop);
        }

        private static bool TryOpenParsedImage(
            string imgPath,
            out FileStream fs,
            out WzImage wzImg,
            out WzMapleVersion version,
            out string error)
        {
            fs = null;
            wzImg = null;
            version = WzMapleVersion.EMS;
            error = "";

            var versions = new List<WzMapleVersion>
            {
                WzImageVersionHelper.DetectPreferredVersionFromGameData(),
                WzMapleVersion.BMS,
                WzMapleVersion.CLASSIC,
                WzMapleVersion.GMS,
                WzMapleVersion.EMS
            };

            var seen = new HashSet<WzMapleVersion>();
            foreach (var candidate in versions)
            {
                if (!seen.Add(candidate))
                    continue;
                try
                {
                    fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                    wzImg = new WzImage(Path.GetFileName(imgPath), fs, candidate);
                    if (wzImg.ParseImage(true))
                    {
                        version = candidate;
                        return true;
                    }
                }
                catch (Exception ex)
                {
                    error = ex.Message;
                }

                try { wzImg?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
                wzImg = null;
                fs = null;
            }

            if (string.IsNullOrEmpty(error))
                error = "Unsupported/invalid .img format";
            return false;
        }

        private static void EnsureDirectoryForFile(string filePath)
        {
            string dir = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);
        }
    }
}
