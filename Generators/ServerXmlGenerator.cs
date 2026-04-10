using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Xml;
using MapleLib.WzLib;
using MapleLib.WzLib.Serializer;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    /// <summary>
    /// Modifies the server-side Skill WZ XML (e.g. 000.img.xml) to add skill nodes.
    /// Inserts into the "skill" imgdir of the job file.
    /// </summary>
    public static class ServerXmlGenerator
    {
        public static void Remove(List<SkillDefinition> skills, bool dryRun)
        {
            var byJob = new Dictionary<int, List<SkillDefinition>>();
            foreach (var sd in skills)
            {
                if (!byJob.ContainsKey(sd.JobId))
                    byJob[sd.JobId] = new List<SkillDefinition>();
                byJob[sd.JobId].Add(sd);
            }

            foreach (var kv in byJob)
            {
                int jobId = kv.Key;
                var list = kv.Value;
                string xmlPath = PathConfig.ServerSkillXml(jobId);

                Console.WriteLine($"\n[ServerXml-Delete] Processing {xmlPath}");

                if (!EnsureXmlFromImg(jobId, xmlPath, dryRun, forceSync: !dryRun))
                    continue;

                if (!File.Exists(xmlPath)) { Console.WriteLine($"  [skip] Not found: {xmlPath}"); continue; }

                var doc = LoadOrRecoverXml(jobId, xmlPath);

                XmlNode skillNode = FindImgDir(doc.DocumentElement, "skill");
                if (skillNode == null) { Console.WriteLine("  [skip] No 'skill' node."); continue; }

                int count = 0;
                foreach (var sd in list)
                {
                    var existing = FindSkillNodeBySkillId(skillNode, sd.SkillId);
                    if (existing == null)
                    {
                        Console.WriteLine($"  [skip] {FormatSkillKey(sd.SkillId)} not found");
                        continue;
                    }
                    if (dryRun)
                    {
                        Console.WriteLine($"  [dry-run] Would remove {existing.Attributes?["name"]?.Value}");
                        continue;
                    }
                    skillNode.RemoveChild(existing);
                    count++;
                    Console.WriteLine($"  [removed] {existing.Attributes?["name"]?.Value}");
                }

                if (!dryRun && count > 0)
                {
                    BackupHelper.Backup(xmlPath);
                    SaveXml(doc, xmlPath);
                    Console.WriteLine($"  [saved] {xmlPath} ({count} removed)");
                }
            }
        }

        public static void Generate(List<SkillDefinition> skills, bool dryRun)
        {
            var byJob = new Dictionary<int, List<SkillDefinition>>();
            foreach (var sd in skills)
            {
                if (!byJob.ContainsKey(sd.JobId))
                    byJob[sd.JobId] = new List<SkillDefinition>();
                byJob[sd.JobId].Add(sd);
            }

            int carrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            var staleCarrierIds = CarrierSkillHelper.GetStaleCarrierIds();
            if (carrierId > 0)
            {
                int carrierJobId = carrierId / 10000;
                if (!byJob.ContainsKey(carrierJobId))
                    byJob[carrierJobId] = new List<SkillDefinition>();
            }
            foreach (int staleCarrierId in staleCarrierIds)
            {
                int staleJobId = staleCarrierId / 10000;
                if (!byJob.ContainsKey(staleJobId))
                    byJob[staleJobId] = new List<SkillDefinition>();
            }

            foreach (var kv in byJob)
            {
                int jobId = kv.Key;
                var list = kv.Value;
                string xmlPath = PathConfig.ServerSkillXml(jobId);

                Console.WriteLine($"\n[ServerXml] Processing {xmlPath}");

                if (!EnsureXmlFromImg(jobId, xmlPath, dryRun, forceSync: !dryRun))
                    continue;

                if (!File.Exists(xmlPath))
                {
                    Console.WriteLine($"  [error] File not found after sync: {xmlPath}");
                    continue;
                }

                var doc = LoadOrRecoverXml(jobId, xmlPath);

                XmlNode skillNode = FindImgDir(doc.DocumentElement, "skill");
                if (skillNode == null)
                {
                    Console.WriteLine("  [error] Could not find <imgdir name=\"skill\"> node.");
                    continue;
                }

                bool modified = false;
                int staleRemoved = RemoveStaleCarrierSkillNodes(skillNode, jobId, staleCarrierIds, dryRun);
                if (!dryRun && staleRemoved > 0)
                    modified = true;

                if (carrierId > 0 && carrierId / 10000 == jobId)
                {
                    var existingCarrierNode = FindSkillNodeBySkillId(skillNode, carrierId) as XmlElement;
                    if (existingCarrierNode == null)
                    {
                        if (dryRun)
                        {
                            Console.WriteLine($"  [dry-run] Would add carrier skill {FormatSkillKey(carrierId)}");
                        }
                        else
                        {
                            XmlElement carrierDir = CreateImgDir(doc, FormatSkillKey(carrierId));
                            XmlElement commonDir = CreateImgDir(doc, "common");
                            AppendIntNode(doc, commonDir, "maxLevel", Math.Max(1, PathConfig.DefaultSuperSpCarrierMaxLevel));
                            carrierDir.AppendChild(commonDir);

                            XmlElement infoDir = CreateImgDir(doc, "info");
                            AppendIntNode(doc, infoDir, "type", 50);
                            carrierDir.AppendChild(infoDir);

                            skillNode.AppendChild(carrierDir);
                            modified = true;
                            Console.WriteLine($"  [added] Carrier skill {FormatSkillKey(carrierId)}");
                        }
                    }
                    else
                    {
                        if (dryRun)
                        {
                            Console.WriteLine($"  [dry-run] Would ensure carrier skill {FormatSkillKey(carrierId)}");
                        }
                        else
                        {
                            bool carrierChanged = EnsureCarrierSkillNode(doc, existingCarrierNode);
                            if (carrierChanged)
                            {
                                modified = true;
                                Console.WriteLine($"  [write] Ensure carrier skill {FormatSkillKey(carrierId)}");
                            }
                            else
                            {
                                Console.WriteLine($"  [skip] Carrier skill {FormatSkillKey(carrierId)} already exists");
                            }
                        }
                    }
                }

                foreach (var sd in list)
                {
                    string idStr = FormatSkillKey(sd.SkillId);
                    var existing = FindSkillNodeBySkillId(skillNode, sd.SkillId);
                    bool overwrite = existing != null;

                    if (dryRun)
                    {
                        Console.WriteLine(overwrite
                            ? $"  [dry-run] Would overwrite skill {idStr} ({sd.Name})"
                            : $"  [dry-run] Would add skill {idStr} ({sd.Name})");
                        continue;
                    }

                    XmlElement skillDir = CreateSkillElement(doc, sd, existing as XmlElement);
                    if (!overwrite)
                    {
                        skillNode.AppendChild(skillDir);
                    }
                    else if (!object.ReferenceEquals(skillDir, existing) && existing != null)
                    {
                        skillNode.ReplaceChild(skillDir, existing);
                    }
                    modified = true;

                    Console.WriteLine(overwrite
                        ? $"  [overwrite] Skill {idStr} ({sd.Name})"
                        : $"  [added] Skill {idStr} ({sd.Name})");
                }

                if (!dryRun && modified)
                {
                    BackupHelper.Backup(xmlPath);
                    SaveXml(doc, xmlPath);
                    Console.WriteLine($"  [saved] {xmlPath}");
                }
            }
        }

        private static int RemoveStaleCarrierSkillNodes(
            XmlNode skillNode,
            int jobId,
            HashSet<int> staleCarrierIds,
            bool dryRun)
        {
            if (skillNode == null || staleCarrierIds == null || staleCarrierIds.Count == 0)
                return 0;

            int removed = 0;
            foreach (int staleCarrierId in staleCarrierIds)
            {
                if (staleCarrierId / 10000 != jobId)
                    continue;

                XmlNode staleNode = FindSkillNodeBySkillId(skillNode, staleCarrierId);
                if (staleNode == null)
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale carrier skill {FormatSkillKey(staleCarrierId)}");
                }
                else
                {
                    skillNode.RemoveChild(staleNode);
                    Console.WriteLine($"  [cleanup] Removed stale carrier skill {FormatSkillKey(staleCarrierId)}");
                }
            }

            return removed;
        }

        private static bool EnsureCarrierSkillNode(XmlDocument doc, XmlElement carrierDir)
        {
            if (doc == null || carrierDir == null)
                return false;

            bool changed = false;

            XmlElement infoDir = FindImgDir(carrierDir, "info") as XmlElement;
            if (infoDir == null)
            {
                infoDir = CreateImgDir(doc, "info");
                carrierDir.AppendChild(infoDir);
                changed = true;
            }
            changed |= EnsureValueNode(doc, infoDir, "int", "type", "50");

            XmlElement commonDir = FindImgDir(carrierDir, "common") as XmlElement;
            if (commonDir == null)
            {
                commonDir = CreateImgDir(doc, "common");
                carrierDir.AppendChild(commonDir);
                changed = true;
            }
            changed |= EnsureValueNode(
                doc,
                commonDir,
                "int",
                "maxLevel",
                Math.Max(1, PathConfig.DefaultSuperSpCarrierMaxLevel).ToString(CultureInfo.InvariantCulture));

            RemoveImgDir(carrierDir, "action");

            return changed;
        }

        private static bool EnsureValueNode(XmlDocument doc, XmlElement parent, string tagName, string name, string value)
        {
            XmlElement existing = FindNamedElement(parent, tagName, name);
            if (existing != null && string.Equals(existing.GetAttribute("value"), value, StringComparison.Ordinal))
                return false;

            ReplaceOrAddValueNode(doc, parent, tagName, name, value);
            return true;
        }

        private static XmlElement CreateSkillElement(XmlDocument doc, SkillDefinition sd, XmlElement existingSkill)
        {
            string idStr = FormatSkillKey(sd.SkillId);

            XmlElement root = TryCreateSkillElementFromCloneSource(doc, sd);
            if (root == null)
                root = existingSkill ?? CreateImgDir(doc, idStr);

            root.SetAttribute("name", idStr);

            ApplySkillOverrides(doc, root, sd);
            EnsureBaselineSkillStructure(doc, root, sd);

            return root;
        }

        private static void EnsureBaselineSkillStructure(XmlDocument doc, XmlElement root, SkillDefinition sd)
        {
            XmlElement infoDir = FindImgDir(root, "info") as XmlElement;
            if (infoDir == null)
            {
                infoDir = CreateImgDir(doc, "info");
                root.AppendChild(infoDir);
            }

            if (FindNamedElement(infoDir, "int", "type") == null)
                AppendIntNode(doc, infoDir, "type", sd.InfoType);

            if (!string.IsNullOrEmpty(sd.Action) && sd.InfoType != 50)
            {
                if (FindImgDir(root, "action") == null)
                {
                    XmlElement actionDir = CreateImgDir(doc, "action");
                    AppendStringNode(doc, actionDir, "0", sd.Action);
                    root.AppendChild(actionDir);
                }
            }

            bool hasCommon = FindImgDir(root, "common") != null;
            bool hasLevel = FindImgDir(root, "level") != null;

            if (!hasCommon && !hasLevel)
            {
                XmlElement commonDir = CreateImgDir(doc, "common");
                AppendIntNode(doc, commonDir, "maxLevel", sd.MaxLevel);
                foreach (var kv in sd.Common)
                {
                    if (kv.Key == "maxLevel")
                        continue;
                    AppendIntOrString(doc, commonDir, kv.Key, kv.Value);
                }
                root.AppendChild(commonDir);
            }
        }

        private static void ApplySkillOverrides(XmlDocument doc, XmlElement root, SkillDefinition sd)
        {
            // To avoid accidental mutation of native skills, merge path does not overwrite info/type.

            if (!string.IsNullOrEmpty(sd.Action) && sd.InfoType != 50)
            {
                MergeActionNode(doc, root, sd.Action);
            }
            else if (sd.InfoType == 50)
            {
                RemoveImgDir(root, "action");
            }

            if (sd.Levels != null && sd.Levels.Count > 0)
            {
                XmlElement levelDir = CreateImgDir(doc, "level");
                foreach (var lvKv in sd.Levels)
                {
                    XmlElement lvNode = CreateImgDir(doc, lvKv.Key.ToString());
                    foreach (var p in lvKv.Value)
                        AppendIntOrString(doc, lvNode, p.Key, p.Value);
                    levelDir.AppendChild(lvNode);
                }
                ReplaceImgDir(root, "level", levelDir);
            }
            if (sd.Common != null && sd.Common.Count > 0)
            {
                XmlElement commonDir = FindImgDir(root, "common") as XmlElement;
                if (commonDir == null)
                {
                    commonDir = CreateImgDir(doc, "common");
                    root.AppendChild(commonDir);
                }
                foreach (var kv in sd.Common)
                {
                    if (kv.Key == "maxLevel") continue;
                    ReplaceOrAddIntOrStringNode(doc, commonDir, kv.Key, kv.Value);
                }
            }

            if (sd.RequiredSkills != null && sd.RequiredSkills.Count > 0)
            {
                XmlElement reqDir = CreateImgDir(doc, "req");
                foreach (var kv in sd.RequiredSkills)
                    AppendIntNode(doc, reqDir, FormatSkillKey(kv.Key), kv.Value);
                ReplaceImgDir(root, "req", reqDir);
            }
        }

        private static XmlElement TryCreateSkillElementFromCloneSource(XmlDocument doc, SkillDefinition sd)
        {
            int sourceId = sd.ResolveCloneSourceSkillId();
            if (sourceId <= 0 || sourceId == sd.SkillId)
                return null;

            var sourceNode = TryLoadSourceSkillNode(sourceId);
            if (sourceNode == null)
                return null;

            return ConvertWzPropertyToXml(doc, sourceNode, includeCanvas: false, forceKeep: true);
        }

        private static WzSubProperty TryLoadSourceSkillNode(int sourceSkillId)
        {
            int sourceJobId = sourceSkillId / 10000;
            string sourceImgPath = PathConfig.GameSkillImg(sourceJobId);
            if (!File.Exists(sourceImgPath))
                return null;

            FileStream fs = null;
            WzImage img = null;
            try
            {
                fs = new FileStream(sourceImgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                img = new WzImage(sourceJobId + ".img", fs, WzMapleVersion.EMS);
                if (!img.ParseImage(true))
                    return null;

                var sourceNode = img.GetFromPath("skill/" + sourceSkillId) as WzSubProperty;
                if (sourceNode == null)
                    return null;

                return (WzSubProperty)sourceNode.DeepClone();
            }
            catch
            {
                return null;
            }
            finally
            {
                try { img?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
            }
        }

        private static XmlElement ConvertWzPropertyToXml(XmlDocument doc, WzImageProperty prop, bool includeCanvas, bool forceKeep)
        {
            if (prop == null)
                return null;

            if (prop is WzSubProperty || prop.PropertyType == WzPropertyType.SubProperty || prop.PropertyType == WzPropertyType.Convex)
            {
                XmlElement dir = CreateImgDir(doc, prop.Name ?? "");
                int added = 0;
                if (prop.WzProperties != null)
                {
                    foreach (WzImageProperty child in prop.WzProperties)
                    {
                        XmlElement childEl = ConvertWzPropertyToXml(doc, child, includeCanvas, false);
                        if (childEl != null)
                        {
                            dir.AppendChild(childEl);
                            added++;
                        }
                    }
                }

                if (!forceKeep && added == 0)
                    return null;

                return dir;
            }

            if (prop is WzCanvasProperty canvas)
            {
                if (!includeCanvas)
                    return null;

                XmlElement el = doc.CreateElement("canvas");
                el.SetAttribute("name", canvas.Name ?? "");
                try
                {
                    if (canvas.PngProperty != null)
                    {
                        el.SetAttribute("width", canvas.PngProperty.Width.ToString(CultureInfo.InvariantCulture));
                        el.SetAttribute("height", canvas.PngProperty.Height.ToString(CultureInfo.InvariantCulture));
                    }
                }
                catch { }

                if (canvas.WzProperties != null)
                {
                    foreach (WzImageProperty child in canvas.WzProperties)
                    {
                        XmlElement childEl = ConvertWzPropertyToXml(doc, child, includeCanvas, false);
                        if (childEl != null)
                            el.AppendChild(childEl);
                    }
                }

                return el;
            }

            if (prop is WzVectorProperty vec)
            {
                XmlElement el = doc.CreateElement("vector");
                el.SetAttribute("name", vec.Name ?? "");
                el.SetAttribute("x", vec.X.Value.ToString(CultureInfo.InvariantCulture));
                el.SetAttribute("y", vec.Y.Value.ToString(CultureInfo.InvariantCulture));
                return el;
            }

            if (prop is WzIntProperty i32)
                return CreateValueElement(doc, "int", i32.Name, i32.Value.ToString(CultureInfo.InvariantCulture));
            if (prop is WzShortProperty i16)
                return CreateValueElement(doc, "short", i16.Name, i16.Value.ToString(CultureInfo.InvariantCulture));
            if (prop is WzLongProperty i64)
                return CreateValueElement(doc, "long", i64.Name, i64.Value.ToString(CultureInfo.InvariantCulture));
            if (prop is WzFloatProperty f)
                return CreateValueElement(doc, "float", f.Name, f.Value.ToString("G", CultureInfo.InvariantCulture));
            if (prop is WzDoubleProperty d)
                return CreateValueElement(doc, "double", d.Name, d.Value.ToString("G", CultureInfo.InvariantCulture));
            if (prop is WzStringProperty s)
                return CreateValueElement(doc, "string", s.Name, s.Value ?? "");
            if (prop is WzUOLProperty u)
                return CreateValueElement(doc, "uol", u.Name, u.Value ?? "");

            return null;
        }

        private static XmlElement CreateValueElement(XmlDocument doc, string tag, string name, string value)
        {
            XmlElement el = doc.CreateElement(tag);
            el.SetAttribute("name", name ?? "");
            el.SetAttribute("value", value ?? "");
            return el;
        }

        private static void ReplaceOrAddValueNode(XmlDocument doc, XmlElement parent, string tagName, string name, string value)
        {
            XmlElement existing = FindNamedElement(parent, tagName, name);
            XmlElement el = CreateValueElement(doc, tagName, name, value);

            if (existing != null)
                parent.ReplaceChild(el, existing);
            else
                parent.AppendChild(el);
        }

        private static XmlElement FindNamedElement(XmlElement parent, string tagName, string name)
        {
            if (parent == null) return null;
            foreach (XmlNode child in parent.ChildNodes)
            {
                if (child.NodeType != XmlNodeType.Element)
                    continue;
                if (!string.Equals(child.Name, tagName, StringComparison.OrdinalIgnoreCase))
                    continue;
                if (child.Attributes?["name"] == null)
                    continue;
                if (child.Attributes["name"].Value == name)
                    return child as XmlElement;
            }
            return null;
        }

        private static void MergeActionNode(XmlDocument doc, XmlElement root, string action)
        {
            XmlElement actionDir = FindImgDir(root, "action") as XmlElement;
            if (actionDir != null)
            {
                XmlElement target = FindNamedElement(actionDir, "string", "0");
                if (target == null)
                    target = FindPreferredActionString(actionDir);

                if (target != null)
                {
                    target.SetAttribute("value", action ?? "");
                }
                else
                {
                    AppendStringNode(doc, actionDir, "0", action);
                }
                return;
            }

            // Some legacy structures store action as a direct string node.
            XmlElement directAction = FindNamedElement(root, "string", "action");
            if (directAction != null)
            {
                directAction.SetAttribute("value", action ?? "");
                return;
            }

            XmlElement newActionDir = CreateImgDir(doc, "action");
            AppendStringNode(doc, newActionDir, "0", action);
            ReplaceImgDir(root, "action", newActionDir);
        }

        private static XmlElement FindPreferredActionString(XmlElement actionDir)
        {
            XmlElement firstString = null;
            XmlElement bestNumeric = null;
            int bestIndex = int.MaxValue;

            if (actionDir == null)
                return null;

            foreach (XmlNode child in actionDir.ChildNodes)
            {
                if (child.NodeType != XmlNodeType.Element || !string.Equals(child.Name, "string", StringComparison.OrdinalIgnoreCase))
                    continue;

                XmlElement el = child as XmlElement;
                string name = el?.Attributes?["name"]?.Value;
                if (string.IsNullOrEmpty(name))
                    continue;

                if (firstString == null)
                    firstString = el;

                if (int.TryParse(name, out int idx) && idx < bestIndex)
                {
                    bestIndex = idx;
                    bestNumeric = el;
                }
            }

            return bestNumeric ?? firstString;
        }

        private static void ReplaceImgDir(XmlElement parent, string dirName, XmlElement replacement)
        {
            RemoveImgDir(parent, dirName);
            if (replacement != null)
                parent.AppendChild(replacement);
        }

        private static void RemoveImgDir(XmlElement parent, string dirName)
        {
            if (parent == null) return;
            var toRemove = new List<XmlNode>();
            foreach (XmlNode child in parent.ChildNodes)
            {
                if (child.NodeType != XmlNodeType.Element || child.Name != "imgdir")
                    continue;
                if (child.Attributes?["name"] == null)
                    continue;
                if (child.Attributes["name"].Value == dirName)
                    toRemove.Add(child);
            }

            foreach (var n in toRemove)
                parent.RemoveChild(n);
        }

        private static string FormatSkillKey(int skillId)
        {
            return skillId <= 9999999
                ? skillId.ToString("D7")
                : skillId.ToString();
        }

        private static XmlNode FindSkillNodeBySkillId(XmlNode parent, int skillId)
        {
            if (parent == null) return null;

            string d7 = FormatSkillKey(skillId);
            var byName = FindImgDir(parent, d7);
            if (byName != null)
                return byName;

            string raw = skillId.ToString();
            byName = FindImgDir(parent, raw);
            if (byName != null)
                return byName;

            foreach (XmlNode child in parent.ChildNodes)
            {
                if (child.NodeType != XmlNodeType.Element || child.Name != "imgdir")
                    continue;
                var attr = child.Attributes?["name"];
                if (attr == null)
                    continue;
                if (int.TryParse(attr.Value, out int parsed) && parsed == skillId)
                    return child;
            }

            return null;
        }

        private static XmlElement CreateImgDir(XmlDocument doc, string name)
        {
            XmlElement el = doc.CreateElement("imgdir");
            el.SetAttribute("name", name);
            return el;
        }

        private static void AppendIntNode(XmlDocument doc, XmlElement parent, string name, int value)
        {
            XmlElement el = doc.CreateElement("int");
            el.SetAttribute("name", name);
            el.SetAttribute("value", value.ToString(CultureInfo.InvariantCulture));
            parent.AppendChild(el);
        }

        private static void AppendStringNode(XmlDocument doc, XmlElement parent, string name, string value)
        {
            XmlElement el = doc.CreateElement("string");
            el.SetAttribute("name", name);
            el.SetAttribute("value", value ?? "");
            parent.AppendChild(el);
        }

        private static void AppendIntOrString(XmlDocument doc, XmlElement parent, string name, string value)
        {
            if (int.TryParse(value, out int iv))
                AppendIntNode(doc, parent, name, iv);
            else
                AppendStringNode(doc, parent, name, value);
        }

        private static void ReplaceOrAddIntOrStringNode(XmlDocument doc, XmlElement parent, string name, string value)
        {
            RemoveScalarValueNodesByName(parent, name);
            AppendIntOrString(doc, parent, name, value);
        }

        private static void RemoveScalarValueNodesByName(XmlElement parent, string name)
        {
            if (parent == null || string.IsNullOrEmpty(name))
                return;

            List<XmlNode> toRemove = new List<XmlNode>();
            foreach (XmlNode child in parent.ChildNodes)
            {
                if (child == null || child.NodeType != XmlNodeType.Element)
                    continue;
                string tag = child.Name;
                if (tag != "int" && tag != "string" && tag != "short" && tag != "long"
                    && tag != "float" && tag != "double" && tag != "uol")
                    continue;
                if (child.Attributes?["name"]?.Value == name)
                    toRemove.Add(child);
            }

            foreach (XmlNode item in toRemove)
                parent.RemoveChild(item);
        }

        private static XmlNode FindImgDir(XmlNode parent, string name)
        {
            if (parent == null) return null;
            foreach (XmlNode child in parent.ChildNodes)
            {
                if (child.NodeType == XmlNodeType.Element
                    && child.Name == "imgdir"
                    && child.Attributes?["name"] != null
                    && child.Attributes["name"].Value == name)
                {
                    return child;
                }
            }
            return null;
        }

        private static void SaveXml(XmlDocument doc, string path)
        {
            EnsureDirectoryForFile(path);
            RemoveWhitespaceNodes(doc.DocumentElement);

            var settings = new XmlWriterSettings
            {
                Encoding = new UTF8Encoding(false),
                Indent = false,
                OmitXmlDeclaration = false
            };
            using (var writer = XmlWriter.Create(path, settings))
            {
                doc.Save(writer);
            }
        }

        private static void EnsureDirectoryForFile(string filePath)
        {
            string dir = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrWhiteSpace(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);
        }

        private static void RemoveWhitespaceNodes(XmlNode node)
        {
            if (node == null)
                return;

            for (int i = node.ChildNodes.Count - 1; i >= 0; i--)
            {
                XmlNode child = node.ChildNodes[i];
                if (child.NodeType == XmlNodeType.Whitespace
                    || child.NodeType == XmlNodeType.SignificantWhitespace
                    || (child.NodeType == XmlNodeType.Text && string.IsNullOrWhiteSpace(child.Value)))
                {
                    node.RemoveChild(child);
                    continue;
                }

                RemoveWhitespaceNodes(child);
            }
        }

        private static XmlDocument LoadOrRecoverXml(int jobId, string xmlPath)
        {
            var doc = new XmlDocument();
            doc.PreserveWhitespace = true;
            try
            {
                doc.Load(xmlPath);
                return doc;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [warn] XML invalid, trying rebuild from {jobId}.img: {ex.Message}");
                if (!TryRebuildFromGameSkillImg(jobId, xmlPath, out string rebuildError))
                {
                    Console.WriteLine($"  [warn] Rebuild from game data failed: {rebuildError}");
                    if (!TryCreateEmptySkillXml(jobId, xmlPath, out string createError))
                        throw new Exception("Failed to recover Skill XML: " + createError, ex);

                    doc = new XmlDocument();
                    doc.PreserveWhitespace = true;
                    doc.Load(xmlPath);
                    Console.WriteLine($"  [recovered] Created empty {jobId}.img.xml");
                    return doc;
                }

                doc = new XmlDocument();
                doc.PreserveWhitespace = true;
                doc.Load(xmlPath);
                Console.WriteLine($"  [recovered] Rebuilt {jobId}.img.xml from game data");
                return doc;
            }
        }

        private static bool TryRebuildFromGameSkillImg(int jobId, string xmlPath, out string error)
        {
            error = "";
            string imgPath = PathConfig.GameSkillImg(jobId);
            if (!File.Exists(imgPath))
            {
                error = "Game skill img not found: " + imgPath;
                return false;
            }

            FileStream fs = null;
            WzImage wzImg = null;
            try
            {
                EnsureDirectoryForFile(xmlPath);
                BackupHelper.Backup(xmlPath);

                fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                wzImg = new WzImage(jobId + ".img", fs, WzMapleVersion.EMS);
                if (!wzImg.ParseImage(true))
                {
                    error = "Failed to parse game skill img";
                    return false;
                }

                var serializer = new WzClassicXmlSerializer(0, LineBreak.None, false);
                serializer.SerializeImage(wzImg, xmlPath);
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
            finally
            {
                try { wzImg?.Dispose(); } catch { }
                try { fs?.Dispose(); } catch { }
            }
        }

        private static bool EnsureXmlFromImg(int jobId, string xmlPath, bool dryRun, bool forceSync)
        {
            if (dryRun)
            {
                if (forceSync || !File.Exists(xmlPath))
                {
                    string imgPath = PathConfig.GameSkillImg(jobId);
                    if (File.Exists(imgPath))
                    {
                        Console.WriteLine(File.Exists(xmlPath)
                            ? $"  [dry-run] Would sync {jobId}.img.xml from game {jobId}.img"
                            : $"  [dry-run] Would rebuild missing {jobId}.img.xml from game {jobId}.img");
                    }
                    else
                    {
                        Console.WriteLine(File.Exists(xmlPath)
                            ? $"  [dry-run] Would keep existing {jobId}.img.xml (game {jobId}.img missing)"
                            : $"  [dry-run] Would create empty {jobId}.img.xml (game {jobId}.img missing)");
                    }
                    return true;
                }

                return true;
            }

            if (forceSync || !File.Exists(xmlPath))
            {
                bool existed = File.Exists(xmlPath);
                if (TryRebuildFromGameSkillImg(jobId, xmlPath, out string err))
                {
                    Console.WriteLine(existed
                        ? $"  [sync] {jobId}.img.xml synced from game data"
                        : $"  [created] {jobId}.img.xml rebuilt from game data");
                    return true;
                }

                if (!File.Exists(xmlPath))
                {
                    if (TryCreateEmptySkillXml(jobId, xmlPath, out string createErr))
                    {
                        Console.WriteLine($"  [created] Empty {jobId}.img.xml (game data missing: {err})");
                        return true;
                    }

                    Console.WriteLine($"  [error] Failed to build/create {jobId}.img.xml: {createErr}");
                    return false;
                }

                Console.WriteLine($"  [warn] Skill XML sync failed, continue with existing file: {err}");
            }

            return true;
        }

        private static bool TryCreateEmptySkillXml(int jobId, string xmlPath, out string error)
        {
            error = "";
            try
            {
                string dir = Path.GetDirectoryName(xmlPath);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                    Directory.CreateDirectory(dir);

                var doc = new XmlDocument();
                doc.PreserveWhitespace = true;

                var declaration = doc.CreateXmlDeclaration("1.0", "UTF-8", "yes");
                doc.AppendChild(declaration);

                XmlElement root = CreateImgDir(doc, jobId + ".img");
                doc.AppendChild(root);
                root.AppendChild(CreateImgDir(doc, "skill"));

                SaveXml(doc, xmlPath);
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }
    }
}
