using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using MapleLib.Helpers;

namespace SuperSkillTool
{
    class Program
    {
        [STAThread]
        static int Main(string[] args)
        {
            Console.OutputEncoding = Encoding.UTF8;

            // Load saved settings
            SettingsManager.Load();

            // SuperSkillTool targets legacy/pre-BB data packs.
            // Force MapleLib to avoid DXT/modern auto-selected formats.
            ImageFormatDetector.UsePreBigBangImageFormats = true;

            // No args -> launch GUI
            if (args.Length == 0)
            {
                try
                {
                    System.Windows.Forms.Application.EnableVisualStyles();
                    System.Windows.Forms.Application.SetCompatibleTextRenderingDefault(false);
                    System.Windows.Forms.Application.Run(new MainForm());
                }
                catch (Exception ex)
                {
                    System.Windows.Forms.MessageBox.Show(
                        ex.ToString(), "SuperSkillTool Error",
                        System.Windows.Forms.MessageBoxButtons.OK,
                        System.Windows.Forms.MessageBoxIcon.Error);
                }
                return 0;
            }

            // CLI mode
            Console.WriteLine("================================================================");
            Console.WriteLine("  SuperSkillTool - MapleStory Super Skill System Generator");
            Console.WriteLine("  Build: 2026-04-07-auto-create-imgxml-img");
            Console.WriteLine($"  RunDir: {AppDomain.CurrentDomain.BaseDirectory}");
            Console.WriteLine("================================================================");
            Console.WriteLine();

            string command = args[0].ToLower();

            switch (command)
            {
                case "add":
                    return RunAdd(args);
                case "verify":
                    return RunVerify(args);
                case "remove":
                    return RunRemove(args);
                case "wztest":
                    WzTest.Run();
                    return 0;
                default:
                    Console.WriteLine($"Unknown command: {command}");
                    PrintUsage();
                    return 1;
            }
        }

        static int RunAdd(string[] args)
        {
            string configPath = null;
            bool dryRun = false;
            bool skipImg = false;
            bool interactive = false;

            for (int i = 1; i < args.Length; i++)
            {
                string arg = args[i].ToLower();
                switch (arg)
                {
                    case "--config":
                        if (i + 1 < args.Length) configPath = args[++i];
                        break;
                    case "--dry-run":
                        dryRun = true;
                        break;
                    case "--skip-img":
                        skipImg = true;
                        break;
                    case "--interactive":
                        interactive = true;
                        break;
                    default:
                        if (!arg.StartsWith("--") && configPath == null)
                            configPath = args[i];
                        break;
                }
            }

            try
            {
                List<SkillDefinition> skills;

                if (interactive)
                {
                    skills = InteractiveWizard.Run();
                    if (skills == null || skills.Count == 0)
                    {
                        Console.WriteLine("No skills defined. Exiting.");
                        return 0;
                    }
                }
                else
                {
                    if (string.IsNullOrEmpty(configPath))
                    {
                        Console.WriteLine("[error] --config <path> is required (or use --interactive).");
                        PrintUsage();
                        return 1;
                    }

                    if (!Path.IsPathRooted(configPath))
                        configPath = Path.Combine(PathConfig.ToolRoot, configPath);

                    if (!File.Exists(configPath))
                    {
                        Console.WriteLine($"[error] Config file not found: {configPath}");
                        return 1;
                    }

                    Console.WriteLine($"Config: {configPath}");
                    skills = SkillDefinition.LoadFromFile(configPath);
                }

                if (dryRun)
                    Console.WriteLine("\n*** DRY RUN MODE - no files will be modified ***\n");

                Console.WriteLine($"Processing {skills.Count} skill(s)...");

                if (!Directory.Exists(PathConfig.OutputDir))
                    Directory.CreateDirectory(PathConfig.OutputDir);

                // Run all generators
                if (!skipImg)
                {
                    ServerXmlGenerator.Generate(skills, dryRun);
                    ServerStringXmlGenerator.Generate(skills, dryRun);
                }
                else
                {
                    Console.WriteLine("\n[ServerXml] Skipped (--skip-img)");
                    Console.WriteLine("[ServerStringXml] Skipped (--skip-img)");
                }

                DllJsonGenerator.GenerateSkillImgJson(skills, dryRun);
                DllJsonGenerator.GenerateStringImgJson(skills, dryRun);
                ImgWriteGenerator.Generate(skills, dryRun);
                ConfigJsonGenerator.Generate(skills, dryRun);
                SqlGenerator.Generate(skills, dryRun);
                ChecklistGenerator.Generate(skills, dryRun);
                HarepackerGuideGenerator.Generate(skills, dryRun);

                Console.WriteLine();
                Console.WriteLine("================================================================");
                if (dryRun)
                {
                    Console.WriteLine("  DRY RUN COMPLETE - no files were modified.");
                }
                else
                {
                    Console.WriteLine("  ALL DONE!");
                    Console.WriteLine($"  Backups: {BackupHelper.GetSessionBackupDir()}");
                    Console.WriteLine($"  Outputs: {PathConfig.OutputDir}");
                }
                Console.WriteLine("================================================================");
                return 0;
            }
            catch (Exception ex)
            {
                Console.WriteLine();
                Console.WriteLine("  ERROR: " + ex.Message);
                Console.WriteLine(ex.StackTrace);
                return 1;
            }
        }

