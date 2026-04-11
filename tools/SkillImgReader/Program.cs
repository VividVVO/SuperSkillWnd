using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using MapleLib.WzLib;
using MapleLib.WzLib.WzProperties;

namespace SkillImgReader;

internal enum BehaviorKind
{
    Unknown = 0,
    Attack = 1,
    Buff = 2,
    Passive = 3,
    SummonLike = 4,
    MorphLike = 5,
    MountLike = 6
}

internal sealed class SkillRecord
{
    public int SkillId { get; set; }
    public string NameUtf8 { get; set; } = "";
    public string DescUtf8 { get; set; } = "";
    public string H1Utf8 { get; set; } = "";
    public string HUtf8 { get; set; } = "";
    public string PdescUtf8 { get; set; } = "";
    public string PhUtf8 { get; set; } = "";
    public byte[] IconPngBytes { get; set; } = Array.Empty<byte>();
    public byte[] IconMouseOverPngBytes { get; set; } = Array.Empty<byte>();
    public byte[] IconDisabledPngBytes { get; set; } = Array.Empty<byte>();
    public int MaxLevel { get; set; } = 1;
    public bool PassiveHint { get; set; }
    public BehaviorKind BehaviorKind { get; set; } = BehaviorKind.Unknown;
    public List<SkillLevelRecord> Levels { get; } = new();
}

internal sealed class SkillLevelRecord
{
    public int Level { get; set; }
    public Dictionary<string, string> Values { get; } = new(StringComparer.OrdinalIgnoreCase);
}

internal static class Program
{
    private const string EnvSuperSkillRoot = "SUPER_SKILL_ROOT";
    private const string EnvSuperSkillConfigDir = "SUPER_SKILL_CONFIG_DIR";
    private const string EnvSuperSkillCachePath = "SUPER_SKILL_CACHE_PATH";
    private const string EnvSuperSkillGameDataDir = "SUPER_SKILL_GAME_DATA_DIR";
    private const string DevFallbackGameDataBaseDir = @"G:\code\mxd\Data";

    private static readonly int[] FallbackTrackedSkillIds =
    {
        73, 74, 110, 111, 112,
        1000, 1001, 1002, 1003, 1005, 1006, 1007, 1009, 1010, 1013, 1016,
        1036, 1037, 1039, 1040, 1069, 1075, 1076, 1096, 1001037
    };

    private static int Main(string[] args)
    {
        RuntimeSettings settings = ResolveRuntimeSettings();
        string outputPath = ResolveOutputPath(args, settings);
        return BuildCacheCore(outputPath, settings, logToConsole: true);
    }

    internal static int BuildDefaultCache()
    {
        RuntimeSettings settings = ResolveRuntimeSettings();
        return BuildCacheCore(settings.OutputPath, settings, logToConsole: false);
    }

