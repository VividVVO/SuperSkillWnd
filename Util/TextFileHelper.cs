using System.IO;
using System.Text;

namespace SuperSkillTool
{
    internal static class TextFileHelper
    {
        private static readonly UTF8Encoding Utf8Strict = new UTF8Encoding(false, true);

        /// <summary>
        /// Read text with robust encoding fallback.
        /// Priority: BOM -> strict UTF-8 -> GB18030/GBK -> system default.
        /// </summary>
        public static string ReadAllTextAuto(string path)
        {
            byte[] bytes = File.ReadAllBytes(path);
            if (bytes.Length == 0) return "";

            // UTF-8 BOM
            if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
                return Encoding.UTF8.GetString(bytes, 3, bytes.Length - 3);

            // UTF-16 LE BOM
            if (bytes.Length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
                return Encoding.Unicode.GetString(bytes, 2, bytes.Length - 2);

            // UTF-16 BE BOM
            if (bytes.Length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
                return Encoding.BigEndianUnicode.GetString(bytes, 2, bytes.Length - 2);

            try
            {
                return Utf8Strict.GetString(bytes);
            }
            catch (DecoderFallbackException)
            {
                // ignore and fallback
            }

            try
            {
                return Encoding.GetEncoding("GB18030").GetString(bytes);
            }
            catch
            {
                // ignore and fallback
            }

            try
            {
                return Encoding.GetEncoding(936).GetString(bytes); // GBK/CP936
            }
            catch
            {
                // ignore and fallback
            }

            return Encoding.Default.GetString(bytes);
        }
    }
}
