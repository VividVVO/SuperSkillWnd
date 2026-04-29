using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace SuperSkillTool
{
    /// <summary>
    /// Minimal JSON reader / writer that works with .NET Framework 4.8.
    /// Only supports the subset needed by this tool:
    ///   - objects  { }
    ///   - arrays   [ ]
    ///   - strings  "..."
    ///   - numbers  (int / double)
    ///   - booleans true / false
    ///   - null
    ///
    /// NOT a general-purpose JSON library. Enough to parse the config files
    /// this project uses and to produce well-formed JSON output.
    /// </summary>
    public static class SimpleJson
    {
        // ────────────────────────────────────────────────────────
        //  Public read API
        // ────────────────────────────────────────────────────────

        public static object Parse(string json)
        {
            int idx = 0;
            var result = ParseValue(json, ref idx);
            return result;
        }

        public static Dictionary<string, object> ParseObject(string json)
        {
            var obj = Parse(json);
            if (obj is Dictionary<string, object> dict) return dict;
            throw new FormatException("Expected JSON object at top level.");
        }

        public static List<object> ParseArray(string json)
        {
            var obj = Parse(json);
            if (obj is List<object> list) return list;
            throw new FormatException("Expected JSON array at top level.");
        }

        // ────────────────────────────────────────────────────────
        //  Public write API
        // ────────────────────────────────────────────────────────

        public static string Serialize(object value, int indent = 2)
        {
            var sb = new StringBuilder();
            WriteValue(sb, value, 0, indent);
            return sb.ToString();
        }

        // ────────────────────────────────────────────────────────
        //  Convenience helpers
        // ────────────────────────────────────────────────────────

        public static string GetString(Dictionary<string, object> obj, string key, string fallback = "")
        {
            if (obj != null && obj.TryGetValue(key, out var v) && v is string s) return s;
            return fallback;
        }

        public static int GetInt(Dictionary<string, object> obj, string key, int fallback = 0)
        {
            if (obj != null && obj.TryGetValue(key, out var v))
            {
                if (v is long l) return (int)l;
                if (v is double d) return (int)d;
                if (v is int i) return i;
                if (v is string s && int.TryParse(s, out int parsed)) return parsed;
            }
            return fallback;
        }

        public static double GetDouble(Dictionary<string, object> obj, string key, double fallback = 0d)
        {
            if (obj != null && obj.TryGetValue(key, out var v))
            {
                if (v is double d) return d;
                if (v is float f) return f;
                if (v is long l) return l;
                if (v is int i) return i;
                if (v is string s
                    && double.TryParse(
                        s,
                        System.Globalization.NumberStyles.Float,
                        System.Globalization.CultureInfo.InvariantCulture,
                        out double parsed))
                {
                    return parsed;
                }
            }
            return fallback;
        }

        public static bool GetBool(Dictionary<string, object> obj, string key, bool fallback = false)
        {
            if (obj != null && obj.TryGetValue(key, out var v) && v is bool b) return b;
            return fallback;
        }

        public static Dictionary<string, object> GetObject(Dictionary<string, object> obj, string key)
        {
            if (obj != null && obj.TryGetValue(key, out var v) && v is Dictionary<string, object> d) return d;
            return null;
        }

        public static List<object> GetArray(Dictionary<string, object> obj, string key)
        {
            if (obj != null && obj.TryGetValue(key, out var v) && v is List<object> a) return a;
            return null;
        }

        // ────────────────────────────────────────────────────────
        //  Parser internals
        // ────────────────────────────────────────────────────────

        private static void SkipWhitespace(string json, ref int idx)
        {
            while (idx < json.Length && char.IsWhiteSpace(json[idx])) idx++;
        }

        private static object ParseValue(string json, ref int idx)
        {
            SkipWhitespace(json, ref idx);
            if (idx >= json.Length) throw new FormatException("Unexpected end of JSON.");
            char c = json[idx];
            if (c == '{') return ParseObjectInternal(json, ref idx);
            if (c == '[') return ParseArrayInternal(json, ref idx);
            if (c == '"') return ParseString(json, ref idx);
            if (c == 't') return ParseLiteral(json, ref idx, "true", true);
            if (c == 'f') return ParseLiteral(json, ref idx, "false", false);
            if (c == 'n') return ParseLiteral(json, ref idx, "null", null);
            if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(json, ref idx);
            throw new FormatException($"Unexpected character '{c}' at position {idx}.");
        }

        private static Dictionary<string, object> ParseObjectInternal(string json, ref int idx)
        {
            var dict = new Dictionary<string, object>();
            idx++; // skip {
            SkipWhitespace(json, ref idx);
            if (idx < json.Length && json[idx] == '}') { idx++; return dict; }

            while (true)
            {
                SkipWhitespace(json, ref idx);
                if (idx >= json.Length) throw new FormatException("Unterminated object.");
                string key = ParseString(json, ref idx);
                SkipWhitespace(json, ref idx);
                if (idx >= json.Length || json[idx] != ':')
                    throw new FormatException($"Expected ':' at position {idx}.");
                idx++; // skip :
                object val = ParseValue(json, ref idx);
                dict[key] = val;
                SkipWhitespace(json, ref idx);
                if (idx >= json.Length) throw new FormatException("Unterminated object.");
                if (json[idx] == '}') { idx++; return dict; }
                if (json[idx] == ',') { idx++; continue; }
                throw new FormatException($"Expected ',' or '}}' at position {idx}.");
            }
        }

        private static List<object> ParseArrayInternal(string json, ref int idx)
        {
            var list = new List<object>();
            idx++; // skip [
            SkipWhitespace(json, ref idx);
            if (idx < json.Length && json[idx] == ']') { idx++; return list; }

            while (true)
            {
                object val = ParseValue(json, ref idx);
                list.Add(val);
                SkipWhitespace(json, ref idx);
                if (idx >= json.Length) throw new FormatException("Unterminated array.");
                if (json[idx] == ']') { idx++; return list; }
                if (json[idx] == ',') { idx++; continue; }
                throw new FormatException($"Expected ',' or ']' at position {idx}.");
            }
        }

        private static string ParseString(string json, ref int idx)
        {
            if (json[idx] != '"')
                throw new FormatException($"Expected '\"' at position {idx}.");
            idx++; // skip opening "
            var sb = new StringBuilder();
            while (idx < json.Length)
            {
                char c = json[idx];
                if (c == '\\')
                {
                    idx++;
                    if (idx >= json.Length) throw new FormatException("Unterminated string escape.");
                    char esc = json[idx];
                    switch (esc)
                    {
                        case '"': sb.Append('"'); break;
                        case '\\': sb.Append('\\'); break;
                        case '/': sb.Append('/'); break;
                        case 'b': sb.Append('\b'); break;
                        case 'f': sb.Append('\f'); break;
                        case 'n': sb.Append('\n'); break;
                        case 'r': sb.Append('\r'); break;
                        case 't': sb.Append('\t'); break;
                        case 'u':
                            if (idx + 4 >= json.Length)
                                throw new FormatException("Unterminated unicode escape.");
                            string hex = json.Substring(idx + 1, 4);
                            sb.Append((char)Convert.ToInt32(hex, 16));
                            idx += 4;
                            break;
                        default:
                            sb.Append(esc);
                            break;
                    }
                }
                else if (c == '"')
                {
                    idx++; // skip closing "
                    return sb.ToString();
                }
                else
                {
                    sb.Append(c);
                }
                idx++;
            }
            throw new FormatException("Unterminated string.");
        }

        private static object ParseNumber(string json, ref int idx)
        {
            int start = idx;
            if (json[idx] == '-') idx++;
            while (idx < json.Length && json[idx] >= '0' && json[idx] <= '9') idx++;
            bool isFloat = false;
            if (idx < json.Length && json[idx] == '.')
            {
                isFloat = true;
                idx++;
                while (idx < json.Length && json[idx] >= '0' && json[idx] <= '9') idx++;
            }
            if (idx < json.Length && (json[idx] == 'e' || json[idx] == 'E'))
            {
                isFloat = true;
                idx++;
                if (idx < json.Length && (json[idx] == '+' || json[idx] == '-')) idx++;
                while (idx < json.Length && json[idx] >= '0' && json[idx] <= '9') idx++;
            }
            string num = json.Substring(start, idx - start);
            if (isFloat) return double.Parse(num, System.Globalization.CultureInfo.InvariantCulture);
            if (long.TryParse(num, out long lv)) return lv;
            return double.Parse(num, System.Globalization.CultureInfo.InvariantCulture);
        }

        private static object ParseLiteral(string json, ref int idx, string literal, object value)
        {
            if (json.Length - idx < literal.Length || json.Substring(idx, literal.Length) != literal)
                throw new FormatException($"Expected '{literal}' at position {idx}.");
            idx += literal.Length;
            return value;
        }

        // ────────────────────────────────────────────────────────
        //  Writer internals
        // ────────────────────────────────────────────────────────

        private static void WriteValue(StringBuilder sb, object value, int depth, int indent)
        {
            if (value == null)
            {
                sb.Append("null");
            }
            else if (value is string s)
            {
                WriteString(sb, s);
            }
            else if (value is bool b)
            {
                sb.Append(b ? "true" : "false");
            }
            else if (value is int iv)
            {
                sb.Append(iv);
            }
            else if (value is long lv)
            {
                sb.Append(lv);
            }
            else if (value is double dv)
            {
                sb.Append(dv.ToString(System.Globalization.CultureInfo.InvariantCulture));
            }
            else if (value is Dictionary<string, object> dict)
            {
                WriteObject(sb, dict, depth, indent);
            }
            else if (value is List<object> list)
            {
                WriteArray(sb, list, depth, indent);
            }
            else
            {
                // Fallback: treat as string
                WriteString(sb, value.ToString());
            }
        }

        private static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\b': sb.Append("\\b"); break;
                    case '\f': sb.Append("\\f"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        // Keep JSON ASCII-safe so legacy readers that only
                        // properly decode \uXXXX escapes can still read text.
                        if (c < 0x20 || c > 0x7F)
                            sb.AppendFormat("\\u{0:x4}", (int)c);
                        else
                            sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }

        private static void WriteObject(StringBuilder sb, Dictionary<string, object> dict, int depth, int indent)
        {
            if (dict.Count == 0) { sb.Append("{}"); return; }
            string pad = new string(' ', (depth + 1) * indent);
            string closePad = new string(' ', depth * indent);
            sb.Append("{\n");
            int i = 0;
            foreach (var kv in dict)
            {
                sb.Append(pad);
                WriteString(sb, kv.Key);
                sb.Append(": ");
                WriteValue(sb, kv.Value, depth + 1, indent);
                if (i < dict.Count - 1) sb.Append(',');
                sb.Append('\n');
                i++;
            }
            sb.Append(closePad);
            sb.Append('}');
        }

        private static void WriteArray(StringBuilder sb, List<object> list, int depth, int indent)
        {
            if (list.Count == 0) { sb.Append("[]"); return; }
            string pad = new string(' ', (depth + 1) * indent);
            string closePad = new string(' ', depth * indent);
            sb.Append("[\n");
            for (int i = 0; i < list.Count; i++)
            {
                sb.Append(pad);
                WriteValue(sb, list[i], depth + 1, indent);
                if (i < list.Count - 1) sb.Append(',');
                sb.Append('\n');
            }
            sb.Append(closePad);
            sb.Append(']');
        }
    }
}