    private static int BuildCacheCore(string outputPath, RuntimeSettings settings, bool logToConsole)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);

            var trackedSkillIds = LoadTrackedSkillIds(settings.ConfigPaths);
            using var loader = new SkillImageLoader(
                Path.Combine(settings.GameDataBaseDir, "Skill"),
                Path.Combine(settings.GameDataBaseDir, "String", "Skill.img"));

            var records = new List<SkillRecord>(trackedSkillIds.Count);
            foreach (int skillId in trackedSkillIds)
                records.Add(loader.LoadRecord(skillId));

            WriteCache(outputPath, records);
            if (logToConsole)
                Console.WriteLine($"[SkillImgReader] wrote records={records.Count} path={outputPath}");
            return 0;
        }
        catch (Exception ex)
        {
            if (logToConsole)
                Console.Error.WriteLine("[SkillImgReader] ERROR: " + ex);
            return Marshal.GetHRForException(ex);
        }
    }

    private sealed class RuntimeSettings
    {
        public string RootDir { get; init; } = "";
        public string SkillConfigDir { get; init; } = "";
        public string GameDataBaseDir { get; init; } = "";
        public string OutputPath { get; init; } = "";
        public string[] ConfigPaths { get; init; } = Array.Empty<string>();
    }

    private static RuntimeSettings ResolveRuntimeSettings()
    {
        string rootDir = ResolveRootDir();
        string skillConfigDir = ResolveDirectoryFromEnv(EnvSuperSkillConfigDir, Path.Combine(rootDir, "skill"));
        string gameDataBaseDir = ResolveGameDataBaseDir(rootDir);
        string outputPath = ResolveFileFromEnv(EnvSuperSkillCachePath, Path.Combine(skillConfigDir, "skill_local_cache.bin"));

        return new RuntimeSettings
        {
            RootDir = rootDir,
            SkillConfigDir = skillConfigDir,
            GameDataBaseDir = gameDataBaseDir,
            OutputPath = outputPath,
            ConfigPaths = new[]
            {
                Path.Combine(skillConfigDir, "super_skills.json"),
                Path.Combine(skillConfigDir, "custom_skill_routes.json"),
                Path.Combine(skillConfigDir, "native_skill_injections.json")
            }
        };
    }

    private static bool IsReaderDirName(string? dirName)
    {
        return string.Equals(dirName, "Reader", StringComparison.OrdinalIgnoreCase) ||
               string.Equals(dirName, "SkillImgReader", StringComparison.OrdinalIgnoreCase);
    }

    private static string ResolveRootDir()
    {
        string? envRoot = Environment.GetEnvironmentVariable(EnvSuperSkillRoot);
        if (!string.IsNullOrWhiteSpace(envRoot))
            return Path.GetFullPath(envRoot);

        string baseDir = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string dirName = Path.GetFileName(baseDir);
        string? parent = Directory.GetParent(baseDir)?.FullName;

        if (IsReaderDirName(dirName) && parent != null)
        {
            string parentName = Path.GetFileName(parent.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            string? grandParent = Directory.GetParent(parent)?.FullName;
            if (string.Equals(parentName, "build", StringComparison.OrdinalIgnoreCase) && grandParent != null)
                return Path.GetFullPath(grandParent);
            if (string.Equals(parentName, "Plugins", StringComparison.OrdinalIgnoreCase) && grandParent != null)
            {
                string grandParentName = Path.GetFileName(grandParent.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
                string? greatGrandParent = Directory.GetParent(grandParent)?.FullName;
                if (string.Equals(grandParentName, "Data", StringComparison.OrdinalIgnoreCase) && greatGrandParent != null)
                    return Path.GetFullPath(greatGrandParent);
                return Path.GetFullPath(grandParent);
            }
            return Path.GetFullPath(parent);
        }

        return Path.GetFullPath(baseDir);
    }

    private static string ResolveDirectoryFromEnv(string envName, string fallback)
    {
        string? value = Environment.GetEnvironmentVariable(envName);
        if (!string.IsNullOrWhiteSpace(value))
            return Path.GetFullPath(value);
        return Path.GetFullPath(fallback);
    }

    private static string ResolveFileFromEnv(string envName, string fallback)
    {
        string? value = Environment.GetEnvironmentVariable(envName);
        if (!string.IsNullOrWhiteSpace(value))
            return Path.GetFullPath(value);
        return Path.GetFullPath(fallback);
    }

    private static string ResolveGameDataBaseDir(string rootDir)
    {
        string? envDataDir = Environment.GetEnvironmentVariable(EnvSuperSkillGameDataDir);
        if (!string.IsNullOrWhiteSpace(envDataDir))
            return Path.GetFullPath(envDataDir);

        string relativeDataDir = Path.Combine(rootDir, "Data");
        if (Directory.Exists(Path.Combine(relativeDataDir, "Skill")) &&
            File.Exists(Path.Combine(relativeDataDir, "String", "Skill.img")))
        {
            return Path.GetFullPath(relativeDataDir);
        }

        return DevFallbackGameDataBaseDir;
    }

    private static string ResolveOutputPath(string[] args, RuntimeSettings settings)
    {
        for (int i = 0; i + 1 < args.Length; ++i)
        {
            if (string.Equals(args[i], "--out", StringComparison.OrdinalIgnoreCase))
                return Path.GetFullPath(args[i + 1]);
        }

        return settings.OutputPath;
    }

    private static List<int> LoadTrackedSkillIds(IEnumerable<string> configPaths)
    {
        var skillIds = new HashSet<int>();
        foreach (string configPath in configPaths)
            CollectSkillIdsFromJson(configPath, skillIds);

        if (skillIds.Count == 0)
        {
            foreach (int skillId in FallbackTrackedSkillIds)
                skillIds.Add(skillId);
        }

        var result = skillIds.ToList();
        result.Sort();
        return result;
    }

    private static void CollectSkillIdsFromJson(string path, HashSet<int> outSkillIds)
    {
        if (!File.Exists(path))
            return;

        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(path, Encoding.UTF8));
        CollectSkillIdsRecursive(document.RootElement, outSkillIds);
    }

    private static void CollectSkillIdsRecursive(JsonElement element, HashSet<int> outSkillIds)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.Object:
                foreach (JsonProperty property in element.EnumerateObject())
                {
                    if (string.Equals(property.Name, "skillId", StringComparison.OrdinalIgnoreCase) &&
                        TryReadInt(property.Value, out int skillId) &&
                        skillId > 0)
                    {
                        outSkillIds.Add(skillId);
                    }

                    CollectSkillIdsRecursive(property.Value, outSkillIds);
                }
                break;

            case JsonValueKind.Array:
                foreach (JsonElement item in element.EnumerateArray())
                    CollectSkillIdsRecursive(item, outSkillIds);
                break;
        }
    }

    private static bool TryReadInt(JsonElement element, out int value)
    {
        value = 0;

        if (element.ValueKind == JsonValueKind.Number)
            return element.TryGetInt32(out value);

        if (element.ValueKind == JsonValueKind.String)
            return int.TryParse(element.GetString(), out value);

        return false;
    }

    private static void WriteCache(string outputPath, List<SkillRecord> records)
    {
        using var stream = new FileStream(outputPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        using var writer = new BinaryWriter(stream, new UTF8Encoding(false), leaveOpen: false);

        writer.Write(new byte[] { (byte)'S', (byte)'L', (byte)'D', (byte)'1' });
        writer.Write(2);
        writer.Write(records.Count);
        foreach (SkillRecord record in records)
        {
            writer.Write(record.SkillId);
            writer.Write(record.MaxLevel);
            writer.Write(record.PassiveHint);
            writer.Write((int)record.BehaviorKind);
            WriteBlob(writer, record.NameUtf8);
            WriteBlob(writer, record.DescUtf8);
            WriteBlob(writer, record.H1Utf8);
            WriteBlob(writer, record.HUtf8);
            WriteBlob(writer, record.PdescUtf8);
            WriteBlob(writer, record.PhUtf8);
            WriteBlob(writer, record.IconPngBytes);
            WriteBlob(writer, record.IconMouseOverPngBytes);
            WriteBlob(writer, record.IconDisabledPngBytes);
            writer.Write(record.Levels.Count);
            foreach (SkillLevelRecord levelRecord in record.Levels)
            {
                writer.Write(levelRecord.Level);
                writer.Write(levelRecord.Values.Count);
                foreach (KeyValuePair<string, string> pair in levelRecord.Values)
                {
                    WriteBlob(writer, pair.Key);
                    WriteBlob(writer, pair.Value);
                }
            }
        }
    }

    private static void WriteBlob(BinaryWriter writer, string value)
    {
        WriteBlob(writer, string.IsNullOrEmpty(value) ? Array.Empty<byte>() : Encoding.UTF8.GetBytes(value));
    }

    private static void WriteBlob(BinaryWriter writer, byte[] value)
    {
        byte[] bytes = value ?? Array.Empty<byte>();
        writer.Write(bytes.Length);
        writer.Write(bytes);
    }
}

