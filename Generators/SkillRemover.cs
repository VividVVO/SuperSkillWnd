using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Xml;

namespace SuperSkillTool
{
    /// <summary>
    /// Removes a skill ID from all config/data files.
    /// Always creates backups before modification.
    /// </summary>
    public static class SkillRemover
    {
        public static void Remove(int skillId, bool dryRun)
        {
            Console.WriteLine($"\n================================================================");
            Console.WriteLine($"  Removing skill {skillId} ({PathConfig.SkillKey(skillId)}) from all files");
            if (dryRun) Console.WriteLine("  *** DRY RUN - no changes ***");
            Console.WriteLine($"================================================================\n");

            int jobId = skillId / 10000;

            // 1. Server Skill XML
            RemoveFromServerSkillXml(skillId, jobId, dryRun);

            // 2. Server String XML
            RemoveFromServerStringXml(skillId, dryRun);

            // 3. Config JSONs (array-based)
            RemoveFromJsonArrayFile(PathConfig.SuperSkillsJson,
                "super_skills.json", "skills", skillId, dryRun);

            RemoveFromJsonArrayFile(PathConfig.CustomSkillRoutesJson,
                "custom_skill_routes.json", "routes", skillId, dryRun);

            RemoveFromJsonArrayFile(PathConfig.NativeSkillInjectionsJson,
                "native_skill_injections.json", "skills", skillId, dryRun);

            RemoveFromJsonArrayFile(PathConfig.SuperSkillsServerJson,
                "super_skills_server.json", "skills", skillId, dryRun);

            Console.WriteLine();
            Console.WriteLine("================================================================");
            if (dryRun)
                Console.WriteLine("  DRY RUN COMPLETE - no files modified.");
            else
                Console.WriteLine("  REMOVAL COMPLETE.");
            Console.WriteLine("================================================================");
        }

        private static void RemoveFromServerSkillXml(int skillId, int jobId, bool dryRun)
        {
            string path = PathConfig.ServerSkillXml(jobId);
            string label = $"Server Skill XML ({jobId:D3}.img.xml)";

            if (!File.Exists(path))
            {
                Console.WriteLine($"  [skip] {label}: file not found");
                return;
            }

            var doc = new XmlDocument();
            doc.PreserveWhitespace = true;
            doc.Load(path);

            XmlNode skillRoot = doc.SelectSingleNode("//imgdir[@name='skill']");
            if (skillRoot == null)
            {
                Console.WriteLine($"  [skip] {label}: no skill node");
                return;
            }

            string idStr = PathConfig.SkillKey(skillId);
            var targets = FindChildNodesBySkillId(skillRoot, skillId);

            if (targets.Count == 0)
            {
                Console.WriteLine($"  [skip] {label}: skill not found");
                return;
            }

            if (dryRun)
            {
                Console.WriteLine($"  [dry-run] Would remove {targets.Count} node(s) for {idStr} from {label}");
                return;
            }

            BackupHelper.Backup(path);
            foreach (XmlNode target in targets)
                skillRoot.RemoveChild(target);
            SaveXml(doc, path);
            Console.WriteLine($"  [removed] {targets.Count} node(s) for {idStr} from {label}");
        }

        private static void RemoveFromServerStringXml(int skillId, bool dryRun)
        {
            string path = PathConfig.ServerStringXml;
            string label = "Server String XML";

            if (!File.Exists(path))
            {
                Console.WriteLine($"  [skip] {label}: file not found");
                return;
            }

            var doc = new XmlDocument();
            doc.PreserveWhitespace = true;
            doc.Load(path);

            XmlNode root = doc.DocumentElement;
            string idStr = PathConfig.SkillKey(skillId);
            var targets = FindChildNodesBySkillId(root, skillId);

            if (targets.Count == 0)
            {
                Console.WriteLine($"  [skip] {label}: skill not found");
                return;
            }

            if (dryRun)
            {
                Console.WriteLine($"  [dry-run] Would remove {targets.Count} node(s) for {idStr} from {label}");
                return;
            }

            BackupHelper.Backup(path);
            foreach (XmlNode target in targets)
                root.RemoveChild(target);
            SaveXml(doc, path);
            Console.WriteLine($"  [removed] {targets.Count} node(s) for {idStr} from {label}");
        }

