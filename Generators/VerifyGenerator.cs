using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Xml;

namespace SuperSkillTool
{
    /// <summary>
    /// Verifies that a skill ID exists consistently across all config files.
    /// </summary>
    public static class VerifyGenerator
    {
        public static void Verify(int skillId)
        {
            Console.WriteLine($"\n================================================================");
            Console.WriteLine($"  Verifying skill {skillId} across all files");
            Console.WriteLine($"================================================================\n");

            int found = 0;
            int missing = 0;
            int jobId = skillId / 10000;

            // 1. Server Skill XML
            found += CheckServerSkillXml(skillId, jobId, ref missing);

            // 2. Server String XML
            found += CheckServerStringXml(skillId, ref missing);

            // 3. DLL Skill IMG JSON
            found += CheckFileContains(PathConfig.DllSkillImgJson(jobId),
                "DLL Skill IMG JSON", skillId, ref missing);

            // 4. DLL String JSON
            found += CheckFileContains(PathConfig.DllStringJson,
                "DLL String JSON", skillId, ref missing);

            // 5. super_skills.json
            found += CheckFileContains(PathConfig.SuperSkillsJson,
                "super_skills.json", skillId, ref missing);

            // 6. custom_skill_routes.json
            found += CheckFileContains(PathConfig.CustomSkillRoutesJson,
                "custom_skill_routes.json", skillId, ref missing);

            // 7. native_skill_injections.json
            CheckFileContainsOptional(PathConfig.NativeSkillInjectionsJson,
                "native_skill_injections.json", skillId);

            // 8. super_skills_server.json
            found += CheckFileContains(PathConfig.SuperSkillsServerJson,
                "super_skills_server.json", skillId, ref missing);

            Console.WriteLine();
            Console.WriteLine("────────────────────────────────────────");
            Console.WriteLine($"  Result: {found} found, {missing} missing");
            if (missing == 0)
                Console.WriteLine("  Status: ALL CONSISTENT");
            else
                Console.WriteLine("  Status: INCOMPLETE - some files missing this skill");
            Console.WriteLine("────────────────────────────────────────");
        }

        public static void VerifyAll()
        {
            Console.WriteLine("\n================================================================");
            Console.WriteLine("  Scanning all registered super skills...");
            Console.WriteLine("================================================================\n");

            string path = PathConfig.SuperSkillsJson;
            if (!File.Exists(path))
            {
                Console.WriteLine($"  [error] {path} not found.");
                return;
            }

            string content = TextFileHelper.ReadAllTextAuto(path);
            var root = SimpleJson.ParseObject(content);
            var skills = SimpleJson.GetArray(root, "skills");
            if (skills == null || skills.Count == 0)
            {
                Console.WriteLine("  No skills registered.");
                return;
            }

            foreach (var item in skills)
            {
                if (item is Dictionary<string, object> obj)
                {
                    int id = SimpleJson.GetInt(obj, "skillId");
                    if (id > 0) Verify(id);
                }
            }
        }

        private static int CheckServerSkillXml(int skillId, int jobId, ref int missing)
        {
            string path = PathConfig.ServerSkillXml(jobId);
            string label = $"Server Skill XML ({jobId:D3}.img.xml)";
            if (!File.Exists(path))
            {
                Console.WriteLine($"  [MISSING] {label}: file not found");
                missing++;
                return 0;
            }

            var doc = new XmlDocument();
            doc.Load(path);
            XmlNode skillRoot = doc.SelectSingleNode("//imgdir[@name='skill']");
            if (skillRoot == null)
            {
                Console.WriteLine($"  [ERROR]   {label}: no <imgdir name='skill'> node");
                missing++;
                return 0;
            }

            string idStr = skillId.ToString();
            foreach (XmlNode child in skillRoot.ChildNodes)
            {
                if (child.NodeType == XmlNodeType.Element
                    && child.Attributes["name"] != null
                    && child.Attributes["name"].Value == idStr)
                {
                    Console.WriteLine($"  [OK]      {label}");
                    return 1;
                }
            }

            Console.WriteLine($"  [MISSING] {label}: skill node not found");
            missing++;
            return 0;
        }

