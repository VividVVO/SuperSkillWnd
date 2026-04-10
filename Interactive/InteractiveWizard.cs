using System;
using System.Collections.Generic;
using System.Text;

namespace SuperSkillTool
{
    /// <summary>
    /// Interactive wizard for adding skills via console prompts.
    /// </summary>
    public static class InteractiveWizard
    {
        public static List<SkillDefinition> Run()
        {
            Console.OutputEncoding = Encoding.UTF8;
            Console.WriteLine();
            Console.WriteLine("================================================================");
            Console.WriteLine("  Super Skill Tool - Interactive Wizard");
            Console.WriteLine("================================================================");
            Console.WriteLine();

            var skills = new List<SkillDefinition>();
            bool addMore = true;

            while (addMore)
            {
                var sd = PromptOneSkill(skills.Count + 1);
                if (sd != null)
                    skills.Add(sd);

                Console.Write("\nAdd another skill? (y/n) [n]: ");
                string ans = Console.ReadLine();
                addMore = !string.IsNullOrEmpty(ans)
                    && (ans.Trim().ToLower() == "y" || ans.Trim().ToLower() == "yes");
            }

            if (skills.Count == 0)
            {
                Console.WriteLine("No skills defined. Exiting.");
                return null;
            }

            Console.WriteLine($"\n{skills.Count} skill(s) defined. Proceeding...\n");
            return skills;
        }

        private static SkillDefinition PromptOneSkill(int index)
        {
            Console.WriteLine($"--- Skill #{index} ---");
            Console.WriteLine();

            // Skill ID
            int skillId = PromptInt("Skill ID (e.g. 1001070): ", 0);
            if (skillId <= 0)
            {
                Console.WriteLine("Invalid skill ID.");
                return null;
            }

            // Name
            string name = PromptString("Name: ", "");
            if (string.IsNullOrEmpty(name))
            {
                Console.WriteLine("Name is required.");
                return null;
            }

            // Desc
            string desc = PromptString("Description (use \\n for newline): ", "");

            // Type
            Console.WriteLine();
            Console.WriteLine("Available types:");
            Console.WriteLine("  1. active_melee   - Melee attack skill");
            Console.WriteLine("  2. active_ranged  - Ranged attack skill");
            Console.WriteLine("  3. active_magic   - Magic attack skill");
            Console.WriteLine("  4. buff           - Buff skill");
            Console.WriteLine("  5. passive        - Passive skill");
            Console.WriteLine("  6. newbie_level   - Level-based beginner skill");
            string typeStr = PromptString("Type (1-6 or name) [1]: ", "1");
            string type = ResolveType(typeStr);

            // MaxLevel
            int defaultMax = SkillTemplate.Get(type).MaxLevel;
            int maxLevel = PromptInt($"Max Level [{defaultMax}]: ", defaultMax);

            // SuperSpCost
            int spCost = PromptInt("Super SP Cost [1]: ", 1);

            // Tab
            string defaultTab = (type == "passive") ? "passive" : (type == "buff") ? "buff" : "active";
            string tab = PromptString($"Tab (active/buff/passive) [{defaultTab}]: ", defaultTab);

            // Icon
            string icon = PromptString("Icon path (relative to tool dir, or empty for placeholder): ", "");

            // Proxy
            int defaultProxy = SkillTemplate.Get(type).ProxySkillId;
            int proxyId = 0;
            if (type != "passive")
                proxyId = PromptInt($"Proxy Skill ID [{defaultProxy}]: ", defaultProxy);

            // Flags
            bool hideNative = PromptBool("Hide from native skill window? [Y]: ", true);
            bool injectNative = PromptBool("Inject to native skill window? [N]: ", false);

            // Build definition
            var sd = new SkillDefinition();
            sd.SkillId = skillId;
            sd.Name = name;
            sd.Desc = desc;
            sd.Type = type;
            sd.Tab = tab;
            sd.MaxLevel = maxLevel;
            sd.SuperSpCost = spCost;
            sd.Icon = icon;
            sd.ProxySkillId = proxyId;
            sd.HideFromNativeSkillWnd = hideNative;
            sd.InjectToNative = injectNative;

            // hLevels
            Console.Write("Add h-level descriptions? (y/n) [n]: ");
            string hAns = Console.ReadLine();
            if (!string.IsNullOrEmpty(hAns) && hAns.Trim().ToLower().StartsWith("y"))
            {
                Console.WriteLine("Enter h-level descriptions (empty key to stop):");
                while (true)
                {
                    string hKey = PromptString("  Key (e.g. h1, h5, h10): ", "");
                    if (string.IsNullOrEmpty(hKey)) break;
                    string hVal = PromptString($"  {hKey} text: ", "");
                    if (!string.IsNullOrEmpty(hVal))
                        sd.HLevels[hKey] = hVal;
                }
            }

            // Apply template
            var tpl = SkillTemplate.Get(sd.Type);
            sd.ApplyTemplate(tpl);

            Console.WriteLine();
            Console.WriteLine($"  Skill defined: [{sd.SkillId}] {sd.Name} ({sd.Type})");
            return sd;
        }

        // ── Prompt helpers ─────────────────────────────────────

        private static string PromptString(string prompt, string defaultValue)
        {
            Console.Write(prompt);
            string input = Console.ReadLine();
            if (string.IsNullOrEmpty(input)) return defaultValue;
            return input.Trim();
        }

        private static int PromptInt(string prompt, int defaultValue)
        {
            Console.Write(prompt);
            string input = Console.ReadLine();
            if (string.IsNullOrEmpty(input)) return defaultValue;
            if (int.TryParse(input.Trim(), out int val)) return val;
            return defaultValue;
        }

        private static bool PromptBool(string prompt, bool defaultValue)
        {
            Console.Write(prompt);
            string input = Console.ReadLine();
            if (string.IsNullOrEmpty(input)) return defaultValue;
            string lower = input.Trim().ToLower();
            if (lower == "y" || lower == "yes" || lower == "true" || lower == "1") return true;
            if (lower == "n" || lower == "no" || lower == "false" || lower == "0") return false;
            return defaultValue;
        }

        private static string ResolveType(string input)
        {
            if (string.IsNullOrEmpty(input)) return "active_melee";
            string trimmed = input.Trim().ToLower();
            switch (trimmed)
            {
                case "1": return "active_melee";
                case "2": return "active_ranged";
                case "3": return "active_magic";
                case "4": return "buff";
                case "5": return "passive";
                case "6": return "newbie_level";
                case "active_melee":
                case "active_ranged":
                case "active_magic":
                case "buff":
                case "passive":
                case "newbie_level":
                    return trimmed;
                default:
                    Console.WriteLine($"  [warn] Unknown type '{input}', defaulting to active_melee.");
                    return "active_melee";
            }
        }
    }
}