        static int RunVerify(string[] args)
        {
            int skillId = 0;
            bool all = false;

            for (int i = 1; i < args.Length; i++)
            {
                string arg = args[i].ToLower();
                if (arg == "--skill-id" && i + 1 < args.Length)
                    int.TryParse(args[++i], out skillId);
                else if (arg == "--all")
                    all = true;
                else if (!arg.StartsWith("--"))
                    int.TryParse(args[i], out skillId);
            }

            if (all)
            {
                VerifyGenerator.VerifyAll();
                return 0;
            }

            if (skillId <= 0)
            {
                Console.WriteLine("[error] --skill-id <id> is required (or use --all).");
                return 1;
            }

            VerifyGenerator.Verify(skillId);
            return 0;
        }

        static int RunRemove(string[] args)
        {
            int skillId = 0;
            bool dryRun = false;

            for (int i = 1; i < args.Length; i++)
            {
                string arg = args[i].ToLower();
                if (arg == "--skill-id" && i + 1 < args.Length)
                    int.TryParse(args[++i], out skillId);
                else if (arg == "--dry-run")
                    dryRun = true;
                else if (!arg.StartsWith("--"))
                    int.TryParse(args[i], out skillId);
            }

            if (skillId <= 0)
            {
                Console.WriteLine("[error] --skill-id <id> is required.");
                return 1;
            }

            SkillRemover.Remove(skillId, dryRun);
            return 0;
        }

        static void PrintUsage()
        {
            Console.WriteLine("Usage:");
            Console.WriteLine("  SuperSkillTool.exe                                   Launch GUI");
            Console.WriteLine("  SuperSkillTool.exe add --config <path>               Add skills from JSON");
            Console.WriteLine("  SuperSkillTool.exe add --config <path> --dry-run      Preview only");
            Console.WriteLine("  SuperSkillTool.exe add --config <path> --skip-img     Skip server XMLs");
            Console.WriteLine("  SuperSkillTool.exe add --interactive                  Interactive wizard");
            Console.WriteLine("  SuperSkillTool.exe verify --skill-id <id>             Verify skill consistency");
            Console.WriteLine("  SuperSkillTool.exe verify --all                       Verify all registered skills");
            Console.WriteLine("  SuperSkillTool.exe remove --skill-id <id>             Remove skill from all files");
            Console.WriteLine("  SuperSkillTool.exe remove --skill-id <id> --dry-run   Preview removal");
            Console.WriteLine();
            Console.WriteLine("Config file format: see new_skill_example.json");
        }
    }
}
