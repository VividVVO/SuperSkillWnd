using System;
using System.Drawing;
using System.IO;
using MapleLib.WzLib;
using MapleLib.WzLib.WzProperties;

namespace SuperSkillTool
{
    public static class WzTest
    {
        public static void Run()
        {
            string imgPath = @"G:\code\mxd\Data\Skill\000.img";
            Console.WriteLine($"=== WzTest: Loading {imgPath} ===");
            Console.WriteLine($"File size: {new FileInfo(imgPath).Length} bytes");
            Console.WriteLine();

            // Try each WzMapleVersion to find which one works
            WzMapleVersion[] versions = {
                WzMapleVersion.BMS,
                WzMapleVersion.GMS,
                WzMapleVersion.EMS,
                WzMapleVersion.CLASSIC,
                WzMapleVersion.GENERATE,
            };

            foreach (var ver in versions)
            {
                Console.WriteLine($"--- Trying WzMapleVersion.{ver} ---");
                try
                {
                    using (var fs = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read))
                    {
                        var wzImg = new WzImage("000.img", fs, ver);
                        bool ok = wzImg.ParseImage(true);
                        Console.WriteLine($"  ParseImage returned: {ok}");

                        if (!ok || wzImg.WzProperties == null || wzImg.WzProperties.Count == 0)
                        {
                            Console.WriteLine($"  -> No properties. Skipping.");
                            wzImg.Dispose();
                            continue;
                        }

                        Console.WriteLine($"  Top-level property count: {wzImg.WzProperties.Count}");

                        // List top-level children (max 10)
                        int count = 0;
                        foreach (var prop in wzImg.WzProperties)
                        {
                            Console.WriteLine($"    [{prop.PropertyType}] {prop.Name}");
                            if (++count >= 10) { Console.WriteLine("    ... (truncated)"); break; }
                        }

                        // Try to access skill/0001000
                        var skillNode = wzImg.GetFromPath("skill/0001000");
                        if (skillNode != null)
                        {
                            Console.WriteLine($"\n  Found skill/0001000: [{skillNode.PropertyType}] {skillNode.Name}");
                            if (skillNode.WzProperties != null)
                            {
                                foreach (var child in skillNode.WzProperties)
                                {
                                    Console.WriteLine($"    [{child.PropertyType}] {child.Name}");
                                }
                            }

                            // Try icon
                            var iconNode = wzImg.GetFromPath("skill/0001000/icon");
                            if (iconNode != null)
                            {
                                Console.WriteLine($"\n  icon node: [{iconNode.PropertyType}] {iconNode.Name}");
                                if (iconNode is WzCanvasProperty canvas)
                                {
                                    try
                                    {
                                        Bitmap bmp = canvas.GetBitmap();
                                        if (bmp != null)
                                        {
                                            Console.WriteLine($"  icon bitmap: {bmp.Width}x{bmp.Height}");
                                            bmp.Dispose();
                                        }
                                        else
                                        {
                                            Console.WriteLine("  icon bitmap: null");
                                        }
                                    }
                                    catch (Exception ex)
                                    {
                                        Console.WriteLine($"  icon bitmap error: {ex.Message}");
                                    }
                                }
                            }
                            else
                            {
                                Console.WriteLine("  icon node: not found");
                            }

                            // Try effect
                            var effectNode = wzImg.GetFromPath("skill/0001000/effect");
                            if (effectNode != null)
                            {
                                Console.WriteLine($"\n  effect node: [{effectNode.PropertyType}] {effectNode.Name}");
                                if (effectNode.WzProperties != null)
                                {
                                    int ec = 0;
                                    foreach (var child in effectNode.WzProperties)
                                    {
                                        Console.WriteLine($"    [{child.PropertyType}] {child.Name}");
                                        if (++ec >= 5) { Console.WriteLine("    ..."); break; }
                                    }
                                }
                            }

                            // Try level
                            var levelNode = wzImg.GetFromPath("skill/0001000/level");
                            if (levelNode != null)
                            {
                                Console.WriteLine($"\n  level node: [{levelNode.PropertyType}] {levelNode.Name}");
                                // Show level/1 params
                                var lv1 = wzImg.GetFromPath("skill/0001000/level/1");
                                if (lv1 != null && lv1.WzProperties != null)
                                {
                                    Console.WriteLine("  level/1 params:");
                                    foreach (var p in lv1.WzProperties)
                                    {
                                        string val = "";
                                        if (p is WzIntProperty ip) val = ip.Value.ToString();
                                        else if (p is WzStringProperty sp) val = sp.Value;
                                        else if (p is WzFloatProperty fp) val = fp.Value.ToString();
                                        else val = p.ToString();
                                        Console.WriteLine($"    {p.Name} = {val}");
                                    }
                                }
                            }
                        }
                        else
                        {
                            Console.WriteLine("  skill/0001000 not found");

                            // Try alternate: just look for "skill" top-level
                            var skillTop = wzImg.GetFromPath("skill");
                            if (skillTop != null)
                            {
                                Console.WriteLine($"\n  Found 'skill' node: [{skillTop.PropertyType}]");
                                if (skillTop.WzProperties != null)
                                {
                                    int sc = 0;
                                    foreach (var child in skillTop.WzProperties)
                                    {
                                        Console.WriteLine($"    [{child.PropertyType}] {child.Name}");
                                        if (++sc >= 10) { Console.WriteLine("    ..."); break; }
                                    }
                                }
                            }
                            else
                            {
                                Console.WriteLine("  'skill' top-level node also not found");
                            }
                        }

                        Console.WriteLine($"\n  >>> WzMapleVersion.{ver} WORKS! <<<\n");
                        wzImg.Dispose();
                        return; // Found working version, stop testing
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  FAILED: {ex.Message}");
                    if (ex.InnerException != null)
                        Console.WriteLine($"  Inner: {ex.InnerException.Message}");
                }
                Console.WriteLine();
            }

            Console.WriteLine("=== None of the versions worked ===");
        }
    }
}