public static class SkillImgReaderExports
{
    [UnmanagedCallersOnly(EntryPoint = "BuildSkillCache", CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int BuildSkillCache()
    {
        return Program.BuildDefaultCache();
    }
}

internal sealed class SkillImageLoader : IDisposable
{
    private readonly string _skillDir;
    private readonly string _stringSkillPath;
    private readonly Dictionary<int, WzImage> _skillImages = new();
    private readonly Dictionary<int, FileStream> _skillStreams = new();
    private WzImage? _stringImage;
    private FileStream? _stringStream;

    public SkillImageLoader(string skillDir, string stringSkillPath)
    {
        _skillDir = skillDir;
        _stringSkillPath = stringSkillPath;
    }

    public SkillRecord LoadRecord(int skillId)
    {
        var record = new SkillRecord { SkillId = skillId };

        LoadStringRecord(skillId, record);
        record.PassiveHint =
            !string.IsNullOrEmpty(record.HUtf8) &&
            string.IsNullOrEmpty(record.H1Utf8) &&
            string.IsNullOrEmpty(record.PdescUtf8) &&
            string.IsNullOrEmpty(record.PhUtf8);

        if (!string.IsNullOrEmpty(record.DescUtf8))
            record.MaxLevel = ExtractFirstPositiveInt(record.DescUtf8, record.MaxLevel);

        WzImageProperty? skillNode = TryGetSkillNode(skillId);
        if (skillNode == null)
            return record;

        record.IconPngBytes = SafeGetBitmapPng(skillNode, "icon");
        record.IconMouseOverPngBytes = SafeGetBitmapPng(skillNode, "iconMouseOver");
        record.IconDisabledPngBytes = SafeGetBitmapPng(skillNode, "iconDisabled");
        record.MaxLevel = ExtractMaxLevel(skillNode, record.MaxLevel);
        record.BehaviorKind = InferBehaviorKind(skillNode, record);
        ExtractLevelRecords(skillNode, record);
        return record;
    }

    public void Dispose()
    {
        foreach ((_, WzImage image) in _skillImages)
        {
            try { image.Dispose(); } catch { }
        }

        foreach ((_, FileStream stream) in _skillStreams)
        {
            try { stream.Dispose(); } catch { }
        }

        try { _stringImage?.Dispose(); } catch { }
        try { _stringStream?.Dispose(); } catch { }
    }

    private void LoadStringRecord(int skillId, SkillRecord record)
    {
        WzImage stringImage = GetOrLoadStringImage();
        WzImageProperty? node = stringImage[skillId.ToString()] ?? stringImage[skillId.ToString("D7")];
        if (node == null)
            return;

        record.NameUtf8 = ReadString(node["name"]);
        record.DescUtf8 = ReadString(node["desc"]);
        record.H1Utf8 = ReadString(node["h1"]);
        record.HUtf8 = ReadString(node["h"]);
        record.PdescUtf8 = ReadString(node["pdesc"]);
        record.PhUtf8 = ReadString(node["ph"]);
    }

    private WzImageProperty? TryGetSkillNode(int skillId)
    {
        int jobId = skillId / 10000;
        string skillPath = "skill/" + skillId;
        try
        {
            WzImage image = GetOrLoadSkillImage(jobId);
            return image.GetFromPath(skillPath);
        }
        catch
        {
            return null;
        }
    }

    private WzImage GetOrLoadStringImage()
    {
        if (_stringImage != null)
            return _stringImage;

        if (!File.Exists(_stringSkillPath))
            throw new FileNotFoundException("String Skill.img not found", _stringSkillPath);

        WzMapleVersion version = WzImageVersionHelper.DetectVersionForStringImg(_stringSkillPath);
        _stringStream = new FileStream(_stringSkillPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        _stringImage = new WzImage("Skill.img", _stringStream, version);
        if (!_stringImage.ParseImage(true))
            throw new InvalidDataException("Failed to parse String Skill.img");

        return _stringImage;
    }

    private WzImage GetOrLoadSkillImage(int jobId)
    {
        if (_skillImages.TryGetValue(jobId, out WzImage? cached))
            return cached;

        string path = Path.Combine(_skillDir, jobId + ".img");
        if (!File.Exists(path))
            throw new FileNotFoundException("Skill .img not found", path);

        WzMapleVersion version = WzImageVersionHelper.DetectVersionForSkillImg(path);
        var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        var image = new WzImage(jobId + ".img", stream, version);
        if (!image.ParseImage(true))
            throw new InvalidDataException("Failed to parse " + path);

        _skillStreams[jobId] = stream;
        _skillImages[jobId] = image;
        return image;
    }

    private static string ReadString(WzImageProperty? node)
    {
        if (node is WzStringProperty stringProperty)
            return stringProperty.Value ?? "";

        return "";
    }

    private static int ExtractMaxLevel(WzImageProperty skillNode, int fallbackValue)
    {
        WzImageProperty? commonMaxLevel = skillNode.GetFromPath("common/maxLevel");
        if (TryReadInt(commonMaxLevel, out int commonValue) && commonValue > 0)
            return commonValue;

        WzImageProperty? levelNode = skillNode["level"];
        if (levelNode?.WzProperties != null)
        {
            int levelCount = 0;
            foreach (WzImageProperty child in levelNode.WzProperties)
            {
                if (int.TryParse(child.Name, out _))
                    ++levelCount;
            }

            if (levelCount > 0)
                return levelCount;
        }

        return fallbackValue > 0 ? fallbackValue : 1;
    }

    private static BehaviorKind InferBehaviorKind(WzImageProperty skillNode, SkillRecord record)
    {
        if (record.PassiveHint || HasAnyProperty(skillNode, "psd", "psdSkill"))
            return BehaviorKind.Passive;

        int infoType = 0;
        TryReadInt(skillNode.GetFromPath("info/type"), out infoType);
        string action = ExtractAction(skillNode["action"]).ToLowerInvariant();

        if (infoType == 33 || HasAnyProperty(skillNode, "summon", "minionAttack"))
            return BehaviorKind.SummonLike;

        if (HasAnyProperty(skillNode, "morph"))
            return BehaviorKind.MorphLike;

        if (action == "fly" || HasAnyProperty(skillNode, "vehicleID", "ride"))
            return BehaviorKind.MountLike;

        if (infoType == 50)
            return BehaviorKind.Passive;

        if (infoType == 10 || infoType == 2 || action == "alert2")
            return BehaviorKind.Buff;

        if (infoType == 1)
            return BehaviorKind.Attack;

        return BehaviorKind.Attack;
    }

    private static bool HasAnyProperty(WzImageProperty skillNode, params string[] names)
    {
        foreach (string name in names)
        {
            if (skillNode.GetFromPath(name) != null)
                return true;
        }

        return false;
    }

    private static string ExtractAction(WzImageProperty? actionNode)
    {
        if (actionNode == null)
            return "";

        if (actionNode is WzStringProperty direct)
            return direct.Value ?? "";

        string? directValue = GetPropertyValueString(actionNode);
        if (!string.IsNullOrEmpty(directValue))
            return directValue.StartsWith("-> ", StringComparison.Ordinal) ? directValue[3..] : directValue;

        if (actionNode.WzProperties == null || actionNode.WzProperties.Count == 0)
            return "";

        if (actionNode["0"] is WzStringProperty child0)
            return child0.Value ?? "";

        int bestIndex = int.MaxValue;
        string bestValue = "";
        foreach (WzImageProperty child in actionNode.WzProperties)
        {
            if (!int.TryParse(child.Name, out int index))
                continue;

            string? value = GetPropertyValueString(child);
            if (string.IsNullOrEmpty(value))
                continue;

            if (value.StartsWith("-> ", StringComparison.Ordinal))
                value = value[3..];

            if (index < bestIndex)
            {
                bestIndex = index;
                bestValue = value;
            }
        }

        return bestValue;
    }

    private static string? GetPropertyValueString(WzImageProperty? property)
    {
        if (property is WzIntProperty intProperty) return intProperty.Value.ToString();
        if (property is WzShortProperty shortProperty) return shortProperty.Value.ToString();
        if (property is WzLongProperty longProperty) return longProperty.Value.ToString();
        if (property is WzFloatProperty floatProperty) return floatProperty.Value.ToString("G");
        if (property is WzDoubleProperty doubleProperty) return doubleProperty.Value.ToString("G");
        if (property is WzStringProperty stringProperty) return stringProperty.Value ?? "";
        if (property is WzUOLProperty uolProperty) return "-> " + (uolProperty.Value ?? "");
        return null;
    }

    private static bool TryReadInt(WzImageProperty? property, out int value)
    {
        value = 0;
        if (property is WzIntProperty intProperty) { value = intProperty.Value; return true; }
        if (property is WzShortProperty shortProperty) { value = shortProperty.Value; return true; }
        if (property is WzLongProperty longProperty) { value = (int)longProperty.Value; return true; }
        if (property is WzStringProperty stringProperty && int.TryParse(stringProperty.Value, out int parsed))
        {
            value = parsed;
            return true;
        }

        return false;
    }

    private static void ExtractLevelRecords(WzImageProperty skillNode, SkillRecord record)
    {
        if (!HasAnyTooltipPlaceholder(record))
            return;

        var levels = new SortedSet<int>();
        WzImageProperty? levelNode = skillNode["level"];
        if (levelNode?.WzProperties != null)
        {
            foreach (WzImageProperty child in levelNode.WzProperties)
            {
                if (int.TryParse(child.Name, out int level) && level > 0)
                    levels.Add(level);
            }
        }

        if (levels.Count == 0)
        {
            const int generatedLevelLimit = 500;
            int maxLevel = Math.Clamp(record.MaxLevel, 1, generatedLevelLimit);
            for (int level = 1; level <= maxLevel; ++level)
                levels.Add(level);
        }

        foreach (int level in levels)
        {
            var levelRecord = new SkillLevelRecord { Level = level };
            CollectScalarValues(skillNode["common"], level, levelRecord.Values);
            CollectScalarValues(levelNode?[level.ToString()], level, levelRecord.Values);
            if (levelRecord.Values.Count > 0)
                record.Levels.Add(levelRecord);
        }
    }

    private static bool HasAnyTooltipPlaceholder(SkillRecord record)
    {
        return ContainsPlaceholder(record.HUtf8) ||
               ContainsPlaceholder(record.H1Utf8) ||
               ContainsPlaceholder(record.PhUtf8) ||
               ContainsPlaceholder(record.PdescUtf8) ||
               ContainsPlaceholder(record.DescUtf8);
    }

    private static bool ContainsPlaceholder(string text)
    {
        if (string.IsNullOrEmpty(text))
            return false;

        for (int i = 0; i + 1 < text.Length; ++i)
        {
            if (text[i] == '#' && (char.IsLetter(text[i + 1]) || text[i + 1] == '_'))
                return true;
        }
        return false;
    }

    private static void CollectScalarValues(WzImageProperty? container, int level, Dictionary<string, string> values)
    {
        if (container?.WzProperties == null)
            return;

        foreach (WzImageProperty child in container.WzProperties)
        {
            if (string.IsNullOrEmpty(child.Name) || child.Name[0] == '_')
                continue;

            WzImageProperty? resolved = ResolveProperty(child);
            if (!TryReadScalarValue(resolved, level, out string value))
                continue;

            values[child.Name] = value;
        }
    }

    private static bool TryReadScalarValue(WzImageProperty? property, int level, out string value)
    {
        value = "";

        if (property is WzIntProperty intProperty)
        {
            value = intProperty.Value.ToString(CultureInfo.InvariantCulture);
            return true;
        }
        if (property is WzShortProperty shortProperty)
        {
            value = shortProperty.Value.ToString(CultureInfo.InvariantCulture);
            return true;
        }
        if (property is WzLongProperty longProperty)
        {
            value = longProperty.Value.ToString(CultureInfo.InvariantCulture);
            return true;
        }
        if (property is WzFloatProperty floatProperty)
        {
            value = FormatScalarNumber(floatProperty.Value);
            return true;
        }
        if (property is WzDoubleProperty doubleProperty)
        {
            value = FormatScalarNumber(doubleProperty.Value);
            return true;
        }
        if (property is WzStringProperty stringProperty)
        {
            string rawValue = stringProperty.Value ?? "";
            if (TryEvaluateSkillFormula(rawValue, level, out int evaluated))
                value = evaluated.ToString(CultureInfo.InvariantCulture);
            else
                value = rawValue;
            return true;
        }

        return false;
    }

    private static string FormatScalarNumber(double value)
    {
        if (Math.Abs(value - Math.Round(value)) < 0.000001)
            return ((int)Math.Round(value)).ToString(CultureInfo.InvariantCulture);
        return value.ToString("G", CultureInfo.InvariantCulture);
    }

    private static bool TryEvaluateSkillFormula(string rawValue, int level, out int value)
    {
        value = 0;
        if (string.IsNullOrWhiteSpace(rawValue))
            return false;

        string expression = rawValue.Trim();
        if (!LooksLikeSkillFormula(expression))
            return false;

        var parser = new SkillFormulaParser(expression, level);
        if (!parser.TryParse(out double result))
            return false;

        value = (int)Math.Floor(result + 0.000001);
        return true;
    }

    private static bool LooksLikeSkillFormula(string expression)
    {
        bool hasFormulaToken = false;
        foreach (char ch in expression)
        {
            if (char.IsWhiteSpace(ch) || char.IsDigit(ch) || ch == '.' ||
                ch == '+' || ch == '-' || ch == '*' || ch == '/' ||
                ch == '(' || ch == ')')
            {
                if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '(' || ch == ')')
                    hasFormulaToken = true;
                continue;
            }

            if (ch == 'x' || ch == 'X' || ch == 'u' || ch == 'U' || ch == 'd' || ch == 'D')
            {
                hasFormulaToken = true;
                continue;
            }

            return false;
        }

        return hasFormulaToken || int.TryParse(expression, NumberStyles.Integer, CultureInfo.InvariantCulture, out _);
    }

    private static byte[] SafeGetBitmapPng(WzImageProperty skillNode, string childName)
    {
        try
        {
            WzImageProperty? node = ResolveProperty(skillNode[childName]);
            if (node is not WzCanvasProperty canvas)
                return Array.Empty<byte>();

            using Bitmap? bitmap = CloneBitmapSafe(canvas.GetBitmap());
            if (bitmap == null)
                return Array.Empty<byte>();

            using var stream = new MemoryStream();
            bitmap.Save(stream, ImageFormat.Png);
            return stream.ToArray();
        }
        catch
        {
            return Array.Empty<byte>();
        }
    }

    private static WzImageProperty? ResolveProperty(WzImageProperty? property)
    {
        if (property is WzUOLProperty)
            return property.GetLinkedWzImageProperty() ?? property;

        return property;
    }

    private static Bitmap? CloneBitmapSafe(Bitmap? source)
    {
        if (source == null)
            return null;

        var cloned = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb);
        using Graphics graphics = Graphics.FromImage(cloned);
        graphics.DrawImage(source, 0, 0, source.Width, source.Height);
        return cloned;
    }

    private static int ExtractFirstPositiveInt(string text, int fallbackValue)
    {
        for (int i = 0; i < text.Length; ++i)
        {
            if (!char.IsDigit(text[i]))
                continue;

            int value = 0;
            int j = i;
            while (j < text.Length && char.IsDigit(text[j]))
            {
                value = (value * 10) + (text[j] - '0');
                ++j;
            }

            if (value > 0)
                return value;

            i = j;
        }

        return fallbackValue;
    }
}

internal sealed class SkillFormulaParser
{
    private readonly string _text;
    private readonly int _level;
    private int _pos;

