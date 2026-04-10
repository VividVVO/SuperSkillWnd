using System;
using System.Collections.Generic;

namespace SuperSkillTool
{
    /// <summary>
    /// Six built-in skill templates.
    /// </summary>
    public class SkillTemplate
    {
        public string TypeName;
        public int InfoType;
        public string Action;
        public string PacketRoute;
        public int ProxySkillId;
        public int MaxLevel;
        public Dictionary<string, string> DefaultCommon;

        // For newbie_level template
        public Func<Dictionary<int, Dictionary<string, string>>> BuildDefaultLevels;

        private static readonly Dictionary<string, SkillTemplate> Templates
            = new Dictionary<string, SkillTemplate>(StringComparer.OrdinalIgnoreCase);

        static SkillTemplate()
        {
            Register(new SkillTemplate
            {
                TypeName = "active_melee",
                InfoType = 1,
                Action = "swingO1",
                PacketRoute = "close_range",
                ProxySkillId = 1301008,
                MaxLevel = 20,
                DefaultCommon = new Dictionary<string, string>
                {
                    { "damage", "120+10*x" },
                    { "mpCon", "10+2*x" },
                    { "mobCount", "1" },
                    { "attackCount", "1" },
                    { "range", "350" }
                }
            });

            Register(new SkillTemplate
            {
                TypeName = "active_ranged",
                InfoType = 1,
                Action = "shoot1",
                PacketRoute = "ranged_attack",
                ProxySkillId = 3001005,
                MaxLevel = 20,
                DefaultCommon = new Dictionary<string, string>
                {
                    { "damage", "100+8*x" },
                    { "mpCon", "8+2*x" },
                    { "mobCount", "1" },
                    { "attackCount", "1" },
                    { "range", "600" }
                }
            });

            Register(new SkillTemplate
            {
                TypeName = "active_magic",
                InfoType = 1,
                Action = "swingO1",
                PacketRoute = "magic_attack",
                ProxySkillId = 2001004,
                MaxLevel = 20,
                DefaultCommon = new Dictionary<string, string>
                {
                    { "damage", "150+12*x" },
                    { "mpCon", "15+3*x" },
                    { "mobCount", "3" },
                    { "attackCount", "1" },
                    { "range", "400" }
                }
            });

            Register(new SkillTemplate
            {
                TypeName = "buff",
                InfoType = 10,
                Action = "alert2",
                PacketRoute = "special_move",
                ProxySkillId = 1001003,
                MaxLevel = 20,
                DefaultCommon = new Dictionary<string, string>
                {
                    { "time", "100+10*x" },
                    { "mpCon", "6+2*u(x/5)" },
                    { "pdd", "10*x" }
                }
            });

            Register(new SkillTemplate
            {
                TypeName = "passive",
                InfoType = 50,
                Action = "",       // no action for passives
                PacketRoute = "",  // no route for passives
                ProxySkillId = 0,
                MaxLevel = 10,
                DefaultCommon = new Dictionary<string, string>
                {
                    { "pdd", "5*x" }
                }
            });

            var newbieLevel = new SkillTemplate
            {
                TypeName = "newbie_level",
                InfoType = 1,
                Action = "swingO1",
                PacketRoute = "close_range",
                ProxySkillId = 1301008,
                MaxLevel = 3,
                DefaultCommon = new Dictionary<string, string>() // empty - uses levels instead
            };
            newbieLevel.BuildDefaultLevels = () =>
            {
                var levels = new Dictionary<int, Dictionary<string, string>>();
                levels[1] = new Dictionary<string, string>
                {
                    { "fixdamage", "10" },
                    { "mpCon", "3" }
                };
                levels[2] = new Dictionary<string, string>
                {
                    { "fixdamage", "25" },
                    { "mpCon", "5" }
                };
                levels[3] = new Dictionary<string, string>
                {
                    { "fixdamage", "40" },
                    { "mpCon", "7" }
                };
                return levels;
            };
            Register(newbieLevel);
        }

        private static void Register(SkillTemplate tpl)
        {
            Templates[tpl.TypeName] = tpl;
        }

        public static SkillTemplate Get(string typeName)
        {
            if (Templates.TryGetValue(typeName, out var tpl))
                return tpl;
            Console.WriteLine($"  [warn] Unknown skill type '{typeName}', falling back to active_melee.");
            return Templates["active_melee"];
        }

        public static IEnumerable<string> AllTypeNames => Templates.Keys;
    }
}