        /// <summary>
        /// Removes a top-level key matching skillId from a JSON object file.
        /// Used for .img.json files where skills are top-level keys.
        /// </summary>
        private static void RemoveFromJsonFile(string path, string label, string skillIdStr, bool dryRun)
        {
            if (!File.Exists(path))
            {
                Console.WriteLine($"  [skip] {label}: file not found");
                return;
            }

            string content = TextFileHelper.ReadAllTextAuto(path);
            if (!content.Contains("\"" + skillIdStr + "\""))
            {
                Console.WriteLine($"  [skip] {label}: skill not found");
                return;
            }

            if (dryRun)
            {
                Console.WriteLine($"  [dry-run] Would remove {skillIdStr} from {label}");
                return;
            }

            // Parse, remove key, re-serialize
            try
            {
                var obj = SimpleJson.ParseObject(content);
                if (obj.ContainsKey(skillIdStr))
                {
                    obj.Remove(skillIdStr);
                    BackupHelper.Backup(path);
                    File.WriteAllText(path, SimpleJson.Serialize(obj), new UTF8Encoding(false));
                    Console.WriteLine($"  [removed] {skillIdStr} from {label}");
                }
                else
                {
                    // Might be nested under a "skill" key
                    bool removed = false;
                    foreach (var kv in obj)
                    {
                        if (kv.Value is Dictionary<string, object> sub && sub.ContainsKey(skillIdStr))
                        {
                            sub.Remove(skillIdStr);
                            BackupHelper.Backup(path);
                            File.WriteAllText(path, SimpleJson.Serialize(obj), new UTF8Encoding(false));
                            Console.WriteLine($"  [removed] {skillIdStr} from {label} (under '{kv.Key}')");
                            removed = true;
                            break;
                        }
                    }
                    if (!removed)
                        Console.WriteLine($"  [skip] {label}: key found in text but not as removable entry");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [error] {label}: {ex.Message}");
            }
        }

        /// <summary>
        /// Removes an entry with matching skillId from a JSON array within a file.
        /// Used for super_skills.json, custom_skill_routes.json, etc.
        /// </summary>
        private static void RemoveFromJsonArrayFile(string path, string label, string arrayName, int skillId, bool dryRun)
        {
            if (!File.Exists(path))
            {
                Console.WriteLine($"  [skip] {label}: file not found");
                return;
            }

            string content = TextFileHelper.ReadAllTextAuto(path);
            string idStr = skillId.ToString();
            if (!content.Contains(idStr))
            {
                Console.WriteLine($"  [skip] {label}: skill not found");
                return;
            }

            if (dryRun)
            {
                Console.WriteLine($"  [dry-run] Would remove {idStr} from {label}");
                return;
            }

            try
            {
                var root = SimpleJson.ParseObject(content);
                var arr = SimpleJson.GetArray(root, arrayName);
                if (arr == null)
                {
                    Console.WriteLine($"  [skip] {label}: array '{arrayName}' not found");
                    return;
                }

                int removed = 0;
                for (int i = arr.Count - 1; i >= 0; i--)
                {
                    if (arr[i] is Dictionary<string, object> entry)
                    {
                        int entryId = SimpleJson.GetInt(entry, "skillId");
                        if (entryId == skillId)
                        {
                            arr.RemoveAt(i);
                            removed++;
                        }
                    }
                }

                if (removed > 0)
                {
                    BackupHelper.Backup(path);
                    File.WriteAllText(path, SimpleJson.Serialize(root), new UTF8Encoding(false));
                    Console.WriteLine($"  [removed] {removed} entry(s) for {idStr} from {label}");
                }
                else
                {
                    Console.WriteLine($"  [skip] {label}: no matching array entry");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [error] {label}: {ex.Message}");
            }
        }

        private static void SaveXml(XmlDocument doc, string path)
        {
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

        private static List<XmlNode> FindChildNodesBySkillId(XmlNode parent, int skillId)
        {
            var result = new List<XmlNode>();
            if (parent == null || skillId <= 0)
                return result;

            var expected = new HashSet<string>(PathConfig.SkillKeyCandidates(skillId), StringComparer.OrdinalIgnoreCase);
            foreach (XmlNode child in parent.ChildNodes)
            {
                if (child.NodeType != XmlNodeType.Element || child.Attributes["name"] == null)
                    continue;

                string name = child.Attributes["name"].Value;
                if (expected.Contains(name) || (int.TryParse(name, out int parsed) && parsed == skillId))
                    result.Add(child);
            }

            return result;
        }
    }
}