    public SkillFormulaParser(string text, int level)
    {
        _text = text;
        _level = level;
    }

    public bool TryParse(out double value)
    {
        value = 0.0;
        try
        {
            value = ParseExpression();
            SkipWhitespace();
            return _pos == _text.Length && !double.IsNaN(value) && !double.IsInfinity(value);
        }
        catch
        {
            value = 0.0;
            return false;
        }
    }

    private double ParseExpression()
    {
        double value = ParseTerm();
        while (true)
        {
            SkipWhitespace();
            if (Match('+'))
                value += ParseTerm();
            else if (Match('-'))
                value -= ParseTerm();
            else
                return value;
        }
    }

    private double ParseTerm()
    {
        double value = ParseFactor();
        while (true)
        {
            SkipWhitespace();
            if (Match('*'))
            {
                value *= ParseFactor();
            }
            else if (Match('/'))
            {
                double divisor = ParseFactor();
                if (Math.Abs(divisor) < 0.000001)
                    throw new DivideByZeroException();
                value /= divisor;
            }
            else
            {
                return value;
            }
        }
    }

    private double ParseFactor()
    {
        SkipWhitespace();

        if (Match('+'))
            return ParseFactor();
        if (Match('-'))
            return -ParseFactor();

        if (Match('('))
        {
            double nested = ParseExpression();
            SkipWhitespace();
            if (!Match(')'))
                throw new FormatException("missing closing parenthesis");
            return nested;
        }

        if (MatchIdentifier('x'))
            return _level;

        if (MatchIdentifier('u'))
            return ParseFunction(Math.Ceiling);

        if (MatchIdentifier('d'))
            return ParseFunction(Math.Floor);

        return ParseNumber();
    }

