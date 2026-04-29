using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Xml;
using MapleLib.WzLib;
using MapleLib.WzLib.Serializer;

namespace SuperSkillTool
{
    /// <summary>
    /// Modifies the server-side String.wz/Skill.img.xml to add name/desc/h entries.
    /// </summary>
    public static class ServerStringXmlGenerator
    {
        public static void Remove(List<SkillDefinition> skills, bool dryRun)
        {
            string xmlPath = PathConfig.ServerStringXml;
            Console.WriteLine($"\n[ServerStringXml-Delete] Processing {xmlPath}");

            if (!EnsureXmlFromImg(xmlPath, dryRun, forceSync: !dryRun))
                return;

            if (!File.Exists(xmlPath))
            {
                Console.WriteLine($"  [skip] Not found: {xmlPath}");
                return;
            }

            var doc = LoadOrRecoverXml(xmlPath);

            XmlNode root = doc.DocumentElement;
            if (root == null) { Console.WriteLine("  [skip] Empty XML."); return; }

            int count = 0;
            foreach (var sd in skills)
            {
                var existing = FindStringEntryBySkillId(root, sd.SkillId);
                if (existing == null) { Console.WriteLine($"  [skip] {FormatSkillKey(sd.SkillId)} not found"); continue; }
                if (dryRun) { Console.WriteLine($"  [dry-run] Would remove {existing.Attributes?["name"]?.Value}"); continue; }
                root.RemoveChild(existing);
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

        public static void Generate(List<SkillDefinition> skills, bool dryRun)
        {
            string xmlPath = PathConfig.ServerStringXml;
            Console.WriteLine($"\n[ServerStringXml] Processing {xmlPath}");

            if (!EnsureXmlFromImg(xmlPath, dryRun, forceSync: !dryRun))
                return;

            if (!File.Exists(xmlPath))
            {
                Console.WriteLine($"  [error] File not found after sync: {xmlPath}");
                return;
            }

            var doc = LoadOrRecoverXml(xmlPath);

            // The root element should be the top-level imgdir.
            XmlNode root = doc.DocumentElement;
            if (root == null)
            {
                Console.WriteLine("  [error] Empty XML document.");
                return;
            }

            bool modified = false;
            var staleCarrierIds = CarrierSkillHelper.GetStaleCarrierIds();
            int staleRemoved = RemoveStaleCarrierStringEntries(root, staleCarrierIds, dryRun);
            if (!dryRun && staleRemoved > 0)
                modified = true;

            // Ensure carrier skill has a string entry
            int carrierId = PathConfig.DefaultSuperSpCarrierSkillId;
            if (carrierId > 0)
            {
                string carrierStr = FormatSkillKey(carrierId);
                XmlElement carrierEntry = FindStringEntryBySkillId(root, carrierId) as XmlElement;
                if (carrierEntry == null)
                {
                    if (!dryRun)
                    {
                        carrierEntry = doc.CreateElement("imgdir");
                        carrierEntry.SetAttribute("name", carrierStr);
                        EnsureCarrierStringEntry(doc, carrierEntry);
                        root.AppendChild(carrierEntry);
                        modified = true;
                        Console.WriteLine($"  [added] Carrier string entry {carrierStr}");
                    }
                    else
                    {
                        Console.WriteLine($"  [dry-run] Would add carrier string entry {carrierStr}");
                    }
                }
                else if (!dryRun)
                {
                    if (EnsureCarrierStringEntry(doc, carrierEntry))
                    {
                        modified = true;
                        Console.WriteLine($"  [write] Ensure carrier string entry {carrierStr}");
                    }
                }
            }

            foreach (var sd in skills)
            {
                string idStr = FormatSkillKey(sd.SkillId);
                var existing = FindStringEntryBySkillId(root, sd.SkillId);
                bool overwrite = existing != null;

                if (dryRun)
                {
                    Console.WriteLine(overwrite
                        ? $"  [dry-run] Would overwrite string entry {idStr} ({sd.Name})"
                        : $"  [dry-run] Would add string entry {idStr} ({sd.Name})");
                    continue;
                }

                if (overwrite)
                    root.RemoveChild(existing);

                // Build entry
                XmlElement entry = doc.CreateElement("imgdir");
                entry.SetAttribute("name", idStr);

                // name
                AppendStringNode(doc, entry, "name", sd.Name);

                // desc
                if (!string.IsNullOrEmpty(sd.Desc))
                    AppendStringNode(doc, entry, "desc", sd.Desc);

                // h template text (with #mpCon, #damage placeholders)
                if (!string.IsNullOrEmpty(sd.H))
                    AppendStringNode(doc, entry, "h", sd.H);

                // pdesc
                if (!string.IsNullOrEmpty(sd.PDesc))
                    AppendStringNode(doc, entry, "pdesc", sd.PDesc);

                // ph
                if (!string.IsNullOrEmpty(sd.Ph))
                    AppendStringNode(doc, entry, "ph", sd.Ph);

                // hLevels
                foreach (var kv in sd.HLevels)
                {
                    AppendStringNode(doc, entry, kv.Key, kv.Value);
                }

                root.AppendChild(entry);
                modified = true;

                Console.WriteLine(overwrite
                    ? $"  [overwrite] String entry {idStr} ({sd.Name})"
                    : $"  [added] String entry {idStr} ({sd.Name})");
            }

            if (!dryRun && modified)
            {
                BackupHelper.Backup(xmlPath);
                SaveXml(doc, xmlPath);
                Console.WriteLine($"  [saved] {xmlPath}");
            }
        }

        private static int RemoveStaleCarrierStringEntries(
            XmlNode root,
            HashSet<int> staleCarrierIds,
            bool dryRun)
        {
            if (root == null || staleCarrierIds == null || staleCarrierIds.Count == 0)
                return 0;

            int removed = 0;
            foreach (int staleCarrierId in staleCarrierIds)
            {
                XmlNode staleEntry = FindStringEntryBySkillId(root, staleCarrierId);
                if (staleEntry == null)
                    continue;

                removed++;
                if (dryRun)
                {
                    Console.WriteLine($"  [dry-run] Would remove stale carrier string entry {FormatSkillKey(staleCarrierId)}");
                }
                else
                {
                    root.RemoveChild(staleEntry);
                    Console.WriteLine($"  [cleanup] Removed stale carrier string entry {FormatSkillKey(staleCarrierId)}");
                }
            }

            return removed;
        }

        private static void AppendStringNode(XmlDocument doc, XmlElement parent, string name, string value)
        {
            XmlElement el = doc.CreateElement("string");
            el.SetAttribute("name", name);
            el.SetAttribute("value", value);
            parent.AppendChild(el);
        }

        private static bool EnsureCarrierStringEntry(XmlDocument doc, XmlElement entry)
        {
            if (doc == null || entry == null)
                return false;

            bool changed = false;
            changed |= EnsureStringNode(doc, entry, "name", "Super SP");
            changed |= EnsureStringNode(doc, entry, "desc", "超级SP载体技能。");
            changed |= EnsureStringNode(doc, entry, "h1", "仅用于承载超级SP，不在技能栏显示。");
            changed |= EnsureIntNode(doc, entry, "_superSkill", 1);
            return changed;
        }

        private static bool EnsureStringNode(XmlDocument doc, XmlElement parent, string name, string value)
        {
            return EnsureValueNode(doc, parent, "string", name, value ?? "");
        }

        private static bool EnsureIntNode(XmlDocument doc, XmlElement parent, string name, int value)
        {
            return EnsureValueNode(doc, parent, "int", name, value.ToString());
        }

        private static bool EnsureValueNode(XmlDocument doc, XmlElement parent, string tagName, string name, string value)
        {
            XmlElement existing = FindNamedElement(parent, tagName, name);
            if (existing != null && string.Equals(existing.GetAttribute("value"), value, StringComparison.Ordinal))
                return false;

            XmlElement replacement = doc.CreateElement(tagName);
            replacement.SetAttribute("name", name);
            replacement.SetAttribute("value", value ?? "");
            if (existing != null)
                parent.ReplaceChild(replacement, existing);
            else
                parent.AppendChild(replacement);
            return true;
        }

        private static XmlElement FindNamedElement(XmlElement parent, string tagName, string name)
        {
            if (parent == null || string.IsNullOrEmpty(tagName) || string.IsNullOrEmpty(name))
                return null;

            foreach (XmlNode child in parent.ChildNodes)
            {
                if (child.NodeType != XmlNodeType.Element)
                    continue;
                if (!string.Equals(child.Name, tagName, StringComparison.OrdinalIgnoreCase))
                    continue;
                if (child.Attributes?["name"]?.Value == name)
                    return child as XmlElement;
            }
            return null;
        }

        private static XmlNode FindImgDir(XmlNode parent, string name)
        {
            if (parent == null) return null;
            foreach (XmlNode child in parent.ChildNodes)
            {
                if (child.NodeType == XmlNodeType.Element
                    && child.Name == "imgdir"
                    && child.Attributes["name"] != null
                    && child.Attributes["name"].Value == name)
                {
                    return child;
                }
            }
            return null;
        }

        private static string FormatSkillKey(int skillId)
        {
            return PathConfig.SkillKey(skillId);
        }

        private static XmlNode FindStringEntryBySkillId(XmlNode parent, int skillId)
        {
            string d7 = FormatSkillKey(skillId);
            var found = FindImgDir(parent, d7);
            if (found != null) return found;

            string raw = skillId.ToString();
            found = FindImgDir(parent, raw);
            if (found != null) return found;

            if (parent == null) return null;
            foreach (XmlNode child in parent.ChildNodes)
            {
                if (child.NodeType != XmlNodeType.Element || child.Name != "imgdir")
                    continue;
                var nameAttr = child.Attributes?["name"]?.Value;
                if (string.IsNullOrEmpty(nameAttr))
                    continue;
                if (int.TryParse(nameAttr, out int parsed) && parsed == skillId)
                    return child;
            }

            return null;
        }

        private static XmlDocument LoadOrRecoverXml(string xmlPath)
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
                Console.WriteLine($"  [warn] XML invalid, trying rebuild from Skill.img: {ex.Message}");
                if (!TryRebuildFromGameStringImg(xmlPath, out string rebuildError))
                {
                    throw new Exception("Failed to recover String XML: " + rebuildError, ex);
                }

                doc = new XmlDocument();
                doc.PreserveWhitespace = true;
                doc.Load(xmlPath);
                Console.WriteLine("  [recovered] Rebuilt String XML from game Skill.img");
                return doc;
            }
        }