        private static int CheckServerStringXml(int skillId, ref int missing)
        {
            string path = PathConfig.ServerStringXml;
            string label = "Server String XML";
            if (!File.Exists(path))
            {
                Console.WriteLine($"  [MISSING] {label}: file not found");
                missing++;
                return 0;
            }

            var doc = new XmlDocument();
            doc.Load(path);
            string idStr = skillId.ToString();
            XmlNode root = doc.DocumentElement;
            if (root != null)
            {
                foreach (XmlNode child in root.ChildNodes)
                {
                    if (child.NodeType == XmlNodeType.Element
                        && child.Attributes["name"] != null
                        && child.Attributes["name"].Value == idStr)
                    {
                        Console.WriteLine($"  [OK]      {label}");
                        return 1;
                    }
                }
            }

            Console.WriteLine($"  [MISSING] {label}: string entry not found");
            missing++;
            return 0;
        }

        private static int CheckFileContains(string path, string label, int skillId, ref int missing)
        {
            if (!File.Exists(path))
            {
                Console.WriteLine($"  [MISSING] {label}: file not found");
                missing++;
                return 0;
            }

            string content = TextFileHelper.ReadAllTextAuto(path);
            bool found = false;
            try
            {
                var root = SimpleJson.ParseObject(content);
                found = ContainsSkillId(root, skillId);
            }
            catch
            {
                string needle = "\"" + skillId.ToString() + "\"";
                found = content.Contains(needle);
            }

            if (found)
            {
                Console.WriteLine($"  [OK]      {label}");
                return 1;
            }

            Console.WriteLine($"  [MISSING] {label}: skill not found in file");
            missing++;
            return 0;
        }

        private static void CheckFileContainsOptional(string path, string label, int skillId)
        {
            if (!File.Exists(path))
            {
                Console.WriteLine($"  [SKIP]    {label}: file not found (optional)");
                return;
            }

            string content = TextFileHelper.ReadAllTextAuto(path);
            bool found = false;
            try
            {
                var root = SimpleJson.ParseObject(content);
                found = ContainsSkillId(root, skillId);
            }
            catch
            {
                string needle = "\"" + skillId.ToString() + "\"";
                found = content.Contains(needle);
            }

            if (found)
                Console.WriteLine($"  [OK]      {label} (optional)");
            else
                Console.WriteLine($"  [SKIP]    {label}: not injected (optional)");
        }

        private static bool ContainsSkillId(Dictionary<string, object> obj, int skillId)
        {
            if (obj == null) return false;
            string idKey = skillId.ToString();

            foreach (var kv in obj)
            {
                if (kv.Key == idKey) return true;
                if (kv.Key == "skillId" && ValueMatchesSkillId(kv.Value, skillId)) return true;

                if (kv.Value is Dictionary<string, object> childObj)
                {
                    if (ContainsSkillId(childObj, skillId)) return true;
                }
                else if (kv.Value is List<object> arr)
                {
                    if (ContainsSkillId(arr, skillId)) return true;
                }
            }
            return false;
        }

        private static bool ContainsSkillId(List<object> arr, int skillId)
        {
            if (arr == null) return false;
            foreach (var item in arr)
            {
                if (item is Dictionary<string, object> obj)
                {
                    if (ContainsSkillId(obj, skillId)) return true;
                }
                else if (item is List<object> childArr)
                {
                    if (ContainsSkillId(childArr, skillId)) return true;
                }
                else if (ValueMatchesSkillId(item, skillId))
                {
                    return true;
                }
            }
            return false;
        }

        private static bool ValueMatchesSkillId(object value, int skillId)
        {
            if (value is long l) return l == skillId;
            if (value is int i) return i == skillId;
            if (value is double d) return (int)d == skillId;
            if (value is string s && int.TryParse(s, out int parsed)) return parsed == skillId;
            return false;
        }
    }
}