    private double ParseFunction(Func<double, double> fn)
    {
        SkipWhitespace();
        if (!Match('('))
            throw new FormatException("missing function opening parenthesis");

        double nested = ParseExpression();
        SkipWhitespace();
        if (!Match(')'))
            throw new FormatException("missing function closing parenthesis");

        return fn(nested);
    }

    private double ParseNumber()
    {
        SkipWhitespace();
        int start = _pos;
        bool hasDot = false;
        while (_pos < _text.Length)
        {
            char ch = _text[_pos];
            if (char.IsDigit(ch))
            {
                ++_pos;
                continue;
            }

            if (ch == '.' && !hasDot)
            {
                hasDot = true;
                ++_pos;
                continue;
            }

            break;
        }

        if (start == _pos)
            throw new FormatException("number expected");

        return double.Parse(_text.Substring(start, _pos - start), CultureInfo.InvariantCulture);
    }

    private bool Match(char expected)
    {
        if (_pos >= _text.Length || _text[_pos] != expected)
            return false;

        ++_pos;
        return true;
    }

    private bool MatchIdentifier(char expected)
    {
        if (_pos >= _text.Length || char.ToLowerInvariant(_text[_pos]) != expected)
            return false;

        ++_pos;
        return true;
    }

    private void SkipWhitespace()
    {
        while (_pos < _text.Length && char.IsWhiteSpace(_text[_pos]))
            ++_pos;
    }
}

internal static class WzImageVersionHelper
{
    private enum ImageKind
    {
        Skill,
        StringSkill
    }

