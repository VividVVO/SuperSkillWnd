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

            try
            {
                string actionImgPath = PathConfig.GameMountActionImg(mountItemId);
                if (!File.Exists(actionImgPath))
                    return false;

                int value = ReadActionTamingMobId(actionImgPath, 0);
                if (value <= 0)
                    return false;

                tamingMobId = value;
                return true;
            }
            catch
            {
                return false;
            }
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
            {
                data.ActionTree = CreateEmptyImageTree(Path.GetFileName(data.ActionImgPath));
                return data;
            }

            if (!TryOpenParsedImage(data.ActionImgPath, out var actionFs, out var actionImg, out _, out string actionErr))
                throw new InvalidDataException("无法读取坐骑动作文件: " + actionErr);

            try
            {
                data.ActionTree = BuildNodeTree(actionImg);
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
                        data.DataTree = BuildNodeTree(dataImg);
                        if (dataImg["info"] is WzSubProperty info)
                            data.DataInfo = ReadScalarMap(info);
                    }
                    finally
                    {
                        try { dataImg.Dispose(); } catch { }
                        try { dataFs.Dispose(); } catch { }
                    }
                }
                else
                {
                    data.DataTree = CreateEmptyImageTree(Path.GetFileName(data.DataImgPath));
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

            if (data.ActionTree != null)
            {
                data.ActionTree.Name = Path.GetFileName(imgPath);
                if (data.TamingMobId > 0)
                    data.ActionInfo["tamingMob"] = data.TamingMobId.ToString();
                ApplyInfoMapToTree(data.ActionTree, data.ActionInfo);
                SaveImageTree(data.ActionTree, imgPath);
                return;
            }

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

            if (data.DataTree != null)
            {
                data.DataTree.Name = Path.GetFileName(imgPath);
                ApplyInfoMapToTree(data.DataTree, data.DataInfo);
                SaveImageTree(data.DataTree, imgPath);
                return;
            }

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

        public static WzNodeInfo CreateEmptyImageTree(string imageName)
        {
            return new WzNodeInfo
            {
                Name = string.IsNullOrWhiteSpace(imageName) ? "new.img" : imageName,
                TypeName = "ImgDir",
                Children = new List<WzNodeInfo>()
            };
        }

        public static void ApplyInfoMapToTree(WzNodeInfo root, Dictionary<string, string> map)
        {
            if (root == null)
                return;

            if (root.Children == null)
                root.Children = new List<WzNodeInfo>();

            WzNodeInfo info = FindChild(root, "info");
            if (info == null)
            {
                info = new WzNodeInfo
                {
                    Name = "info",
                    TypeName = "SubProperty",
                    Children = new List<WzNodeInfo>()
                };
                root.Children.Insert(0, info);
            }
            if (info.Children == null)
                info.Children = new List<WzNodeInfo>();

            for (int i = info.Children.Count - 1; i >= 0; i--)
            {
                if (IsScalarNode(info.Children[i]))
                {
                    DisposeNodePayload(info.Children[i]);
                    info.Children.RemoveAt(i);
                }
            }

            if (map == null)
                return;

            var keys = new List<string>(map.Keys);
            keys.Sort(StringComparer.OrdinalIgnoreCase);
            foreach (string key in keys)
            {
                if (string.IsNullOrWhiteSpace(key))
                    continue;
                info.Children.Add(BuildScalarNode(key.Trim(), map.TryGetValue(key, out string value) ? value : ""));
            }
        }

        public static Dictionary<string, string> ExtractInfoMapFromTree(WzNodeInfo root)
        {
            var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            WzNodeInfo info = FindChild(root, "info");
            if (info?.Children == null)
                return map;

            foreach (var child in info.Children)
            {
                if (child == null || string.IsNullOrWhiteSpace(child.Name))
                    continue;
                if (!IsScalarNode(child))
                    continue;
                map[child.Name] = child.Value ?? "";
            }
            return map;
        }

        public static void ApplyActionFramesToTree(
            WzNodeInfo root,
            Dictionary<string, List<WzEffectFrame>> framesByNode,
            HashSet<string> removedNodes)
        {
            if (root == null)
                return;
            if (root.Children == null)
                root.Children = new List<WzNodeInfo>();

            if (removedNodes != null)
            {
                foreach (string removed in removedNodes)
                    RemoveChild(root, removed);
            }

            if (framesByNode == null)
                return;

            foreach (var kv in framesByNode)
            {
                string nodeName = (kv.Key ?? "").Trim();
                if (string.IsNullOrEmpty(nodeName))
                    continue;

                WzNodeInfo previous = FindChild(root, nodeName);
                WzNodeInfo rebuilt = BuildFrameNodeInfo(nodeName, kv.Value, previous);
                ReplaceChild(root, rebuilt);
            }
        }

        private static WzNodeInfo BuildNodeTree(WzImage image)
        {
            var root = CreateEmptyImageTree(image?.Name ?? "");
            if (image?.WzProperties == null)
                return root;

            foreach (var child in image.WzProperties)
            {
                if (child != null)
                    root.Children.Add(BuildNodeTree(child, 0));
            }
            return root;
        }

        private static WzNodeInfo BuildNodeTree(WzImageProperty node, int depth)
        {
            var info = new WzNodeInfo
            {
                Name = node?.Name ?? "",
                TypeName = node?.PropertyType.ToString() ?? "SubProperty",
                Children = new List<WzNodeInfo>()
            };

            string value = GetPropertyValueString(node);
            if (value != null)
                info.Value = value;

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

            if (depth < 32 && node?.WzProperties != null)
            {
                foreach (var child in node.WzProperties)
                {
                    if (child != null)
                        info.Children.Add(BuildNodeTree(child, depth + 1));
                }
            }
            return info;
        }

        private static void SaveImageTree(WzNodeInfo root, string imgPath)
        {
            if (root == null)
                throw new InvalidDataException("节点树为空，无法保存");

            EnsureDirectoryForFile(imgPath);
            WzMapleVersion version = WzImageVersionHelper.DetectPreferredVersionFromGameData();
            if (File.Exists(imgPath)
                && TryOpenParsedImage(imgPath, out var oldFs, out var oldImg, out var detected, out _))
            {
                version = detected;
                try { oldImg.Dispose(); } catch { }
                try { oldFs.Dispose(); } catch { }
            }

            WzImage img = null;
            try
            {
                img = BuildImageFromTree(root, Path.GetFileName(imgPath));
                img.Changed = true;
                byte[] iv = WzTool.GetIvByMapleVersion(version);
                var serializer = new WzImgSerializer(iv);
                byte[] bytes = serializer.SerializeImage(img);
                if (File.Exists(imgPath))
                    BackupHelper.Backup(imgPath);
                ImgWriteGenerator.WriteWithRetry(imgPath, bytes);
            }
            finally
            {
                try { img?.Dispose(); } catch { }
            }
        }

        private static WzImage BuildImageFromTree(WzNodeInfo root, string imageName)
        {
            var img = new WzImage(string.IsNullOrWhiteSpace(imageName) ? (root?.Name ?? "new.img") : imageName);
            if (root?.Children == null)
                return img;

            foreach (var child in root.Children)
            {
                var prop = BuildPropertyFromNode(child);
                if (prop != null)
                    img.AddProperty(prop);
            }
            return img;
        }

        private static WzImageProperty BuildPropertyFromNode(WzNodeInfo node)
        {
            if (node == null || string.IsNullOrWhiteSpace(node.Name))
                return null;

            string type = (node.TypeName ?? "").Trim();
            if (string.Equals(type, "ImgDir", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "Image", StringComparison.OrdinalIgnoreCase))
            {
                return null;
            }

            if (string.Equals(type, "SubProperty", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "自定义", StringComparison.OrdinalIgnoreCase))
            {
                var sub = new WzSubProperty(node.Name);
                AddBuiltChildren(sub, node);
                return sub;
            }
            if (string.Equals(type, "Convex", StringComparison.OrdinalIgnoreCase))
            {
                var convex = new WzConvexProperty(node.Name);
                AddBuiltChildren(convex, node);
                return convex;
            }
            if (string.Equals(type, "Canvas", StringComparison.OrdinalIgnoreCase))
            {
                var canvas = new WzCanvasProperty(node.Name);
                canvas.PngProperty = BuildPngProperty(node);
                AddBuiltChildren(canvas, node);
                return canvas;
            }
            if (string.Equals(type, "Vector", StringComparison.OrdinalIgnoreCase))
            {
                TryParseVectorValue(node.Value, out int x, out int y);
                return new WzVectorProperty(node.Name, x, y);
            }
            if (string.Equals(type, "UOL", StringComparison.OrdinalIgnoreCase))
            {
                string value = node.Value ?? "";
                if (value.StartsWith("-> "))
                    value = value.Substring(3);
                return new WzUOLProperty(node.Name, value);
            }
            if (string.Equals(type, "Null", StringComparison.OrdinalIgnoreCase))
                return new WzNullProperty(node.Name);
            if (string.Equals(type, "Short", StringComparison.OrdinalIgnoreCase)
                && short.TryParse(node.Value, out short s16))
                return new WzShortProperty(node.Name, s16);
            if (string.Equals(type, "Int", StringComparison.OrdinalIgnoreCase)
                && int.TryParse(node.Value, out int i32))
                return new WzIntProperty(node.Name, i32);
            if (string.Equals(type, "Long", StringComparison.OrdinalIgnoreCase)
                && long.TryParse(node.Value, out long i64))
                return new WzLongProperty(node.Name, i64);
            if (string.Equals(type, "Float", StringComparison.OrdinalIgnoreCase)
                && float.TryParse(node.Value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float f32))
                return new WzFloatProperty(node.Name, f32);
            if (string.Equals(type, "Double", StringComparison.OrdinalIgnoreCase)
                && double.TryParse(node.Value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double f64))
                return new WzDoubleProperty(node.Name, f64);
            if (string.Equals(type, "String", StringComparison.OrdinalIgnoreCase))
                return new WzStringProperty(node.Name, node.Value ?? "");

            if (node.Children != null && node.Children.Count > 0)
            {
                var sub = new WzSubProperty(node.Name);
                AddBuiltChildren(sub, node);
                return sub;
            }
            return BuildScalarProperty(node.Name, node.Value ?? "");
        }

        private static WzPngProperty BuildPngProperty(WzNodeInfo node)
        {
            var png = new WzPngProperty();
            if (node.CanvasCompressedBytes != null
                && node.CanvasCompressedBytes.Length > 0
                && node.CanvasWidth > 0
                && node.CanvasHeight > 0
                && node.CanvasPngFormat > 0)
            {
                png.SetCompressedBytes(
                    (byte[])node.CanvasCompressedBytes.Clone(),
                    node.CanvasWidth,
                    node.CanvasHeight,
                    (WzPngFormat)node.CanvasPngFormat);
                return png;
            }

            Bitmap source = node.CanvasBitmap;
            if (source == null)
            {
                ParseCanvasSize(node, out int width, out int height);
                width = Math.Max(width, 1);
                height = Math.Max(height, 1);
                using (var blank = new Bitmap(width, height, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                    png.SetBitmapBgra4444(blank);
                return png;
            }

            using (var bmp = new Bitmap(source))
                png.SetBitmapBgra4444(bmp);
            return png;
        }

        private static void AddBuiltChildren(WzSubProperty parent, WzNodeInfo node)
        {
            if (parent == null || node?.Children == null)
                return;
            foreach (var child in node.Children)
            {
                var prop = BuildPropertyFromNode(child);
                if (prop != null)
                    parent.AddProperty(prop);
            }
        }

        private static void AddBuiltChildren(WzCanvasProperty parent, WzNodeInfo node)
        {
            if (parent == null || node?.Children == null)
                return;
            foreach (var child in node.Children)
            {
                var prop = BuildPropertyFromNode(child);
                if (prop != null)
                    parent.AddProperty(prop);
            }
        }

        private static void AddBuiltChildren(WzConvexProperty parent, WzNodeInfo node)
        {
            if (parent == null || node?.Children == null)
                return;
            foreach (var child in node.Children)
            {
                var prop = BuildPropertyFromNode(child);
                if (prop != null)
                    parent.AddProperty(prop);
            }
        }

        private static WzNodeInfo BuildFrameNodeInfo(string nodeName, List<WzEffectFrame> frames, WzNodeInfo previous)
        {
            var node = new WzNodeInfo
            {
                Name = nodeName ?? "",
                TypeName = "SubProperty",
                Children = new List<WzNodeInfo>()
            };

            if (frames == null)
                return node;

            for (int i = 0; i < frames.Count; i++)
            {
                WzEffectFrame frame = frames[i];
                if (frame == null)
                    continue;

                WzNodeInfo previousFrame = FindChild(previous, i.ToString());
                var canvas = new WzNodeInfo
                {
                    Name = i.ToString(),
                    TypeName = "Canvas",
                    CanvasWidth = frame.Width,
                    CanvasHeight = frame.Height,
                    Value = frame.Width + "x" + frame.Height,
                    Children = new List<WzNodeInfo>()
                };

                if (frame.Bitmap != null)
                {
                    canvas.CanvasBitmap = CloneBitmapSafe(frame.Bitmap);
                    canvas.CanvasWidth = frame.Bitmap.Width;
                    canvas.CanvasHeight = frame.Bitmap.Height;
                    canvas.Value = canvas.CanvasWidth + "x" + canvas.CanvasHeight;
                }
                else
                {
                    CopyCanvasPayload(previousFrame, canvas);
                }

                var handledChildNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                canvas.Children.Add(new WzNodeInfo
                {
                    Name = "delay",
                    TypeName = "Int",
                    Value = (frame.Delay > 0 ? frame.Delay : 100).ToString(),
                    Children = new List<WzNodeInfo>()
                });
                handledChildNames.Add("delay");

                if (frame.Vectors != null)
                {
                    foreach (var vk in frame.Vectors)
                    {
                        if (string.IsNullOrWhiteSpace(vk.Key) || vk.Value == null)
                            continue;
                        canvas.Children.Add(new WzNodeInfo
                        {
                            Name = vk.Key,
                            TypeName = "Vector",
                            Value = vk.Value.X + "," + vk.Value.Y,
                            Children = new List<WzNodeInfo>()
                        });
                        handledChildNames.Add(vk.Key);
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
                        canvas.Children.Add(BuildScalarNode(pk.Key, pk.Value ?? ""));
                        handledChildNames.Add(pk.Key);
                    }
                }

                PreserveExtraFrameChildren(previousFrame, canvas, handledChildNames);
                node.Children.Add(canvas);
            }

            return node;
        }

        private static void PreserveExtraFrameChildren(WzNodeInfo previousFrame, WzNodeInfo rebuiltFrame, HashSet<string> handledChildNames)
        {
            if (previousFrame?.Children == null || rebuiltFrame == null)
                return;
            if (rebuiltFrame.Children == null)
                rebuiltFrame.Children = new List<WzNodeInfo>();

            foreach (var child in previousFrame.Children)
            {
                string name = (child?.Name ?? "").Trim();
                if (string.IsNullOrEmpty(name))
                    continue;
                if (handledChildNames != null && handledChildNames.Contains(name))
                    continue;
                if (FindChild(rebuiltFrame, name) != null)
                    continue;
                rebuiltFrame.Children.Add(CloneNodeInfo(child));
            }
        }

        private static WzNodeInfo CloneNodeInfo(WzNodeInfo source)
        {
            if (source == null)
                return null;
            var clone = new WzNodeInfo
            {
                Name = source.Name,
                TypeName = source.TypeName,
                Value = source.Value,
                CanvasWidth = source.CanvasWidth,
                CanvasHeight = source.CanvasHeight,
                CanvasPngFormat = source.CanvasPngFormat,
                CanvasCompressedBytes = source.CanvasCompressedBytes != null ? (byte[])source.CanvasCompressedBytes.Clone() : null,
                CanvasBitmap = source.CanvasBitmap != null ? CloneBitmapSafe(source.CanvasBitmap) : null,
                Children = new List<WzNodeInfo>()
            };
            if (source.Children != null)
            {
                foreach (var child in source.Children)
                {
                    var clonedChild = CloneNodeInfo(child);
                    if (clonedChild != null)
                        clone.Children.Add(clonedChild);
                }
            }
            return clone;
        }

        private static WzNodeInfo BuildScalarNode(string name, string value)
        {
            string text = value ?? "";
            string type = "String";
            if (int.TryParse(text, out _))
                type = "Int";
            else if (long.TryParse(text, out _))
                type = "Long";
            else if (float.TryParse(text, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out _))
                type = "Float";

            return new WzNodeInfo
            {
                Name = name ?? "",
                TypeName = type,
                Value = text,
                Children = new List<WzNodeInfo>()
            };
        }

        private static WzNodeInfo FindChild(WzNodeInfo parent, string name)
        {
            if (parent?.Children == null || string.IsNullOrWhiteSpace(name))
                return null;
            foreach (var child in parent.Children)
            {
                if (child != null && string.Equals(child.Name, name, StringComparison.OrdinalIgnoreCase))
                    return child;
            }
            return null;
        }

        private static void ReplaceChild(WzNodeInfo parent, WzNodeInfo replacement)
        {
            if (parent == null || replacement == null || string.IsNullOrWhiteSpace(replacement.Name))
                return;
            if (parent.Children == null)
                parent.Children = new List<WzNodeInfo>();

            for (int i = 0; i < parent.Children.Count; i++)
            {
                if (parent.Children[i] != null
                    && string.Equals(parent.Children[i].Name, replacement.Name, StringComparison.OrdinalIgnoreCase))
                {
                    DisposeNodePayload(parent.Children[i]);
                    parent.Children[i] = replacement;
                    return;
                }
            }
            parent.Children.Add(replacement);
        }

        private static void RemoveChild(WzNodeInfo parent, string name)
        {
            if (parent?.Children == null || string.IsNullOrWhiteSpace(name))
                return;
            for (int i = parent.Children.Count - 1; i >= 0; i--)
            {
                if (parent.Children[i] != null
                    && string.Equals(parent.Children[i].Name, name, StringComparison.OrdinalIgnoreCase))
                {
                    DisposeNodePayload(parent.Children[i]);
                    parent.Children.RemoveAt(i);
                }
            }
        }

        private static bool IsScalarNode(WzNodeInfo node)
        {
            if (node == null)
                return false;
            string type = node.TypeName ?? "";
            return string.Equals(type, "Int", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "Short", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "Long", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "Float", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "Double", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "String", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "UOL", StringComparison.OrdinalIgnoreCase)
                || string.Equals(type, "Null", StringComparison.OrdinalIgnoreCase);
        }

        private static void CopyCanvasPayload(WzNodeInfo source, WzNodeInfo target)
        {
            if (source == null || target == null)
                return;
            target.CanvasWidth = source.CanvasWidth;
            target.CanvasHeight = source.CanvasHeight;
            target.CanvasPngFormat = source.CanvasPngFormat;
            if (source.CanvasCompressedBytes != null)
                target.CanvasCompressedBytes = (byte[])source.CanvasCompressedBytes.Clone();
            if (source.CanvasBitmap != null)
                target.CanvasBitmap = CloneBitmapSafe(source.CanvasBitmap);
            if (target.CanvasWidth > 0 && target.CanvasHeight > 0)
                target.Value = target.CanvasWidth + "x" + target.CanvasHeight;
        }

        private static void DisposeNodePayload(WzNodeInfo node)
        {
            if (node == null)
                return;
            try { node.CanvasBitmap?.Dispose(); } catch { }
            node.CanvasBitmap = null;
            if (node.Children == null)
                return;
            foreach (var child in node.Children)
                DisposeNodePayload(child);
        }

        private static void ParseCanvasSize(WzNodeInfo node, out int width, out int height)
        {
            width = node?.CanvasWidth ?? 0;
            height = node?.CanvasHeight ?? 0;
            string text = node?.Value ?? "";
            if (width > 0 && height > 0)
                return;
            string[] parts = text.Split(new[] { 'x', 'X', ',', ' ' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length >= 2)
            {
                int.TryParse(parts[0], out width);
                int.TryParse(parts[1], out height);
            }
        }

        private static bool TryParseVectorValue(string value, out int x, out int y)
        {
            x = 0;
            y = 0;
            string[] parts = (value ?? "").Split(new[] { ',', ' ', ';', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length < 2)
                return false;
            bool okX = int.TryParse(parts[0], out x);
            bool okY = int.TryParse(parts[1], out y);
            return okX && okY;
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
                var png = new WzPngProperty();
                using (var bmp = new Bitmap(frame.Bitmap))
                {
                    png.SetBitmapBgra4444(bmp);
                }
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
                    fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
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