        private static bool TryRebuildFromGameStringImg(string xmlPath, out string error)
        {
            error = "";
            string imgPath = PathConfig.GameStringSkillImg;
            if (!File.Exists(imgPath))
            {
                error = "Game String Skill.img not found: " + imgPath;
                return false;
            }

            FileStream fs = null;
            WzImage wzImg = null;
            try
            {
                EnsureDirectoryForFile(xmlPath);
                BackupHelper.Backup(xmlPath);

                fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                wzImg = new WzImage("Skill.img", fs, WzMapleVersion.EMS);
                if (!wzImg.ParseImage(true))
                {
                    error = "Failed to parse game String Skill.img";
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

        private static bool EnsureXmlFromImg(string xmlPath, bool dryRun, bool forceSync)
        {
            if (dryRun)
            {
                if (forceSync)
                {
                    Console.WriteLine(File.Exists(xmlPath)
                        ? "  [dry-run] Would sync String XML from game Skill.img"
                        : "  [dry-run] Would rebuild missing String XML from game Skill.img");
                }
                return File.Exists(xmlPath);
            }

            if (forceSync || !File.Exists(xmlPath))
            {
                bool existed = File.Exists(xmlPath);
                if (TryRebuildFromGameStringImg(xmlPath, out string err))
                {
                    Console.WriteLine(existed
                        ? "  [sync] String XML synced from game Skill.img"
                        : "  [created] String XML rebuilt from game Skill.img");
                    return true;
                }

                if (!File.Exists(xmlPath))
                {
                    Console.WriteLine($"  [error] Failed to build String XML from game Skill.img: {err}");
                    return false;
                }

                Console.WriteLine($"  [warn] String XML sync failed, continue with existing file: {err}");
            }

            return true;
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
    }
}