    private static readonly WzMapleVersion[] CandidateVersions =
    {
        WzMapleVersion.BMS,
        WzMapleVersion.CLASSIC,
        WzMapleVersion.GMS,
        WzMapleVersion.EMS
    };

    private static readonly Dictionary<string, WzMapleVersion> Cache = new(StringComparer.OrdinalIgnoreCase);
    private static readonly object SyncRoot = new();

    public static WzMapleVersion DetectVersionForSkillImg(string imgPath)
    {
        return DetectVersion(imgPath, ImageKind.Skill);
    }

    public static WzMapleVersion DetectVersionForStringImg(string imgPath)
    {
        return DetectVersion(imgPath, ImageKind.StringSkill);
    }

    private static WzMapleVersion DetectVersion(string imgPath, ImageKind kind)
    {
        if (string.IsNullOrWhiteSpace(imgPath) || !File.Exists(imgPath))
            return WzMapleVersion.EMS;

        string fullPath = Path.GetFullPath(imgPath);
        string cacheKey = kind + "|" + fullPath;

        lock (SyncRoot)
        {
            if (Cache.TryGetValue(cacheKey, out WzMapleVersion cached))
                return cached;
        }

        int bestScore = int.MinValue;
        WzMapleVersion bestVersion = WzMapleVersion.EMS;
        foreach (WzMapleVersion version in CandidateVersions)
        {
            int score = ScoreVersion(fullPath, version, kind);
            if (score > bestScore)
            {
                bestScore = score;
                bestVersion = version;
            }
        }

        lock (SyncRoot)
            Cache[cacheKey] = bestVersion;

        return bestVersion;
    }

    private static int ScoreVersion(string imgPath, WzMapleVersion version, ImageKind kind)
    {
        FileStream? stream = null;
        WzImage? image = null;
        try
        {
            stream = new FileStream(imgPath, FileMode.Open, FileAccess.Read, FileShare.Read);
            image = new WzImage(Path.GetFileName(imgPath), stream, version);
            if (!image.ParseImage(true))
                return int.MinValue / 2;

            return kind == ImageKind.StringSkill ? ScoreStringSkillImage(image) : ScoreSkillImage(image);
        }
        catch
        {
            return int.MinValue / 2;
        }
        finally
        {
            try { image?.Dispose(); } catch { }
            try { stream?.Dispose(); } catch { }
        }
    }

    private static int ScoreSkillImage(WzImage image)
    {
        if (image.WzProperties == null)
            return -10000;

        int score = image.WzProperties.Count > 0 ? 10 : -1000;
        if (image.GetFromPath("skill") is not WzSubProperty skill)
            return score - 6000;

        score += 2000;
        if (skill.WzProperties == null || skill.WzProperties.Count == 0)
            return score - 100;

        int scanned = 0;
        int numericChildren = 0;
        int withInfo = 0;
        int withCommon = 0;
        int withAction = 0;
        foreach (WzImageProperty child in skill.WzProperties)
        {
            if (++scanned > 120)
                break;

            if (int.TryParse(child.Name, out _))
                ++numericChildren;

            if (child is not WzSubProperty sub)
                continue;

            if (sub["info"] != null) ++withInfo;
            if (sub["common"] != null) ++withCommon;
            if (sub["action"] != null) ++withAction;
        }

        score += numericChildren * 12;
        score += withInfo * 6;
        score += withCommon * 4;
        score += withAction * 2;
        if (numericChildren == 0)
            score -= 1500;

        return score;
    }

    private static int ScoreStringSkillImage(WzImage image)
    {
        if (image.WzProperties == null)
            return -10000;

        int scanned = 0;
        int numericEntries = 0;
        int withName = 0;
        int withDesc = 0;
        foreach (WzImageProperty child in image.WzProperties)
        {
            if (++scanned > 240)
                break;

            if (!int.TryParse(child.Name, out _))
                continue;

            ++numericEntries;
            if (child is not WzSubProperty sub)
                continue;

            if (sub["name"] is WzStringProperty) ++withName;
            if (sub["desc"] is WzStringProperty) ++withDesc;
        }

        if (numericEntries == 0)
            return -7000;

        int score = 1800;
        score += numericEntries * 10;
        score += withName * 8;
        score += withDesc * 5;
        return score;
    }
}
